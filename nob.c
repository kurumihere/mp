#define NOB_IMPLEMENTATION
#include "thirdparty/nob.h"

typedef enum {
    LINK_STATIC,
    LINK_DYNAMIC,
} Link_Mode;

static void append_system_libraries(Cmd *cmd)
{
    cmd_append(cmd, "-lGL", "-lm", "-lpthread", "-ldl", "-lrt", "-lX11");
}

static bool build_raylib(Link_Mode mode)
{
    const char *sources[] = {
        "thirdparty/raylib/src/rcore.c",   "thirdparty/raylib/src/rglfw.c",
        "thirdparty/raylib/src/rshapes.c", "thirdparty/raylib/src/rtextures.c",
        "thirdparty/raylib/src/rtext.c",
    };

    const char *objects[] = {
        "build/cache/rcore.o",   "build/cache/rglfw.o",
        "build/cache/rshapes.o", "build/cache/rtextures.o",
        "build/cache/rtext.o",
    };

    Cmd cmd = {0};

    for (size_t i = 0; i < ARRAY_LEN(sources); ++i) {
        const char *inputs[6] = {
            sources[i],
            "thirdparty/raylib/src/raylib.h",
            "thirdparty/raylib/src/config.h",
            "thirdparty/raylib/src/rlgl.h",
            "nob.c",
        };
        size_t input_count = 5;

        if (i == 4) {
            inputs[input_count++] = "thirdparty/raylib/src/rtext_cyrillic.h";
        }

        int rebuild = needs_rebuild(objects[i], inputs, input_count);

        if (rebuild < 0) {
            cmd_free(cmd);
            return false;
        }

        if (rebuild == 0) continue;

        cmd_append(&cmd, "cc");

        cmd_append(&cmd, "-c", sources[i], "-o", objects[i]);

        cmd_append(&cmd, "-std=c99", "-g", "-fPIC");

        cmd_append(&cmd, "-D_DEBUG", "-D_GNU_SOURCE", "-DPLATFORM_DESKTOP_GLFW",
                   "-DGRAPHICS_API_OPENGL_33", "-D_GLFW_X11",
                   "-DSUPPORT_MODULE_RMODELS=0", "-DSUPPORT_MODULE_RAUDIO=0",
                   "-DSUPPORT_FILEFORMAT_JPG=1");

        if (i == 1) {
            cmd_append(&cmd, "-U_GNU_SOURCE");
        }

        cmd_append(&cmd, "-Wall", "-Wno-missing-braces",
                   "-fno-strict-aliasing");

        cmd_append(&cmd, "-I", "thirdparty/raylib/src", "-I",
                   "thirdparty/raylib/src/external/glfw/include");

        if (!cmd_run(&cmd)) {
            cmd_free(cmd);
            return false;
        }
    }

    const char *library = mode == LINK_STATIC ? "build/cache/libraylib.a"
                                              : "build/cache/libraylib.so";

    const char *library_inputs[] = {
        objects[0], objects[1], objects[2], objects[3], objects[4],
    };

    int rebuild =
        needs_rebuild(library, library_inputs, ARRAY_LEN(library_inputs));

    if (rebuild < 0) {
        cmd_free(cmd);
        return false;
    }

    if (rebuild == 0) {
        cmd_free(cmd);
        return true;
    }

    if (mode == LINK_STATIC) {
        cmd_append(&cmd, "ar", "rcs", "build/cache/libraylib.a");
    } else {
        cmd_append(&cmd, "cc", "-shared", "-o", "build/cache/libraylib.so");
    }

    for (size_t i = 0; i < ARRAY_LEN(objects); ++i) {
        cmd_append(&cmd, objects[i]);
    }

    if (mode == LINK_DYNAMIC) {
        append_system_libraries(&cmd);
    }

    bool result = cmd_run(&cmd);
    cmd_free(cmd);

    return result;
}

static bool build_miniaudio(void)
{
    const char *inputs[] = {
        "thirdparty/miniaudio/miniaudio.c",
        "thirdparty/miniaudio/miniaudio.h",
    };

    int rebuild =
        needs_rebuild("build/cache/miniaudio.o", inputs, ARRAY_LEN(inputs));

    if (rebuild < 0) return false;
    if (rebuild == 0) return true;

    Cmd cmd = {0};

    cmd_append(&cmd, "cc", "-c", "thirdparty/miniaudio/miniaudio.c", "-o",
               "build/cache/miniaudio.o");

    cmd_append(&cmd, "-std=c99", "-g", "-fPIC");

    bool result = cmd_run(&cmd);
    cmd_free(cmd);

    return result;
}

