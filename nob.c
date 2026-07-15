#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L
#define NOB_EXPERIMENTAL_TRACE_CMD_RUN_FAIL
#define NOB_IMPLEMENTATION
#include "thirdparty/nob.h"

typedef enum {
    COMMAND_BUILD,
    COMMAND_RUN,
    COMMAND_TEST,
    COMMAND_RAYLIB,
} Command;

static void append_system_libraries(Cmd *cmd)
{
#if defined(_WIN32)
    cmd_append(cmd, "-lopengl32", "-lgdi32", "-lwinmm", "-lshell32");
#elif defined(__APPLE__)
    cmd_append(cmd, "-framework", "OpenGL", "-framework", "Cocoa", "-framework",
               "IOKit", "-framework", "CoreAudio", "-framework", "CoreVideo");
#else
    cmd_append(cmd, "-lGL", "-lm", "-lpthread", "-ldl", "-lrt", "-lX11");
#endif
}

static bool build_raylib(bool force)
{
    const char *sources[] = {
        "thirdparty/raylib/src/rcore.c",   "thirdparty/raylib/src/rglfw.c",
        "thirdparty/raylib/src/rshapes.c", "thirdparty/raylib/src/rtextures.c",
        "thirdparty/raylib/src/rtext.c",
    };
    const char *objects[] = {
        "build/cache/raylib-rcore.o",   "build/cache/raylib-rglfw.o",
        "build/cache/raylib-rshapes.o", "build/cache/raylib-rtextures.o",
        "build/cache/raylib-rtext.o",
    };
    const char *common_inputs[] = {
        "thirdparty/raylib/src/raylib.h",
        "thirdparty/raylib/src/config.h",
        "thirdparty/raylib/src/rlgl.h",
        "thirdparty/raylib/src/rtext_cyrillic.h",
        "nob.c",
    };
    Cmd cmd = {0};

    for (size_t i = 0; i < ARRAY_LEN(sources); ++i) {
        const char *inputs[ARRAY_LEN(common_inputs) + 1];
        inputs[0] = sources[i];
        memcpy(inputs + 1, common_inputs, sizeof(common_inputs));

        int rebuild =
            force ? 1 : needs_rebuild(objects[i], inputs, ARRAY_LEN(inputs));
        if (rebuild < 0) {
            cmd_free(cmd);
            return false;
        }
        if (rebuild == 0) continue;

        nob_cc(&cmd);
#if defined(__APPLE__)
        if (i == 1) cmd_append(&cmd, "-x", "objective-c");
#endif
        cmd_append(&cmd, "-c", sources[i], "-o", objects[i], "-std=c99", "-O2",
                   "-DPLATFORM_DESKTOP_GLFW", "-DGRAPHICS_API_OPENGL_33",
                   "-DSUPPORT_MODULE_RMODELS=0", "-DSUPPORT_MODULE_RAUDIO=0",
                   "-DSUPPORT_FILEFORMAT_JPG=1", "-Wall", "-Wno-missing-braces",
                   "-fno-strict-aliasing", "-I", "thirdparty/raylib/src", "-I",
                   "thirdparty/raylib/src/external/glfw/include");

#if defined(_WIN32)
        cmd_append(&cmd, "-D_GLFW_WIN32");
#elif defined(__APPLE__)
        cmd_append(&cmd, "-D_GLFW_COCOA", "-fPIC");
#else
        cmd_append(&cmd, "-D_GNU_SOURCE", "-D_GLFW_X11", "-fPIC");
        if (i == 1) cmd_append(&cmd, "-U_GNU_SOURCE");
#endif

        if (!cmd_run(&cmd)) {
            cmd_free(cmd);
            return false;
        }
    }

    const char *library = "build/cache/libraylib.a";
    int rebuild =
        force ? 1 : needs_rebuild(library, objects, ARRAY_LEN(objects));
    if (rebuild < 0) {
        cmd_free(cmd);
        return false;
    }

    if (rebuild > 0) {
        cmd_append(&cmd, "ar", "rcs", library);
        da_append_many(&cmd, objects, ARRAY_LEN(objects));
        if (!cmd_run(&cmd)) {
            cmd_free(cmd);
            return false;
        }
    }

    cmd_free(cmd);
    return true;
}

typedef struct {
    const char *source;
    const char *object;
    const char **inputs;
    size_t input_count;
    const char **flags;
    size_t flag_count;
    bool required_by_tests;
} Build_Unit;

