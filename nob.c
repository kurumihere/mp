#define _POSIX_C_SOURCE 200809L
#define NOB_EXPERIMENTAL_TRACE_CMD_RUN_FAIL
#define NOB_IMPLEMENTATION
#include "thirdparty/nob.h"

typedef enum {
    LINK_STATIC,
    LINK_DYNAMIC,
} Link_Mode;

typedef enum {
    COMMAND_BUILD,
    COMMAND_RUN,
    COMMAND_TEST,
    COMMAND_RAYLIB,
} Command;

static void append_system_libraries(Cmd *cmd)
{
    cmd_append(cmd, "-lGL", "-lm", "-lpthread", "-ldl", "-lrt", "-lX11");
}

static bool prepare_raylib(Link_Mode mode)
{
    const char *source = mode == LINK_STATIC
                             ? "thirdparty/raylib/lib/libraylib.a"
                             : "thirdparty/raylib/lib/libraylib.so";

    if (!file_exists(source)) {
        nob_log(ERROR, "Missing prebuilt raylib library: %s", source);
        return false;
    }

    if (mode == LINK_STATIC) return true;

    const char *inputs[] = {source, "nob.c"};
    const char *destination = "build/cache/libraylib.so";
    int rebuild = needs_rebuild(destination, inputs, ARRAY_LEN(inputs));

    if (rebuild < 0) return false;
    if (rebuild == 0) return true;

    return copy_file(source, destination);
}

