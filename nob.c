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

typedef enum {
    PLATFORM_LINUX,
    PLATFORM_WINDOWS,
    PLATFORM_MACOS,
} Platform;

typedef struct {
    Platform platform;
    const char *cc;
    const char *ar;
    const char *cache_dir;
    const char *executable;
    const char *temporary_executable;
    const char *cache_executable;
    const char *test_executable;
    const char *runner;
} Build_Config;

#define BUILD_PATH_CAPACITY 256

static bool format_cache_path(char *path, size_t capacity,
                              const Build_Config *config, const char *name)
{
    int count = snprintf(path, capacity, "%s/%s", config->cache_dir, name);
    if (count < 0 || (size_t)count >= capacity) {
        nob_log(ERROR, "Build path is too long: %s/%s", config->cache_dir,
                name);
        return false;
    }

    return true;
}

static void append_compiler(Cmd *cmd, const Build_Config *config)
{
    if (config->cc != NULL) {
        cmd_append(cmd, config->cc);
    } else {
        nob_cc(cmd);
    }
}

static void append_system_libraries(Cmd *cmd, const Build_Config *config)
{
    if (config->platform == PLATFORM_WINDOWS) {
        cmd_append(cmd, "-lopengl32", "-lgdi32", "-lwinmm", "-lshell32");
    } else if (config->platform == PLATFORM_MACOS) {
        cmd_append(cmd, "-framework", "OpenGL", "-framework", "Cocoa",
                   "-framework", "IOKit", "-framework", "CoreAudio",
                   "-framework", "CoreVideo");
    } else {
        cmd_append(cmd, "-lGL", "-lm", "-lpthread", "-ldl", "-lrt", "-lX11");
    }
}

static bool build_raylib(const Build_Config *config, bool force)
{
    const char *sources[] = {
        "thirdparty/raylib/src/rcore.c",   "thirdparty/raylib/src/rglfw.c",
        "thirdparty/raylib/src/rshapes.c", "thirdparty/raylib/src/rtextures.c",
        "thirdparty/raylib/src/rtext.c",
    };
    const char *object_names[] = {
        "raylib-rcore.o",     "raylib-rglfw.o", "raylib-rshapes.o",
        "raylib-rtextures.o", "raylib-rtext.o",
    };
    const char *common_inputs[] = {
        "thirdparty/raylib/src/raylib.h",
        "thirdparty/raylib/src/config.h",
        "thirdparty/raylib/src/rlgl.h",
        "thirdparty/raylib/src/rtext_cyrillic.h",
        "nob.c",
    };
    char object_paths[ARRAY_LEN(object_names)][BUILD_PATH_CAPACITY];
    const char *objects[ARRAY_LEN(object_names)];
    char library[BUILD_PATH_CAPACITY];
    Cmd cmd = {0};

    for (size_t i = 0; i < ARRAY_LEN(object_names); ++i) {
        if (!format_cache_path(object_paths[i], sizeof(object_paths[i]), config,
                               object_names[i])) {
            return false;
        }
        objects[i] = object_paths[i];
    }

    if (!format_cache_path(library, sizeof(library), config, "libraylib.a")) {
        return false;
    }

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

        append_compiler(&cmd, config);
        if (config->platform == PLATFORM_MACOS && i == 1) {
            cmd_append(&cmd, "-x", "objective-c");
        }
        cmd_append(&cmd, "-c", sources[i], "-o", objects[i], "-std=c99", "-O2",
                   "-DPLATFORM_DESKTOP_GLFW", "-DGRAPHICS_API_OPENGL_33",
                   "-DSUPPORT_MODULE_RMODELS=0", "-DSUPPORT_MODULE_RAUDIO=0",
                   "-DSUPPORT_FILEFORMAT_JPG=1", "-Wall", "-Wno-missing-braces",
                   "-fno-strict-aliasing", "-I", "thirdparty/raylib/src", "-I",
                   "thirdparty/raylib/src/external/glfw/include");

        if (config->platform == PLATFORM_MACOS) {
            cmd_append(&cmd, "-fPIC");
        } else if (config->platform == PLATFORM_LINUX) {
            cmd_append(&cmd, "-D_GNU_SOURCE", "-D_GLFW_X11", "-fPIC");
            if (i == 1) cmd_append(&cmd, "-U_GNU_SOURCE");
        }

        if (!cmd_run(&cmd)) {
            cmd_free(cmd);
            return false;
        }
    }

    int rebuild =
        force ? 1 : needs_rebuild(library, objects, ARRAY_LEN(objects));
    if (rebuild < 0) {
        cmd_free(cmd);
        return false;
    }

    if (rebuild > 0) {
        cmd_append(&cmd, config->ar, "rcs", library);
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
    const char *object_name;
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
    UNIT("thirdparty/miniaudio/miniaudio.c", "miniaudio.o", miniaudio_inputs,
         vendor_flags, false),
    UNIT("thirdparty/nanosvg/nanosvg.c", "nanosvg.o", nanosvg_inputs,
         nanosvg_flags, false),
};

