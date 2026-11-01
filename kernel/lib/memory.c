#include <stddef.h>

#include "string.h"

void *memset(void *destination, int value, size_t count) {
    unsigned char *cursor = destination;
    volatile unsigned char byte = (unsigned char)value;

    for (size_t i = 0; i < count; i++) {
        cursor[i] = byte;
    }
    return destination;
}
