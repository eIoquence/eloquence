#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "input_queue.h"

static struct input_event queue[INPUT_QUEUE_SIZE];
static uint32_t head = 0U;
static uint32_t tail = 0U;
static uint32_t count = 0U;

bool input_queue_init(void) {
    head = 0U;
    tail = 0U;
    count = 0U;
    return true;
}

bool input_queue_push(const struct input_event *event) {
    if (event == 0 || count >= INPUT_QUEUE_SIZE) {
        return false;
    }
    queue[tail] = *event;
    tail = (tail + 1U) % INPUT_QUEUE_SIZE;
    count++;
    return true;
}

bool input_queue_pop(struct input_event *event) {
    if (event == 0 || count == 0U) {
        return false;
    }
    *event = queue[head];
    head = (head + 1U) % INPUT_QUEUE_SIZE;
    count--;
    return true;
}

uint32_t input_queue_count(void) {
    return count;
}
