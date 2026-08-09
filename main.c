#define COBJMACROS
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <mmsystem.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>

#include "core.h"
#include "cli.h"
#include "render_gdi.h"
#include "ui.h"
#include "uia.h"
#include "resource.h"
#include "theme.h"
#include "version.h"
#include "widgets.h"
#include "layout.h"
#include "app_internal.h"

#define WINDOW_TITLE L"Settings ∙ Bellwin"
#define APP_MUTEX L"Local\\Bellwin.SingleInstance"
#define CLI_WINDOW_CLASS L"Bellwin.Cli.Response.Window"
#define BELLWIN_COPYDATA_REQUEST ((ULONG_PTR)0x42575131u)
#define BELLWIN_COPYDATA_RESPONSE ((ULONG_PTR)0x42575231u)
#define TIMER_SCHEDULE 1
#define TIMER_VOLUME_PREVIEW 2
#define TIMER_TRAY_SINGLE_CLICK 3
#define WM_TRAY (WM_APP + 1)
#define WM_SHOW_BELLWIN (WM_APP + 2)
#define CMD_TRAY_SHOW 1001
#define CMD_TRAY_RING 1002
#define CMD_TRAY_PAUSE_30_MINUTES 1003
#define CMD_TRAY_PAUSE_1_HOUR 1004
#define CMD_TRAY_PAUSE_2_HOURS 1005
#define CMD_TRAY_PAUSE_INDEFINITELY 1006
#define CMD_TRAY_EXIT 1007
#define CMD_TRAY_UNPAUSE 1008
#define MAX_SUPPORTED_UNIX_SECONDS 32503680000ULL

typedef BOOL (WINAPI *SetProcessDpiAwarenessContextFn)(HANDLE);
typedef UINT (WINAPI *GetDpiForWindowFn)(HWND);

typedef struct PauseDuration {
    UINT command;
    int minutes;
    const wchar_t *label;
} PauseDuration;

static const PauseDuration PAUSE_DURATIONS[] = {
    {CMD_TRAY_PAUSE_30_MINUTES, 30, L"30 minutes"},
    {CMD_TRAY_PAUSE_1_HOUR, 60, L"1 hour"},
    {CMD_TRAY_PAUSE_2_HOURS, 120, L"2 hours"},
};

AppState g_app;
static UINT g_taskbarCreated;

static void update_tray_state(void);
int app_px(int logical) {
    return MulDiv(logical, g_app.dpi, 96);
}

float app_ui_scale(void) {
    return (float)g_app.dpi / 96.0f;
}

RECT app_rect_from_box(Clay_BoundingBox box) {
    RECT rect = {
        (int)(box.x + 0.5f),
        (int)(box.y + 0.5f),
        (int)(box.x + box.width + 0.5f),
        (int)(box.y + box.height + 0.5f),
    };
    return rect;
}

#define px app_px
#define ui_scale app_ui_scale
#define rect_from_box app_rect_from_box

static void delete_fonts(void) {
    if (g_app.titleFont) DeleteObject(g_app.titleFont);
    if (g_app.bodyFont) DeleteObject(g_app.bodyFont);
    if (g_app.smallFont) DeleteObject(g_app.smallFont);
    g_app.titleFont = NULL;
    g_app.bodyFont = NULL;
    g_app.smallFont = NULL;
}

static HFONT make_font(int height, int weight) {
    return CreateFontW(
        -px(height), 0, 0, 0, weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );
}

static void create_fonts(void) {
    delete_fonts();
    g_app.titleFont = make_font(22, FW_SEMIBOLD);
    g_app.bodyFont = make_font(18, FW_NORMAL);
    g_app.smallFont = make_font(15, FW_NORMAL);
}

static UINT current_dpi(HWND window) {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    GetDpiForWindowFn getDpi = user32
        ? (GetDpiForWindowFn)(void *)GetProcAddress(user32, "GetDpiForWindow")
        : NULL;
    if (getDpi && window) return getDpi(window);
    HDC dc = GetDC(window);
    UINT dpi = dc ? (UINT)GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc) ReleaseDC(window, dc);
    return dpi ? dpi : 96;
}

static void enable_dpi_awareness(void) {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    SetProcessDpiAwarenessContextFn setContext = user32
        ? (SetProcessDpiAwarenessContextFn)(void *)GetProcAddress(user32, "SetProcessDpiAwarenessContext")
        : NULL;
    if (setContext) {
        setContext((HANDLE)(INT_PTR)-4);
    } else {
        SetProcessDPIAware();
    }
}