static bool rebuild_raylib(void)
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
    Cmd cmd = {0};

    for (size_t i = 0; i < ARRAY_LEN(sources); ++i) {
        cmd_append(&cmd, "cc", "-c", sources[i], "-o", objects[i], "-std=c99",
                   "-O2", "-fPIC", "-D_DEBUG", "-D_GNU_SOURCE",
                   "-DPLATFORM_DESKTOP_GLFW", "-DGRAPHICS_API_OPENGL_33",
                   "-D_GLFW_X11", "-DSUPPORT_MODULE_RMODELS=0",
                   "-DSUPPORT_MODULE_RAUDIO=0", "-DSUPPORT_FILEFORMAT_JPG=1");

        if (i == 1) cmd_append(&cmd, "-U_GNU_SOURCE");

        cmd_append(&cmd, "-Wall", "-Wno-missing-braces", "-fno-strict-aliasing",
                   "-I", "thirdparty/raylib/include", "-I",
                   "thirdparty/raylib/src", "-I",
                   "thirdparty/raylib/src/external/glfw/include");

        if (!cmd_run(&cmd)) {
            cmd_free(cmd);
            return false;
        }
    }

    const char *shared = "thirdparty/raylib/lib/libraylib.so.new";
    const char *archive = "thirdparty/raylib/lib/libraylib.a.new";

    cmd_append(&cmd, "cc", "-shared", "-o", shared);

    for (size_t i = 0; i < ARRAY_LEN(objects); ++i) {
        cmd_append(&cmd, objects[i]);
    }

    append_system_libraries(&cmd);

    if (!cmd_run(&cmd)) {
        cmd_free(cmd);
        return false;
    }

    cmd_append(&cmd, "strip", "--strip-unneeded", shared);

    if (!cmd_run(&cmd)) {
        cmd_free(cmd);
        return false;
    }

    if (remove(archive) != 0 && errno != ENOENT) {
        nob_log(ERROR, "Could not replace %s: %s", archive, strerror(errno));
        cmd_free(cmd);
        return false;
    }

    cmd_append(&cmd, "ar", "rcs", archive);

    for (size_t i = 0; i < ARRAY_LEN(objects); ++i) {
        cmd_append(&cmd, objects[i]);
    }

    if (!cmd_run(&cmd)) {
        cmd_free(cmd);
        return false;
    }

    cmd_append(&cmd, "strip", "--strip-debug", archive);

    if (!cmd_run(&cmd)) {
        cmd_free(cmd);
        return false;
    }

    if (rename(shared, "thirdparty/raylib/lib/libraylib.so") != 0 ||
        rename(archive, "thirdparty/raylib/lib/libraylib.a") != 0) {
        nob_log(ERROR, "Could not install prebuilt raylib libraries: %s",
                strerror(errno));
        cmd_free(cmd);
        return false;
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
    "thirdparty/raylib/include/raylib.h",
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
    "thirdparty/raylib/include/raylib.h",
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
    "-I",         "thirdparty/raylib/include",
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
    "-I",         "thirdparty/raylib/include",
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
    cmd_append(&cmd, "cc", "-c", unit->source, "-o", unit->object);
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

static bool install_executable(const char *source)
{
    const char *temporary = "build/mp.new";

    FILE *source_file = fopen(source, "rb");
    FILE *installed_file = fopen("build/mp", "rb");
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

    if (rename(temporary, "build/mp") != 0) {
        nob_log(ERROR, "Could not replace build/mp: %s", strerror(errno));
        return false;
    }

    return true;
}

static bool build_app(Link_Mode mode)
{
    Cmd cmd = {0};

    const char *library = mode == LINK_STATIC
                              ? "thirdparty/raylib/lib/libraylib.a"
                              : "build/cache/libraylib.so";

    const char *output = mode == LINK_STATIC ? "build/cache/mp-static"
                                             : "build/cache/mp-dynamic";

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
        cmd_append(&cmd, "cc", "-o", output);
        da_append_many(&cmd, app_objects, ARRAY_LEN(app_objects));

        if (mode == LINK_STATIC) {
            cmd_append(&cmd, "thirdparty/raylib/lib/libraylib.a");
        } else {
            cmd_append(&cmd, "-L", "build/cache", "-lraylib",
                       "-Wl,-rpath,$ORIGIN/cache");
        }

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
        cmd_append(&cmd, "cc", "-c", "src/test.c", "-o", "build/cache/test.o",
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

    rebuild = needs_rebuild("build/cache/mp-tests", link_inputs,
                            ARRAY_LEN(link_inputs));

    if (rebuild < 0) {
        cmd_free(cmd);
        return false;
    }

    if (rebuild > 0) {
        cmd_append(&cmd, "cc", "-o", "build/cache/mp-tests");
        da_append_many(&cmd, test_objects, ARRAY_LEN(test_objects));
        cmd_append(&cmd, "-lm");

        if (!cmd_run(&cmd)) {
            cmd_free(cmd);
            return false;
        }
    }

    cmd_append(&cmd, "./build/cache/mp-tests");
    bool result = cmd_run(&cmd);
    cmd_free(cmd);

    return result;
}

static bool run_app(int argc, char **argv)
{
    Cmd cmd = {0};
    cmd_append(&cmd, "./build/mp");

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
    Link_Mode mode = LINK_DYNAMIC;

    if (argc > 0) {
        const char *argument = shift(argv, argc);

        if (strcmp(argument, "static") == 0) {
            mode = LINK_STATIC;
        } else if (strcmp(argument, "run") == 0) {
            command = COMMAND_RUN;

            if (argc > 0 && strcmp(*argv, "static") == 0) {
                shift(argv, argc);
                mode = LINK_STATIC;
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

    if (command == COMMAND_RAYLIB) return rebuild_raylib() ? 0 : 1;

    if (command == COMMAND_TEST) {
        if (!compile_units(app_units, ARRAY_LEN(app_units), true)) return 1;
        return run_tests() ? 0 : 1;
    }

    if (!prepare_raylib(mode)) return 1;
    if (!compile_units(vendor_units, ARRAY_LEN(vendor_units), false)) return 1;
    if (!compile_units(app_units, ARRAY_LEN(app_units), false)) return 1;
    if (!build_app(mode)) return 1;

    if (command == COMMAND_RUN && !run_app(argc, argv)) return 1;

    return 0;
}
