# x86-64 Serial Monitor

**Status:** Booted and exercised in QEMU; polling monitor with PIT clock

After the boot diagnostics, the kernel enters a polling command monitor on
COM1. The monitor is kernel-resident, single-tasked, and busy-polls for input;
it is an interim diagnostic interface, not a user-space shell.

| Command | Behavior |
| --- | --- |
| `help` | List supported commands |
| `hwinfo` | Show architecture, bootloader, PCI function count, and block-device count |
| `mem` | Print the bootloader memory map |
| `uptime` | Show elapsed seconds and PIT timer ticks |
| `irq status` | Show CPU interrupt enable, PIC mask/pending state, and APIC status |
| `disk list` | List registered block devices |
| `gpt list` | Scan and display the primary GPT on each registered device |
| `gpt list N` | Scan the primary GPT on block-device index `N` |
| `halt` | Stop the CPU |

Input is limited to 127 printable characters. Backspace and Ctrl-U edit the
current line; CR, LF, and CRLF are accepted. An overlong line is discarded.
Storage commands only read device metadata; this monitor has no disk-write
command. The GPT parser's primary-only scope and validation limits are described
in [Read-only GPT Discovery](gpt.md).

Runtime observation: the x86-64 kernel booted under QEMU with `-machine q35
-m 512M -device edu`; the commands in this table returned expected output.
The PIT ran at 100 Hz through PIC IRQ0 and local APIC virtual-wire delivery.
QEMU q35 did not expose the optional IMCR, so firmware PIC routing was retained;
`irq status` showed no pending or in-service IRQ, and `uptime` advanced to
1681 ticks in about 17 seconds. The boot-time EDU test also passed its ring-3
read/write, read-only, foreign-domain, revocation, stale-handle, and fault
isolation scenarios. See [the run record](../experiments/2026-11-03-qemu-x86_64-first-boot.md).
This monitor does not provide multitasking, persistence, or a user-space shell.