static int ensure_app_directory(void) {
    wchar_t localAppData[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE, NULL, SHGFP_TYPE_CURRENT, localAppData))) {
        return 0;
    }
    if (swprintf_s(g_app.appDataDirectory, MAX_PATH, L"%ls\\Bellwin", localAppData) < 0) return 0;
    if (!CreateDirectoryW(g_app.appDataDirectory, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return 0;
    if (swprintf_s(g_app.settingsPath, MAX_PATH, L"%ls\\settings.ini", g_app.appDataDirectory) < 0) return 0;
    if (swprintf_s(g_app.soundPath, MAX_PATH, L"%ls\\BellSound.mp3", g_app.appDataDirectory) < 0) return 0;
    if (swprintf_s(g_app.installedExePath, MAX_PATH, L"%ls\\Bellwin.exe", g_app.appDataDirectory) < 0) return 0;
    return 1;
}

static void write_setting(const wchar_t *key, int value) {
    wchar_t text[32];
    swprintf_s(text, 32, L"%d", value);
    WritePrivateProfileStringW(L"Bellwin", key, text, g_app.settingsPath);
}

static void write_u64_setting(const wchar_t *key, uint64_t value) {
    wchar_t text[32];
    swprintf_s(text, 32, L"%llu", (unsigned long long)value);
    WritePrivateProfileStringW(L"Bellwin", key, text, g_app.settingsPath);
}

static uint64_t read_u64_setting(const wchar_t *key) {
    wchar_t text[32];
    GetPrivateProfileStringW(L"Bellwin", key, L"0", text, 32, g_app.settingsPath);
    uint64_t value = 0;
    for (const wchar_t *character = text; *character; ++character) {
        if (*character < L'0' || *character > L'9') return 0;
        unsigned digit = (unsigned)(*character - L'0');
        if (value > (UINT64_MAX - digit) / 10) return 0;
        value = value * 10 + digit;
    }
    return value;
}

static uint64_t current_unix_seconds(void) {
    __time64_t now = _time64(NULL);
    return now > 0 ? (uint64_t)now : 0;
}

void save_settings(void) {
    write_setting(L"Volume", g_app.settings.volume);
    write_setting(L"MinimumMinutes", g_app.settings.minimumMinutes);
    write_setting(L"MaximumMinutes", g_app.settings.maximumMinutes);
    write_setting(L"QuietStartMinutes", g_app.settings.quietStartMinutes);
    write_setting(L"QuietEndMinutes", g_app.settings.quietEndMinutes);
}

void clamp_minimum_interval(void) {
    if (g_app.settings.maximumMinutes < g_app.settings.minimumMinutes) {
        g_app.settings.maximumMinutes = g_app.settings.minimumMinutes;
    }
}

void clamp_maximum_interval(void) {
    if (g_app.settings.minimumMinutes > g_app.settings.maximumMinutes) {
        g_app.settings.minimumMinutes = g_app.settings.maximumMinutes;
    }
}

static void save_runtime_state(void) {
    write_u64_setting(L"LastRingTime", g_app.lastRingUnixSeconds);
    write_setting(L"PauseMode", (int)g_app.pause.mode);
    write_u64_setting(L"PauseStartedTime", g_app.pause.startedUnixSeconds);
    write_u64_setting(L"PauseUntilTime", g_app.pause.untilUnixSeconds);
    write_setting(L"PauseSelectedMinutes", g_app.pause.selectedMinutes);
}

static void load_settings(void) {
    g_app.settings.volume = GetPrivateProfileIntW(L"Bellwin", L"Volume", 50, g_app.settingsPath);
    g_app.settings.minimumMinutes = GetPrivateProfileIntW(L"Bellwin", L"MinimumMinutes", 60, g_app.settingsPath);
    g_app.settings.maximumMinutes = GetPrivateProfileIntW(L"Bellwin", L"MaximumMinutes", 240, g_app.settingsPath);
    g_app.settings.quietStartMinutes = GetPrivateProfileIntW(L"Bellwin", L"QuietStartMinutes", 22 * 60, g_app.settingsPath);
    g_app.settings.quietEndMinutes = GetPrivateProfileIntW(L"Bellwin", L"QuietEndMinutes", 10 * 60, g_app.settingsPath);
    bellwin_clamp_settings(&g_app.settings);

    g_app.lastRingUnixSeconds = read_u64_setting(L"LastRingTime");
    int pauseMode = GetPrivateProfileIntW(L"Bellwin", L"PauseMode", PAUSE_NONE, g_app.settingsPath);
    g_app.pause.mode = pauseMode >= PAUSE_NONE && pauseMode <= PAUSE_INDEFINITE
        ? (PauseMode)pauseMode
        : PAUSE_NONE;
    g_app.pause.startedUnixSeconds = read_u64_setting(L"PauseStartedTime");
    g_app.pause.untilUnixSeconds = read_u64_setting(L"PauseUntilTime");
    g_app.pause.selectedMinutes = GetPrivateProfileIntW(L"Bellwin", L"PauseSelectedMinutes", 0, g_app.settingsPath);
    uint64_t now = current_unix_seconds();
    int runtimeStateChanged = 0;
    if (g_app.lastRingUnixSeconds > MAX_SUPPORTED_UNIX_SECONDS
        || g_app.lastRingUnixSeconds > now) {
        g_app.lastRingUnixSeconds = 0;
        runtimeStateChanged = 1;
    }
    if (g_app.pause.mode == PAUSE_TIMED
        && (!bellwin_timed_pause_is_valid(
                g_app.pause.startedUnixSeconds,
                g_app.pause.untilUnixSeconds,
                g_app.pause.selectedMinutes,
                MAX_SUPPORTED_UNIX_SECONDS
            )
            || now >= g_app.pause.untilUnixSeconds)) {
        ZeroMemory(&g_app.pause, sizeof(g_app.pause));
        runtimeStateChanged = 1;
    } else if (g_app.pause.mode != PAUSE_TIMED) {
        if (g_app.pause.startedUnixSeconds != 0
            || g_app.pause.untilUnixSeconds != 0
            || g_app.pause.selectedMinutes != 0) {
            runtimeStateChanged = 1;
        }
        g_app.pause.startedUnixSeconds = 0;
        g_app.pause.untilUnixSeconds = 0;
        g_app.pause.selectedMinutes = 0;
    }
    if (runtimeStateChanged) save_runtime_state();
}

static int extract_sound(void) {
    HRSRC resource = FindResourceW(g_app.instance, MAKEINTRESOURCEW(IDR_BELL_SOUND), RT_RCDATA);
    if (!resource) return 0;
    HGLOBAL loaded = LoadResource(g_app.instance, resource);
    if (!loaded) return 0;
    const void *bytes = LockResource(loaded);
    DWORD size = SizeofResource(g_app.instance, resource);
    if (!bytes || size == 0) return 0;

    HANDLE file = CreateFileW(g_app.soundPath, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    DWORD written = 0;
    BOOL ok = WriteFile(file, bytes, size, &written, NULL);
    CloseHandle(file);
    return ok && written == size;
}

static int play_bell(void) {
    wchar_t command[MAX_PATH + 96];
    mciSendStringW(L"close bellwin_sound", NULL, 0, NULL);
    if (swprintf_s(command, MAX_PATH + 96, L"open \"%ls\" type mpegvideo alias bellwin_sound", g_app.soundPath) < 0) return 0;
    if (mciSendStringW(command, NULL, 0, NULL) != 0) return 0;
    swprintf_s(command, MAX_PATH + 96, L"setaudio bellwin_sound volume to %d", g_app.settings.volume * 10);
    mciSendStringW(command, NULL, 0, NULL);
    return mciSendStringW(L"play bellwin_sound from 0", NULL, 0, NULL) == 0;
}

static uint32_t random_u32(void) {
    uint32_t value = (uint32_t)rand();
    value = (value << 15) ^ (uint32_t)rand();
    value = (value << 2) ^ (uint32_t)rand();
    return value;
}

static int current_second_of_day(void) {
    SYSTEMTIME now;
    GetLocalTime(&now);
    return now.wHour * 60 * 60 + now.wMinute * 60 + now.wSecond;
}

static uint64_t file_time_ticks(const FILETIME *time) {
    ULARGE_INTEGER value;
    value.LowPart = time->dwLowDateTime;
    value.HighPart = time->dwHighDateTime;
    return value.QuadPart;
}

static uint64_t seconds_until_local_minute_of_day(int targetMinuteOfDay) {
    SYSTEMTIME localNow;
    GetLocalTime(&localNow);

    SYSTEMTIME localTarget = localNow;
    targetMinuteOfDay = bellwin_normalize_day_minute(targetMinuteOfDay);
    localTarget.wHour = (WORD)(targetMinuteOfDay / 60);
    localTarget.wMinute = (WORD)(targetMinuteOfDay % 60);
    localTarget.wSecond = 0;
    localTarget.wMilliseconds = 0;

    int currentSecond = localNow.wHour * 60 * 60 + localNow.wMinute * 60 + localNow.wSecond;
    if (targetMinuteOfDay * 60 <= currentSecond) {
        FILETIME targetAsUtc;
        if (!SystemTimeToFileTime(&localTarget, &targetAsUtc)) {
            return bellwin_seconds_until_minute_of_day(currentSecond, targetMinuteOfDay);
        }
        ULARGE_INTEGER nextDay;
        nextDay.LowPart = targetAsUtc.dwLowDateTime;
        nextDay.HighPart = targetAsUtc.dwHighDateTime;
        nextDay.QuadPart += 24ULL * 60ULL * 60ULL * 10000000ULL;
        targetAsUtc.dwLowDateTime = nextDay.LowPart;
        targetAsUtc.dwHighDateTime = nextDay.HighPart;
        if (!FileTimeToSystemTime(&targetAsUtc, &localTarget)) {
            return bellwin_seconds_until_minute_of_day(currentSecond, targetMinuteOfDay);
        }
    }

    SYSTEMTIME utcTarget;
    SYSTEMTIME utcNow;
    FILETIME targetFileTime;
    FILETIME nowFileTime;
    GetSystemTime(&utcNow);
    if (!TzSpecificLocalTimeToSystemTime(NULL, &localTarget, &utcTarget)
        || !SystemTimeToFileTime(&utcTarget, &targetFileTime)
        || !SystemTimeToFileTime(&utcNow, &nowFileTime)) {
        return bellwin_seconds_until_minute_of_day(currentSecond, targetMinuteOfDay);
    }

    uint64_t targetTicks = file_time_ticks(&targetFileTime);
    uint64_t nowTicks = file_time_ticks(&nowFileTime);
    if (targetTicks <= nowTicks) {
        return bellwin_seconds_until_minute_of_day(currentSecond, targetMinuteOfDay);
    }
    uint64_t deltaTicks = targetTicks - nowTicks;
    return (deltaTicks + 9999999ULL) / 10000000ULL;
}

static int pause_is_active(uint64_t now) {
    return bellwin_pause_is_active(now, g_app.pause.untilUnixSeconds, g_app.pause.mode == PAUSE_INDEFINITE);
}

static void arm_timer_after_milliseconds(UINT timerId, uint64_t milliseconds) {
    if (!g_app.window) return;
    KillTimer(g_app.window, timerId);

    if (milliseconds > 0x7fffffffULL) milliseconds = 0x7fffffffULL;
    if (milliseconds < USER_TIMER_MINIMUM) milliseconds = USER_TIMER_MINIMUM;
    SetTimer(g_app.window, timerId, (UINT)milliseconds, NULL);
}

static void arm_timer_after_seconds(UINT timerId, uint64_t seconds) {
    uint64_t milliseconds = seconds > UINT64_MAX / 1000 ? UINT64_MAX : seconds * 1000ULL;
    arm_timer_after_milliseconds(timerId, milliseconds);
}

void schedule_volume_preview(void) {
    arm_timer_after_milliseconds(TIMER_VOLUME_PREVIEW, 200);
}

static void clear_active_segment(void) {
    g_app.plannedActiveSeconds = 0;
    g_app.activeSegmentStartTick = 0;
}

static void accrue_active_segment(void) {
    if (g_app.plannedActiveSeconds == 0) return;

    uint64_t elapsedSeconds = (GetTickCount64() - g_app.activeSegmentStartTick) / 1000ULL;
    if (elapsedSeconds > g_app.plannedActiveSeconds) elapsedSeconds = g_app.plannedActiveSeconds;
    if (elapsedSeconds > g_app.remainingActiveSeconds) elapsedSeconds = g_app.remainingActiveSeconds;
    g_app.remainingActiveSeconds -= elapsedSeconds;
    clear_active_segment();
}

static void arm_schedule_timer(void) {
    if (!g_app.window) return;
    KillTimer(g_app.window, TIMER_SCHEDULE);

    uint64_t now = current_unix_seconds();
    if (g_app.pause.mode == PAUSE_INDEFINITE) return;
    if (g_app.pause.mode == PAUSE_TIMED && pause_is_active(now)) {
        arm_timer_after_seconds(TIMER_SCHEDULE, g_app.pause.untilUnixSeconds - now);
        return;
    }
    int currentSecond = current_second_of_day();
    int currentMinute = currentSecond / 60;
    if (bellwin_is_quiet(currentMinute, g_app.settings.quietStartMinutes, g_app.settings.quietEndMinutes)) {
        clear_active_segment();
        arm_timer_after_seconds(
            TIMER_SCHEDULE,
            seconds_until_local_minute_of_day(g_app.settings.quietEndMinutes)
        );
        return;
    }
    if (g_app.remainingActiveSeconds == 0) return;

    uint64_t activeSeconds = g_app.remainingActiveSeconds;
    if (g_app.settings.quietStartMinutes != g_app.settings.quietEndMinutes) {
        activeSeconds = bellwin_limit_active_segment(
            activeSeconds,
            seconds_until_local_minute_of_day(g_app.settings.quietStartMinutes)
        );
    }
    g_app.plannedActiveSeconds = activeSeconds;
    g_app.activeSegmentStartTick = GetTickCount64();
    arm_timer_after_seconds(TIMER_SCHEDULE, activeSeconds);
}

void schedule_next_bell(void) {
    uint64_t now = current_unix_seconds();
    if (g_app.pause.mode == PAUSE_TIMED && !pause_is_active(now)) {
        ZeroMemory(&g_app.pause, sizeof(g_app.pause));
        save_runtime_state();
        update_tray_state();
    }
    if (pause_is_active(now)) {
        g_app.remainingActiveSeconds = 0;
        clear_active_segment();
        arm_schedule_timer();
        return;
    }

    int activeMinutes = bellwin_random_delay_minutes(
        g_app.settings.minimumMinutes,
        g_app.settings.maximumMinutes,
        random_u32()
    );
    g_app.remainingActiveSeconds = (uint64_t)activeMinutes * 60ULL;
    clear_active_segment();
    arm_schedule_timer();
}

static void remember_last_ring(void) {
    g_app.lastRingUnixSeconds = current_unix_seconds();
    save_runtime_state();
}

static int ring_and_remember(void) {
    int played = play_bell();
    if (played) remember_last_ring();
    return played;
}

static void continue_schedule_or_ring(void) {
    int currentMinute = current_second_of_day() / 60;
    if (g_app.remainingActiveSeconds != 0
        || bellwin_is_quiet(currentMinute, g_app.settings.quietStartMinutes, g_app.settings.quietEndMinutes)) {
        arm_schedule_timer();
        return;
    }
    ring_and_remember();
    schedule_next_bell();
}

static void pause_for_minutes(int minutes) {
    g_app.pause.mode = PAUSE_TIMED;
    g_app.pause.selectedMinutes = minutes;
    g_app.pause.startedUnixSeconds = current_unix_seconds();
    g_app.pause.untilUnixSeconds = g_app.pause.startedUnixSeconds + (uint64_t)minutes * 60ULL;
    g_app.remainingActiveSeconds = 0;
    clear_active_segment();
    save_runtime_state();
    update_tray_state();
    arm_schedule_timer();
}

static void resume_ringing(void) {
    ZeroMemory(&g_app.pause, sizeof(g_app.pause));
    save_runtime_state();
    update_tray_state();
    schedule_next_bell();
}

static void toggle_indefinite_pause(void) {
    if (g_app.pause.mode == PAUSE_INDEFINITE) {
        resume_ringing();
        return;
    }
    g_app.pause.mode = PAUSE_INDEFINITE;
    g_app.pause.startedUnixSeconds = 0;
    g_app.pause.untilUnixSeconds = 0;
    g_app.pause.selectedMinutes = 0;
    g_app.remainingActiveSeconds = 0;
    clear_active_segment();
    save_runtime_state();
    update_tray_state();
    arm_schedule_timer();
}

static int is_autostart_enabled(void) {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return 0;
    }
    DWORD type = 0;
    DWORD size = 0;
    LONG result = RegQueryValueExW(key, APP_NAME, NULL, &type, NULL, &size);
    RegCloseKey(key);
    return result == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ) && size > sizeof(wchar_t);
}

