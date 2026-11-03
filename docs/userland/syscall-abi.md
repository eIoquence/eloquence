# Experimental x86-64 syscall ABI

The current ABI is a small synchronous interface for ring-3 prototypes. It is
specific to x86-64, has no stability promise, and uses `SYSCALL` with the
operation number in `RAX` and arguments in `RDI`, `RSI`, and `RDX`.

| Number | Operation | Arguments | Result |
| --- | --- | --- | --- |
| 0 | Exit | `RDI`: process status | Does not return to user mode |
| 1 | MMIO read32 | `RDI`: capability handle, `RSI`: offset | `RAX[63:32]`: result code; `RAX[31:0]`: value |
| 2 | MMIO write32 | `RDI`: capability handle, `RSI`: offset, `RDX`: value | `RAX`: result code |
| 3 | Revoke MMIO capability | `RDI`: capability handle | `RAX`: result code |
| 4 | Console write character | `RDI`: ASCII value | `RAX[63:32]`: result code |

Console output accepts printable ASCII plus line feed, carriage return, and
horizontal tab. Line feed is translated to CRLF. The call accepts a scalar
character and never follows a user-provided pointer; programs currently make
one syscall per character. This keeps the first output boundary simple while
the kernel has no user-memory copy API. Invalid values return the
`MMIO_CAPABILITY_INVALID_ARGUMENT` result code.

Capability handles are predictable slot/generation values and are not
unforgeable. The MMIO operations remain experimental, and the packed results
are not a stable general-purpose error convention. There is no console input,
buffered output, process scheduling, signal handling, or system-call
compatibility promise.
