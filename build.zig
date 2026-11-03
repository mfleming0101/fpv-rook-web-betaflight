const std = @import("std");

pub fn build(b: *std.Build) void {
    const betaflight = b.dependency("betaflight", .{});
    const patch = b.addSystemCommand(&.{ "sh", "-c", "mkdir -p \"$2/lib/main\" && cp -R \"$0/src\" \"$2\" && cp -R \"$0/lib/main/dyad\" \"$2/lib/main\" && cd \"$2\" && GIT_CEILING_DIRECTORIES=\"$(dirname \"$PWD\")\" git apply \"$1\"" });
    patch.addDirectoryArg(betaflight.path(""));
    patch.addFileArg(b.path("0e533ba.patch"));
    const tree = patch.addOutputDirectoryArg("betaflight");

    const bf = b.addExecutable(.{ .name = "bf", .root_module = b.createModule(.{
        .target = b.resolveTargetQuery(.{ .cpu_arch = .wasm32, .os_tag = .wasi }),
        .optimize = .ReleaseFast,
        .link_libc = true,
        .strip = true,
    }) });
    bf.wasi_exec_model = .reactor;
    bf.entry = .disabled;
    for ([_][]const u8{ "src/main", "src/main/target/SITL", "lib/main/dyad" }) |dir| bf.root_module.addIncludePath(tree.path(b, dir));
    const flags = [_][]const u8{ "-Wno-date-time", "-D_GNU_SOURCE", "-DSITL", "-DSIMULATOR_BUILD", "-D__TARGET__=\"SITL\"", "-D__REVISION__=\"0e533ba\"", "-O3", "-ffast-math" };
    var sources: std.ArrayList([]const u8) = .empty;
    var lines = std.mem.tokenizeScalar(u8, @embedFile("srcs.txt"), '\n');
    while (lines.next()) |line| sources.append(b.allocator, line) catch @panic("OOM");
    bf.root_module.addCSourceFiles(.{ .root = tree, .files = sources.items, .flags = &flags });
    bf.root_module.addCSourceFiles(.{ .root = b.path("."), .files = &.{ "platform.c", "dyad_stub.c" }, .flags = &flags });
    b.installArtifact(bf);
}