static int file_exists(const wchar_t *path) {
    DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static int set_autostart(int enabled) {
    HKEY key;
    DWORD disposition;
    if (RegCreateKeyExW(
            HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, NULL, 0,
            KEY_SET_VALUE, NULL, &key, &disposition) != ERROR_SUCCESS) {
        return 0;
    }
    (void)disposition;
    LONG result;
    if (!enabled) {
        result = RegDeleteValueW(key, APP_NAME);
        if (result == ERROR_FILE_NOT_FOUND) result = ERROR_SUCCESS;
    } else {
        wchar_t currentExe[MAX_PATH];
        const wchar_t *target = g_app.installedExePath;
        if (!file_exists(target)) {
            if (!GetModuleFileNameW(NULL, currentExe, MAX_PATH)) {
                RegCloseKey(key);
                return 0;
            }
            target = currentExe;
        }
        wchar_t command[MAX_PATH + 32];
        if (swprintf_s(command, MAX_PATH + 32, L"\"%ls\" --background", target) < 0) {
            RegCloseKey(key);
            return 0;
        }
        result = RegSetValueExW(key, APP_NAME, 0, REG_SZ, (const BYTE *)command, (DWORD)((wcslen(command) + 1) * sizeof(wchar_t)));
    }
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

static uint64_t executable_version(const wchar_t *path) {
    DWORD ignored = 0;
    DWORD size = GetFileVersionInfoSizeW(path, &ignored);
    if (!size) return 0;
    void *data = malloc(size);
    if (!data) return 0;
    uint64_t version = 0;
    if (GetFileVersionInfoW(path, 0, size, data)) {
        VS_FIXEDFILEINFO *info = NULL;
        UINT infoSize = 0;
        if (VerQueryValueW(data, L"\\", (void **)&info, &infoSize) && info && infoSize >= sizeof(*info)) {
            version = ((uint64_t)HIWORD(info->dwFileVersionMS) << 48)
                    | ((uint64_t)LOWORD(info->dwFileVersionMS) << 32)
                    | ((uint64_t)HIWORD(info->dwFileVersionLS) << 16)
                    | (uint64_t)LOWORD(info->dwFileVersionLS);
        }
    }
    free(data);
    return version;
}

uint64_t current_version(void) {
    return ((uint64_t)VER_MAJOR << 48) | ((uint64_t)VER_MINOR << 32) | ((uint64_t)VER_PATCH << 16) | (uint64_t)VER_BUILD;
}

void format_version_utf8(uint64_t version, char *buffer, size_t count) {
    snprintf(
        buffer,
        count,
        "%u.%u.%u.%u",
        (unsigned)((version >> 48) & 0xffff),
        (unsigned)((version >> 32) & 0xffff),
        (unsigned)((version >> 16) & 0xffff),
        (unsigned)(version & 0xffff)
    );
}

static int known_folder_file_path(REFKNOWNFOLDERID folderId, const wchar_t *fileName, wchar_t *path, size_t pathCount) {
    PWSTR folder = NULL;
    HRESULT result = SHGetKnownFolderPath(folderId, 0, NULL, &folder);
    if (FAILED(result) || !folder) return 0;
    int ok = swprintf_s(path, pathCount, L"%ls\\%ls", folder, fileName) >= 0;
    CoTaskMemFree(folder);
    return ok;
}

static void refresh_install_state(void) {
    int oldShowInstall = g_app.showInstall;
    int oldUpdateAvailable = g_app.updateAvailable;
    int installed = file_exists(g_app.installedExePath);
    g_app.installedVersion = installed ? executable_version(g_app.installedExePath) : 0;
    wchar_t startMenuShortcut[MAX_PATH];
    int shortcutsReady = known_folder_file_path(&FOLDERID_Programs, L"Bellwin.lnk", startMenuShortcut, MAX_PATH)
        && file_exists(startMenuShortcut);
    g_app.updateAvailable = installed && g_app.installedVersion < current_version();
    g_app.showInstall = !installed || g_app.updateAvailable || !shortcutsReady;
    if (!g_app.showInstall) g_app.hoverInstall = 0;
    uia_notify_install_state(oldShowInstall, oldUpdateAvailable);
}

int install_visible(void) {
    return g_app.showInstall;
}

static HRESULT create_shortcut(REFKNOWNFOLDERID folderId, const wchar_t *fileName, const wchar_t *target) {
    wchar_t shortcutPath[MAX_PATH];
    if (!known_folder_file_path(folderId, fileName, shortcutPath, MAX_PATH)) return E_FAIL;

    IShellLinkW *link = NULL;
    HRESULT result = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void **)&link);
    if (FAILED(result) || !link) return result;

    result = link->lpVtbl->SetPath(link, target);
    if (SUCCEEDED(result)) result = link->lpVtbl->SetDescription(link, L"Mindfulness bell");
    if (SUCCEEDED(result)) result = link->lpVtbl->SetWorkingDirectory(link, g_app.appDataDirectory);
    if (SUCCEEDED(result)) result = link->lpVtbl->SetIconLocation(link, target, 0);

    IPersistFile *persist = NULL;
    if (SUCCEEDED(result)) result = link->lpVtbl->QueryInterface(link, &IID_IPersistFile, (void **)&persist);
    if (SUCCEEDED(result) && persist) result = persist->lpVtbl->Save(persist, shortcutPath, TRUE);
    if (persist) persist->lpVtbl->Release(persist);
    link->lpVtbl->Release(link);
    return result;
}