static const char *miniaudio_inputs[] = {
    "thirdparty/miniaudio/miniaudio.c",
    "thirdparty/miniaudio/miniaudio.h",
};
static const char *nanosvg_inputs[] = {
    "thirdparty/nanosvg/nanosvg.c",
    "thirdparty/nanosvg/nanosvg.h",
    "thirdparty/nanosvg/nanosvgrast.h",
};
static const char *svg_inputs[] = {
    "src/svg.c",
    "src/svg.h",
    "src/log.h",
    "thirdparty/raylib/src/raylib.h",
    "thirdparty/nanosvg/nanosvg.h",
    "thirdparty/nanosvg/nanosvgrast.h",
    "nob.c",
};
static const char *log_inputs[] = {"src/log.c", "src/log.h", "nob.c"};
static const char *player_inputs[] = {
    "src/player.c", "src/player.h",
    "src/log.h",    "thirdparty/miniaudio/miniaudio.h",
    "nob.c",
};
static const char *playback_order_inputs[] = {
    "src/playback_order.c",
    "src/playback_order.h",
    "nob.c",
};
static const char *playlist_inputs[] = {
    "src/playlist.c", "src/playlist.h", "src/log.h", "src/metadata.h", "nob.c",
};
static const char *m3u_inputs[] = {
    "src/m3u.c", "src/m3u.h", "src/log.h", "src/playlist.h", "nob.c",
};
static const char *session_inputs[] = {
    "src/session.c", "src/session.h", "src/log.h", "src/playlist.h", "nob.c",
};
static const char *metadata_inputs[] = {
    "src/metadata.c",
    "src/metadata.h",
    "nob.c",
};
static const char *spectrum_inputs[] = {
    "src/spectrum.c",
    "src/spectrum.h",
    "nob.c",
};
static const char *mp_inputs[] = {
    "src/mp.c",
    "thirdparty/raylib/src/raylib.h",
    "thirdparty/miniaudio/miniaudio.h",
    "src/log.h",
    "src/m3u.h",
    "src/metadata.h",
    "src/playback_order.h",
    "src/player.h",
    "src/playlist.h",
    "src/session.h",
    "src/spectrum.h",
    "src/svg.h",
    "nob.c",
};

static const char *vendor_flags[] = {"-std=c99", "-g", "-fPIC"};
static const char *nanosvg_flags[] = {
    "-std=c99", "-g", "-fPIC", "-I", "thirdparty/nanosvg",
};
static const char *app_flags[] = {
    "-std=c99", "-g", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
};
static const char *svg_flags[] = {
    "-std=c99",   "-g",
    "-Wall",      "-Wextra",
    "-Wpedantic", "-Werror",
    "-I",         "thirdparty/raylib/src",
    "-I",         "thirdparty/nanosvg",
};
static const char *player_flags[] = {
    "-std=c99",   "-g",      "-Wall", "-Wextra",
    "-Wpedantic", "-Werror", "-I",    "thirdparty/miniaudio",
};
static const char *mp_flags[] = {
    "-std=c99",   "-g",
    "-Wall",      "-Wextra",
    "-Wpedantic", "-Werror",
    "-I",         "thirdparty/raylib/src",
    "-I",         "thirdparty/miniaudio",
};

#define UNIT(source_, object_, inputs_, flags_, tests_)                        \
    {source_, object_,           inputs_, ARRAY_LEN(inputs_),                  \
     flags_,  ARRAY_LEN(flags_), tests_}

static const Build_Unit vendor_units[] = {
    UNIT("thirdparty/miniaudio/miniaudio.c", "build/cache/miniaudio.o",
         miniaudio_inputs, vendor_flags, false),
    UNIT("thirdparty/nanosvg/nanosvg.c", "build/cache/nanosvg.o",
         nanosvg_inputs, nanosvg_flags, false),
};

static const Build_Unit app_units[] = {
    UNIT("src/mp.c", "build/cache/mp.o", mp_inputs, mp_flags, false),
    UNIT("src/svg.c", "build/cache/svg.o", svg_inputs, svg_flags, false),
    UNIT("src/log.c", "build/cache/log.o", log_inputs, app_flags, true),
    UNIT("src/player.c", "build/cache/player.o", player_inputs, player_flags,
         false),
    UNIT("src/playback_order.c", "build/cache/playback_order.o",
         playback_order_inputs, app_flags, true),
    UNIT("src/playlist.c", "build/cache/playlist.o", playlist_inputs, app_flags,
         true),
    UNIT("src/m3u.c", "build/cache/m3u.o", m3u_inputs, app_flags, true),
    UNIT("src/session.c", "build/cache/session.o", session_inputs, app_flags,
         true),
    UNIT("src/metadata.c", "build/cache/metadata.o", metadata_inputs, app_flags,
         true),
    UNIT("src/spectrum.c", "build/cache/spectrum.o", spectrum_inputs, app_flags,
         true),
};

