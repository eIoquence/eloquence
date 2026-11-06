#ifndef ELOQUENCE_DRIVERS_INPUT_INPUT_QUEUE_H
#define ELOQUENCE_DRIVERS_INPUT_INPUT_QUEUE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define INPUT_QUEUE_SIZE 16U

struct input_event {
    uint64_t timestamp_ms;
    uint8_t  type;      /* 0 = key_down, 1 = key_up, 2 = mouse_move, 3 = mouse_btn_down, 4 = mouse_btn_up */
    uint8_t  key_code;  /* scancode tastatura sau buton mouse (0=left,1=right,2=middle) */
    int32_t  x, y;      /* pozitie mouse absoluta sau 0 pentru tastatura */
    int32_t  dx, dy;    /* miscare relativa mouse */
};

bool input_queue_init(void);
bool input_queue_push(const struct input_event *event);
bool input_queue_pop(struct input_event *event);
uint32_t input_queue_count(void);

#endif
