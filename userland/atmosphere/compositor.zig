const std = @import("std");
const Serial = @import("serial.zig");
const Gfx = @import("gfx.zig");
const Input = @import("input.zig");

/// Desenează un cadru desktop simplu (doar dreptunghiuri, fără fonturi care
/// ar necesita caractere bitmap în kernel).
fn drawFrame() void {
    const info_ptr = std.heap.page_allocator.alloc(Gfx.GfxInfo, 1) catch return;
    defer std.heap.page_allocator.free(info_ptr);

    const info = info_ptr.*;
    if (!Gfx.gfxGetInfo(&info)) {
        Serial.serialWrite("compositor: gfxGetInfo failed\n");
        return;
    }

    const width = info.width;
    const height = info.height;

    // Background gradient (nuancier albastru-închis)
    var y: u64 = 0;
    while (y < height) : (y += 8) {
        const h8 = if (height == 0) 1 else height;
        const b = @intCast(@truncate((10 + (y * 50 / h8)) % 256));
        _ = Gfx.gfxFillRect(0, y, width, 8, Gfx.gfxRgb(5, 7, b));
    }

    // Top bar
    _ = Gfx.gfxFillRect(0, 0, width, 40, Gfx.gfxRgb(0x1a, 0x1a, 0x2a));
    _ = Gfx.gfxFillRect(0, 40, width, 2, Gfx.gfxRgb(0x2a, 0x2a, 0x3a));

    // Placeholder-uri pentru ore și dată (dreptunghiuri gri)
    _ = Gfx.gfxFillRect(20, 10, 60, 8, Gfx.gfxRgb(0x2a, 0x2a, 0x3a));
    _ = Gfx.gfxFillRect(20, 24, 60, 8, Gfx.gfxRgb(0x2a, 0x2a, 0x3a));

    // Dock cu 4 sloturi goale
    const dock_w: u64 = 120;
    const dock_x: u64 = 20;
    const dock_y: u64 = 76;
    const cell: u64 = 48;
    _ = Gfx.gfxFillRect(dock_x, dock_y, dock_w, 200, Gfx.gfxRgb(0x1a, 0x1a, 0x22));
    _ = Gfx.gfxFillRect(dock_x, dock_y, dock_w, 2, Gfx.gfxRgb(0x3a, 0x3a, 0x3a));

    var i: u32 = 0;
    while (i < 4) : (i += 1) {
        const col = i % 2;
        const row = i / 2;
        const cx = dock_x + 24 + @intCast(col) * cell;
        const cy = dock_y + 30 + @intCast(row) * cell;
        _ = Gfx.gfxFillRect(cx - 12, cy - 12, 24, 24, Gfx.gfxRgb(0x2a, 0x2a, 0x32));
        _ = Gfx.gfxFillRect(cx - 8, cy - 8, 16, 16, Gfx.gfxRgb(0x1a, 0x1a, 0x1a));
    }

    // Taskbar (sub sol)
    const tb_y = if (height >= 44) height - 44 else 0;
    _ = Gfx.gfxFillRect(0, tb_y, width, 44, Gfx.gfxRgb(0x1a, 0x1a, 0x1a));
    _ = Gfx.gfxFillRect(0, tb_y, width, 2, Gfx.gfxRgb(0x2a, 0x2a, 0x3a));
    _ = Gfx.gfxFillRect(14, tb_y + 10, 24, 24, Gfx.gfxRgb(0x2a, 0x2a, 0x3a));
    _ = Gfx.gfxFillRect(26, tb_y + 22, 10, 10, Gfx.gfxRgb(0xee, 0xee, 0xee));
    _ = Gfx.gfxFillRect(width - 130, tb_y + 22, 10, 10, Gfx.gfxRgb(0x88, 0x88, 0x88));
    _ = Gfx.gfxFillRect(width - 110, tb_y + 10, 40, 8, Gfx.gfxRgb(0x2a, 0x2a, 0x3a));

    // Cursor mouse (centrat)
    const mx = width / 2 - 4;
    const my = height / 2 - 4;
    _ = Gfx.gfxFillRect(mx, my, 8, 8, Gfx.gfxRgb(0xee, 0xee, 0xee));

    _ = Gfx.gfxFlip();
}

/// Afișează detaliile unui eveniment de input pe serial.
fn handleInputEvent(event: Input.InputEvent) void {
    Serial.serialWrite("input: type=");
    Serial.serialWriteDec(@intCast(event.type));
    Serial.serialWrite(" code=");
    Serial.serialWriteDec(@intCast(event.key_code));
    Serial.serialWrite(" x=");
    Serial.serialWriteDec(@intCast(@bitCast(u32, event.x)));
    Serial.serialWrite(" y=");
    Serial.serialWriteDec(@intCast(@bitCast(u32, event.y)));
    Serial.serialWrite("\n");
}

pub export fn _start() noreturn void {
    Serial.serialWrite("atmosphere: entry\n");
    Serial.serialWrite("atmosphere: gui started\n");

    while (true) {
        // Desenează un cadru (kernel syscall GFX_FRAME = 89)
        if (!Gfx.gfxFrame()) {
            Serial.serialWrite("compositor: gfxFrame failed\n");
        }

        // Procesează toate evenimentele de input aflate în coadă
        var count = Input.inputPoll();
        while (count > 0) : (count -= 1) {
            const event = Input.inputGet();
            if (event) |ev| {
                handleInputEvent(ev);
            }
        }
    }
}
