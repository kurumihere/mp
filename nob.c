#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L
#define NOB_EXPERIMENTAL_TRACE_CMD_RUN_FAIL
#define NOB_IMPLEMENTATION
#include "thirdparty/nob.h"

#include <ctype.h>

typedef enum {
    MP_PLATFORM_LINUX,
    MP_PLATFORM_WINDOWS,
    MP_PLATFORM_MACOS,
} Platform;

typedef struct {
    Platform platform;
    const char *cc;
    const char *executable;
    const char *object_dir;
    const char *runner;
} Build;

typedef struct {
    const char *path;
    const char *symbol;
    const char *name;
} Asset_Source;

#define GENERATED_ASSETS_SOURCE "build/generated/assets.c"

static const Asset_Source asset_sources[] = {
    {"assets/icons/black/back-black.svg", "back_black_svg",
     "back-black.svg"},
    {"assets/icons/black/forward-black.svg", "forward_black_svg",
     "forward-black.svg"},
    {"assets/icons/black/pause-black.svg", "pause_black_svg",
     "pause-black.svg"},
    {"assets/icons/black/play-black.svg", "play_black_svg",
     "play-black.svg"},
    {"assets/icons/black/repeat-black.svg", "repeat_black_svg",
     "repeat-black.svg"},
    {"assets/icons/black/repeat-one-black.svg", "repeat_one_black_svg",
     "repeat-one-black.svg"},
    {"assets/icons/black/settings-black.svg", "settings_black_svg",
     "settings-black.svg"},
    {"assets/icons/black/shuffle-black.svg", "shuffle_black_svg",
     "shuffle-black.svg"},
    {"assets/icons/black/playlist-black.svg", "playlist_black_svg",
     "playlist-black.svg"},
    {"assets/icons/white/back-white.svg", "back_white_svg",
     "back-white.svg"},
    {"assets/icons/white/forward-white.svg", "forward_white_svg",
     "forward-white.svg"},
    {"assets/icons/white/pause-white.svg", "pause_white_svg",
     "pause-white.svg"},
    {"assets/icons/white/play-white.svg", "play_white_svg",
     "play-white.svg"},
    {"assets/icons/white/repeat-white.svg", "repeat_white_svg",
     "repeat-white.svg"},
    {"assets/icons/white/repeat-one-white.svg", "repeat_one_white_svg",
     "repeat-one-white.svg"},
    {"assets/icons/white/settings-white.svg", "settings_white_svg",
     "settings-white.svg"},
    {"assets/icons/white/shuffle-white.svg", "shuffle_white_svg",
     "shuffle-white.svg"},
    {"assets/icons/white/playlist-white.svg", "playlist_white_svg",
     "playlist-white.svg"},
    {"assets/icon.png", "icon_png", "icon.png"},
    {"assets/fonts/NotoSans.ttf", "noto_sans_ttf", "NotoSans.ttf"},
    {"assets/fonts/NotoSansJP.ttf", "noto_sans_jp_ttf", "NotoSansJP.ttf"},
    {"assets/fonts/NotoSansKR.ttf", "noto_sans_kr_ttf", "NotoSansKR.ttf"},
    {"assets/fonts/NotoSansTC.ttf", "noto_sans_tc_ttf", "NotoSansTC.ttf"},
};

static const char *app_sources[] = {
    GENERATED_ASSETS_SOURCE,
    "src/mp.c",
    "src/font_renderer.c",
    "src/config.c",
    "src/svg.c",
    "src/log.c",
    "src/player.c",
    "src/playback_order.c",
    "src/playlist.c",
    "src/playlist_search.c",
    "src/worker_thread.c",
    "src/m3u.c",
    "src/session.c",
    "src/metadata.c",
    "src/spectrum.c",
    "src/theme.c",
    "thirdparty/miniaudio/miniaudio.c",
    "thirdparty/nanosvg/nanosvg.c",
    "thirdparty/tinyfiledialogs/tinyfiledialogs.c",
};

static const char *raylib_sources[] = {
    "thirdparty/raylib/src/rcore.c",
    "thirdparty/raylib/src/rshapes.c",
    "thirdparty/raylib/src/rtextures.c",
    "thirdparty/raylib/src/rtext.c",
    "thirdparty/raylib/src/rglfw.c",
};

