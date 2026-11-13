const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const exe = b.addExecutable(.{
        .name = "compositor",
        .root_source_file = b.path("compositor.zig"),
        .target = target,
        .optimize = optimize,
    });

    // Freestanding userland ELF: fără libc, fără exceptions, fără RTTI.
    // Link script-ul este userland/user.ld (adresa de încărcare 0x400000).
    exe.setBuildMode(std.Mode.ReleaseSmall);

    exe.installArtifact(exe);

    const run_cmd = b.addRunArtifact(exe);
    run_cmd.step.dependOn(b.getInstallStep());

    if (b.args) |args| {
        run_cmd.addArgs(args);
    }
}
