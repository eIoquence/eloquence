# QEMU EDU PCI Driver

**Status:** x86-64 source prototype; not boot-validated

The driver binds only PCI vendor/device `1234:11e8`. It validates that BAR0 is
a memory BAR, probes its size with the existing targeted PCI configuration
transaction, maps only the first two registers as uncached MMIO, and checks
the documented identity value. It does not enable PCI bus mastering or make
assumptions about other devices' register layouts.

The liveness register at offset `0x04` returns the bitwise inverse of each
written value. Its check saves the original state, verifies the inversion,
writes the inverse of the saved value to restore the original state, and then
verifies the restoration. The user-mode capability experiment runs only after the
driver releases its mapping, because the current mapper rejects overlapping
physical mappings.

The driver is intended for the QEMU EDU device described in the
[QEMU EDU device specification](https://www.qemu.org/docs/master/specs/edu.html).
It does not implement the EDU device's DMA engine, interrupts, or other
registers. Successful compilation does not establish that the experiment
works in a booted machine.
