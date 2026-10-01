// nob.c - the build script. Bootstrap once, then it rebuilds itself.
//
//   cc -o nob nob.c      (Windows with Visual Studio: cl nob.c)
//   ./nob                build everything into build/
//   ./nob test           build and run the tests
//   ./nob run            build and start the client
//   ./nob server [port]  build and start the server
//
// The client's server address lives in build/server.h (generated on the
// first build). Edit it, run ./nob again.
#define NOB_IMPLEMENTATION
#include "thirdparty/nob.h"

#define BUILD   "build/"
#define SRC     "src/"
#define THIRD   "thirdparty/"
#define RAYLIB  THIRD "raylib/src/"

#if defined(_MSC_VER)
#   define MSVC 1
#   define OBJ ".obj"
#else
#   define MSVC 0
#   define OBJ ".o"
#endif

static const char *raylib_modules[] = { "rcore", "rshapes", "rtextures", "rtext", "utils", "rglfw" };

static void compiler(Cmd *cmd, bool quiet)
{
    nob_cc(cmd);
    if (MSVC) {
        cmd_append(cmd, "/nologo", "/D_CRT_SECURE_NO_WARNINGS", quiet ? "/w" : "/W3");
    } else {
        cmd_append(cmd, "-g");
        if (quiet) cmd_append(cmd, "-O2", "-w");
        else cmd_append(cmd, "-Wall", "-Wextra", "-Wno-unused-function", "-Wno-missing-field-initializers");
    }
    cmd_append(cmd, "-I.", "-I" SRC, "-I" THIRD, "-I" RAYLIB, "-I" BUILD);
}

static void object_output(Cmd *cmd, const char *path)
{
    if (MSVC) cmd_append(cmd, "/c", temp_sprintf("/Fo:%s", path));
    else cmd_append(cmd, "-c", "-o", path);
}

static void platform_libs(Cmd *cmd, bool gui)
{
#if defined(_WIN32)
    if (MSVC) {
        if (gui) cmd_append(cmd, "gdi32.lib", "winmm.lib", "user32.lib", "shell32.lib", "opengl32.lib");
        cmd_append(cmd, "ws2_32.lib", "bcrypt.lib");
    } else {
        if (gui) cmd_append(cmd, "-lgdi32", "-lwinmm", "-lopengl32");
        cmd_append(cmd, "-lws2_32", "-lbcrypt");
    }
#elif defined(__APPLE__)
    if (gui) cmd_append(cmd, "-framework", "CoreVideo", "-framework", "IOKit", "-framework", "Cocoa",
                        "-framework", "GLUT", "-framework", "OpenGL");
    cmd_append(cmd, "-lm");
#else
    if (gui) cmd_append(cmd, "-lGL", "-lX11");
    cmd_append(cmd, "-lm", "-lpthread", "-ldl", "-lrt");
#endif
}

static bool build_raylib(void)
{
    bool result = true;
    Cmd cmd = {0};
    Procs procs = {0};
    File_Paths objects = {0};

    if (!mkdir_if_not_exists(BUILD "raylib")) return_defer(false);
    for (size_t i = 0; i < ARRAY_LEN(raylib_modules); ++i) {
        const char *in = temp_sprintf(RAYLIB "%s.c", raylib_modules[i]);
        const char *out = temp_sprintf(BUILD "raylib/%s" OBJ, raylib_modules[i]);
        da_append(&objects, out);
        if (!needs_rebuild1(out, in)) continue;
        compiler(&cmd, true);
        cmd_append(&cmd, "-DPLATFORM_DESKTOP", "-DGRAPHICS_API_OPENGL_33", "-I" RAYLIB "external/glfw/include");
#if defined(__APPLE__)
        if (strcmp(raylib_modules[i], "rglfw") == 0) cmd_append(&cmd, "-x", "objective-c");
#elif !defined(_WIN32)
        cmd_append(&cmd, "-D_GLFW_X11");
#endif
        cmd_append(&cmd, in);
        object_output(&cmd, out);
        if (!cmd_run(&cmd, .async = &procs)) return_defer(false);
    }
    if (!procs_flush(&procs)) return_defer(false);

    const char *lib = MSVC ? BUILD "raylib.lib" : BUILD "libraylib.a";
    if (needs_rebuild(lib, objects.items, objects.count)) {
        if (MSVC) cmd_append(&cmd, "lib", "/nologo", temp_sprintf("/OUT:%s", lib));
        else cmd_append(&cmd, "ar", "-crs", lib);
        da_append_many(&cmd, objects.items, objects.count);
        if (!cmd_run(&cmd)) return_defer(false);
    }

defer:
    da_free(cmd);
    da_free(procs);
    da_free(objects);
    return result;
}