static bool build_nanosvg(void)
{
    const char *inputs[] = {
        "thirdparty/nanosvg/nanosvg.c",
        "thirdparty/nanosvg/nanosvg.h",
        "thirdparty/nanosvg/nanosvgrast.h",
    };

    int rebuild =
        needs_rebuild("build/cache/nanosvg.o", inputs, ARRAY_LEN(inputs));

    if (rebuild < 0) return false;
    if (rebuild == 0) return true;

    Cmd cmd = {0};

    cmd_append(&cmd, "cc", "-c", "thirdparty/nanosvg/nanosvg.c", "-o",
               "build/cache/nanosvg.o", "-std=c99", "-g", "-fPIC", "-I",
               "thirdparty/nanosvg");

    bool result = cmd_run(&cmd);
    cmd_free(cmd);

    return result;
}

static bool build_svg(void)
{
    const char *inputs[] = {
        "src/svg.c",
        "src/svg.h",
        "src/log.h",
        "thirdparty/raylib/src/raylib.h",
        "thirdparty/nanosvg/nanosvg.h",
        "thirdparty/nanosvg/nanosvgrast.h",
        "nob.c",
    };

    int rebuild = needs_rebuild("build/cache/svg.o", inputs, ARRAY_LEN(inputs));

    if (rebuild < 0) return false;
    if (rebuild == 0) return true;

    Cmd cmd = {0};

    cmd_append(&cmd, "cc", "-c", "src/svg.c", "-o", "build/cache/svg.o",
               "-std=c99", "-g", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
               "-I", "thirdparty/raylib/src", "-I", "thirdparty/nanosvg");

    bool result = cmd_run(&cmd);
    cmd_free(cmd);

    return result;
}

static bool build_log(void)
{
    const char *inputs[] = {
        "src/log.c",
        "src/log.h",
        "nob.c",
    };

    int rebuild = needs_rebuild("build/cache/log.o", inputs, ARRAY_LEN(inputs));

    if (rebuild < 0) return false;
    if (rebuild == 0) return true;

    Cmd cmd = {0};

    cmd_append(&cmd, "cc", "-c", "src/log.c", "-o", "build/cache/log.o",
               "-std=c99", "-g", "-Wall", "-Wextra", "-Wpedantic", "-Werror");

    bool result = cmd_run(&cmd);
    cmd_free(cmd);

    return result;
}

static bool build_player(void)
{
    const char *inputs[] = {
        "src/player.c", "src/player.h",
        "src/log.h",    "thirdparty/miniaudio/miniaudio.h",
        "nob.c",
    };

    int rebuild =
        needs_rebuild("build/cache/player.o", inputs, ARRAY_LEN(inputs));

    if (rebuild < 0) return false;
    if (rebuild == 0) return true;

    Cmd cmd = {0};

    cmd_append(&cmd, "cc", "-c", "src/player.c", "-o", "build/cache/player.o",
               "-std=c99", "-g", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
               "-I", "thirdparty/miniaudio");

    bool result = cmd_run(&cmd);
    cmd_free(cmd);

    return result;
}

static bool build_playback_order(void)
{
    const char *inputs[] = {
        "src/playback_order.c",
        "src/playback_order.h",
        "nob.c",
    };

    int rebuild = needs_rebuild("build/cache/playback_order.o", inputs,
                                ARRAY_LEN(inputs));

    if (rebuild < 0) return false;
    if (rebuild == 0) return true;

    Cmd cmd = {0};

    cmd_append(&cmd, "cc", "-c", "src/playback_order.c", "-o",
               "build/cache/playback_order.o", "-std=c99", "-g", "-Wall",
               "-Wextra", "-Wpedantic", "-Werror");

    bool result = cmd_run(&cmd);
    cmd_free(cmd);

    return result;
}

