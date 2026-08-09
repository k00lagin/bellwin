#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "../uninstall_helper.h"

static void wait_until_missing(const wchar_t *path) {
    for (int attempt = 0; attempt < 200; ++attempt) {
        if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES
                && (GetLastError() == ERROR_FILE_NOT_FOUND
                    || GetLastError() == ERROR_PATH_NOT_FOUND)) {
            return;
        }
        Sleep(25);
    }
    assert(!"temporary file was not deleted");
}

static void test_suspended_temporary_copy_can_start_and_clean_itself(void) {
    wchar_t source[MAX_PATH];
    assert(GetModuleFileNameW(NULL, source, MAX_PATH) > 0);

    wchar_t copy[MAX_PATH];
    assert(bellwin_create_temporary_executable_copy(source, copy, MAX_PATH));
    assert(wcsstr(copy, L".exe") != NULL);

    wchar_t commandLine[MAX_PATH + 32];
    assert(swprintf_s(commandLine, MAX_PATH + 32, L"\"%ls\" --child", copy) >= 0);
    STARTUPINFOW startup;
    PROCESS_INFORMATION process;
    ZeroMemory(&startup, sizeof(startup));
    ZeroMemory(&process, sizeof(process));
    startup.cb = sizeof(startup);
    assert(CreateProcessW(
        copy, commandLine, NULL, NULL, FALSE, CREATE_SUSPENDED | CREATE_NO_WINDOW,
        NULL, NULL, &startup, &process
    ));
    assert(bellwin_start_temporary_file_cleanup(copy));
    assert(GetFileAttributesW(copy) != INVALID_FILE_ATTRIBUTES);
    assert(ResumeThread(process.hThread) != (DWORD)-1);
    assert(WaitForSingleObject(process.hProcess, 5000) == WAIT_OBJECT_0);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    wait_until_missing(copy);
}

static void test_cleanup_script_retries_a_locked_file(void) {
    wchar_t directory[MAX_PATH];
    wchar_t target[MAX_PATH];
    assert(GetTempPathW(MAX_PATH, directory) > 0);
    assert(GetTempFileNameW(directory, L"BLW", 0, target));
    HANDLE lock = CreateFileW(
        target, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, NULL
    );
    assert(lock != INVALID_HANDLE_VALUE);

    assert(bellwin_start_temporary_file_cleanup(target));
    assert(GetFileAttributesW(target) != INVALID_FILE_ATTRIBUTES);
    CloseHandle(lock);
    wait_until_missing(target);
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--child") == 0) return 0;
    test_suspended_temporary_copy_can_start_and_clean_itself();
    test_cleanup_script_retries_a_locked_file();
    puts("uninstall helper tests passed");
    return 0;
}
