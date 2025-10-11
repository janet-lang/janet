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

const test_scripts = [_][]const u8{
    "test/suite-array.janet",
    "test/suite-asm.janet",
    "test/suite-boot.janet",
    "test/suite-buffer.janet",
    "test/suite-bundle.janet",
    "test/suite-capi.janet",
    "test/suite-cfuns.janet",
    "test/suite-compile.janet",
    "test/suite-corelib.janet",
    "test/suite-debug.janet",
    "test/suite-ev.janet",
    "test/suite-ev2.janet",
    "test/suite-ffi.janet",
    "test/suite-filewatch.janet",
    "test/suite-inttypes.janet",
    "test/suite-io.janet",
    "test/suite-marsh.janet",
    "test/suite-math.janet",
    "test/suite-os.janet",
    "test/suite-parse.janet",
    "test/suite-peg.janet",
    "test/suite-pp.janet",
    "test/suite-specials.janet",
    "test/suite-string.janet",
    "test/suite-strtod.janet",
    "test/suite-struct.janet",
    "test/suite-symcache.janet",
    "test/suite-table.janet",
    "test/suite-tuple.janet",
    "test/suite-unknown.janet",
    "test/suite-value.janet",
    "test/suite-vm.janet",
};

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const prefix = b.option([]const u8, "prefix", "Installation prefix") orelse "/usr/local";

    const janetconf_header = b.option([]const u8, "janetconf_header", "Path to configuration heaeder") orelse "src/conf/janetconf.h";
    const includedir = b.option([]const u8, "includedir", "Header installation path") orelse b.fmt("{s}/include", .{prefix});
    _ = includedir;
    const bindir = b.option([]const u8, "bindir", "Binary installation path") orelse b.fmt("{s}/bin", .{prefix});
    _ = bindir;
    const libdir = b.option([]const u8, "libdir", "Library installation path") orelse b.fmt("{s}/lib", .{prefix});
    // TODO: JANET_BUILD?="\"$(shell git log --pretty=format:'%h' -n 1 2> /dev/null || echo local)\""
    const janet_build = b.option([]const u8, "janet_build", "Build version identifier") orelse "\"local\"";
    // const clibs = [_][]const u8{ "-lm", "-lpthread" };
    const janet_path = b.option([]const u8, "janet_path", "Janet library installation path") orelse b.fmt("{s}/janet", .{libdir});
    const janet_manpath = b.option([]const u8, "janet_manpath", "Man page installation path") orelse b.fmt("{s}/share/man/man1/", .{prefix});
    _ = janet_manpath;
    const janet_pkg_config_path = b.option([]const u8, "janet_pkg_config_path", "pkg-config files installation path") orelse b.fmt("{s}/pkgconfig", .{libdir});
    _ = janet_pkg_config_path;
    const janet_dist_dir = b.option([]const u8, "janet_dist_dir", "Output directory for distribution files") orelse "janet-dist";
    _ = janet_dist_dir;
    const jpm_tag = b.option([]const u8, "jpm_tag", "Git tag for jpm build") orelse "master";
    _ = jpm_tag;
    const spork_tag = b.option([]const u8, "spork_tag", "Git tag for spork build") orelse "master";
    _ = spork_tag;
    const has_shared = b.option(bool, "has_shared", "Build shared library") orelse true;

    const cflags = [_][]const u8{ "-O2", "-g" };

    const common_cflags = [_][]const u8{ "-std=c99", "-Wall", "-Wextra", "-fvisibility=hidden", "-fPIC" };
    const boot_cflags = [_][]const u8{ "-DJANET_BOOTSTRAP", b.fmt("-DJANET_BUILD={s}", .{janet_build}), "-O0", "-g" } ++ common_cflags;
    const build_cflags = cflags ++ common_cflags;

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

    const janet_boot_run = b.addRunArtifact(janet_boot_exe);

    // janet_boot_run.addArg(".");
    janet_boot_run.addDirectoryArg(b.path(""));
    janet_boot_run.addArg("JANET_PATH");
    janet_boot_run.addArg(b.fmt("'{s}'", .{janet_path}));

    const janet_no_amalg = b.option(bool, "janet_no_amalg", "Disable amalgamated build") orelse false;
    // Disable amalgamated build
    if (janet_no_amalg) {
        janet_boot_run.addArg("image-only");
    }

    const janet_boot_output = janet_boot_run.captureStdOut();
    b.getInstallStep().dependOn(&b.addInstallFile(janet_boot_output, "c/janet.c").step);

    const janet_mod = b.addModule("janet", .{
        .optimize = optimize,
        .target = target,
    });
    janet_mod.addCSourceFile(.{
        .file = janet_boot_output,
        .flags = &build_cflags,
        .language = .c,
    });
    janet_mod.addCSourceFile(.{
        .file = b.path("src/mainclient/shell.c"),
        .flags = &build_cflags,
    });
    if (janet_no_amalg) {
        janet_mod.addCSourceFiles(.{
            .files = &(core_sources),
            .flags = &build_cflags,
        });
    }
    janet_mod.addIncludePath(b.path("src/include"));
    janet_mod.addIncludePath(b.path("src/conf"));
    const janet_exe = b.addExecutable(.{
        .name = "janet",
        .root_module = janet_mod,
    });
    b.installArtifact(janet_exe);

    const janet_h = b.addRunArtifact(janet_exe);
    janet_h.addFileArg(b.path("tools/patch-header.janet"));
    janet_h.addFileArg(b.path("src/include/janet.h"));
    janet_h.addFileArg(b.path(janetconf_header));
    const janet_h_output = janet_h.addOutputFileArg("janet.h");
    b.getInstallStep().dependOn(&b.addInstallFile(janet_h_output, "janet.h").step);

    if (has_shared) {
        const janet_library = b.addLibrary(.{
            .name = "janet",
            .linkage = .dynamic,
            .root_module = janet_mod,
        });
        b.installArtifact(janet_library);
    }

    const janet_static_library = b.addLibrary(.{
        .name = "janet",
        .linkage = .static,
        .root_module = janet_mod,
    });
    b.installArtifact(janet_static_library);

    const run_step = b.step("run", "Run the Janet REPL");

    const run_cmd = b.addRunArtifact(janet_exe);
    run_step.dependOn(&run_cmd.step);

    run_cmd.step.dependOn(b.getInstallStep());

    if (b.args) |args| {
        run_cmd.addArgs(args);
    }

    const test_step = b.step("test", "Run tests");
    for (test_scripts) |test_script| {
        const run_test_cmd = b.addRunArtifact(janet_exe);
        run_test_cmd.addFileArg(b.path(test_script));
        test_step.dependOn(&run_test_cmd.step);
    }
}
