#ifndef ELOQUENCE_DRIVERS_ATA_PIO_H
#define ELOQUENCE_DRIVERS_ATA_PIO_H

#include <stdbool.h>

/* Probe legacy primary and secondary ATA channels without writing disk data. */
bool ata_pio_initialize(void);

#endif
