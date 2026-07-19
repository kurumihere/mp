#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L
#define NOB_EXPERIMENTAL_TRACE_CMD_RUN_FAIL
#define NOB_IMPLEMENTATION
#include "thirdparty/nob.h"

typedef enum {
    PLATFORM_LINUX,
    PLATFORM_WINDOWS,
    PLATFORM_MACOS,
} Platform;

typedef struct {
    Platform platform;
    const char *cc;
    const char *executable;
    const char *runner;
} Build;

static const char *app_sources[] = {
    "src/mp.c",
    "src/svg.c",
    "src/log.c",
    "src/player.c",
    "src/playback_order.c",
    "src/playlist.c",
    "src/m3u.c",
    "src/session.c",
    "src/metadata.c",
    "src/spectrum.c",
    "thirdparty/miniaudio/miniaudio.c",
    "thirdparty/nanosvg/nanosvg.c",
};

static const char *raylib_sources[] = {
    "thirdparty/raylib/src/rcore.c",
    "thirdparty/raylib/src/rshapes.c",
    "thirdparty/raylib/src/rtextures.c",
    "thirdparty/raylib/src/rtext.c",
};

static const char *environment_tool(const char *name)
{
    const char *value = getenv(name);
    return value != NULL && value[0] != '\0' ? value : NULL;
}

static Build configure_build(bool wine)
{
    Build build = {0};
    build.cc = environment_tool("CC");

    if (wine) {
        build.platform = PLATFORM_WINDOWS;
        if (build.cc == NULL) build.cc = "x86_64-w64-mingw32-cc";
        build.executable = "build/mp.exe";
        build.runner = "wine";
        return build;
    }

#if defined(_WIN32)
    build.platform = PLATFORM_WINDOWS;
    build.executable = "build/mp.exe";
#elif defined(__APPLE__)
    build.platform = PLATFORM_MACOS;
    build.executable = "build/mp";
#else
    build.platform = PLATFORM_LINUX;
    build.executable = "build/mp";
#endif

    return build;
}

static void append_compiler(Cmd *cmd, const Build *build)
{
    if (build->cc != NULL) {
        cmd_append(cmd, build->cc);
    } else {
        nob_cc(cmd);
    }
}

static void append_platform_options(Cmd *cmd, Platform platform)
{
    if (platform == PLATFORM_WINDOWS) {
        cmd_append(cmd, "-lopengl32", "-lgdi32", "-lwinmm", "-lshell32");
    } else if (platform == PLATFORM_MACOS) {
        cmd_append(cmd, "-framework", "OpenGL", "-framework", "Cocoa",
                   "-framework", "IOKit", "-framework", "CoreAudio",
                   "-framework", "CoreVideo");
    } else {
        cmd_append(cmd, "-lGL", "-lm", "-lpthread", "-ldl", "-lrt", "-lX11");
    }
}

static bool build_app(const Build *build)
{
    Cmd cmd = {0};
    append_compiler(&cmd, build);
    cmd_append(&cmd, "-o", build->executable, "-std=c99", "-g", "-Wall",
               "-Wextra", "-Wno-unused-parameter", "-Wno-sign-compare",
               "-Wno-format", "-Wno-missing-braces",
               "-Wno-missing-field-initializers", "-fno-strict-aliasing",
               "-Werror=implicit-function-declaration",
               "-DPLATFORM_DESKTOP_GLFW", "-DGRAPHICS_API_OPENGL_33",
               "-DSUPPORT_MODULE_RMODELS=0", "-DSUPPORT_MODULE_RAUDIO=0",
               "-DSUPPORT_FILEFORMAT_JPG=1", "-I", "thirdparty/raylib/src",
               "-I", "thirdparty/raylib/src/external/glfw/include", "-I",
               "thirdparty/miniaudio", "-I", "thirdparty/nanosvg");

    if (build->platform == PLATFORM_LINUX) {
        cmd_append(&cmd, "-D_GLFW_X11");
    } else if (build->platform == PLATFORM_WINDOWS) {
        cmd_append(&cmd, "-DUNICODE");
    }

    da_append_many(&cmd, app_sources, ARRAY_LEN(app_sources));
    da_append_many(&cmd, raylib_sources, ARRAY_LEN(raylib_sources));

    if (build->platform == PLATFORM_MACOS) {
        cmd_append(&cmd, "-x", "objective-c", "thirdparty/raylib/src/rglfw.c",
                   "-x", "c");
    } else {
        cmd_append(&cmd, "thirdparty/raylib/src/rglfw.c");
    }

    append_platform_options(&cmd, build->platform);
    bool result = cmd_run(&cmd);
    cmd_free(cmd);
    return result;
}

static bool run_app(const Build *build, int argc, char **argv)
{
    Cmd cmd = {0};
    if (build->runner != NULL) {
        cmd_append(&cmd, "env", "WINEDEBUG=-all", build->runner);
    }
    cmd_append(&cmd, build->executable);

    while (argc > 0)
        cmd_append(&cmd, shift(argv, argc));

    bool result = cmd_run(&cmd);
    cmd_free(cmd);
    return result;
}

static int usage(const char *program)
{
    nob_log(INFO, "Usage: %s [run [audio files...] | wine [audio files...]]",
            program);
    return 1;
}

int main(int argc, char **argv)
{
    GO_REBUILD_URSELF(argc, argv);

    const char *program = shift(argv, argc);
    const char *command = argc > 0 ? shift(argv, argc) : "build";
    bool wine = strcmp(command, "wine") == 0;
    bool run = wine || strcmp(command, "run") == 0;

    if (!run && strcmp(command, "build") != 0) return usage(program);
    if (!run && argc > 0) return usage(program);

#if defined(_WIN32)
    if (wine) {
        nob_log(ERROR, "Wine build is only available on non-Windows hosts");
        return 1;
    }
#endif

    if (!mkdir_if_not_exists("build")) return 1;

    Build build = configure_build(wine);
    if (!build_app(&build)) return 1;
    if (run && !run_app(&build, argc, argv)) return 1;
    return 0;
}