static const char *app_objects[] = {
    "build/cache/mp.o",       "build/cache/miniaudio.o",
    "build/cache/nanosvg.o",  "build/cache/log.o",
    "build/cache/m3u.o",      "build/cache/metadata.o",
    "build/cache/player.o",   "build/cache/playback_order.o",
    "build/cache/playlist.o", "build/cache/session.o",
    "build/cache/spectrum.o", "build/cache/svg.o",
};

static const char *test_objects[] = {
    "build/cache/test.o",
    "build/cache/log.o",
    "build/cache/m3u.o",
    "build/cache/metadata.o",
    "build/cache/playback_order.o",
    "build/cache/playlist.o",
    "build/cache/session.o",
    "build/cache/spectrum.o",
};

#undef UNIT

static bool compile_unit(const Build_Unit *unit, Procs *procs)
{
    int rebuild = needs_rebuild(unit->object, unit->inputs, unit->input_count);
    if (rebuild < 0) return false;
    if (rebuild == 0) return true;

    Cmd cmd = {0};
    nob_cc(&cmd);
    cmd_append(&cmd, "-c", unit->source, "-o", unit->object);
    da_append_many(&cmd, unit->flags, unit->flag_count);

    bool result = cmd_run(&cmd, .async = procs);
    cmd_free(cmd);
    return result;
}

static bool compile_units(const Build_Unit *units, size_t count,
                          bool tests_only)
{
    Procs procs = {0};
    bool result = true;

    for (size_t i = 0; i < count; ++i) {
        if (tests_only && !units[i].required_by_tests) continue;
        if (!compile_unit(&units[i], &procs)) {
            result = false;
            break;
        }
    }

    if (!procs_flush(&procs)) result = false;
    da_free(procs);
    return result;
}

#ifdef _WIN32
#define MP_EXECUTABLE "build/mp.exe"
#define MP_TEMPORARY_EXECUTABLE "build/mp.exe.new"
#define MP_CACHE_EXECUTABLE "build/cache/mp.exe"
#define MP_TEST_EXECUTABLE "build/cache/mp-tests.exe"
#else
#define MP_EXECUTABLE "build/mp"
#define MP_TEMPORARY_EXECUTABLE "build/mp.new"
#define MP_CACHE_EXECUTABLE "build/cache/mp"
#define MP_TEST_EXECUTABLE "build/cache/mp-tests"
#endif

static bool install_executable(const char *source)
{
    const char *temporary = MP_TEMPORARY_EXECUTABLE;

    FILE *source_file = fopen(source, "rb");
    FILE *installed_file = fopen(MP_EXECUTABLE, "rb");
    bool identical = source_file != NULL && installed_file != NULL;
    unsigned char source_buffer[4096];
    unsigned char installed_buffer[4096];

    while (identical) {
        size_t source_count =
            fread(source_buffer, 1, sizeof(source_buffer), source_file);
        size_t installed_count = fread(
            installed_buffer, 1, sizeof(installed_buffer), installed_file);

        if (source_count != installed_count ||
            memcmp(source_buffer, installed_buffer, source_count) != 0) {
            identical = false;
            break;
        }

        if (source_count < sizeof(source_buffer)) break;
    }

    if (source_file != NULL) fclose(source_file);
    if (installed_file != NULL) fclose(installed_file);
    if (identical) return true;

    if (!copy_file(source, temporary)) return false;

    if (rename(temporary, MP_EXECUTABLE) != 0) {
        nob_log(ERROR, "Could not replace %s: %s", MP_EXECUTABLE,
                strerror(errno));
        return false;
    }

    return true;
}

static bool build_app(void)
{
    Cmd cmd = {0};
    const char *library = "build/cache/libraylib.a";
    const char *output = MP_CACHE_EXECUTABLE;

    const char *link_inputs[ARRAY_LEN(app_objects) + 2];
    memcpy(link_inputs, app_objects, sizeof(app_objects));
    link_inputs[ARRAY_LEN(app_objects)] = library;
    link_inputs[ARRAY_LEN(app_objects) + 1] = "nob.c";

    int rebuild = needs_rebuild(output, link_inputs, ARRAY_LEN(link_inputs));

    if (rebuild < 0) {
        cmd_free(cmd);
        return false;
    }

    if (rebuild > 0) {
        nob_cc(&cmd);
        cmd_append(&cmd, "-o", output);
        da_append_many(&cmd, app_objects, ARRAY_LEN(app_objects));

        cmd_append(&cmd, library);

        append_system_libraries(&cmd);

        if (!cmd_run(&cmd)) {
            cmd_free(cmd);
            return false;
        }
    }

    bool result = install_executable(output);
    cmd_free(cmd);

    return result;
}