static const Build_Unit app_units[] = {
    UNIT("src/mp.c", "mp.o", mp_inputs, mp_flags, false),
    UNIT("src/svg.c", "svg.o", svg_inputs, svg_flags, false),
    UNIT("src/log.c", "log.o", log_inputs, app_flags, true),
    UNIT("src/player.c", "player.o", player_inputs, player_flags, false),
    UNIT("src/playback_order.c", "playback_order.o", playback_order_inputs,
         app_flags, true),
    UNIT("src/playlist.c", "playlist.o", playlist_inputs, app_flags, true),
    UNIT("src/m3u.c", "m3u.o", m3u_inputs, app_flags, true),
    UNIT("src/session.c", "session.o", session_inputs, app_flags, true),
    UNIT("src/metadata.c", "metadata.o", metadata_inputs, app_flags, true),
    UNIT("src/spectrum.c", "spectrum.o", spectrum_inputs, app_flags, true),
};

static const char *app_object_names[] = {
    "mp.o",       "miniaudio.o", "nanosvg.o",  "log.o",
    "m3u.o",      "metadata.o",  "player.o",   "playback_order.o",
    "playlist.o", "session.o",   "spectrum.o", "svg.o",
};

static const char *test_object_names[] = {
    "test.o",           "log.o",      "m3u.o",     "metadata.o",
    "playback_order.o", "playlist.o", "session.o", "spectrum.o",
};

#undef UNIT

static bool compile_unit(const Build_Config *config, const Build_Unit *unit,
                         Procs *procs)
{
    char object[BUILD_PATH_CAPACITY];
    if (!format_cache_path(object, sizeof(object), config, unit->object_name)) {
        return false;
    }

    int rebuild = needs_rebuild(object, unit->inputs, unit->input_count);
    if (rebuild < 0) return false;
    if (rebuild == 0) return true;

    Cmd cmd = {0};
    append_compiler(&cmd, config);
    cmd_append(&cmd, "-c", unit->source, "-o", object);
    da_append_many(&cmd, unit->flags, unit->flag_count);

    bool result = cmd_run(&cmd, .async = procs);
    cmd_free(cmd);
    return result;
}

static bool compile_units(const Build_Config *config, const Build_Unit *units,
                          size_t count, bool tests_only)
{
    Procs procs = {0};
    bool result = true;

    for (size_t i = 0; i < count; ++i) {
        if (tests_only && !units[i].required_by_tests) continue;
        if (!compile_unit(config, &units[i], &procs)) {
            result = false;
            break;
        }
    }

    if (!procs_flush(&procs)) result = false;
    da_free(procs);
    return result;
}

static bool install_executable(const Build_Config *config, const char *source)
{
    const char *temporary = config->temporary_executable;

    FILE *source_file = fopen(source, "rb");
    FILE *installed_file = fopen(config->executable, "rb");
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

    if (rename(temporary, config->executable) != 0) {
        nob_log(ERROR, "Could not replace %s: %s", config->executable,
                strerror(errno));
        return false;
    }

    return true;
}

