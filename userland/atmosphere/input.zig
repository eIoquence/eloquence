/// Input syscall wrappers (77-78).

const INPUT_GET: u64 = 77;
const INPUT_POLL: u64 = 78;

const RESULT_OK: u32 = 0;
const RESULT_WOULD_BLOCK: u32 = 2;

const EVENT_KEY_DOWN: u8 = 0;
const EVENT_KEY_UP: u8 = 1;
const EVENT_MOUSE_MOVE: u8 = 2;
const EVENT_MOUSE_BTN_DOWN: u8 = 3;
const EVENT_MOUSE_BTN_UP: u8 = 4;

fn syscall1(number: u64, a0: u64) u64 {
    var result: u64 = undefined;
    asm volatile (
        "syscall"
        : [result] = result
        : [number] = number, [a0] = a0
    );
    return result;
}

fn eventTypeOf(pk: u64) u8 {
    return @as(u8, @intCast(pk >> 56));
}

fn keyCodeOf(pk: u64) u8 {
    return @as(u8, @intCast((pk >> 48) & 0xff));
}

fn eventXOf(pk: u64) i32 {
    return @as(i32, @intCast(pk >> 32));
}

fn eventYOf(pk: u64) i32 {
    return @as(i32, @intCast(pk));
}

fn statusOf(pk: u64) u32 {
    return @as(u32, @intCast(pk >> 32));
}

pub const InputEvent = struct {
    type: u8 = 0,
    key_code: u8 = 0,
    x: i32 = 0,
    y: i32 = 0,
};

pub fn decodeEvent(pk: u64) InputEvent {
    return .{
        .type = eventTypeOf(pk),
        .key_code = keyCodeOf(pk),
        .x = eventXOf(pk),
        .y = eventYOf(pk),
    };
}

pub fn inputPoll() u64 {
    const r = syscall1(INPUT_POLL, 0);
    if (statusOf(r) != RESULT_OK) return 0;
    return @intCast(r);
}

pub fn inputGet() ?InputEvent {
    const r = syscall1(INPUT_GET, 0);
    if (statusOf(r) != RESULT_OK) return null;
    return decodeEvent(r);
}