static bool generate_assets(void)
{
    const char *inputs[ARRAY_LEN(asset_sources) + 1];
    inputs[0] = "nob.c";

    for (size_t i = 0; i < ARRAY_LEN(asset_sources); ++i) {
        inputs[i + 1] = asset_sources[i].path;
    }

    if (!mkdir_if_not_exists("build/generated")) return false;

    int rebuild =
        needs_rebuild(GENERATED_ASSETS_SOURCE, inputs, ARRAY_LEN(inputs));

    if (rebuild < 0) return false;
    if (rebuild == 0) return true;

    String_Builder output = {0};
    sb_append_cstr(&output, "#include \"assets.h\"\n\n");

    for (size_t asset_index = 0; asset_index < ARRAY_LEN(asset_sources);
         ++asset_index) {
        const Asset_Source *asset = &asset_sources[asset_index];
        String_Builder data = {0};

        if (!read_entire_file(asset->path, &data) || data.count == 0) {
            sb_free(data);
            sb_free(output);
            return false;
        }

        sb_appendf(&output, "static const unsigned char %s_data[] = {\n",
                   asset->symbol);

        static const char hex[] = "0123456789abcdef";

        for (size_t i = 0; i < data.count; ++i) {
            if (i % 12 == 0) sb_append_cstr(&output, "    ");

            unsigned char byte = (unsigned char)data.items[i];
            char encoded[] = {'0', 'x', hex[byte >> 4], hex[byte & 0x0f], ','};
            sb_append_buf(&output, encoded, sizeof(encoded));

            if (i % 12 == 11 || i + 1 == data.count) {
                sb_append_cstr(&output, "\n");
            }
        }

        sb_append_cstr(&output, "};\n\n");
        sb_free(data);
    }

    sb_append_cstr(&output,
                   "static const Embedded_Asset assets[ASSET_COUNT] = {\n");

    for (size_t i = 0; i < ARRAY_LEN(asset_sources); ++i) {
        const Asset_Source *asset = &asset_sources[i];
        sb_appendf(&output, "    {%s_data, sizeof(%s_data), \"%s\"},\n",
                   asset->symbol, asset->symbol, asset->name);
    }

    sb_appendf(&output,
               "};\n\n_Static_assert(ASSET_COUNT == %zu, \"asset list mismatch\");\n\n",
               ARRAY_LEN(asset_sources));
    sb_append_cstr(
        &output,
        "Embedded_Asset asset_get(Asset_Id id)\n"
        "{\n"
        "    if ((int)id < 0 || id >= ASSET_COUNT) return (Embedded_Asset){0};\n"
        "    return assets[id];\n"
        "}\n");

    const char *temporary = "build/generated/assets.c.tmp";
    bool result = write_entire_file(temporary, output.items, output.count) &&
                  nob_rename(temporary, GENERATED_ASSETS_SOURCE);

    if (!result) delete_file(temporary);

    sb_free(output);
    return result;
}

static bool collect_source_file(Walk_Entry entry)
{
    if (entry.type != FILE_REGULAR) return true;

    String_View path = sv_from_cstr(entry.path);
    if (!sv_ends_with_cstr(path, ".c") && !sv_ends_with_cstr(path, ".h") &&
        !sv_ends_with_cstr(path, ".m")) {
        return true;
    }

    File_Paths *inputs = entry.data;
    da_append(inputs, temp_strdup(entry.path));
    return true;
}

static int app_needs_rebuild(const Build *build)
{
    const char *roots[] = {
        "src",
        "thirdparty/miniaudio",
        "thirdparty/nanosvg",
        "thirdparty/raylib/src",
    };
    File_Paths inputs = {0};
    da_append(&inputs, "nob.c");
    da_append(&inputs, "thirdparty/nob.h");
    da_append(&inputs, GENERATED_ASSETS_SOURCE);

    for (size_t i = 0; i < ARRAY_LEN(roots); ++i) {
        if (!walk_dir(roots[i], collect_source_file, .data = &inputs)) {
            da_free(inputs);
            return -1;
        }
    }

    int result = needs_rebuild(build->executable, inputs.items, inputs.count);
    da_free(inputs);
    return result;
}

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
        build.platform = MP_PLATFORM_WINDOWS;
        if (build.cc == NULL) build.cc = "x86_64-w64-mingw32-cc";
        build.executable = "build/mp.exe";
        build.object_dir = "build/obj/windows";
        build.runner = "wine";
        return build;
    }

