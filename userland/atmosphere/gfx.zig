/// GFX syscall wrappers (83-89).
/// Fiecare syscall returnează un pachet în rax: status în biții 32-63, payload în 0-31.

const Syscall = .{
    GFX_GET_ADDR = 83,
    GFX_GET_WIDTH = 84,
    GFX_GET_HEIGHT = 85,
    GFX_GET_PITCH = 86,
    GFX_GET_BPP = 87,
    GFX_GET_SIZE = 88,
    GFX_FRAME = 89,
};

const Result = .{
    OK = 0,
    INVALID_ARGUMENT = 1,
    WOULD_BLOCK = 2,
    END_OF_FILE = 3,
    DENIED = 4,
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

fn syscall5(number: u64, a0: u64, a1: u64, a2: u64, a3: u64, a4: u64) u64 {
    var result: u64 = undefined;
    asm volatile (
        "mov %4, %%r10\n\t"
        "mov %5, %%r8\n\t"
        "syscall"
        : [result] "=a"(result)
        : [number] "0"(number), [a0] "D"(a0), [a1] "S"(a1), [a2] "d"(a2), [a3] "r"(a3), [a4] "r"(a4)
        : "rcx", "r11", "r10", "r8", "memory"
    );
    return result;
}

/// Extrage statusul (biții 32-63) dintr-un pachet syscall.
fn statusOf(packed: u64) u32 {
    return @intCast(@truncate(packed >> 32));
}

/// Extrage payload-ul (biții 0-31) dintr-un pachet syscall.
fn payloadOf(packed: u64) u32 {
    return @intCast(@truncate(packed));
}

pub fn resultOk(packed: u64) bool {
    return statusOf(packed) == Result.OK;
}

pub struct GfxInfo {
    pub address: ?[*]align(1) u8 = null,
    pub width: u64 = 0,
    pub height: u64 = 0,
    pub pitch: u64 = 0,
    pub bpp: u16 = 0,
    pub framebuffer_size: u32 = 0,
    pub rgb: u8 = 0,
}

/// Obține adresa framebuffer-ului (fizică, traducată de kernel prin HHDM).
pub fn gfxGetAddr() ?u64 {
    const r = syscall1(Syscall.GFX_GET_ADDR, 0);
    if (!resultOk(r)) return null;
    return @intCast(payloadOf(r));
}

pub fn gfxGetWidth() ?u64 {
    const r = syscall1(Syscall.GFX_GET_WIDTH, 0);
    if (!resultOk(r)) return null;
    return @intCast(payloadOf(r));
}

pub fn gfxGetHeight() ?u64 {
    const r = syscall1(Syscall.GFX_GET_HEIGHT, 0);
    if (!resultOk(r)) return null;
    return @intCast(payloadOf(r));
}

pub fn gfxGetPitch() ?u64 {
    const r = syscall1(Syscall.GFX_GET_PITCH, 0);
    if (!resultOk(r)) return null;
    return @intCast(payloadOf(r));
}

pub fn gfxGetBpp() ?u16 {
    const r = syscall1(Syscall.GFX_GET_BPP, 0);
    if (!resultOk(r)) return null;
    return @intCast(@truncate(payloadOf(r)));
}

pub fn gfxGetSize() ?u32 {
    const r = syscall1(Syscall.GFX_GET_SIZE, 0);
    if (!resultOk(r)) return null;
    return payloadOf(r);
}

/// Desenează un cadru complet desktop în kernel (evită round-trip-uri userspace).
/// Returnează true dacă cadru a fost desenat cu succes.
pub fn gfxFrame() bool {
    const r = syscall1(Syscall.GFX_FRAME, 0);
    return resultOk(r);
}

/// Desenează un pixel (opțional, pentru desen fin).
pub fn gfxPutPixel(x: u64, y: u64, color: u32) bool {
    const r = syscall5(Syscall.GFX_DRAW_PIXEL, x, y, @intCast(color), 0, 0);
    return resultOk(r);
}

/// Umple un dreptunghi (opțional).
pub fn gfxFillRect(x: u64, y: u64, w: u64, h: u64, color: u32) bool {
    const r = syscall5(Syscall.GFX_FILL_RECT, x, y, w, h, @intCast(color));
    return resultOk(r);
}

/// Șterge ecranul cu o culoare.
pub fn gfxClear(color: u32) bool {
    const r = syscall5(Syscall.GFX_CLEAR, @intCast(color), 0, 0, 0, 0);
    return resultOk(r);
}

/// Flip / sincronizează afișarea.
pub fn gfxFlip() bool {
    const r = syscall5(Syscall.GFX_FLIP, 0, 0, 0, 0, 0);
    return resultOk(r);
}

pub fn gfxRgb(r: u8, g: u8, b: u8) u32 {
    return (@intCast(r) << 16) | (@intCast(g) << 8) | @intCast(b);
}

/// Obține toate informațiile despre framebuffer într-un GfxInfo.
/// Retornează false dacă oricare syscall GET eșuează.
pub fn gfxGetInfo(info: *GfxInfo) bool {
    const addr = gfxGetAddr() or return false;
    info.address = @intToPtr(?[*]align(1) u8, addr);
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
