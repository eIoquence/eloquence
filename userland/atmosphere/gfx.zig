/// GFX syscall wrappers (83-89).

const GFX_GET_ADDR: u64 = 83;
const GFX_GET_WIDTH: u64 = 84;
const GFX_GET_HEIGHT: u64 = 85;
const GFX_GET_PITCH: u64 = 86;
const GFX_GET_BPP: u64 = 87;
const GFX_GET_SIZE: u64 = 88;
const GFX_FRAME: u64 = 89;
const GFX_DRAW_PIXEL: u64 = 73;
const GFX_FILL_RECT: u64 = 74;
const GFX_CLEAR: u64 = 75;
const GFX_FLIP: u64 = 76;

const RESULT_OK: u32 = 0;
const RESULT_INVALID_ARGUMENT: u32 = 1;
const RESULT_WOULD_BLOCK: u32 = 2;
const RESULT_END_OF_FILE: u32 = 3;
const RESULT_DENIED: u32 = 4;

fn syscall1(number: u64, a0: u64) u64 {
    var result: u64 = undefined;
    asm volatile (
        "syscall"
        : [result] = result
        : [number] = number, [a0] = a0
    );
    return result;
}

fn syscall5(number: u64, a0: u64, a1: u64, a2: u64, a3: u64, a4: u64) u64 {
    var result: u64 = undefined;
    asm volatile (
        "mov %4, %%r10; mov %5, %%r8; syscall"
        : [result] = result
        : [number] = number, [a0] = a0, [a1] = a1, [a2] = a2, [a3] = a3, [a4] = a4
    );
    return result;
}

fn statusOf(pk: u64) u32 {
    return @as(u32, @intCast(pk >> 32));
}

fn payloadOf(pk: u64) u32 {
    return @as(u32, @intCast(pk));
}

pub fn resultOk(pk: u64) bool {
    return statusOf(pk) == RESULT_OK;
}

pub const GfxInfo = struct {
    address: ?[*]align(1) u8 = null,
    width: u64 = 0,
    height: u64 = 0,
    pitch: u64 = 0,
    bpp: u16 = 0,
    framebuffer_size: u32 = 0,
    rgb: u8 = 0,
};

pub fn gfxGetAddr() ?u64 {
    const r = syscall1(GFX_GET_ADDR, 0);
    if (!resultOk(r)) return null;
    return @intCast(payloadOf(r));
}

pub fn gfxGetWidth() ?u64 {
    const r = syscall1(GFX_GET_WIDTH, 0);
    if (!resultOk(r)) return null;
    return @intCast(payloadOf(r));
}

pub fn gfxGetHeight() ?u64 {
    const r = syscall1(GFX_GET_HEIGHT, 0);
    if (!resultOk(r)) return null;
    return @intCast(payloadOf(r));
}

pub fn gfxGetPitch() ?u64 {
    const r = syscall1(GFX_GET_PITCH, 0);
    if (!resultOk(r)) return null;
    return @intCast(payloadOf(r));
}

pub fn gfxGetBpp() ?u16 {
    const r = syscall1(GFX_GET_BPP, 0);
    if (!resultOk(r)) return null;
    return @as(u16, @intCast(payloadOf(r)));
}

pub fn gfxGetSize() ?u32 {
    const r = syscall1(GFX_GET_SIZE, 0);
    if (!resultOk(r)) return null;
    return payloadOf(r);
}

pub fn gfxFrame() bool {
    const r = syscall1(GFX_FRAME, 0);
    return resultOk(r);
}

pub fn gfxPutPixel(x: u64, y: u64, color: u32) bool {
    const r = syscall5(GFX_DRAW_PIXEL, x, y, @intCast(color), 0, 0);
    return resultOk(r);
}

pub fn gfxFillRect(x: u64, y: u64, w: u64, h: u64, color: u32) bool {
    const r = syscall5(GFX_FILL_RECT, x, y, w, h, @intCast(color));
    return resultOk(r);
}

pub fn gfxClear(color: u32) bool {
    const r = syscall5(GFX_CLEAR, @intCast(color), 0, 0, 0, 0);
    return resultOk(r);
}

pub fn gfxFlip() bool {
    const r = syscall5(GFX_FLIP, 0, 0, 0, 0, 0);
    return resultOk(r);
}

pub fn gfxRgb(r: u8, g: u8, b: u8) u32 {
    return (@as(u32, @intCast(r)) << 16) | (@as(u32, @intCast(g)) << 8) | @as(u32, @intCast(b));
}

pub fn gfxGetInfo(info: *GfxInfo) bool {
    const addr = gfxGetAddr() or return false;
    info.address = @ptrCast(addr);
    const w = gfxGetWidth() or return false;
    info.width = w;
    const h = gfxGetHeight() or return false;
    info.height = h;
    const p = gfxGetPitch() or return false;
    info.pitch = p;
    const b = gfxGetBpp() or return false;
    info.bpp = b;
    const s = gfxGetSize() or return false;
    info.framebuffer_size = s;
    info.rgb = 1;
    return true;
}
