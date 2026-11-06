#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ps2.h"
#include "input_queue.h"
#include "arch/x86_64/io.h"

static bool mouse_present = false;
static uint8_t mouse_packet[3];
static uint8_t mouse_packet_index = 0U;
static int32_t mouse_x = 0;
static int32_t mouse_y = 0;

/* Stare taste apasate (pentru break codes) */
static bool key_pressed[256];

static bool ps2_wait_output_full(uint32_t polls) {
    for (uint32_t i = 0; i < polls; i++) {
        if ((x86_inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) != 0U) {
            return true;
        }
    }
    return false;
}

static bool ps2_wait_input_empty(uint32_t polls) {
    for (uint32_t i = 0; i < polls; i++) {
        if ((x86_inb(PS2_STATUS_PORT) & PS2_STATUS_INPUT_FULL) == 0U) {
            return true;
        }
    }
    return false;
}

static bool ps2_write_command(uint8_t command) {
    if (!ps2_wait_input_empty(100000U)) {
        return false;
    }
    x86_outb(PS2_CMD_PORT, command);
    return true;
}

static bool ps2_write_data(uint8_t data) {
    if (!ps2_wait_input_empty(100000U)) {
        return false;
    }
    x86_outb(PS2_DATA_PORT, data);
    return true;
}

static bool ps2_read_data(uint8_t *out) {
    if (!ps2_wait_output_full(100000U)) {
        return false;
    }
    *out = x86_inb(PS2_DATA_PORT);
    return true;
}

static bool ps2_self_test(void) {
    if (!ps2_write_command(0xAAU)) {
        return false;
    }
    uint8_t result = 0U;
    if (!ps2_read_data(&result)) {
        return false;
    }
    return result == 0x55U;
}

static bool ps2_first_port_test(void) {
    if (!ps2_write_command(0xABU)) {
        return false;
    }
    uint8_t result = 0U;
    if (!ps2_read_data(&result)) {
        return false;
    }
    return result == 0x00U;
}

static bool ps2_enable_first_port(void) {
    return ps2_write_command(PS2_CMD_ENABLE_FIRST_PORT);
}

static bool ps2_set_config(uint8_t config) {
    if (!ps2_write_command(PS2_CMD_WRITE_CONFIG)) {
        return false;
    }
    return ps2_write_data(config);
}

static uint8_t ps2_get_config(void) {
    if (!ps2_write_command(PS2_CMD_READ_CONFIG)) {
        return 0U;
    }
    uint8_t config = 0U;
    if (!ps2_read_data(&config)) {
        return 0U;
    }
    return config;
}

static bool ps2_mouse_enable_streaming(void) {
    if (!ps2_write_command(PS2_CMD_WRITE_TO_MOUSE)) {
        return false;
    }
    if (!ps2_write_data(PS2_MOUSE_ENABLE_DATA_REPORTING)) {
        return false;
    }
    uint8_t ack = 0U;
    return ps2_read_data(&ack) && ack == 0xFAU;
}

static void ps2_handle_keyboard(uint8_t scancode) {
    struct input_event ev = {0};
    ev.timestamp_ms = 0U;

    bool is_break = false;
    if ((scancode & 0x80U) != 0U) {
        is_break = true;
        scancode &= 0x7FU;
    }

    ev.type = is_break ? 1U : 0U;   /* 0 = key_down, 1 = key_up */
    ev.key_code = scancode;
    ev.x = 0;
    ev.y = 0;
    ev.dx = 0;
    ev.dy = 0;

    key_pressed[scancode] = !is_break;

    (void)input_queue_push(&ev);
}

