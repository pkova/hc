const std = @import("std");

// zig build builds hoon-lsp and hc into zig-out/bin, optimized

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    // optimized unless asked otherwise, -Doptimize=Debug being checked
    // for undefined behavior as it runs
    const optimize = b.option(std.builtin.OptimizeMode, "optimize", "Debug, ReleaseFast, ReleaseSafe or ReleaseSmall") orelse .ReleaseFast;
    const os = target.result.os.tag;

    const flags: []const []const u8 = &.{
        "-Wall",
        "-Wextra",
        "-Wconversion",
        "-Wdouble-promotion",
        "-Wno-unused-parameter",
        "-Wno-unused-function",
        "-Wno-sign-conversion",
        // msvc's pragmas, and the build date in the cache key
        "-Wno-unknown-pragmas",
        "-Wno-date-time",
        // linux and windows supply their own entry point and syscalls,
        // so nothing from a runtime: traps for undefined behavior, not
        // the sanitizer's reporting
        "-fno-stack-protector",
        "-fsanitize-trap=undefined",
    };

    const lsp = b.addExecutable(.{
        .name = "hoon-lsp",
        .root_module = b.createModule(.{ .target = target, .optimize = optimize }),
    });
    lsp.root_module.addCSourceFile(.{ .file = b.path("main.c"), .flags = flags });

    const hc = b.addExecutable(.{
        .name = "hc",
        .root_module = b.createModule(.{ .target = target, .optimize = optimize }),
    });
    hc.root_module.addCSourceFile(.{ .file = b.path("hc/hc.c"), .flags = flags });

    // only macos goes through libc; linux has _start in hoon.c and
    // windows its own entry, so crt1 would clash with them
    if (os == .macos) {
        lsp.root_module.link_libc = true;
        hc.root_module.link_libc = true;
    }

    b.installArtifact(lsp);
    b.installArtifact(hc);
}