static int install_app(void) {
    wchar_t source[MAX_PATH];
    if (!GetModuleFileNameW(NULL, source, MAX_PATH)) return 0;
    uint64_t installedVersion = file_exists(g_app.installedExePath)
        ? executable_version(g_app.installedExePath)
        : 0;
    int shouldCopy = _wcsicmp(source, g_app.installedExePath) != 0
        && (!file_exists(g_app.installedExePath) || installedVersion < current_version());
    if (shouldCopy) {
        if (!CopyFileW(source, g_app.installedExePath, FALSE)) return 0;
    }

    HRESULT startMenu = create_shortcut(&FOLDERID_Programs, L"Bellwin.lnk", g_app.installedExePath);
    if (FAILED(startMenu)) return 0;

    if (g_app.autoStart && !set_autostart(1)) return 0;
    refresh_install_state();
    InvalidateRect(g_app.window, NULL, FALSE);
    return 1;
}

static int change_autostart(int desired) {
    int oldValue = g_app.autoStart;
    if (set_autostart(desired)) {
        g_app.autoStart = desired;
        InvalidateRect(g_app.window, NULL, FALSE);
        uia_notify_toggle(oldValue, desired);
        return 1;
    }
    return 0;
}

void activate_autostart(void) {
    if (!change_autostart(!g_app.autoStart)) {
        MessageBoxW(g_app.window, L"Could not change the startup setting.", APP_NAME, MB_OK | MB_ICONERROR);
    }
}

void activate_install(void) {
    if (!g_app.showInstall) return;
    if (install_app()) {
        MessageBoxW(g_app.window, L"Bellwin was installed. A shortcut was added to the Start menu.", APP_NAME, MB_OK | MB_ICONINFORMATION);
    } else {
        MessageBoxW(g_app.window, L"Bellwin could not be installed.", APP_NAME, MB_OK | MB_ICONERROR);
    }
    if (!g_app.showInstall && g_app.focusedControl == CONTROL_INSTALL) {
        focus_control(CONTROL_AUTOSTART, g_app.focusVisibility);
    }
}

static int format_pause_until_time(wchar_t *buffer, size_t count) {
    __time64_t pauseTime = (__time64_t)g_app.pause.untilUnixSeconds;
    struct tm local;
    if (_localtime64_s(&local, &pauseTime) != 0) return 0;

    SYSTEMTIME time = {0};
    time.wHour = (WORD)local.tm_hour;
    time.wMinute = (WORD)local.tm_min;
    return GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &time, NULL, buffer, (int)count) > 0;
}

static void format_pause_status(wchar_t *buffer, size_t count) {
    if (g_app.pause.mode == PAUSE_INDEFINITE) {
        swprintf_s(buffer, count, L"Paused indefinitely");
        return;
    }

    wchar_t timeText[32];
    if (format_pause_until_time(timeText, sizeof(timeText) / sizeof(timeText[0]))) {
        swprintf_s(buffer, count, L"Paused until %ls", timeText);
    } else {
        swprintf_s(buffer, count, L"Paused");
    }
}

static void format_tray_tip(wchar_t *buffer, size_t count) {
    if (pause_is_active(current_unix_seconds())) {
        wchar_t status[80];
        format_pause_status(status, sizeof(status) / sizeof(status[0]));
        swprintf_s(buffer, count, L"Bellwin — %ls", status);
    } else {
        swprintf_s(buffer, count, L"Bellwin — mindfulness bell");
    }
}

static HICON create_paused_icon(HICON source, int size) {
    if (!source || size <= 0) return NULL;

    BITMAPINFO bitmapInfo = {0};
    bitmapInfo.bmiHeader.biSize = sizeof(bitmapInfo.bmiHeader);
    bitmapInfo.bmiHeader.biWidth = size;
    bitmapInfo.bmiHeader.biHeight = -size;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    uint32_t *pixels = NULL;
    HBITMAP color = CreateDIBSection(NULL, &bitmapInfo, DIB_RGB_COLORS, (void **)&pixels, NULL, 0);
    if (!color || !pixels) {
        if (color) DeleteObject(color);
        return NULL;
    }

    HDC colorDc = CreateCompatibleDC(NULL);
    if (!colorDc) {
        DeleteObject(color);
        return NULL;
    }
    HGDIOBJ oldColor = SelectObject(colorDc, color);
    ZeroMemory(pixels, (size_t)size * (size_t)size * sizeof(*pixels));
    BOOL drawn = DrawIconEx(colorDc, 0, 0, source, size, size, 0, NULL, DI_NORMAL);
    SelectObject(colorDc, oldColor);
    DeleteDC(colorDc);
    if (!drawn) {
        DeleteObject(color);
        return NULL;
    }

    size_t pixelCount = (size_t)size * (size_t)size;
    uint32_t *sourcePixels = (uint32_t *)malloc(pixelCount * sizeof(*sourcePixels));
    if (!sourcePixels) {
        DeleteObject(color);
        return NULL;
    }
    memcpy(sourcePixels, pixels, pixelCount * sizeof(*sourcePixels));

    int hasAlpha = 0;
    for (size_t index = 0; index < pixelCount; ++index) {
        if ((sourcePixels[index] >> 24) != 0) {
            hasAlpha = 1;
            break;
        }
    }

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            uint32_t sourcePixel = sourcePixels[(size_t)x * (size_t)size + (size_t)(size - 1 - y)];
            unsigned blue = sourcePixel & 0xff;
            unsigned green = (sourcePixel >> 8) & 0xff;
            unsigned red = (sourcePixel >> 16) & 0xff;
            unsigned alpha = sourcePixel >> 24;
            unsigned gray = (red * 30 + green * 59 + blue * 11) / 100;
            if (!hasAlpha && (red || green || blue)) alpha = 255;
            pixels[(size_t)y * (size_t)size + (size_t)x] =
                (alpha << 24) | (gray << 16) | (gray << 8) | gray;
        }
    }
    free(sourcePixels);

    HBITMAP mask = CreateBitmap(size, size, 1, 1, NULL);
    if (!mask) {
        DeleteObject(color);
        return NULL;
    }
    HDC maskDc = CreateCompatibleDC(NULL);
    if (!maskDc) {
        DeleteObject(mask);
        DeleteObject(color);
        return NULL;
    }
    HGDIOBJ oldMask = SelectObject(maskDc, mask);
    PatBlt(maskDc, 0, 0, size, size, BLACKNESS);
    SelectObject(maskDc, oldMask);
    DeleteDC(maskDc);

    ICONINFO iconInfo = {0};
    iconInfo.fIcon = TRUE;
    iconInfo.hbmMask = mask;
    iconInfo.hbmColor = color;
    HICON icon = CreateIconIndirect(&iconInfo);
    DeleteObject(mask);
    DeleteObject(color);
    return icon;
}

static HICON current_tray_icon(void) {
    return pause_is_active(current_unix_seconds()) && g_app.pausedSmallIcon
        ? g_app.pausedSmallIcon
        : g_app.smallIcon;
}

static void add_tray_icon(void) {
    ZeroMemory(&g_app.tray, sizeof(g_app.tray));
    g_app.tray.cbSize = sizeof(g_app.tray);
    g_app.tray.hWnd = g_app.window;
    g_app.tray.uID = 1;
    g_app.tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_app.tray.uCallbackMessage = WM_TRAY;
    g_app.tray.hIcon = current_tray_icon();
    format_tray_tip(g_app.tray.szTip, sizeof(g_app.tray.szTip) / sizeof(g_app.tray.szTip[0]));
    Shell_NotifyIconW(NIM_ADD, &g_app.tray);
    g_app.tray.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &g_app.tray);
}

static void update_tray_state(void) {
    if (!g_app.tray.cbSize) return;
    NOTIFYICONDATAW update = g_app.tray;
    update.uFlags = NIF_TIP | NIF_ICON;
    update.hIcon = current_tray_icon();
    format_tray_tip(update.szTip, sizeof(update.szTip) / sizeof(update.szTip[0]));
    Shell_NotifyIconW(NIM_MODIFY, &update);
}

static void remove_tray_icon(void) {
    if (g_app.tray.cbSize) Shell_NotifyIconW(NIM_DELETE, &g_app.tray);
    ZeroMemory(&g_app.tray, sizeof(g_app.tray));
}

static void show_window(void) {
    ShowWindow(g_app.window, SW_RESTORE);
    SetForegroundWindow(g_app.window);
    SetFocus(g_app.window);
}