static bool build_app(const Build_Config *config)
{
    Cmd cmd = {0};
    char object_paths[ARRAY_LEN(app_object_names)][BUILD_PATH_CAPACITY];
    const char *objects[ARRAY_LEN(app_object_names)];
    char library[BUILD_PATH_CAPACITY];
    const char *output = config->cache_executable;

    for (size_t i = 0; i < ARRAY_LEN(app_object_names); ++i) {
        if (!format_cache_path(object_paths[i], sizeof(object_paths[i]), config,
                               app_object_names[i])) {
            return false;
        }
        objects[i] = object_paths[i];
    }

    if (!format_cache_path(library, sizeof(library), config, "libraylib.a")) {
        return false;
    }

    const char *link_inputs[ARRAY_LEN(objects) + 2];
    memcpy(link_inputs, objects, sizeof(objects));
    link_inputs[ARRAY_LEN(objects)] = library;
    link_inputs[ARRAY_LEN(objects) + 1] = "nob.c";

    int rebuild = needs_rebuild(output, link_inputs, ARRAY_LEN(link_inputs));

    if (rebuild < 0) {
        cmd_free(cmd);
        return false;
    }

    if (rebuild > 0) {
        append_compiler(&cmd, config);
        cmd_append(&cmd, "-o", output);
        da_append_many(&cmd, objects, ARRAY_LEN(objects));

        cmd_append(&cmd, library);

        append_system_libraries(&cmd, config);

        if (!cmd_run(&cmd)) {
            cmd_free(cmd);
            return false;
        }
    }

    bool result = install_executable(config, output);
    cmd_free(cmd);

    return result;
}

static bool run_tests(const Build_Config *config)
{
    const char *test_inputs[] = {
        "src/test.c",           "src/m3u.h",      "src/metadata.h",
        "src/playback_order.h", "src/playlist.h", "src/session.h",
        "src/spectrum.h",       "nob.c",
    };

    char object_paths[ARRAY_LEN(test_object_names)][BUILD_PATH_CAPACITY];
    const char *objects[ARRAY_LEN(test_object_names)];

    for (size_t i = 0; i < ARRAY_LEN(test_object_names); ++i) {
        if (!format_cache_path(object_paths[i], sizeof(object_paths[i]), config,
                               test_object_names[i])) {
            return false;
        }
        objects[i] = object_paths[i];
    }

    int rebuild =
        needs_rebuild(objects[0], test_inputs, ARRAY_LEN(test_inputs));

    if (rebuild < 0) return false;

    Cmd cmd = {0};

    if (rebuild > 0) {
        append_compiler(&cmd, config);
        cmd_append(&cmd, "-c", "src/test.c", "-o", objects[0], "-std=c99", "-g",
                   "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-I", "src");

        if (!cmd_run(&cmd)) {
            cmd_free(cmd);
            return false;
        }
    }

    const char *link_inputs[ARRAY_LEN(objects) + 1];
    memcpy(link_inputs, objects, sizeof(objects));
    link_inputs[ARRAY_LEN(objects)] = "nob.c";

    rebuild = needs_rebuild(config->test_executable, link_inputs,
                            ARRAY_LEN(link_inputs));

    if (rebuild < 0) {
        cmd_free(cmd);
        return false;
    }

    if (rebuild > 0) {
        append_compiler(&cmd, config);
        cmd_append(&cmd, "-o", config->test_executable);
        da_append_many(&cmd, objects, ARRAY_LEN(objects));
        cmd_append(&cmd, "-lm");

        if (!cmd_run(&cmd)) {
            cmd_free(cmd);
            return false;
        }
    }

    cmd_append(&cmd, config->test_executable);
    bool result = cmd_run(&cmd);
    cmd_free(cmd);

    return result;
}

