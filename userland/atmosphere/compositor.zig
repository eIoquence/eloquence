const RESULT_OK: u32 = 0;
const RESULT_WOULD_BLOCK: u32 = 2;

const InputEvent = struct {
    type: u8,
    key_code: u8,
    x: i32,
    y: i32,
};

const GfxInfo = struct {
    address: ?[*]align(1) u8 = null,
    width: u64 = 0,
    height: u64 = 0,
    pitch: u64 = 0,
    bpp: u16 = 0,
    framebuffer_size: u32 = 0,
    rgb: u8 = 0,
};

fn statusOf(pk: u64) u32 {
    return @as(u32, @intCast(pk >> 32));
}

fn resultOk(pk: u64) bool {
    return statusOf(pk) == RESULT_OK;
}

fn gfxRgb(r: u8, g: u8, b: u8) u32 {
    return (@as(u32, @intCast(r)) << 16) | (@as(u32, @intCast(g)) << 8) | @as(u32, @intCast(b));
}

extern fn sys1_wrapper(num: u64, a0: u64) u64;
extern fn sys6_wrapper(num: u64, a0: u64, a1: u64, a2: u64, a3: u64, a4: u64) u64;

fn sys1(num: u64, a0: u64) u64 {
    return sys1_wrapper(num, a0);
}

fn sys6(num: u64, a0: u64, a1: u64, a2: u64, a3: u64, a4: u64) u64 {
    return sys6_wrapper(num, a0, a1, a2, a3, a4);
}

fn serialWriteChar(c: u8) void {
    const r = sys1(4, @as(u64, @intCast(c)));
    _ = r;
}

fn serialWrite(s: []const u8) void {
    var i: usize = 0;
    while (i < s.len) : (i += 1) {
        serialWriteChar(s[i]);
    }
}

fn serialWriteDec(value: u64) void {
    if (value == 0) {
        serialWriteChar('0');
        return;
    }
    var buf: [20]u8 = undefined;
    var len: usize = 0;
    var v: u64 = value;
    while (v > 0) : (v /= 10) {
        buf[len] = '0' + @as(u8, @intCast(@as(u8, @truncate(v % 10))));
        len += 1;
    }
    while (len > 0) : (len -= 1) {
        serialWriteChar(buf[len - 1]);
    }
}

fn gfxFillRect(x: u64, y: u64, w: u64, h: u64, color: u32) bool {
    const r = sys6(74, x, y, w, h, @as(u64, @intCast(color)));
    return resultOk(r);
}

fn gfxFlip() bool {
    const r = sys6(76, 0, 0, 0, 0, 0);
    return resultOk(r);
}

fn gfxGetInfo(info: *GfxInfo) bool {
    var r: u64 = 0;
    r = sys1(83, 0);
    if (!resultOk(r)) return false;
    info.address = @ptrCast(@as(u64, @intCast(@as(u32, @intCast(r & 0xffffffff)))));

    r = sys1(84, 0);
    if (!resultOk(r)) return false;
    info.width = @as(u64, @intCast(@as(u32, @intCast(r & 0xffffffff))));

    r = sys1(85, 0);
    if (!resultOk(r)) return false;
    info.height = @as(u64, @intCast(@as(u32, @intCast(r & 0xffffffff))));

    r = sys1(86, 0);
    if (!resultOk(r)) return false;
    info.pitch = @as(u64, @intCast(@as(u32, @intCast(r & 0xffffffff))));

    r = sys1(87, 0);
    if (!resultOk(r)) return false;
    info.bpp = @as(u16, @intCast(@as(u32, @intCast(r & 0xffffffff))));

    r = sys1(88, 0);
    if (!resultOk(r)) return false;
    info.framebuffer_size = @as(u32, @intCast(r & 0xffffffff));

    info.rgb = 1;
    return true;
}

fn gfxFrame() bool {
    const r = sys1(89, 0);
    return resultOk(r);
}

fn inputPoll() u64 {
    const r = sys1(78, 0);
    if (!resultOk(r)) return 0;
    return @intCast(r & 0xffffffff);
}