static bool build_playlist(void)
{
    const char *inputs[] = {
        "src/playlist.c", "src/playlist.h", "src/log.h",
        "src/metadata.h", "nob.c",
    };

    int rebuild =
        needs_rebuild("build/cache/playlist.o", inputs, ARRAY_LEN(inputs));

    if (rebuild < 0) return false;
    if (rebuild == 0) return true;

    Cmd cmd = {0};

    cmd_append(&cmd, "cc", "-c", "src/playlist.c", "-o",
               "build/cache/playlist.o", "-std=c99", "-g", "-Wall", "-Wextra",
               "-Wpedantic", "-Werror");

    bool result = cmd_run(&cmd);
    cmd_free(cmd);

    return result;
}

static bool build_m3u(void)
{
    const char *inputs[] = {
        "src/m3u.c", "src/m3u.h", "src/log.h", "src/playlist.h", "nob.c",
    };

    int rebuild = needs_rebuild("build/cache/m3u.o", inputs, ARRAY_LEN(inputs));

    if (rebuild < 0) return false;
    if (rebuild == 0) return true;

    Cmd cmd = {0};

    cmd_append(&cmd, "cc", "-c", "src/m3u.c", "-o", "build/cache/m3u.o",
               "-std=c99", "-g", "-Wall", "-Wextra", "-Wpedantic", "-Werror");

    bool result = cmd_run(&cmd);
    cmd_free(cmd);

    return result;
}

static bool build_session(void)
{
    const char *inputs[] = {
        "src/session.c",  "src/session.h", "src/log.h",
        "src/playlist.h", "nob.c",
    };

    int rebuild =
        needs_rebuild("build/cache/session.o", inputs, ARRAY_LEN(inputs));

    if (rebuild < 0) return false;
    if (rebuild == 0) return true;

    Cmd cmd = {0};

    cmd_append(&cmd, "cc", "-c", "src/session.c", "-o", "build/cache/session.o",
               "-std=c99", "-g", "-Wall", "-Wextra", "-Wpedantic", "-Werror");

    bool result = cmd_run(&cmd);
    cmd_free(cmd);

    return result;
}

static bool build_metadata(void)
{
    const char *inputs[] = {
        "src/metadata.c",
        "src/metadata.h",
        "nob.c",
    };

    int rebuild =
        needs_rebuild("build/cache/metadata.o", inputs, ARRAY_LEN(inputs));

    if (rebuild < 0) return false;
    if (rebuild == 0) return true;

    Cmd cmd = {0};

    cmd_append(&cmd, "cc", "-c", "src/metadata.c", "-o",
               "build/cache/metadata.o", "-std=c99", "-g", "-Wall", "-Wextra",
               "-Wpedantic", "-Werror");

    bool result = cmd_run(&cmd);
    cmd_free(cmd);

    return result;
}