static bool build_sqlite(void)
{
    const char *in = THIRD "sqlite3.c", *out = BUILD "sqlite3" OBJ;
    if (!needs_rebuild1(out, in)) return true;
    Cmd cmd = {0};
    compiler(&cmd, true);
    cmd_append(&cmd, "-DSQLITE_THREADSAFE=0", "-DSQLITE_OMIT_LOAD_EXTENSION", in);
    object_output(&cmd, out);
    return cmd_run(&cmd);
}

// build/server.h: the server the client talks to. Generated once with the
// default, then yours to edit. It is in build/ so it never gets committed.
static bool generate_server_h(void)
{
    const char *path = BUILD "server.h";
    if (file_exists(path)) return true;
    const char *text =
        "// The server the client connects to, as host or host:port.\n"
        "// Edit this and run ./nob again. Deleting the file restores the default.\n"
        "#define SERVER \"127.0.0.1:5050\"\n";
    if (!write_entire_file(path, text, strlen(text))) return false;
    nob_log(INFO, "Generated %s with the default server. Edit it to point at yours.", path);
    return true;
}

// build/build.h: the git commit, shown on the setup screen.
static bool generate_build_h(void)
{
    Cmd cmd = {0};
    String_Builder hash = {0};
    cmd_append(&cmd, "git", "rev-parse", "--short", "HEAD");
    Log_Handler *saved = get_log_handler();
    set_log_handler(null_log_handler);
    bool have_git = cmd_run(&cmd, .stdout_path = BUILD "git_hash.txt") &&
                    read_entire_file(BUILD "git_hash.txt", &hash);
    set_log_handler(saved);
    while (hash.count > 0 && isspace(da_last(&hash))) hash.count--;
    if (!have_git || hash.count == 0) sb_append_cstr(&hash, "unknown");

    String_Builder out = {0};
    sb_appendf(&out, "#define GIT_HASH \"" SV_Fmt "\"\n", SV_Arg(sb_to_sv(hash)));
    bool ok = write_entire_file(BUILD "build.h", out.items, out.count);
    sb_free(hash);
    sb_free(out);
    return ok;
}

// build/bundle.h: the fonts as byte arrays, so the binary needs no files next to it.
static bool generate_bundle_h(void)
{
    static const struct { const char *name, *path; } fonts[] = {
        { "font_regular", "assets/fonts/Inter-Regular.ttf" },
        { "font_bold",    "assets/fonts/Inter-SemiBold.ttf" },
    };
    const char *out_path = BUILD "bundle.h";
    const char *inputs[ARRAY_LEN(fonts)];
    for (size_t i = 0; i < ARRAY_LEN(fonts); ++i) inputs[i] = fonts[i].path;
    if (!needs_rebuild(out_path, inputs, ARRAY_LEN(fonts))) return true;

    bool result = true;
    String_Builder out = {0}, data = {0};
    for (size_t i = 0; i < ARRAY_LEN(fonts); ++i) {
        data.count = 0;
        if (!read_entire_file(fonts[i].path, &data)) return_defer(false);
        sb_appendf(&out, "static const unsigned char %s[] = {\n", fonts[i].name);
        for (size_t j = 0; j < data.count; ++j) {
            sb_appendf(&out, "%d,", (unsigned char)data.items[j]);
            if (j % 32 == 31) sb_append_cstr(&out, "\n");
        }
        sb_appendf(&out, "\n};\nstatic const size_t %s_size = %zu;\n", fonts[i].name, data.count);
    }
    if (!write_entire_file(out_path, out.items, out.count)) return_defer(false);
    nob_log(INFO, "Generated %s", out_path);
defer:
    sb_free(out);
    sb_free(data);
    return result;
}