static void refresh_theme_state(HWND window) {
    BellwinThemeState nextTheme = bellwin_query_theme();
    if (bellwin_theme_equal(&g_app.theme, &nextTheme)) return;
    g_app.theme = nextTheme;
    bellwin_apply_window_frame(window, &g_app.theme);
    InvalidateRect(window, NULL, FALSE);
}

static void show_tray_menu(void) {
    POINT point;
    GetCursorPos(&point);
    HMENU menu = CreatePopupMenu();
    HMENU pauseMenu = CreatePopupMenu();
    if (!menu || !pauseMenu) {
        if (pauseMenu) DestroyMenu(pauseMenu);
        if (menu) DestroyMenu(menu);
        return;
    }

    uint64_t now = current_unix_seconds();
    int timedPauseActive = g_app.pause.mode == PAUSE_TIMED && pause_is_active(now);
    for (size_t index = 0; index < sizeof(PAUSE_DURATIONS) / sizeof(PAUSE_DURATIONS[0]); ++index) {
        const PauseDuration *duration = &PAUSE_DURATIONS[index];
        AppendMenuW(pauseMenu,
            MF_STRING | (timedPauseActive && g_app.pause.selectedMinutes == duration->minutes ? MF_CHECKED : MF_UNCHECKED),
            duration->command, duration->label);
    }
    if (timedPauseActive) {
        AppendMenuW(pauseMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(pauseMenu, MF_STRING, CMD_TRAY_UNPAUSE, L"Unpause");
    }

    AppendMenuW(menu, MF_STRING, CMD_TRAY_SHOW, L"Settings");
    AppendMenuW(menu, MF_STRING, CMD_TRAY_RING, L"Ring now");
    if (g_app.lastRingUnixSeconds != 0) {
        wchar_t lastRingText[64];
        uint64_t elapsed = now > g_app.lastRingUnixSeconds ? now - g_app.lastRingUnixSeconds : 0;
        bellwin_format_last_ring(elapsed, lastRingText, sizeof(lastRingText) / sizeof(lastRingText[0]));
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, lastRingText);
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    if (pause_is_active(now)) {
        wchar_t pauseText[80];
        format_pause_status(pauseText, sizeof(pauseText) / sizeof(pauseText[0]));
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, pauseText);
    }
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)pauseMenu, L"Pause for");
    AppendMenuW(menu,
        MF_STRING | (g_app.pause.mode == PAUSE_INDEFINITE ? MF_CHECKED : MF_UNCHECKED),
        CMD_TRAY_PAUSE_INDEFINITELY, L"Pause indefinitely");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, CMD_TRAY_EXIT, L"Exit");
    SetMenuDefaultItem(menu, CMD_TRAY_SHOW, FALSE);
    SetForegroundWindow(g_app.window);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN, point.x, point.y, 0, g_app.window, NULL);
    PostMessageW(g_app.window, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

static int ipc_append(char *buffer, size_t count, size_t *length, const char *format, ...) {
    if (*length >= count) return 0;
    va_list arguments;
    va_start(arguments, format);
    int written = vsnprintf(buffer + *length, count - *length, format, arguments);
    va_end(arguments);
    if (written < 0 || (size_t)written >= count - *length) {
        buffer[count - 1] = '\0';
        return 0;
    }
    *length += (size_t)written;
    return 1;
}

static const Widget *widget_for_setting(BellwinSettingKey key) {
    switch (key) {
    case BELLWIN_SETTING_VOLUME: return widget_by_id(CONTROL_VOLUME);
    case BELLWIN_SETTING_MINIMUM_INTERVAL: return widget_by_id(CONTROL_MINIMUM_INTERVAL);
    case BELLWIN_SETTING_MAXIMUM_INTERVAL: return widget_by_id(CONTROL_MAXIMUM_INTERVAL);
    case BELLWIN_SETTING_QUIET_START: return widget_by_id(CONTROL_QUIET_START);
    case BELLWIN_SETTING_QUIET_END: return widget_by_id(CONTROL_QUIET_END);
    case BELLWIN_SETTING_AUTOSTART:
    case BELLWIN_SETTING_INVALID:
        break;
    }
    return NULL;
}

static int ipc_format_setting(
    BellwinSettingKey key,
    char *buffer,
    size_t count,
    size_t *length
) {
    const char *name = bellwin_cli_setting_name(key);
    if (!name) return 0;
    if (key == BELLWIN_SETTING_AUTOSTART) {
        return ipc_append(buffer, count, length, "%s=%s\n", name, g_app.autoStart ? "on" : "off");
    }
    const Widget *widget = widget_for_setting(key);
    if (!widget || !widget->value) return 0;
    if (key == BELLWIN_SETTING_QUIET_START || key == BELLWIN_SETTING_QUIET_END) {
        return ipc_append(
            buffer,
            count,
            length,
            "%s=%02d:%02d\n",
            name,
            *widget->value / 60,
            *widget->value % 60
        );
    }
    return ipc_append(buffer, count, length, "%s=%d\n", name, *widget->value);
}

static int ipc_get_setting(const char *keyText, char *buffer, size_t count) {
    BellwinSettingKey key = bellwin_cli_setting_key(keyText);
    size_t length = 0;
    if (key == BELLWIN_SETTING_INVALID) {
        ipc_append(buffer, count, &length, "unknown setting: %s\n", keyText);
        return 0;
    }
    return ipc_format_setting(key, buffer, count, &length);
}

static int ipc_set_setting(const char *assignment, char *buffer, size_t count) {
    BellwinSettingKey key;
    int value;
    char error[128];
    if (!bellwin_cli_parse_assignment(assignment, &key, &value, error, sizeof(error))) {
        snprintf(buffer, count, "%s\n", error);
        return 0;
    }
    if (key == BELLWIN_SETTING_AUTOSTART) {
        if (value != g_app.autoStart && !change_autostart(value)) {
            snprintf(buffer, count, "could not change autostart\n");
            return 0;
        }
    } else {
        const Widget *widget = widget_for_setting(key);
        if (!widget) {
            snprintf(buffer, count, "setting is unavailable\n");
            return 0;
        }
        widget_set_value(widget, value, 1);
    }
    size_t length = 0;
    return ipc_format_setting(key, buffer, count, &length);
}

static int ipc_format_status(char *buffer, size_t count) {
    size_t length = 0;
    for (BellwinSettingKey key = BELLWIN_SETTING_VOLUME;
            key <= BELLWIN_SETTING_AUTOSTART;
            key = (BellwinSettingKey)(key + 1)) {
        if (!ipc_format_setting(key, buffer, count, &length)) return 0;
    }
    uint64_t now = current_unix_seconds();
    const char *pause = g_app.pause.mode == PAUSE_INDEFINITE
        ? "indefinite"
        : g_app.pause.mode == PAUSE_TIMED && pause_is_active(now) ? "timed" : "none";
    return ipc_append(buffer, count, &length, "pause=%s\n", pause)
        && ipc_append(buffer, count, &length, "pause-until=%llu\n", (unsigned long long)g_app.pause.untilUnixSeconds)
        && ipc_append(buffer, count, &length, "last-ring=%llu\n", (unsigned long long)g_app.lastRingUnixSeconds);
}

static void ipc_send_response(HWND target, const BellwinIpcResponse *response) {
    if (!target || !IsWindow(target)) return;
    COPYDATASTRUCT data = {
        .dwData = BELLWIN_COPYDATA_RESPONSE,
        .cbData = sizeof(*response),
        .lpData = (void *)response,
    };
    DWORD_PTR ignored;
    SendMessageTimeoutW(
        target,
        WM_COPYDATA,
        (WPARAM)g_app.window,
        (LPARAM)&data,
        SMTO_ABORTIFHUNG,
        2000,
        &ignored
    );
}

static LRESULT handle_ipc_request(WPARAM wParam, LPARAM lParam) {
    const COPYDATASTRUCT *data = (const COPYDATASTRUCT *)lParam;
    BellwinIpcResponse response;
    ZeroMemory(&response, sizeof(response));
    response.version = BELLWIN_IPC_PROTOCOL_VERSION;
    response.size = sizeof(response);
    response.status = 1;

    if (!data || data->dwData != BELLWIN_COPYDATA_REQUEST
            || !bellwin_ipc_request_valid((const BellwinIpcRequest *)data->lpData, data->cbData)) {
        snprintf(response.text, sizeof(response.text), "invalid IPC request\n");
        ipc_send_response((HWND)wParam, &response);
        return FALSE;
    }

    const BellwinIpcRequest *request = (const BellwinIpcRequest *)data->lpData;
    int ok = 1;
    switch (request->action) {
    case BELLWIN_CLI_RING:
        ok = ring_and_remember();
        schedule_next_bell();
        snprintf(
            response.text,
            sizeof(response.text),
            ok ? "ring=ok\n" : "could not play the bell\n"
        );
        break;
    case BELLWIN_CLI_PAUSE:
        pause_for_minutes(request->pauseMinutes);
        snprintf(response.text, sizeof(response.text), "pause=%d\n", request->pauseMinutes);
        break;
    case BELLWIN_CLI_UNPAUSE:
        if (g_app.pause.mode != PAUSE_NONE) resume_ringing();
        snprintf(response.text, sizeof(response.text), "pause=none\n");
        break;
    case BELLWIN_CLI_SHOW:
        show_window();
        snprintf(response.text, sizeof(response.text), "show=ok\n");
        break;
    case BELLWIN_CLI_SET:
        ok = ipc_set_setting(request->argument, response.text, sizeof(response.text));
        break;
    case BELLWIN_CLI_GET:
        ok = ipc_get_setting(request->argument, response.text, sizeof(response.text));
        break;
    case BELLWIN_CLI_STATUS:
        ok = ipc_format_status(response.text, sizeof(response.text));
        if (!ok) snprintf(response.text, sizeof(response.text), "could not format status\n");
        break;
    case BELLWIN_CLI_NONE:
        ok = 0;
        snprintf(response.text, sizeof(response.text), "missing action\n");
        break;
    }
    response.status = ok ? 0 : 1;
    ipc_send_response((HWND)wParam, &response);
    return ok ? TRUE : FALSE;
}

static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (g_taskbarCreated && message == g_taskbarCreated) {
        add_tray_icon();
        return 0;
    }

    switch (message) {
    case WM_DWMCOLORIZATIONCOLORCHANGED:
    case WM_SYSCOLORCHANGE:
    case WM_THEMECHANGED:
    case WM_SETTINGCHANGE:
        refresh_theme_state(window);
        return 0;
    case WM_PAINT:
        paint_ui(window);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_GETOBJECT:
        if (uia_is_root_object(lParam)) {
            return uia_handle_getobject(window, wParam, lParam);
        }
        break;
    case WM_COPYDATA:
        return handle_ipc_request(wParam, lParam);
    case WM_SETFOCUS:
        g_app.windowFocused = 1;
        if (g_app.focusedControl != CONTROL_NONE) {
            uia_notify_focus(g_app.focusedControl, g_app.timeEdit.segment);
        }
        InvalidateRect(window, NULL, FALSE);
        return 0;
    case WM_KILLFOCUS:
        g_app.windowFocused = 0;
        g_app.timeEdit.digitCount = 0;
        InvalidateRect(window, NULL, FALSE);
        return 0;
    case WM_DPICHANGED: {
        g_app.dpi = HIWORD(wParam);
        create_fonts();
        bellwin_ui_reset_measure_cache();
        RECT *suggested = (RECT *)lParam;
        SetWindowPos(window, NULL, suggested->left, suggested->top,
            suggested->right - suggested->left, suggested->bottom - suggested->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        InvalidateRect(window, NULL, FALSE);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        float x = (float)GET_X_LPARAM(lParam);
        float y = (float)GET_Y_LPARAM(lParam);
        update_pointer_state(x, y, 1);

        HitInfo hit = hit_test_pointer(x, y);
        switch (hit.kind) {
        case HIT_SLIDER: {
            focus_control(hit.control, FOCUS_HIDDEN);
            Clay_ElementData track = Clay_GetElementData(bellwin_ui_track_id(hit.control));
            float inflate = 10.0f * ui_scale();
            if (track.found
                && x >= track.boundingBox.x - inflate
                && x < track.boundingBox.x + track.boundingBox.width + inflate) {
                g_app.draggingSlider = hit.control;
                SetCapture(window);
                update_slider_from_mouse(widget_by_id(hit.control), x);
            }
            return 0;
        }
        case HIT_TIME_SEGMENT:
            focus_control(hit.control, FOCUS_HIDDEN);
            select_time_segment(hit.segment);
            return 0;
        case HIT_TIME_STEPPER:
            focus_control(hit.control, FOCUS_HIDDEN);
            shift_time_minutes(widget_by_id(hit.control), hit.stepperUp ? 30 : -30);
            return 0;
        case HIT_TOGGLE:
            focus_control(CONTROL_AUTOSTART, FOCUS_HIDDEN);
            invoke_widget(CONTROL_AUTOSTART);
            return 0;
        case HIT_INSTALL:
            focus_control(CONTROL_INSTALL, FOCUS_HIDDEN);
            invoke_widget(CONTROL_INSTALL);
            return 0;
        case HIT_NONE:
            break;
        }
        if (GetFocus() != window) SetFocus(window);
        set_focus_visibility(FOCUS_HIDDEN);
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (!g_app.trackingMouseLeave) {
            TRACKMOUSEEVENT tracking = {
                .cbSize = sizeof(tracking),
                .dwFlags = TME_LEAVE,
                .hwndTrack = window,
            };
            if (TrackMouseEvent(&tracking)) g_app.trackingMouseLeave = 1;
        }
        float x = (float)GET_X_LPARAM(lParam);
        float y = (float)GET_Y_LPARAM(lParam);
        update_pointer_state(x, y, g_app.draggingSlider != CONTROL_NONE);
        if (g_app.draggingSlider) update_slider_from_mouse(widget_by_id(g_app.draggingSlider), x);
        int hover = g_app.showInstall
            && bellwin_ui_is_ready()
            && Clay_PointerOver(bellwin_ui_hit_id(CONTROL_INSTALL));
        if (hover != g_app.hoverInstall) {
            g_app.hoverInstall = hover;
            InvalidateRect(window, NULL, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        g_app.trackingMouseLeave = 0;
        if (g_app.hoverInstall) {
            g_app.hoverInstall = 0;
            InvalidateRect(window, NULL, FALSE);
        }
        return 0;
    case WM_LBUTTONUP:
        if (g_app.draggingSlider) {
            finish_slider_drag();
            ReleaseCapture();
        }
        return 0;
    case WM_CAPTURECHANGED:
        finish_slider_drag();
        return 0;
    case WM_MOUSEWHEEL: {
        float x;
        float y;
        wheel_client_point(lParam, &x, &y);
        update_pointer_state(x, y, 0);
        int delta = direct_gesture_delta(wParam);

        HitInfo hit = hit_test_pointer(x, y);
        if (hit.kind == HIT_SLIDER) {
            int steps = consume_wheel_steps(WHEEL_TARGET_SLIDER, hit.control, BELLWIN_TIME_HOURS, delta);
            if (steps) widget_step(widget_by_id(hit.control), steps, 1);
            return 0;
        }
        if (hit.kind == HIT_TIME_SEGMENT) {
            int steps = consume_wheel_steps(WHEEL_TARGET_TIME_SEGMENT, hit.control, hit.segment, delta);
            if (steps) step_time_value(widget_by_id(hit.control), hit.segment, steps);
            return 0;
        }
        if (hit.kind == HIT_TIME_STEPPER) {
            int steps = consume_wheel_steps(WHEEL_TARGET_TIME_STEPPER, hit.control, BELLWIN_TIME_MINUTES, delta);
            if (steps) shift_time_minutes(widget_by_id(hit.control), steps * 30);
            return 0;
        }
        return 0;
    }
    case WM_MOUSEHWHEEL: {
        float x;
        float y;
        wheel_client_point(lParam, &x, &y);
        update_pointer_state(x, y, 0);
        HitInfo hit = hit_test_pointer(x, y);
        if (hit.kind == HIT_SLIDER) {
            int steps = consume_wheel_steps(
                WHEEL_TARGET_SLIDER_HORIZONTAL, hit.control, BELLWIN_TIME_HOURS, direct_gesture_delta(wParam)
            );
            if (steps) widget_step(widget_by_id(hit.control), steps, 1);
        }
        return 0;
    }
    case WM_SETCURSOR: {
        POINT point;
        GetCursorPos(&point);
        ScreenToClient(window, &point);
        float x = (float)point.x;
        float y = (float)point.y;
        update_pointer_state(x, y, 0);
        HitInfo hit = hit_test_pointer(x, y);
        if (hit.kind == HIT_TIME_SEGMENT) {
            SetCursor(LoadCursorW(NULL, IDC_IBEAM));
            return TRUE;
        }
        if (hit.kind == HIT_TIME_STEPPER || hit.kind == HIT_TOGGLE || hit.kind == HIT_INSTALL) {
            SetCursor(LoadCursorW(NULL, IDC_HAND));
            return TRUE;
        }
        break;
    }
    case WM_TIMECHANGE: {
        uint64_t now = current_unix_seconds();
        update_tray_state();
        if (g_app.pause.mode == PAUSE_TIMED && !pause_is_active(now)) {
            resume_ringing();
        } else if (pause_is_active(now)) {
            arm_schedule_timer();
        } else {
            accrue_active_segment();
            continue_schedule_or_ring();
        }
        return 0;
    }
    case WM_TIMER:
        if (wParam == TIMER_TRAY_SINGLE_CLICK) {
            KillTimer(window, TIMER_TRAY_SINGLE_CLICK);
            show_tray_menu();
            return 0;
        }
        if (wParam == TIMER_VOLUME_PREVIEW) {
            KillTimer(window, TIMER_VOLUME_PREVIEW);
            play_bell();
            return 0;
        }
        if (wParam == TIMER_SCHEDULE) {
            KillTimer(window, TIMER_SCHEDULE);
            uint64_t now = current_unix_seconds();
            if (g_app.pause.mode == PAUSE_INDEFINITE) return 0;
            if (g_app.pause.mode == PAUSE_TIMED) {
                if (pause_is_active(now)) {
                    arm_schedule_timer();
                    return 0;
                }
                resume_ringing();
                return 0;
            }
            accrue_active_segment();
            continue_schedule_or_ring();
        }
        return 0;
    case WM_TRAY: {
        UINT trayEvent = LOWORD(lParam);
        BellwinTrayEvent event;
        if (trayEvent == WM_LBUTTONUP) event = BELLWIN_TRAY_LEFT_BUTTON_UP;
        else if (trayEvent == WM_LBUTTONDBLCLK) event = BELLWIN_TRAY_LEFT_BUTTON_DOUBLE_CLICK;
        else if (trayEvent == NIN_SELECT) event = BELLWIN_TRAY_SELECT;
        else if (trayEvent == NIN_KEYSELECT) event = BELLWIN_TRAY_KEY_SELECT;
        else if (trayEvent == WM_RBUTTONUP || trayEvent == WM_CONTEXTMENU) event = BELLWIN_TRAY_CONTEXT_MENU;
        else return 0;

        BellwinTrayAction action = bellwin_tray_action(event, &g_app.suppressTrayLeftButtonUp);
        if (action == BELLWIN_TRAY_ACTION_DEFER_MENU) {
            SetTimer(window, TIMER_TRAY_SINGLE_CLICK, GetDoubleClickTime(), NULL);
        } else if (action == BELLWIN_TRAY_ACTION_CANCEL_MENU_AND_SHOW_SETTINGS) {
            KillTimer(window, TIMER_TRAY_SINGLE_CLICK);
            show_window();
        } else if (action == BELLWIN_TRAY_ACTION_SHOW_SETTINGS) {
            show_window();
        } else if (action == BELLWIN_TRAY_ACTION_SHOW_MENU) {
            show_tray_menu();
        }
        return 0;
    }
    case WM_COMMAND:
        for (size_t index = 0; index < sizeof(PAUSE_DURATIONS) / sizeof(PAUSE_DURATIONS[0]); ++index) {
            if (LOWORD(wParam) == PAUSE_DURATIONS[index].command) {
                pause_for_minutes(PAUSE_DURATIONS[index].minutes);
                return 0;
            }
        }
        switch (LOWORD(wParam)) {
        case CMD_TRAY_SHOW:
            show_window();
            return 0;
        case CMD_TRAY_RING:
            ring_and_remember();
            schedule_next_bell();
            return 0;
        case CMD_TRAY_PAUSE_INDEFINITELY:
            toggle_indefinite_pause();
            return 0;
        case CMD_TRAY_UNPAUSE:
            if (g_app.pause.mode == PAUSE_TIMED) resume_ringing();
            return 0;
        case CMD_TRAY_EXIT:
            g_app.exiting = 1;
            DestroyWindow(window);
            return 0;
        default:
            break;
        }
        break;
    case WM_SHOW_BELLWIN:
        show_window();
        return 0;
    case WM_UIA_INVOKE:
        invoke_widget((ControlId)wParam);
        return 0;
    case WM_KEYDOWN: {
        if (wParam != VK_ESCAPE && g_app.focusedControl != CONTROL_NONE) {
            set_focus_visibility(FOCUS_VISIBLE);
        }
        if (wParam == VK_TAB) {
            move_focus((GetKeyState(VK_SHIFT) & 0x8000) ? -1 : 1);
            return 0;
        }
        const Widget *focused = widget_by_id(g_app.focusedControl);
        if (focused && handle_widget_key(focused, wParam, (lParam & (1L << 30)) != 0)) {
            return 0;
        }
        if (wParam == VK_ESCAPE) ShowWindow(window, SW_HIDE);
        return 0;
    }
    case WM_CHAR:
        if (wParam >= L'0' && wParam <= L'9') {
            enter_time_digit((int)(wParam - L'0'));
        }
        return 0;
    case WM_CLOSE:
        if (g_app.exiting) DestroyWindow(window);
        else ShowWindow(window, SW_HIDE);
        return 0;
    case WM_QUERYENDSESSION:
        return TRUE;
    case WM_DESTROY:
        uia_disconnect(window);
        KillTimer(window, TIMER_SCHEDULE);
        KillTimer(window, TIMER_VOLUME_PREVIEW);
        KillTimer(window, TIMER_TRAY_SINGLE_CLICK);
        remove_tray_icon();
        mciSendStringW(L"close bellwin_sound", NULL, 0, NULL);
        delete_fonts();
        if (g_app.measureDc) {
            DeleteDC(g_app.measureDc);
            g_app.measureDc = NULL;
        }
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

typedef struct CliResponseState {
    int received;
    BellwinIpcResponse response;
} CliResponseState;

static LRESULT CALLBACK cli_response_window_proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    (void)wParam;
    if (message == WM_NCCREATE) {
        CREATESTRUCTW *create = (CREATESTRUCTW *)lParam;
        SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)create->lpCreateParams);
        return TRUE;
    }
    if (message == WM_COPYDATA) {
        CliResponseState *state = (CliResponseState *)GetWindowLongPtrW(window, GWLP_USERDATA);
        const COPYDATASTRUCT *data = (const COPYDATASTRUCT *)lParam;
        if (state && data && data->dwData == BELLWIN_COPYDATA_RESPONSE &&
            bellwin_ipc_response_valid((const BellwinIpcResponse *)data->lpData, data->cbData)) {
            memcpy(&state->response, data->lpData, sizeof(state->response));
            state->received = 1;
            return TRUE;
        }
        return FALSE;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

static void cli_write(DWORD streamId, const char *text) {
    HANDLE stream = GetStdHandle(streamId);
    if (!stream || stream == INVALID_HANDLE_VALUE) {
        if (AttachConsole(ATTACH_PARENT_PROCESS)) stream = GetStdHandle(streamId);
    }
    if (!stream || stream == INVALID_HANDLE_VALUE || !text) return;
    size_t length = strlen(text);
    while (length > 0) {
        DWORD chunk = length > MAXDWORD ? MAXDWORD : (DWORD)length;
        DWORD written = 0;
        if (!WriteFile(stream, text, chunk, &written, NULL) || written == 0) break;
        text += written;
        length -= written;
    }
}

static int start_background_server(void) {
    wchar_t executable[MAX_PATH];
    wchar_t commandLine[MAX_PATH + 32];
    DWORD length = GetModuleFileNameW(NULL, executable, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return 0;
    int commandLength = swprintf_s(
        commandLine,
        sizeof(commandLine) / sizeof(commandLine[0]),
        L"\"%ls\" --background",
        executable
    );
    if (commandLength < 0 || (size_t)commandLength >= sizeof(commandLine) / sizeof(commandLine[0])) return 0;

    STARTUPINFOW startup;
    PROCESS_INFORMATION process;
    ZeroMemory(&startup, sizeof(startup));
    ZeroMemory(&process, sizeof(process));
    startup.cb = sizeof(startup);
    if (!CreateProcessW(executable, commandLine, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process)) return 0;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return 1;
}

static HWND find_or_start_server(void) {
    HWND server = FindWindowW(APP_CLASS, NULL);
    if (server) return server;
    if (!start_background_server()) return NULL;
    for (int attempt = 0; attempt < 100; ++attempt) {
        server = FindWindowW(APP_CLASS, NULL);
        if (server) return server;
        Sleep(50);
    }
    return NULL;
}

static int send_cli_request(HWND server, const BellwinCliCommand *command, BellwinIpcResponse *response) {
    WNDCLASSEXW windowClass;
    ZeroMemory(&windowClass, sizeof(windowClass));
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = cli_response_window_proc;
    windowClass.hInstance = g_app.instance;
    windowClass.lpszClassName = CLI_WINDOW_CLASS;
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return 0;

    CliResponseState state;
    ZeroMemory(&state, sizeof(state));
    HWND responseWindow = CreateWindowExW(
        0, CLI_WINDOW_CLASS, L"", 0, 0, 0, 0, 0,
        HWND_MESSAGE, NULL, g_app.instance, &state
    );
    if (!responseWindow) return 0;

    BellwinIpcRequest request;
    int madeRequest = bellwin_cli_make_request(command, &request);
    COPYDATASTRUCT data;
    ZeroMemory(&data, sizeof(data));
    data.dwData = BELLWIN_COPYDATA_REQUEST;
    data.cbData = sizeof(request);
    data.lpData = &request;
    DWORD_PTR serverResult = 0;
    int sent = madeRequest && SendMessageTimeoutW(
        server,
        WM_COPYDATA,
        (WPARAM)responseWindow,
        (LPARAM)&data,
        SMTO_ABORTIFHUNG,
        5000,
        &serverResult
    );
    (void)serverResult;
    DestroyWindow(responseWindow);
    if (!sent || !state.received) return 0;
    *response = state.response;
    return 1;
}

static int running_instance_is_older(HWND server) {
    DWORD processId = 0;
    GetWindowThreadProcessId(server, &processId);
    if (!processId) return 0;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process) return 0;
    wchar_t path[MAX_PATH];
    DWORD count = MAX_PATH;
    int older = QueryFullProcessImageNameW(process, 0, path, &count)
        && executable_version(path) < current_version();
    CloseHandle(process);
    return older;
}

static HWND replace_older_server(HWND server) {
    if (!running_instance_is_older(server)) return NULL;
    DWORD processId = 0;
    GetWindowThreadProcessId(server, &processId);
    HANDLE process = processId ? OpenProcess(SYNCHRONIZE, FALSE, processId) : NULL;
    if (!process) return NULL;
    PostMessageW(server, WM_COMMAND, CMD_TRAY_EXIT, 0);
    DWORD waitResult = WaitForSingleObject(process, 5000);
    CloseHandle(process);
    if (waitResult != WAIT_OBJECT_0 || !start_background_server()) return NULL;
    for (int attempt = 0; attempt < 100; ++attempt) {
        HWND replacement = FindWindowW(APP_CLASS, NULL);
        if (replacement) return replacement;
        Sleep(50);
    }
    return NULL;
}

static int run_cli_command(const BellwinCliCommand *command) {
    HWND server = find_or_start_server();
    if (!server) {
        cli_write(STD_ERROR_HANDLE, "bellwin: could not start or find the background instance\n");
        return 1;
    }
    BellwinIpcResponse response;
    if (!send_cli_request(server, command, &response)) {
        server = replace_older_server(server);
        if (!server || !send_cli_request(server, command, &response)) {
            cli_write(STD_ERROR_HANDLE, "bellwin: the running instance did not accept the command\n");
            return 1;
        }
    }
    cli_write(response.status == 0 ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE, response.text);
    return response.status;
}

static int current_copy_is_newer_than_installed(void) {
    wchar_t currentExe[MAX_PATH];
    if (!GetModuleFileNameW(NULL, currentExe, MAX_PATH)) return 0;
    if (_wcsicmp(currentExe, g_app.installedExePath) == 0) return 0;
    if (!file_exists(g_app.installedExePath)) return 0;
    return current_version() > executable_version(g_app.installedExePath);
}

static int take_over_from_older_instance(void) {
    HWND existing = NULL;
    for (int attempt = 0; attempt < 40 && !existing; ++attempt) {
        existing = FindWindowW(APP_CLASS, NULL);
        if (!existing) Sleep(50);
    }
    if (!existing) return 0;
    PostMessageW(existing, WM_COMMAND, CMD_TRAY_EXIT, 0);

    if (g_app.mutex) {
        CloseHandle(g_app.mutex);
        g_app.mutex = NULL;
    }
    for (int attempt = 0; attempt < 100; ++attempt) {
        HANDLE mutex = CreateMutexW(NULL, FALSE, APP_MUTEX);
        if (mutex && GetLastError() != ERROR_ALREADY_EXISTS) {
            g_app.mutex = mutex;
            return 1;
        }
        if (mutex) CloseHandle(mutex);
        Sleep(50);
    }
    return 0;
}

int main(int argc, char **argv) {
    ZeroMemory(&g_app, sizeof(g_app));
    g_app.instance = GetModuleHandleW(NULL);
    BellwinCliCommand command;
    char cliError[256];
    BellwinCliParseResult parseResult = bellwin_cli_parse(argc, argv, &command, cliError, sizeof(cliError));
    if (parseResult == BELLWIN_CLI_PARSE_ERROR) {
        cli_write(STD_ERROR_HANDLE, cliError);
        cli_write(STD_ERROR_HANDLE, "\n\n");
        cli_write(STD_ERROR_HANDLE, bellwin_cli_help_text());
        return 2;
    }
    if (parseResult == BELLWIN_CLI_PARSE_HELP) {
        cli_write(STD_OUTPUT_HANDLE, bellwin_cli_help_text());
        return 0;
    }
    if (command.action != BELLWIN_CLI_NONE) return run_cli_command(&command);
    int background = command.background;

    if (!ensure_app_directory()) {
        MessageBoxW(NULL, L"Could not create the Bellwin data folder.", APP_NAME, MB_OK | MB_ICONERROR);
        return 1;
    }

    g_app.mutex = CreateMutexW(NULL, FALSE, APP_MUTEX);
    if (g_app.mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (current_copy_is_newer_than_installed()) {
            if (!take_over_from_older_instance()) {
                MessageBoxW(NULL, L"The older Bellwin instance could not be closed for updating.", APP_NAME, MB_OK | MB_ICONERROR);
                return 1;
            }
            background = 0;
        } else {
            HWND existing = FindWindowW(APP_CLASS, NULL);
            if (existing && !background) PostMessageW(existing, WM_SHOW_BELLWIN, 0, 0);
            CloseHandle(g_app.mutex);
            return 0;
        }
    }

    enable_dpi_awareness();
    HRESULT com = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    load_settings();
    g_app.autoStart = is_autostart_enabled();
    refresh_install_state();
    g_app.theme = bellwin_query_theme();
    srand((unsigned)(time(NULL) ^ GetTickCount() ^ GetCurrentProcessId()));

    g_app.largeIcon = (HICON)LoadImageW(g_app.instance, MAKEINTRESOURCEW(IDI_BELLWIN), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
    g_app.smallIcon = (HICON)LoadImageW(g_app.instance, MAKEINTRESOURCEW(IDI_BELLWIN), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    g_app.pausedSmallIcon = create_paused_icon(g_app.smallIcon, 16);

    WNDCLASSEXW windowClass;
    ZeroMemory(&windowClass, sizeof(windowClass));
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = window_proc;
    windowClass.hInstance = g_app.instance;
    windowClass.hIcon = g_app.largeIcon;
    windowClass.hIconSm = g_app.smallIcon;
    windowClass.hCursor = LoadCursorW(NULL, IDC_ARROW);
    windowClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    windowClass.lpszClassName = APP_CLASS;
    if (!RegisterClassExW(&windowClass)) return 1;

    g_app.dpi = (int)current_dpi(NULL);
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT size = {0, 0, px(760), px(407)};
    AdjustWindowRectEx(&size, style, FALSE, 0);
    int width = size.right - size.left;
    int height = size.bottom - size.top;
    int x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;
    g_app.window = CreateWindowExW(
        0, APP_CLASS, WINDOW_TITLE, style,
        x, y, width, height,
        NULL, NULL, g_app.instance, NULL
    );
    if (!g_app.window) return 1;

    g_app.dpi = (int)current_dpi(g_app.window);
    RECT adjusted = {0, 0, px(760), px(407)};
    AdjustWindowRectEx(&adjusted, style, FALSE, 0);
    width = adjusted.right - adjusted.left;
    height = adjusted.bottom - adjusted.top;
    MONITORINFO monitorInfo;
    ZeroMemory(&monitorInfo, sizeof(monitorInfo));
    monitorInfo.cbSize = sizeof(monitorInfo);
    GetMonitorInfoW(MonitorFromWindow(g_app.window, MONITOR_DEFAULTTONEAREST), &monitorInfo);
    x = monitorInfo.rcWork.left + (monitorInfo.rcWork.right - monitorInfo.rcWork.left - width) / 2;
    y = monitorInfo.rcWork.top + (monitorInfo.rcWork.bottom - monitorInfo.rcWork.top - height) / 2;
    SetWindowPos(g_app.window, NULL, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    create_fonts();
    g_app.measureDc = CreateCompatibleDC(NULL);
    bellwin_apply_window_frame(g_app.window, &g_app.theme);
    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    add_tray_icon();
    if (!extract_sound()) {
        MessageBoxW(g_app.window, L"The embedded bell sound could not be prepared.", APP_NAME, MB_OK | MB_ICONERROR);
    }
    schedule_next_bell();

    if (!background) {
        ShowWindow(g_app.window, SW_SHOW);
        UpdateWindow(g_app.window);
    }

    MSG message;
    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    if (SUCCEEDED(com)) CoUninitialize();
    bellwin_ui_shutdown();
    if (g_app.largeIcon) DestroyIcon(g_app.largeIcon);
    if (g_app.smallIcon) DestroyIcon(g_app.smallIcon);
    if (g_app.pausedSmallIcon) DestroyIcon(g_app.pausedSmallIcon);
    if (g_app.mutex) CloseHandle(g_app.mutex);
    return (int)message.wParam;
}
