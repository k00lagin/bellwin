#define BUILD_FOLDER ".build/"
#define NOB_REBUILD_URSELF_BACKUP_PATH(binary_path) BUILD_FOLDER "nob.exe.old"
#define NOB_IMPLEMENTATION
#define NOB_UNSTRIP_PREFIX
#include "./thirdparty/nob.h"

#define TARGET "i686-pc-windows-msvc"

static Nob_Cmd cmd = {0};
static Nob_Procs procs = {0};

static bool build_resources(void) {
    struct ResourceBuild {
        const char *output;
        const char *inputs[4];
        size_t inputCount;
    } resources[] = {
        {
            BUILD_FOLDER "version.res",
            {"src/version.rc", "src/version.h"},
            2,
        },
        {
            BUILD_FOLDER "resources.res",
            {"src/resources.rc", "src/resource.h", "resources/BellSound.mp3", "resources/bellwin.ico"},
            4,
        },
    };

    for (size_t i = 0; i < sizeof(resources) / sizeof(resources[0]); ++i) {
        int rebuild = nob_needs_rebuild(resources[i].output, resources[i].inputs, resources[i].inputCount);
        if (rebuild < 0) return false;
        if (rebuild) {
            nob_cmd_append(&cmd, "llvm-rc", resources[i].inputs[0], "/r", "/fo", resources[i].output);
            if (!nob_cmd_run(&cmd, .async = &procs)) return false;
        }
    }
    return nob_procs_flush(&procs);
}

static bool build_tests(void) {
    nob_cmd_append(
        &cmd,
        "clang", "-target", TARGET, "-fuse-ld=lld",
        "-D_WIN32_WINNT=0x0601", "-DWINVER=0x0601",
        "-Wall", "-Wextra", "-Werror",
        "src/tests/core_tests.c",
        "-o", BUILD_FOLDER "core_tests.exe",
        "-Xlinker", "/SUBSYSTEM:CONSOLE,6.01", "-Xlinker", "/OSVERSION:6.1"
    );
    if (!nob_cmd_run(&cmd)) return false;
    nob_cmd_append(&cmd, BUILD_FOLDER "core_tests.exe");
    if (!nob_cmd_run(&cmd)) return false;

    nob_cmd_append(
        &cmd,
        "clang", "-target", TARGET, "-fuse-ld=lld",
        "-D_WIN32_WINNT=0x0601", "-DWINVER=0x0601",
        "-Wall", "-Wextra", "-Werror",
        "src/tests/widget_tests.c",
        "-o", BUILD_FOLDER "widget_tests.exe",
        "-Xlinker", "/SUBSYSTEM:CONSOLE,6.01", "-Xlinker", "/OSVERSION:6.1"
    );
    if (!nob_cmd_run(&cmd)) return false;
    nob_cmd_append(&cmd, BUILD_FOLDER "widget_tests.exe");
    if (!nob_cmd_run(&cmd)) return false;

    nob_cmd_append(
        &cmd,
        "clang", "-target", TARGET, "-fuse-ld=lld",
        "-D_WIN32_WINNT=0x0601", "-DWINVER=0x0601",
        "-Wall", "-Wextra", "-Werror",
        "src/tests/layout_tests.c", "src/layout.c",
        "-o", BUILD_FOLDER "layout_tests.exe",
        "-Xlinker", "/SUBSYSTEM:CONSOLE,6.01", "-Xlinker", "/OSVERSION:6.1"
    );
    if (!nob_cmd_run(&cmd)) return false;
    nob_cmd_append(&cmd, BUILD_FOLDER "layout_tests.exe");
    if (!nob_cmd_run(&cmd)) return false;

    nob_cmd_append(
        &cmd,
        "clang", "-target", TARGET, "-fuse-ld=lld",
        "-D_WIN32_WINNT=0x0601", "-DWINVER=0x0601",
        "-Wall", "-Wextra", "-Werror",
        "src/tests/cli_tests.c", "src/cli.c",
        "-o", BUILD_FOLDER "cli_tests.exe",
        "-Xlinker", "/SUBSYSTEM:CONSOLE,6.01", "-Xlinker", "/OSVERSION:6.1"
    );
    if (!nob_cmd_run(&cmd)) return false;
    nob_cmd_append(&cmd, BUILD_FOLDER "cli_tests.exe");
    if (!nob_cmd_run(&cmd)) return false;

    nob_cmd_append(
        &cmd,
        "clang", "-target", TARGET, "-fuse-ld=lld",
        "-D_WIN32_WINNT=0x0601", "-DWINVER=0x0601", "-DBELLWIN_THEME_TESTING",
        "-Wall", "-Wextra", "-Werror",
        "src/tests/theme_tests.c", "src/theme.c",
        "-o", BUILD_FOLDER "theme_tests.exe",
        "-Xlinker", "/SUBSYSTEM:CONSOLE,6.01", "-Xlinker", "/OSVERSION:6.1",
        "-luser32", "-ladvapi32", "-ldwmapi"
    );
    if (!nob_cmd_run(&cmd)) return false;
    nob_cmd_append(&cmd, BUILD_FOLDER "theme_tests.exe");
    return nob_cmd_run(&cmd);
}

int main(int argc, char **argv) {
    if (!nob_mkdir_if_not_exists(BUILD_FOLDER)) return 1;
    NOB_GO_REBUILD_URSELF_PLUS(
        argc, argv,
        "src/thirdparty/nob.h"
    );

    bool runTests = false;
    bool clean = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--test") == 0 || strcmp(argv[i], "-test") == 0) runTests = true;
        else if (strcmp(argv[i], "--clean") == 0 || strcmp(argv[i], "-clean") == 0) clean = true;
        else {
            nob_log(NOB_ERROR, "unknown argument: %s", argv[i]);
            return 1;
        }
    }

    if (clean) {
        nob_delete_file("Bellwin.exe");
        nob_delete_file("Bellwin.pdb");
        nob_delete_file("Bellwin.exp");
        nob_delete_file("Bellwin.lib");
        return 0;
    }

    if (runTests) return build_tests() ? 0 : 1;
    if (!build_resources()) return 1;

    nob_cmd_append(
        &cmd,
        "clang", "-target", TARGET, "-fuse-ld=lld",
        "-D_WIN32_WINNT=0x0601", "-DWINVER=0x0601", "-DUNICODE", "-D_UNICODE",
        "-Os", "-Wall", "-Wextra", "-Werror",
        "src/main.c", "src/ui.c", "src/layout.c", "src/render_gdi.c", "src/uia.c", "src/cli.c", "src/theme.c", BUILD_FOLDER "version.res", BUILD_FOLDER "resources.res",
        "-o", "Bellwin.exe",
        "-Xlinker", "/SUBSYSTEM:WINDOWS,6.01", "-Xlinker", "/OSVERSION:6.1",
        "-Xlinker", "/ENTRY:mainCRTStartup",
        "-luser32", "-lgdi32", "-lshell32", "-lole32", "-luuid",
        "-lwinmm", "-lversion", "-ladvapi32", "-ldwmapi",
        "-luiautomationcore", "-loleaut32"
    );
    if (!nob_cmd_run(&cmd)) return 1;
    nob_log(NOB_INFO, "Bellwin.exe built for Windows 7+ x86");
    return 0;
}