static bool run_app(const Build_Config *config, int argc, char **argv)
{
    Cmd cmd = {0};
    if (config->runner != NULL) cmd_append(&cmd, config->runner);
    cmd_append(&cmd, config->executable);

    while (argc > 0) {
        cmd_append(&cmd, shift(argv, argc));
    }

    bool result = cmd_run(&cmd);
    cmd_free(cmd);

    return result;
}

static const char *environment_tool(const char *name)
{
    const char *value = getenv(name);
    return value != NULL && value[0] != '\0' ? value : NULL;
}

static bool configure_build(Build_Config *config, bool run_with_wine)
{
    config->cc = environment_tool("CC");
    config->ar = environment_tool("AR");

    if (config->ar == NULL) config->ar = "ar";

    if (run_with_wine) {
#if defined(_WIN32)
        nob_log(ERROR, "Wine build is only available on non-Windows hosts");
        return false;
#else
        config->platform = PLATFORM_WINDOWS;
        if (config->cc == NULL) config->cc = "x86_64-w64-mingw32-cc";
        if (environment_tool("AR") == NULL) {
            config->ar = "x86_64-w64-mingw32-ar";
        }
        config->cache_dir = "build/cache/wine";
        config->executable = "build/mp.exe";
        config->temporary_executable = "build/mp.exe.new";
        config->cache_executable = "build/cache/wine/mp.exe";
        config->test_executable = "build/cache/wine/mp-tests.exe";
        config->runner = "wine";

        return true;
#endif
    }

#if defined(_WIN32)
    config->platform = PLATFORM_WINDOWS;
    config->executable = "build/mp.exe";
    config->temporary_executable = "build/mp.exe.new";
    config->cache_executable = "build/cache/mp.exe";
    config->test_executable = "build/cache/mp-tests.exe";
#elif defined(__APPLE__)
    config->platform = PLATFORM_MACOS;
    config->executable = "build/mp";
    config->temporary_executable = "build/mp.new";
    config->cache_executable = "build/cache/mp";
    config->test_executable = "build/cache/mp-tests";
#else
    config->platform = PLATFORM_LINUX;
    config->executable = "build/mp";
    config->temporary_executable = "build/mp.new";
    config->cache_executable = "build/cache/mp";
    config->test_executable = "build/cache/mp-tests";
#endif
    config->cache_dir = "build/cache";

    return true;
}

int main(int argc, char **argv)
{
    GO_REBUILD_URSELF(argc, argv);

    shift(argv, argc);

    bool run_with_wine = false;
    if (argc > 0 && strcmp(*argv, "wine") == 0) {
        run_with_wine = true;
        shift(argv, argc);
    }

    Build_Config config = {0};
    if (!configure_build(&config, run_with_wine)) return 1;

    Command command = run_with_wine ? COMMAND_RUN : COMMAND_BUILD;
    if (!run_with_wine && argc > 0) {
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
            nob_log(INFO, "Usage: ./nob [static|test|raylib|run [static]|wine] "
                          "[audio files...]");
            return 1;
        }
    }

    if (command != COMMAND_RUN && argc > 0) {
        nob_log(ERROR, "Unexpected argument: %s", shift(argv, argc));
        return 1;
    }

    if (!mkdir_if_not_exists("build")) return 1;
    if (!mkdir_if_not_exists("build/cache")) return 1;
    if (!mkdir_if_not_exists(config.cache_dir)) return 1;

    if (command == COMMAND_RAYLIB) {
        return build_raylib(&config, true) ? 0 : 1;
    }

    if (command == COMMAND_TEST) {
        if (!compile_units(&config, app_units, ARRAY_LEN(app_units), true)) {
            return 1;
        }
        return run_tests(&config) ? 0 : 1;
    }

    if (!build_raylib(&config, false)) return 1;
    if (!compile_units(&config, vendor_units, ARRAY_LEN(vendor_units), false)) {
        return 1;
    }
    if (!compile_units(&config, app_units, ARRAY_LEN(app_units), false))
        return 1;
    if (!build_app(&config)) return 1;

    if (command == COMMAND_RUN && !run_app(&config, argc, argv)) return 1;

    return 0;
}