static void ps2_handle_mouse_byte(uint8_t byte) {
    if (mouse_packet_index == 0U) {
        mouse_packet[0] = byte;
        mouse_packet_index = 1U;
    } else if (mouse_packet_index == 1U) {
        mouse_packet[1] = byte;
        mouse_packet_index = 2U;
    } else {
        mouse_packet[2] = byte;
        mouse_packet_index = 0U;

        uint8_t buttons = mouse_packet[0] & 0x07U;
        int8_t dx = (int8_t)mouse_packet[1];
        int8_t dy = -(int8_t)mouse_packet[2];

        mouse_x += dx;
        mouse_y += dy;
        if (mouse_x < 0) mouse_x = 0;
        if (mouse_y < 0) mouse_y = 0;

        struct input_event ev = {0};
        ev.timestamp_ms = 0U;
        ev.type = 2U;  /* mouse_move */
        ev.key_code = 0;
        ev.x = mouse_x;
        ev.y = mouse_y;
        ev.dx = dx;
        ev.dy = dy;
        (void)input_queue_push(&ev);

        static bool last_buttons[3] = {false, false, false};
        for (uint8_t btn = 0; btn < 3U; btn++) {
            bool pressed = (buttons & (1U << btn)) != 0U;
            if (pressed != last_buttons[btn]) {
                last_buttons[btn] = pressed;
                struct input_event bev = {0};
                bev.timestamp_ms = 0U;
                bev.type = pressed ? 3U : 4U;  /* 3 = down, 4 = up */
                bev.key_code = btn;
                bev.x = mouse_x;
                bev.y = mouse_y;
                bev.dx = 0;
                bev.dy = 0;
                (void)input_queue_push(&bev);
            }
        }
    }
}

static bool ps2_mouse_init(void) {
    if (!ps2_write_command(PS2_CMD_WRITE_TO_MOUSE)) {
        return false;
    }
    if (!ps2_write_data(PS2_MOUSE_RESET)) {
        return false;
    }
    uint8_t ack = 0U;
    if (!ps2_read_data(&ack) || ack != 0xFAU) {
        return false;
    }
    uint8_t bat = 0U;
    if (!ps2_read_data(&bat) || bat != 0xAAU) {
        return false;
    }

    if (!ps2_write_command(PS2_CMD_WRITE_TO_MOUSE)) {
        return false;
    }
    if (!ps2_write_data(PS2_MOUSE_SET_SAMPLING_RATE)) {
        return false;
    }
    if (!ps2_write_data(100U)) {
        return false;
    }
    if (!ps2_read_data(&ack) || ack != 0xFAU) {
        return false;
    }

    if (!ps2_write_command(PS2_CMD_WRITE_TO_MOUSE)) {
        return false;
    }
    if (!ps2_write_data(PS2_MOUSE_SET_RESOLUTION)) {
        return false;
    }
    if (!ps2_write_data(3U)) {
        return false;
    }
    if (!ps2_read_data(&ack) || ack != 0xFAU) {
        return false;
    }

    if (!ps2_write_command(PS2_CMD_WRITE_TO_MOUSE)) {
        return false;
    }
    if (!ps2_write_data(PS2_MOUSE_SET_SCALING_1TO1)) {
        return false;
    }
    if (!ps2_read_data(&ack) || ack != 0xFAU) {
        return false;
    }

    return ps2_mouse_enable_streaming();
}

bool ps2_init(void) {
    if (!ps2_self_test()) {
        return false;
    }
    if (!ps2_first_port_test()) {
        return false;
    }
    if (!ps2_enable_first_port()) {
        return false;
    }

    uint8_t config = ps2_get_config();
    config |= 0x01U;
    config |= 0x02U;
    config &= 0xCFU;
    if (!ps2_set_config(config)) {
        return false;
    }

    mouse_present = ps2_mouse_init();
    return true;
}

void ps2_poll(void) {
    while (ps2_wait_output_full(0U)) {
        uint8_t data = x86_inb(PS2_DATA_PORT);
        uint8_t status = x86_inb(PS2_STATUS_PORT);
        bool from_mouse = (status & 0x20U) != 0U;

        if (from_mouse && mouse_present) {
            ps2_handle_mouse_byte(data);
        } else {
            ps2_handle_keyboard(data);
        }
    }
}
