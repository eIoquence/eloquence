#ifndef ELOQUENCE_DRIVERS_INPUT_PS2_H
#define ELOQUENCE_DRIVERS_INPUT_PS2_H

#include <stdbool.h>
#include <stdint.h>

#include "input_queue.h"

#define PS2_DATA_PORT  0x60U
#define PS2_STATUS_PORT 0x64U
#define PS2_CMD_PORT    0x64U

#define PS2_STATUS_OUTPUT_FULL 0x01U
#define PS2_STATUS_INPUT_FULL  0x02U

#define PS2_CMD_ENABLE_FIRST_PORT  0xAEU
#define PS2_CMD_DISABLE_FIRST_PORT 0xADU
#define PS2_CMD_WRITE_TO_MOUSE     0xD4U
#define PS2_CMD_READ_CONFIG        0x20U
#define PS2_CMD_WRITE_CONFIG       0x60U

#define PS2_MOUSE_ENABLE_DATA_REPORTING 0xF4U
#define PS2_MOUSE_SET_SAMPLING_RATE     0xF3U
#define PS2_MOUSE_SET_RESOLUTION        0xE8U
#define PS2_MOUSE_SET_SCALING_1TO1      0xE6U
#define PS2_MOUSE_RESET                 0xFFU

#define PS2_KEY_BREAK_PREFIX   0xF0U

bool ps2_init(void);
void ps2_poll(void);

#endif