static bool install_executable(const char *source)
{
    const char *temporary = "build/mp.new";

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

    const char *app_inputs[] = {
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
        "src/svg.h",
        "nob.c",
    };

    int rebuild =
        needs_rebuild("build/cache/mp.o", app_inputs, ARRAY_LEN(app_inputs));

    if (rebuild < 0) {
        cmd_free(cmd);
        return false;
    }

    if (rebuild > 0) {
        cmd_append(&cmd, "cc", "-c", "src/mp.c", "-o", "build/cache/mp.o");

        cmd_append(&cmd, "-std=c99", "-g", "-Wall", "-Wextra", "-Wpedantic",
                   "-Werror");

        cmd_append(&cmd, "-I", "thirdparty/raylib/src", "-I",
                   "thirdparty/miniaudio");

        if (!cmd_run(&cmd)) {
            cmd_free(cmd);
            return false;
        }
    }

    const char *library = mode == LINK_STATIC ? "build/cache/libraylib.a"
                                              : "build/cache/libraylib.so";

    const char *output = mode == LINK_STATIC ? "build/cache/mp-static"
                                             : "build/cache/mp-dynamic";

    const char *link_inputs[] = {
        "build/cache/mp.o",
        "build/cache/miniaudio.o",
        "build/cache/nanosvg.o",
        "build/cache/log.o",
        "build/cache/m3u.o",
        "build/cache/metadata.o",
        "build/cache/player.o",
        "build/cache/playback_order.o",
        "build/cache/playlist.o",
        "build/cache/session.o",
        "build/cache/svg.o",
        library,
        "nob.c",
    };

    rebuild = needs_rebuild(output, link_inputs, ARRAY_LEN(link_inputs));

    if (rebuild < 0) {
        cmd_free(cmd);
        return false;
    }

    if (rebuild > 0) {
        cmd_append(&cmd, "cc", "-o", output, "build/cache/mp.o",
                   "build/cache/miniaudio.o", "build/cache/nanosvg.o",
                   "build/cache/log.o", "build/cache/m3u.o",
                   "build/cache/player.o", "build/cache/playback_order.o",
                   "build/cache/playlist.o", "build/cache/session.o",
                   "build/cache/svg.o", "build/cache/metadata.o");

        if (mode == LINK_STATIC) {
            cmd_append(&cmd, "build/cache/libraylib.a");
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
        "tests/test.c",   "src/m3u.h",
        "src/metadata.h", "src/playback_order.h",
        "src/playlist.h", "src/session.h",
        "nob.c",
    };

    int rebuild = needs_rebuild("build/cache/test.o", test_inputs,
                                ARRAY_LEN(test_inputs));

    if (rebuild < 0) return false;

    Cmd cmd = {0};

    if (rebuild > 0) {
        cmd_append(&cmd, "cc", "-c", "tests/test.c", "-o", "build/cache/test.o",
                   "-std=c99", "-g", "-Wall", "-Wextra", "-Wpedantic",
                   "-Werror", "-I", "src");

        if (!cmd_run(&cmd)) {
            cmd_free(cmd);
            return false;
        }
    }

    const char *link_inputs[] = {
        "build/cache/test.o",
        "build/cache/log.o",
        "build/cache/m3u.o",
        "build/cache/metadata.o",
        "build/cache/playback_order.o",
        "build/cache/playlist.o",
        "build/cache/session.o",
        "nob.c",
    };

    rebuild = needs_rebuild("build/cache/mp-tests", link_inputs,
                            ARRAY_LEN(link_inputs));

    if (rebuild < 0) {
        cmd_free(cmd);
        return false;
    }

    if (rebuild > 0) {
        cmd_append(&cmd, "cc", "-o", "build/cache/mp-tests",
                   "build/cache/test.o", "build/cache/log.o",
                   "build/cache/m3u.o", "build/cache/metadata.o",
                   "build/cache/playback_order.o", "build/cache/playlist.o",
                   "build/cache/session.o");

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

    Link_Mode mode = LINK_DYNAMIC;
    bool should_run = false;
    bool should_test = false;

    if (argc > 0) {
        const char *argument = shift(argv, argc);

        if (strcmp(argument, "static") == 0) {
            mode = LINK_STATIC;
        } else if (strcmp(argument, "run") == 0) {
            should_run = true;

            if (argc > 0 && strcmp(*argv, "static") == 0) {
                shift(argv, argc);
                mode = LINK_STATIC;
            }
        } else if (strcmp(argument, "test") == 0) {
            should_test = true;
        } else {
            nob_log(ERROR, "Unknown subcommand: %s", argument);
            nob_log(INFO, "Usage: ./nob [static|test|run [static]]");
            return 1;
        }
    }

    if ((!should_run || should_test) && argc > 0) {
        nob_log(ERROR, "Unexpected argument: %s", shift(argv, argc));
        return 1;
    }

    if (!mkdir_if_not_exists("build")) return 1;
    if (!mkdir_if_not_exists("build/cache")) return 1;

    if (should_test) {
        if (!build_log()) return 1;
        if (!build_metadata()) return 1;
        if (!build_playback_order()) return 1;
        if (!build_playlist()) return 1;
        if (!build_m3u()) return 1;
        if (!build_session()) return 1;
        return run_tests() ? 0 : 1;
    }

    if (!build_raylib(mode)) return 1;
    if (!build_miniaudio()) return 1;
    if (!build_nanosvg()) return 1;
    if (!build_log()) return 1;
    if (!build_metadata()) return 1;
    if (!build_player()) return 1;
    if (!build_playback_order()) return 1;
    if (!build_playlist()) return 1;
    if (!build_m3u()) return 1;
    if (!build_session()) return 1;
    if (!build_svg()) return 1;
    if (!build_app(mode)) return 1;

    if (should_run && !run_app(argc, argv)) return 1;

    return 0;
}