static bool run_tests(void)
{
    const char *test_inputs[] = {
        "src/test.c",           "src/m3u.h",      "src/metadata.h",
        "src/playback_order.h", "src/playlist.h", "src/session.h",
        "src/spectrum.h",       "nob.c",
    };

    int rebuild = needs_rebuild("build/cache/test.o", test_inputs,
                                ARRAY_LEN(test_inputs));

    if (rebuild < 0) return false;

    Cmd cmd = {0};

    if (rebuild > 0) {
        nob_cc(&cmd);
        cmd_append(&cmd, "-c", "src/test.c", "-o", "build/cache/test.o",
                   "-std=c99", "-g", "-Wall", "-Wextra", "-Wpedantic",
                   "-Werror", "-I", "src");

        if (!cmd_run(&cmd)) {
            cmd_free(cmd);
            return false;
        }
    }

    const char *link_inputs[ARRAY_LEN(test_objects) + 1];
    memcpy(link_inputs, test_objects, sizeof(test_objects));
    link_inputs[ARRAY_LEN(test_objects)] = "nob.c";

    rebuild =
        needs_rebuild(MP_TEST_EXECUTABLE, link_inputs, ARRAY_LEN(link_inputs));

    if (rebuild < 0) {
        cmd_free(cmd);
        return false;
    }

    if (rebuild > 0) {
        nob_cc(&cmd);
        cmd_append(&cmd, "-o", MP_TEST_EXECUTABLE);
        da_append_many(&cmd, test_objects, ARRAY_LEN(test_objects));
        cmd_append(&cmd, "-lm");

        if (!cmd_run(&cmd)) {
            cmd_free(cmd);
            return false;
        }
    }

    cmd_append(&cmd, MP_TEST_EXECUTABLE);
    bool result = cmd_run(&cmd);
    cmd_free(cmd);

    return result;
}

static bool run_app(int argc, char **argv)
{
    Cmd cmd = {0};
    cmd_append(&cmd, MP_EXECUTABLE);

    while (argc > 0) {
        cmd_append(&cmd, shift(argv, argc));
    }

    bool result = cmd_run(&cmd);
    cmd_free(cmd);

    return result;
}

int main(int argc, char **argv)
{
    GO_REBUILD_URSELF(argc, argv);

    shift(argv, argc);

    Command command = COMMAND_BUILD;
    if (argc > 0) {
        const char *argument = shift(argv, argc);

        if (strcmp(argument, "static") == 0) {
            // Kept as a compatibility alias; raylib is now always linked
            // statically from source.
        } else if (strcmp(argument, "run") == 0) {
            command = COMMAND_RUN;

            if (argc > 0 && strcmp(*argv, "static") == 0) {
                shift(argv, argc);
            }
        } else if (strcmp(argument, "test") == 0) {
            command = COMMAND_TEST;
        } else if (strcmp(argument, "raylib") == 0) {
            command = COMMAND_RAYLIB;
        } else {
            nob_log(ERROR, "Unknown subcommand: %s", argument);
            nob_log(INFO, "Usage: ./nob [static|test|raylib|run [static]]");
            return 1;
        }
    }

    if (command != COMMAND_RUN && argc > 0) {
        nob_log(ERROR, "Unexpected argument: %s", shift(argv, argc));
        return 1;
    }

    if (!mkdir_if_not_exists("build")) return 1;
    if (!mkdir_if_not_exists("build/cache")) return 1;

    if (command == COMMAND_RAYLIB) return build_raylib(true) ? 0 : 1;

    if (command == COMMAND_TEST) {
        if (!compile_units(app_units, ARRAY_LEN(app_units), true)) return 1;
        return run_tests() ? 0 : 1;
    }

    if (!build_raylib(false)) return 1;
    if (!compile_units(vendor_units, ARRAY_LEN(vendor_units), false)) return 1;
    if (!compile_units(app_units, ARRAY_LEN(app_units), false)) return 1;
    if (!build_app()) return 1;

    if (command == COMMAND_RUN && !run_app(argc, argv)) return 1;

    return 0;
}