// build/compile_commands.json: tells clangd (VS Code, Emacs, ...) how each
// file is compiled. Files that are only ever #included get their prelude
// passed with -include so they check on their own too.
static bool generate_compile_commands(void)
{
    static const struct { const char *file; const char *prelude; } files[] = {
        { "nob.c",         "" },
        { "test.c",        "" },
        { "src/chat.c",    "" },
        { "src/chatd.c",   "" },
        { "src/protocol.h","nob.h" },
        { "src/crypto.h",  "nob.h protocol.h" },
        { "src/net.h",     "nob.h" },
        { "src/client.c",  "nob.h protocol.h crypto.h net.h" },
        { "src/db.c",      "nob.h sqlite3.h protocol.h crypto.h" },
        { "src/ui.c",      "nob.h raylib.h" },
    };
    const char *dir = get_current_dir_temp();
    if (!dir) return false;
    String_Builder out = {0};
    sb_append_cstr(&out, "[\n");
    for (size_t i = 0; i < ARRAY_LEN(files); ++i) {
        Cmd cmd = {0};
        compiler(&cmd, false);
        cmd_append(&cmd, "-xc", "-c");
        String_View prelude = sv_from_cstr(files[i].prelude);
        while (prelude.count > 0) {
            String_View header = sv_chop_by_delim(&prelude, ' ');
            cmd_append(&cmd, "-include", temp_sv_to_cstr(header));
        }
        cmd_append(&cmd, files[i].file);
        sb_appendf(&out, "  {\"directory\": \"%s\", \"file\": \"%s\", \"arguments\": [", dir, files[i].file);
        da_foreach(const char *, arg, &cmd) {
            sb_appendf(&out, "%s\"%s\"", arg == cmd.items ? "" : ", ", *arg);
        }
        sb_appendf(&out, "]}%s\n", i + 1 < ARRAY_LEN(files) ? "," : "");
        da_free(cmd);
    }
    sb_append_cstr(&out, "]\n");
    bool ok = write_entire_file(BUILD "compile_commands.json", out.items, out.count);
    sb_free(out);
    return ok;
}

static bool build_program(const char *out, const char *source, bool gui)
{
    Cmd cmd = {0};
    compiler(&cmd, false);
    nob_cc_output(&cmd, out);
    cmd_append(&cmd, source, THIRD "monocypher.c");
    if (gui) cmd_append(&cmd, BUILD "sqlite3" OBJ, MSVC ? BUILD "raylib.lib" : BUILD "libraylib.a");
    platform_libs(&cmd, gui);
    return cmd_run(&cmd);
}

static bool build_all(void)
{
    if (!mkdir_if_not_exists(BUILD)) return false;
    if (!generate_compile_commands()) return false;
    if (!build_raylib()) return false;
    if (!build_sqlite()) return false;
    if (!generate_server_h()) return false;
    if (!generate_build_h()) return false;
    if (!generate_bundle_h()) return false;
    if (!build_program(BUILD "chatd", SRC "chatd.c", false)) return false;
    if (!build_program(BUILD "chat", SRC "chat.c", true)) return false;
    return true;
}

int main(int argc, char **argv)
{
    GO_REBUILD_URSELF(argc, argv);
    shift(argv, argc); // program name
    const char *command = argc > 0 ? shift(argv, argc) : "build";
    Cmd cmd = {0};

    if (strcmp(command, "build") == 0) {
        return build_all() ? 0 : 1;
    }
    if (strcmp(command, "test") == 0) {
        if (!mkdir_if_not_exists(BUILD)) return 1;
        if (!generate_compile_commands()) return 1;
        if (!build_program(BUILD "test", "test.c", false)) return 1;
        cmd_append(&cmd, BUILD "test");
        return cmd_run(&cmd) ? 0 : 1;
    }
    if (strcmp(command, "run") == 0) {
        if (!build_all()) return 1;
        cmd_append(&cmd, BUILD "chat");
        da_append_many(&cmd, argv, argc);
        return cmd_run(&cmd) ? 0 : 1;
    }
    if (strcmp(command, "server") == 0) {
        if (!build_all()) return 1;
        cmd_append(&cmd, BUILD "chatd");
        da_append_many(&cmd, argv, argc);
        return cmd_run(&cmd) ? 0 : 1;
    }
    nob_log(ERROR, "Unknown command `%s`. Commands: build, test, run, server", command);
    return 1;
}
