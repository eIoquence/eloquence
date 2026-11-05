# QEMU Shell Script Run

## Environment

- Target: x86-64 under QEMU `q35` with the generated FAT32 VirtIO disk.
- Shell: statically linked Ring-3 `SHELL.ELF`.
- File access: root-only 8.3 path through the scalar `FS_READ_CHUNK` syscall.

## Observation

At the shell prompt, the following commands were entered:

```text
echo echo script-ok > DEMO.TXT
script DEMO.TXT
```

The shell confirmed the file write, then read the file and printed `script-ok`.
The shell remained interactive. The kernel and shell compiled, and QEMU booted
to the user shell before this observation.

Variable substitution and command lists were also observed:

```text
echo set GREETING=hello; echo $GREETING > VARS.TXT
script VARS.TXT
```

The script printed `hello` after assigning and expanding `GREETING`.

Scripts are limited to 2 KiB and each command line to the shell's 80-byte line
capacity. Scripts have eight variable slots, `$NAME` substitution, and
semicolon-separated commands. Supported commands are `set`, `echo`, `uname`,
`version`, system reports, and the shell-local pipelines. The built-in
`CONTROL.TXT` fixture exercises variable assignment, true and false `if`
conditions, `else`, and a three-iteration `repeat` block. QEMU printed
`conditional-pass`, `else-pass`, and `loop-pass` three times. Control blocks
nest to depth eight; `repeat` counts are limited to 16, and scripts to 128
commands. Directives occupy whole lines: `if $NAME=VALUE`, optional `else`,
`endif`, and `repeat COUNT`/`endrepeat`. Process pipelines and nested scripts
are unsupported. File reads are root-only and use scalar chunks of up to three
bytes.