#if defined(_WIN32)
    build.platform = MP_PLATFORM_WINDOWS;
    build.executable = "build/mp.exe";
    build.object_dir = "build/obj/windows";
#elif defined(__APPLE__)
    build.platform = MP_PLATFORM_MACOS;
    build.executable = "build/mp";
    build.object_dir = "build/obj/macos";
#else
    build.platform = MP_PLATFORM_LINUX;
    build.executable = "build/mp";
    build.object_dir = "build/obj/linux";
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
    if (platform == MP_PLATFORM_WINDOWS) {
        cmd_append(cmd, "-lopengl32", "-lgdi32", "-lwinmm", "-lshell32",
                   "-ladvapi32", "-lcomdlg32", "-lole32");
    } else if (platform == MP_PLATFORM_MACOS) {
        cmd_append(cmd, "-framework", "OpenGL", "-framework", "Cocoa",
                   "-framework", "IOKit", "-framework", "CoreAudio",
                   "-framework", "CoreVideo", "-framework",
                   "CoreFoundation");
    } else {
        cmd_append(cmd, "-lGL", "-lm", "-lpthread", "-ldl", "-lrt", "-lX11");
    }
}

static void append_compile_options(Cmd *cmd, Platform platform)
{
    cmd_append(cmd, "-std=c11", "-g", "-Wall",
               "-Wextra", "-Wno-unused-parameter", "-Wno-sign-compare",
               "-Wno-format", "-Wno-missing-braces",
               "-Wno-missing-field-initializers", "-fno-strict-aliasing",
               "-Werror=implicit-function-declaration",
               "-DPLATFORM_DESKTOP_GLFW", "-DGRAPHICS_API_OPENGL_33",
               "-DSUPPORT_MODULE_RMODELS=0", "-DSUPPORT_MODULE_RAUDIO=0",
               "-DSUPPORT_FILEFORMAT_JPG=1", "-I", "thirdparty/raylib/src",
               "-I", "thirdparty/raylib/src/external/glfw/include", "-I",
               "thirdparty/miniaudio", "-I", "thirdparty/nanosvg", "-I",
               "thirdparty", "-I", "src");

    if (platform == MP_PLATFORM_LINUX) {
        cmd_append(cmd, "-D_GLFW_X11");
    } else if (platform == MP_PLATFORM_WINDOWS) {
        cmd_append(cmd, "-DUNICODE");
    }
}

static const char *source_object_path(const Build *build, const char *source)
{
    return temp_sprintf("%s/%s.o", build->object_dir, path_name(source));
}

static bool compile_source(const Build *build, const char *source,
                           const char *object,
                           const File_Paths *platform_compile_options,
                           Procs *procs, size_t jobs)
{
    Cmd cmd = {0};
    append_compiler(&cmd, build);
    append_compile_options(&cmd, build->platform);
    da_append_many(&cmd, platform_compile_options->items,
                   platform_compile_options->count);

    if (build->platform == MP_PLATFORM_MACOS &&
        strcmp(source, "thirdparty/raylib/src/rglfw.c") == 0) {
        cmd_append(&cmd, "-x", "objective-c");
    }

    cmd_append(&cmd, "-c", source, "-o", object);
    bool result = cmd_run(&cmd, .async = procs, .max_procs = jobs);
    cmd_free(cmd);
    return result;
}

static int non_space(int character)
{
    return !isspace((unsigned char)character);
}