fn inputGet() ?InputEvent {
    const r = sys1(77, 0);
    if (!resultOk(r)) return null;
    return .{
        .type = @as(u8, @intCast(r >> 56)),
        .key_code = @as(u8, @intCast((r >> 48) & 0xff)),
        .x = @as(i32, @intCast(r >> 32)),
        .y = @as(i32, @intCast(r)),
    };
}

fn drawFrame() void {
    var info = GfxInfo{};
    if (!gfxGetInfo(&info)) {
        serialWrite("compositor: gfxGetInfo failed\n");
        return;
    }

    const width = info.width;
    const height = info.height;

    var y: u64 = 0;
    while (y < height) : (y += 8) {
        const h8 = if (height == 0) 1 else height;
        const b: u8 = @as(u8, @truncate((10 + (y * 50 / h8)) % 256));
        _ = gfxFillRect(0, y, width, 8, gfxRgb(5, 7, b));
    }

    _ = gfxFillRect(0, 0, width, 40, gfxRgb(0x1a, 0x1a, 0x2a));
    _ = gfxFillRect(0, 40, width, 2, gfxRgb(0x2a, 0x2a, 0x3a));
    _ = gfxFillRect(20, 10, 60, 8, gfxRgb(0x2a, 0x2a, 0x3a));
    _ = gfxFillRect(20, 24, 60, 8, gfxRgb(0x2a, 0x2a, 0x3a));

    const dock_w: u64 = 120;
    const dock_x: u64 = 20;
    const dock_y: u64 = 76;
    const cell: u64 = 48;
    _ = gfxFillRect(dock_x, dock_y, dock_w, 200, gfxRgb(0x1a, 0x1a, 0x22));
    _ = gfxFillRect(dock_x, dock_y, dock_w, 2, gfxRgb(0x3a, 0x3a, 0x3a));

    var i: u32 = 0;
    while (i < 4) : (i += 1) {
        const col = i % 2;
        const row = i / 2;
        const cx = dock_x + 24 + @as(u64, @intCast(col)) * cell;
        const cy = dock_y + 30 + @as(u64, @intCast(row)) * cell;
        _ = gfxFillRect(cx - 12, cy - 12, 24, 24, gfxRgb(0x2a, 0x2a, 0x32));
        _ = gfxFillRect(cx - 8, cy - 8, 16, 16, gfxRgb(0x1a, 0x1a, 0x1a));
    }

    const tb_y = if (height >= 44) height - 44 else 0;
    _ = gfxFillRect(0, tb_y, width, 44, gfxRgb(0x1a, 0x1a, 0x1a));
    _ = gfxFillRect(0, tb_y, width, 2, gfxRgb(0x2a, 0x2a, 0x3a));
    _ = gfxFillRect(14, tb_y + 10, 24, 24, gfxRgb(0x2a, 0x2a, 0x3a));
    _ = gfxFillRect(26, tb_y + 22, 10, 10, gfxRgb(0xee, 0xee, 0xee));
    _ = gfxFillRect(width - 130, tb_y + 22, 10, 10, gfxRgb(0x88, 0x88, 0x88));
    _ = gfxFillRect(width - 110, tb_y + 10, 40, 8, gfxRgb(0x2a, 0x2a, 0x3a));

    const mx = width / 2 - 4;
    const my = height / 2 - 4;
    _ = gfxFillRect(mx, my, 8, 8, gfxRgb(0xee, 0xee, 0xee));

    _ = gfxFlip();
}

fn handleInputEvent(ev: InputEvent) void {
    serialWrite("input: type=");
    serialWriteDec(@as(u64, @intCast(ev.type)));
    serialWrite(" code=");
    serialWriteDec(@as(u64, @intCast(ev.key_code)));
    serialWrite(" x=");
    serialWriteDec(@as(u64, @intCast(ev.x)));
    serialWrite(" y=");
    serialWriteDec(@as(u64, @intCast(ev.y)));
    serialWrite("\n");
}

pub export fn _start() void {
    serialWrite("atmosphere: entry\n");
    serialWrite("atmosphere: gui started\n");

    while (true) {
        if (!gfxFrame()) {
            serialWrite("compositor: gfxFrame failed\n");
        }

        var count = inputPoll();
        while (count > 0) : (count -= 1) {
            const ev = inputGet();
            if (ev) |e| {
                handleInputEvent(e);
            }
        }
    }
}
