/// Input syscall wrappers (77-78).
/// Kernelul returnează evenimente pachetate în rax:
///   biții 56-63: type (0=key_down, 1=key_up, 2=mouse_move, 3=mouse_btn_down, 4=mouse_btn_up)
///   biții 48-55: key_code
///   biții 32-47: x (int32 truncat la u32)
///   biții  0-31: y (int32 truncat la u32)

const Syscall = .{
    INPUT_GET = 77,
    INPUT_POLL = 78,
};

const Result = .{
    OK = 0,
    WOULD_BLOCK = 2,
};

const EventType = .{
    KEY_DOWN = 0,
    KEY_UP = 1,
    MOUSE_MOVE = 2,
    MOUSE_BTN_DOWN = 3,
    MOUSE_BTN_UP = 4,
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

/// Extrage type dintr-un pachet eveniment (biții 56-63).
fn eventTypeOf(packed: u64) u8 {
    return @intCast(@truncate(packed >> 56));
}

/// Extrage key_code dintr-un pachet eveniment (biții 48-55).
fn keyCodeOf(packed: u64) u8 {
    return @intCast(@truncate((packed >> 48) & 0xff));
}

/// Extrage x dintr-un pachet eveniment (biții 32-47, semnat).
fn eventXOf(packed: u64) i32 {
    return @intCast(@truncate(packed >> 32));
}

/// Extrage y dintr-un pachet eveniment (biții 0-31, semnat).
fn eventYOf(packed: u64) i32 {
    return @intCast(@truncate(packed));
}

pub struct InputEvent {
    pub type: u8 = 0,
    pub key_code: u8 = 0,
    pub x: i32 = 0,
    pub y: i32 = 0,
}

/// Decodifică un pachet eveniment într-o structură InputEvent.
pub fn decodeEvent(packed: u64) InputEvent {
    return .{
        .type = eventTypeOf(packed),
        .key_code = keyCodeOf(packed),
        .x = eventXOf(packed),
        .y = eventYOf(packed),
    };
}

/// Returnează numărul de evenimente aflate în coada kernel.
pub fn inputPoll() u64 {
    const r = syscall1(Syscall.INPUT_POLL, 0);
    if (statusOf(r) != Result.OK) return 0;
    return @intCast(r);
}

/// Pop-e un eveniment din coada kernel. Retornează null dacă coada e goală.
pub fn inputGet() ?InputEvent {
    const r = syscall1(Syscall.INPUT_GET, 0);
    if (statusOf(r) != Result.OK) return null;
    return decodeEvent(r);
}
