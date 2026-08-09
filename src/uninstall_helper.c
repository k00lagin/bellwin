#include "uninstall_helper.h"

#include <windows.h>
#include <stdio.h>
#include <wchar.h>

static HANDLE create_temporary_file(
    const wchar_t *extension,
    wchar_t *path,
    size_t pathCount
) {
    if (!extension || !path || pathCount < MAX_PATH) return INVALID_HANDLE_VALUE;

    wchar_t directory[MAX_PATH];
    DWORD directoryLength = GetTempPathW(MAX_PATH, directory);
    if (directoryLength == 0 || directoryLength >= MAX_PATH) return INVALID_HANDLE_VALUE;

    for (int attempt = 0; attempt < 16; ++attempt) {
        wchar_t original[MAX_PATH];
        if (!GetTempFileNameW(directory, L"BLW", 0, original)) return INVALID_HANDLE_VALUE;

        wcscpy_s(path, pathCount, original);
        wchar_t *dot = wcsrchr(path, L'.');
        if (!dot || wcslen(extension) > wcslen(dot)) {
            DeleteFileW(original);
            return INVALID_HANDLE_VALUE;
        }
        wcscpy_s(dot, (size_t)(path + pathCount - dot), extension);
        HANDLE file = CreateFileW(
            path,
            GENERIC_WRITE,
            0,
            NULL,
            CREATE_NEW,
            FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_OPEN_REPARSE_POINT,
            NULL
        );
        DWORD createError = GetLastError();
        DeleteFileW(original);
        if (file != INVALID_HANDLE_VALUE) return file;
        if (createError != ERROR_ALREADY_EXISTS && createError != ERROR_FILE_EXISTS) {
            return INVALID_HANDLE_VALUE;
        }
    }
    return INVALID_HANDLE_VALUE;
}

int bellwin_create_temporary_executable_copy(
    const wchar_t *sourcePath,
    wchar_t *temporaryPath,
    size_t temporaryPathCount
) {
    if (!sourcePath || !temporaryPath) return 0;
    HANDLE source = CreateFileW(
        sourcePath,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    if (source == INVALID_HANDLE_VALUE) return 0;
    HANDLE destination = create_temporary_file(
        L".exe", temporaryPath, temporaryPathCount
    );
    if (destination == INVALID_HANDLE_VALUE) {
        CloseHandle(source);
        return 0;
    }

    BYTE buffer[64 * 1024];
    int copied = 1;
    for (;;) {
        DWORD read = 0;
        if (!ReadFile(source, buffer, sizeof(buffer), &read, NULL)) {
            copied = 0;
            break;
        }
        if (read == 0) break;
        DWORD offset = 0;
        while (offset < read) {
            DWORD written = 0;
            if (!WriteFile(destination, buffer + offset, read - offset, &written, NULL)
                    || written == 0) {
                copied = 0;
                break;
            }
            offset += written;
        }
        if (!copied) break;
    }
    if (copied && !FlushFileBuffers(destination)) copied = 0;
    CloseHandle(destination);
    CloseHandle(source);
    if (!copied) DeleteFileW(temporaryPath);
    return copied;
}

int bellwin_start_temporary_file_cleanup(const wchar_t *targetPath) {
    static const char script[] =
        "var fso = new ActiveXObject(\"Scripting.FileSystemObject\");\r\n"
        "var self = WScript.ScriptFullName;\r\n"
        "var target = WScript.Arguments.Item(0);\r\n"
        "try { fso.DeleteFile(self, true); } catch (e) {}\r\n"
        "for (var count = 0; count < 480 && fso.FileExists(target); count++) {\r\n"
        "  try { fso.DeleteFile(target, true); } catch (e) {}\r\n"
        "  if (fso.FileExists(target)) WScript.Sleep(250);\r\n"
        "}\r\n";

    if (!targetPath || !targetPath[0]) return 0;

    wchar_t scriptPath[MAX_PATH];
    HANDLE scriptFile = create_temporary_file(L".js", scriptPath, MAX_PATH);
    if (scriptFile == INVALID_HANDLE_VALUE) return 0;
    DWORD written = 0;
    int saved = WriteFile(scriptFile, script, (DWORD)(sizeof(script) - 1), &written, NULL)
        && written == sizeof(script) - 1
        && FlushFileBuffers(scriptFile);
    CloseHandle(scriptFile);
    if (!saved) {
        DeleteFileW(scriptPath);
        return 0;
    }

    wchar_t systemDirectory[MAX_PATH];
    UINT systemLength = GetSystemDirectoryW(systemDirectory, MAX_PATH);
    wchar_t wscriptPath[MAX_PATH];
    if (systemLength == 0 || systemLength >= MAX_PATH
            || swprintf_s(wscriptPath, MAX_PATH, L"%ls\\wscript.exe", systemDirectory) < 0) {
        DeleteFileW(scriptPath);
        return 0;
    }

    wchar_t commandLine[MAX_PATH * 2 + 96];
    if (swprintf_s(
            commandLine,
            sizeof(commandLine) / sizeof(commandLine[0]),
            L"\"%ls\" //B //NoLogo //E:jscript \"%ls\" \"%ls\"",
            wscriptPath,
            scriptPath,
            targetPath
        ) < 0) {
        DeleteFileW(scriptPath);
        return 0;
    }

    STARTUPINFOW startup;
    PROCESS_INFORMATION process;
    ZeroMemory(&startup, sizeof(startup));
    ZeroMemory(&process, sizeof(process));
    startup.cb = sizeof(startup);
    int created = CreateProcessW(
        wscriptPath,
        commandLine,
        NULL,
        NULL,
        FALSE,
        CREATE_NO_WINDOW,
        NULL,
        NULL,
        &startup,
        &process
    );
    if (!created) {
        DeleteFileW(scriptPath);
        return 0;
    }

    int started = 0;
    for (int attempt = 0; attempt < 100; ++attempt) {
        if (GetFileAttributesW(scriptPath) == INVALID_FILE_ATTRIBUTES
                && (GetLastError() == ERROR_FILE_NOT_FOUND
                    || GetLastError() == ERROR_PATH_NOT_FOUND)) {
            started = 1;
            break;
        }
        if (WaitForSingleObject(process.hProcess, 0) == WAIT_OBJECT_0) break;
        Sleep(50);
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (!started) DeleteFileW(scriptPath);
    return started;
}
