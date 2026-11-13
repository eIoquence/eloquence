/// Serial I/O prin syscall CONSOLE_WRITE_CHAR (4).
/// Kernelul returnează OK (0) sau INVALID_ARGUMENT (1) în biții 32-63 ai rax.

const Syscall = .{
    CONSOLE_WRITE_CHAR = 4,
};

fn syscall1(number: u64, a0: u64) u64 {
    var result: u64 = undefined;
    asm volatile (
        "syscall"
        : [result] "=a"(result)
        : [number] "0"(number), [a0] "D"(a0)
        : "rcx", "r11", "memory"
    );
    return result;
}

pub fn serialWriteChar(c: u8) void {
    _ = syscall1(Syscall.CONSOLE_WRITE_CHAR, @intCast(c));
}

pub fn serialWrite(s: []const u8) void {
    var i: usize = 0;
    while (i < s.len) : (i += 1) {
        serialWriteChar(s[i]);
    }
}

pub fn serialWriteHex(value: u64) void {
    const digits = "0123456789abcdef";
    var i: u8 = 0;
    while (i < 16) : (i += 1) {
        const shift: u64 = (15 - @intCast(i)) * 4;
        const nibble: u8 = @intCast(@truncate((value >> shift) & 0xf));
        serialWriteChar(digits[nibble]);
    }
}

pub fn serialWriteDec(value: u64) void {
    if (value == 0) {
        serialWriteChar('0');
        return;
    }
    var buf: [20]u8 = undefined;
    var len: usize = 0;
    var v: u64 = value;
    while (v > 0) : (v /= 10) {
        buf[len] = '0' + @intCast(@truncate(v % 10));
        len += 1;
    }
    while (len > 0) : (len -= 1) {
        serialWriteChar(buf[len - 1]);
    }
}
