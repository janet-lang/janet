const std = @import("std");

const core_sources = [_][]const u8{
    "src/core/abstract.c",
    "src/core/array.c",
    "src/core/asm.c",
    "src/core/buffer.c",
    "src/core/bytecode.c",
    "src/core/capi.c",
    "src/core/cfuns.c",
    "src/core/compile.c",
    "src/core/corelib.c",
    "src/core/debug.c",
    "src/core/emit.c",
    "src/core/ev.c",
    "src/core/ffi.c",
    "src/core/fiber.c",
    "src/core/filewatch.c",
    "src/core/gc.c",
    "src/core/inttypes.c",
    "src/core/io.c",
    "src/core/marsh.c",
    "src/core/math.c",
    "src/core/net.c",
    "src/core/os.c",
    "src/core/parse.c",
    "src/core/peg.c",
    "src/core/pp.c",
    "src/core/regalloc.c",
    "src/core/run.c",
    "src/core/specials.c",
    "src/core/state.c",
    "src/core/string.c",
    "src/core/strtod.c",
    "src/core/struct.c",
    "src/core/symcache.c",
    "src/core/table.c",
    "src/core/tuple.c",
    "src/core/util.c",
    "src/core/value.c",
    "src/core/vector.c",
    "src/core/vm.c",
    "src/core/wrap.c",
};

const boot_sources = [_][]const u8{
    "src/boot/array_test.c",
    "src/boot/boot.c",
    "src/boot/buffer_test.c",
    "src/boot/number_test.c",
    "src/boot/system_test.c",
    "src/boot/table_test.c",
};

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    // JANET_BUILD?="\"$(shell git log --pretty=format:'%h' -n 1 2> /dev/null || echo local)\""
    const janet_build = "\"local\"";
    const clibs = [_][]const u8{ "-lm", "-lpthread" };
    _ = clibs;

    const cflags = [_][]const u8{ "-O2", "-g" };

    const common_cflags = [_][]const u8{ "-std=c99", "-Wall", "-Wextra", "-fvisibility=hidden", "-fPIC" };
    const boot_cflags = [_][]const u8{ "-DJANET_BOOTSTRAP", b.fmt("-DJANET_BUILD={s}", .{janet_build}), "-O0", "-g" } ++ common_cflags;
    const build_cflags = cflags ++ common_cflags;
    _ = build_cflags;

    const janet_boot_mod = b.addModule("janet_boot", .{
        .optimize = optimize,
        .target = target,
    });
    janet_boot_mod.addCSourceFiles(.{
        .files = &(core_sources ++ boot_sources),
        .flags = &boot_cflags,
    });
    janet_boot_mod.addIncludePath(b.path("src/include"));
    janet_boot_mod.addIncludePath(b.path("src/conf"));

    const janet_boot_exe = b.addExecutable(.{
        .name = "janet_boot",
        .root_module = janet_boot_mod,
    });
    b.installArtifact(janet_boot_exe);

    const mod = b.addModule("janet", .{
        .root_source_file = b.path("src/root.zig"),
        .target = target,
    });

    const exe = b.addExecutable(.{
        .name = "janet",
        .root_module = b.createModule(.{
            .root_source_file = b.path("src/main.zig"),
            .target = target,
            .optimize = optimize,
            .imports = &.{
                .{ .name = "janet", .module = mod },
            },
        }),
    });

    b.installArtifact(exe);

    const run_step = b.step("run", "Run the app");

    const run_cmd = b.addRunArtifact(exe);
    run_step.dependOn(&run_cmd.step);

    run_cmd.step.dependOn(b.getInstallStep());

    if (b.args) |args| {
        run_cmd.addArgs(args);
    }

    const mod_tests = b.addTest(.{
        .root_module = mod,
    });

    const run_mod_tests = b.addRunArtifact(mod_tests);

    const exe_tests = b.addTest(.{
        .root_module = exe.root_module,
    });

    const run_exe_tests = b.addRunArtifact(exe_tests);

    const test_step = b.step("test", "Run tests");
    test_step.dependOn(&run_mod_tests.step);
    test_step.dependOn(&run_exe_tests.step);
}
