# Experimental x86-64 Syscall ABI

**Status:** Prototype; not a stable application ABI

This ABI began as the privilege boundary for the MMIO capability experiment.
It is a single-CPU prototype, intentionally accepts only scalar values, and
supports a 100 Hz round-robin scheduler for user processes. It has no user
pointers, IPC, or compatibility guarantee.

## Register contract

User code enters with the `SYSCALL` instruction:

| Register | Input |
| --- | --- |
| `RAX` | Syscall number |
| `RDI` | Argument 0 |
| `RSI` | Argument 1 |
| `RDX` | Argument 2 |

`RCX` and `R11` are clobbered by the architectural transition. The kernel
derives the actor domain from kernel-owned current-domain state; no syscall
accepts a domain ID.

## Calls

| Number | Name | Arguments | Result |
| --- | --- | --- | --- |
| 0 | `EXIT` | `RDI`: status | Does not return; resumes the kernel caller |
| 1 | `MMIO_READ32` | `RDI`: handle, `RSI`: byte offset | `RAX[63:32]`: result code; `RAX[31:0]`: value on success |
| 2 | `MMIO_WRITE32` | `RDI`: handle, `RSI`: byte offset, `RDX[31:0]`: value | `RAX[31:0]`: result code |
| 3 | `MMIO_REVOKE` | `RDI`: handle | `RAX[31:0]`: result code |
| 4 | `CONSOLE_WRITE_CHAR` | `RDI`: ASCII value | `RAX[63:32]`: result code |
| 5 | `CONSOLE_READ_CHAR` | None | `RAX[63:32]`: result code; `RAX[7:0]`: character on success |
| 6 | `CONSOLE_CLEAR` | None | `RAX[63:32]`: result code |
| 7 | `UPTIME_TICKS` | None | `RAX`: monotonic timer ticks since timer initialization |
| 8 | `SYSTEM_REPORT` | `RDI`: report selector from `syscall_abi.h` | `RAX[63:32]`: result code; report text is formatted by the kernel |
| 9 | `FS_LIST` | `RDI`: block device index, `RSI`: GPT partition number | `RAX[63:32]`: result code; prints the FAT32 root directory |
| 10 | `FS_CAT` | `RDI`: packed 8-character base name, `RSI`: packed 3-character extension | `RAX[63:32]`: result code; prints a FAT32 root file |
| 11 | `FS_STAT` | `RDI`: packed 8-character base name, `RSI`: packed 3-character extension | `RAX[63:32]`: result code; prints root-file metadata |
| 12 | `FS_TOUCH` | `RDI`: packed 8.3 name | `RAX[63:32]`: result code; creates an empty root file if absent |
| 13 | `FS_DELETE` | `RDI`: packed 8.3 name | `RAX[63:32]`: result code; removes a root file |
| 14 | `FS_WRITE_BEGIN` | `RDI`: packed 8.3 name | `RAX[63:32]`: result code; starts a bounded buffered replacement |
| 15 | `FS_WRITE_DATA` | `RDI`: up to 8 ASCII bytes packed little-endian; `RSI`: byte count | `RAX[63:32]`: result code; appends to pending write |
| 16 | `FS_WRITE_COMMIT` | None | `RAX[63:32]`: result code; commits pending write |
| 17 | `FS_MKDIR` | `RDI`: packed 8.3 name | `RAX[63:32]`: result code; creates a root directory |
| 18 | `FS_WRITE_ABORT` | None | `RAX[63:32]`: result code; discards pending write |
| 19 | `PROCESS_LIST` | None | `RAX[63:32]`: result code; prints the bounded process history |
| 20 | `PROCESS_KILL` | `RDI`: process ID | Terminates the caller with status 137 only when the ID is the caller's current process ID |
| 21 | `PROCESS_EXEC` | `RDI`: packed 8-character base name, `RSI`: packed 3-character extension | Loads and runs a static ELF from the FAT32 root synchronously as a child |
| 22 | `PROCESS_SPAWN` | `RDI`: packed 8-character base name, `RSI`: packed 3-character extension | Loads a static ELF as a runnable child; low 32 bits of `RAX` contain its PID |
| 23 | `FS_READ_CHUNK` | `RDI`: packed 8-character base name, `RSI`: packed 3-character extension, `RDX`: byte offset | `RAX[63:32]`: result; `RAX[31:30]`: byte count; `RAX[23:0]`: up to 3 file bytes packed little-endian |