static bool pkg_config_arguments(const char *option, const char *package,
                                 const char *output_path,
                                 File_Paths *arguments)
{
    Cmd cmd = {0};
    const char *pkg_config = environment_tool("PKG_CONFIG");
    cmd_append(&cmd, pkg_config == NULL ? "pkg-config" : pkg_config, option,
               package);

    if (!cmd_run(&cmd, .stdout_path = output_path)) {
        cmd_free(cmd);
        return false;
    }

    cmd_free(cmd);

    String_Builder output = {0};

    if (!read_entire_file(output_path, &output)) {
        sb_free(output);
        return false;
    }

    String_View remaining = sb_to_sv(output);

    while (remaining.count > 0) {
        remaining = sv_trim_left(remaining);

        if (remaining.count == 0) break;

        String_View argument = sv_chop_while(&remaining, non_space);
        da_append(arguments, temp_sv_to_cstr(argument));
    }

    sb_free(output);
    return true;
}

static bool build_app(const Build *build)
{
    if (!mkdir_if_not_exists("build/obj")) return false;
    if (!mkdir_if_not_exists(build->object_dir)) return false;

    File_Paths objects = {0};
    File_Paths platform_compile_options = {0};
    File_Paths platform_link_options = {0};
    Procs procs = {0};
    int processor_count = nob_nprocs();
    size_t jobs = processor_count > 0 ? (size_t) processor_count : 1;
    bool result = true;

    if (build->platform == MP_PLATFORM_LINUX) {
        result =
            pkg_config_arguments("--cflags", "gio-2.0",
                                 "build/gio-cflags.txt",
                                 &platform_compile_options) &&
            pkg_config_arguments("--libs", "gio-2.0", "build/gio-libs.txt",
                                 &platform_link_options);
    }

    if (result) {
        result = pkg_config_arguments("--cflags", "freetype2",
                                      "build/freetype-cflags.txt",
                                      &platform_compile_options) &&
                 pkg_config_arguments("--libs", "freetype2",
                                      "build/freetype-libs.txt",
                                      &platform_link_options);
    }

    for (size_t i = 0; result && i < ARRAY_LEN(app_sources); ++i) {
        const char *object = source_object_path(build, app_sources[i]);
        da_append(&objects, object);
        if (!compile_source(build, app_sources[i], object,
                            &platform_compile_options, &procs, jobs)) {
            result = false;
            break;
        }
    }

    for (size_t i = 0; result && i < ARRAY_LEN(raylib_sources); ++i) {
        const char *object = source_object_path(build, raylib_sources[i]);
        da_append(&objects, object);
        if (!compile_source(build, raylib_sources[i], object,
                            &platform_compile_options, &procs, jobs)) {
            result = false;
            break;
        }
    }

    if (!procs_flush(&procs)) result = false;

    if (result) {
        Cmd cmd = {0};
        append_compiler(&cmd, build);
        cmd_append(&cmd, "-o", build->executable);
        da_append_many(&cmd, objects.items, objects.count);
        da_append_many(&cmd, platform_link_options.items,
                       platform_link_options.count);
        append_platform_options(&cmd, build->platform);
        result = cmd_run(&cmd);
        cmd_free(cmd);
    }

    da_free(procs);
    da_free(objects);
    da_free(platform_compile_options);
    da_free(platform_link_options);
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
    nob_log(INFO,
            "usage: %s [run [audio files, playlists, or folders ...] | "
            "wine [audio files, playlists, or folders ...]]",
            program);
    return 1;
}

int main(int argc, char **argv)
{
    GO_REBUILD_URSELF_PLUS(argc, argv, "thirdparty/nob.h");

    const char *program = shift(argv, argc);
    const char *command = argc > 0 ? shift(argv, argc) : "build";
    bool wine = strcmp(command, "wine") == 0;
    bool run = wine || strcmp(command, "run") == 0;

    if (!run && strcmp(command, "build") != 0) return usage(program);
    if (!run && argc > 0) return usage(program);

#if defined(_WIN32)
    if (wine) {
        nob_log(ERROR, "wine build is only available on non-Windows hosts");
        return 1;
    }
#endif

    if (!mkdir_if_not_exists("build")) return 1;
    if (!generate_assets()) return 1;

    Build build = configure_build(wine);
    int rebuild = app_needs_rebuild(&build);
    if (rebuild < 0) return 1;
    if (rebuild > 0) {
        if (!build_app(&build)) return 1;
    } else {
        nob_log(INFO, "%s is up to date", build.executable);
    }
    if (run && !run_app(&build, argc, argv)) return 1;
    return 0;
}