For calls 1–3, result codes are the `mmio_capability_result` values declared in
`kernel/capability/mmio_capability.h`. An invalid syscall number returns
`MMIO_CAPABILITY_INVALID_ARGUMENT` in the high half of `RAX`.

Console calls use `ELOQUENCE_SYSCALL_RESULT_*` values from the public
`include/eloquence/syscall_abi.h`: success is 0, invalid output character is 1,
and an empty non-blocking input queue is 2 (`WOULD_BLOCK`). Console output
accepts printable ASCII, line feed, carriage return, and horizontal tab; line
feed is translated to CRLF. `CONSOLE_CLEAR` emits the terminal clear/home
sequence through the kernel serial console. These calls use scalar arguments
and never follow a user pointer. `UPTIME_TICKS` returns ticks at the configured
100 Hz timer rate.
`SYSTEM_REPORT` accepts only enumerated scalar selectors and prints sanitized
kernel-owned snapshots; it never accepts user pointers. The report interface
is experimental and currently covers system, usable-memory, CPUID vendor and
family/model/features, device, timer/IRQ, and bootloader summaries.
Filesystem calls target device 0, partition 1 and currently expose only the
FAT32 root. Names are validated ASCII 8.3 names. Writes are limited to 16 KiB
and staged by scalar calls; the kernel does not accept user pointers. The block
device must support write and flush. FAT32 metadata updates are not journaled,
so power loss during a mutation can leave the volume inconsistent. Directory
creation and file operations are root-only; the shell has no subdirectory path
traversal yet. `FS_TOUCH` leaves an existing file intact, while `FS_WRITE_BEGIN`
replaces it on commit.
`FS_READ_CHUNK` reads up to three bytes from the root FAT32 volume at the
requested byte offset; the top two bits of the low half contain the returned
byte count. End of file is reported with
`ELOQUENCE_SYSCALL_RESULT_END_OF_FILE`.

`PROCESS_LIST` reports up to 16 most recent process records, including PID,
parent PID, state, exit status, and runtime ticks. `PROCESS_KILL` accepts the
caller or another live scheduler-managed process. `PROCESS_EXEC` blocks its
caller until the child exits, then resumes it. `PROCESS_SPAWN` creates a
separate address space and kernel syscall stack and returns immediately. The
x86-64 scheduler uses the 100 Hz timer for round-robin preemption. This remains
a single-CPU prototype with a fixed process limit; there is no `wait`, IPC,
SMP scheduling, or stable syscall compatibility guarantee.

The handle encoding is `generation[63:32]`, reserved zero bits `[31:8]`, and
slot `[7:0]`. Invalid reserved bits are rejected. Handles are currently
predictable slot/generation values; this ABI does not provide unforgeability.

The ring-3 demonstration also exercises an unknown syscall number and a handle
with nonzero reserved bits. Those are intended runtime checks; source presence
and compilation do not show that the checks have passed.

`EXIT` uses an internal all-ones return sentinel and accepts the full 64-bit
status range. The kernel reports a user exception separately from the status
value and records the exception vector as its fault status. A user exit status
can therefore never be confused with fault termination. This is an internal
prototype result contract, not a process ABI.

## Protection boundary

The demo maps code user-readable/executable and stack user-readable/writable
and non-executable. Kernel high-half mappings remain supervisor-only. Syscalls
do not copy or dereference user pointers. MMIO stays kernel-mapped and is
accessed only after capability ownership, rights, alignment, and bounds checks.
External interrupts remain disabled while the demo runs.

The implementation supports one active domain on one CPU. A scheduler,
address-space teardown, frame reclamation, handle hardening, asynchronous
interrupts, and a stable syscall versioning scheme are future work. The
current ring-3 scenarios were observed under QEMU; the run record is linked
from the [serial monitor documentation](../drivers/serial-monitor.md).
Arbitrary kernel-memory access attempts and general process teardown still
need dedicated isolation coverage.
