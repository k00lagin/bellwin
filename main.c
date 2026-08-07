#define COBJMACROS
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <mmsystem.h>
#include <dwmapi.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>

#include "core.h"
#include "rendering.h"
#include "resource.h"
#include "version.h"

#define APP_CLASS L"Bellwin.Settings.Window"
#define APP_NAME L"Bellwin"
#define APP_MUTEX L"Local\\Bellwin.SingleInstance"
#define TIMER_SCHEDULE 1
#define TIMER_VOLUME_PREVIEW 2
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

typedef enum ControlId {
    CONTROL_NONE,
    CONTROL_VOLUME,
    CONTROL_MINIMUM_INTERVAL,
    CONTROL_MAXIMUM_INTERVAL,
    CONTROL_QUIET_START,
    CONTROL_QUIET_END,
    CONTROL_AUTOSTART,
    CONTROL_INSTALL,
} ControlId;

typedef struct TimeEditState {
    BellwinTimeSegment segment;
    int digitCount;
    int firstDigit;
} TimeEditState;

typedef enum WheelTargetKind {
    WHEEL_TARGET_NONE,
    WHEEL_TARGET_SLIDER,
    WHEEL_TARGET_SLIDER_HORIZONTAL,
    WHEEL_TARGET_TIME_SEGMENT,
    WHEEL_TARGET_TIME_STEPPER,
} WheelTargetKind;

typedef enum FocusVisibility {
    FOCUS_HIDDEN,
    FOCUS_VISIBLE,
} FocusVisibility;

typedef struct WheelState {
    WheelTargetKind kind;
    ControlId control;
    BellwinTimeSegment segment;
    int remainder;
} WheelState;

typedef enum PauseMode {
    PAUSE_NONE,
    PAUSE_TIMED,
    PAUSE_INDEFINITE,
} PauseMode;

typedef struct PauseState {
    PauseMode mode;
    uint64_t startedUnixSeconds;
    uint64_t untilUnixSeconds;
    int selectedMinutes;
} PauseState;

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

enum UiGeometry {
    SLIDER_LEFT = 330,
    SLIDER_RIGHT = 565,
    VOLUME_SLIDER_Y = 122,
    MINIMUM_SLIDER_Y = 178,
    MAXIMUM_SLIDER_Y = 234,
    QUIET_START_X = 330,
    QUIET_END_X = 480,
    QUIET_TIME_Y = 292,
    TIME_BOX_WIDTH = 110,
    TIME_BOX_HEIGHT = 40,
    TIME_STEPPER_X_OFFSET = 84,
    TOGGLE_X = 190,
    TOGGLE_Y = 390,
    INSTALL_LEFT = 610,
    INSTALL_TOP = 385,
    INSTALL_RIGHT = 710,
    INSTALL_BOTTOM = 425,
};

typedef struct AppState {
    HINSTANCE instance;
    HWND window;
    HANDLE mutex;
    HICON largeIcon;
    HICON smallIcon;
    HICON pausedSmallIcon;
    NOTIFYICONDATAW tray;
    BellwinSettings settings;
    HFONT titleFont;
    HFONT bodyFont;
    HFONT smallFont;
    int dpi;
    ControlId draggingSlider;
    ControlId focusedControl;
    FocusVisibility focusVisibility;
    TimeEditState timeEdit;
    WheelState wheel;
    int windowFocused;
    int hoverInstall;
    int trackingMouseLeave;
    int autoStart;
    int showInstall;
    int updateAvailable;
    uint64_t installedVersion;
    int exiting;
    uint64_t remainingActiveSeconds;
    uint64_t plannedActiveSeconds;
    ULONGLONG activeSegmentStartTick;
    uint64_t lastRingUnixSeconds;
    PauseState pause;
    wchar_t appDataDirectory[MAX_PATH];
    wchar_t settingsPath[MAX_PATH];
    wchar_t soundPath[MAX_PATH];
    wchar_t installedExePath[MAX_PATH];
} AppState;

static AppState g_app;
static UINT g_taskbarCreated;

static void update_tray_state(void);

static int px(int logical) {
    return MulDiv(logical, g_app.dpi, 96);
}

static int logical_x(LPARAM lParam) {
    return MulDiv(GET_X_LPARAM(lParam), 96, g_app.dpi);
}

static int logical_y(LPARAM lParam) {
    return MulDiv(GET_Y_LPARAM(lParam), 96, g_app.dpi);
}

static RECT logical_rect(int left, int top, int right, int bottom) {
    RECT rect = {px(left), px(top), px(right), px(bottom)};
    return rect;
}

static int point_in(int x, int y, int left, int top, int right, int bottom) {
    return x >= left && x < right && y >= top && y < bottom;
}

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

static void save_settings(void) {
    write_setting(L"Volume", g_app.settings.volume);
    write_setting(L"MinimumMinutes", g_app.settings.minimumMinutes);
    write_setting(L"MaximumMinutes", g_app.settings.maximumMinutes);
    write_setting(L"QuietStartMinutes", g_app.settings.quietStartMinutes);
    write_setting(L"QuietEndMinutes", g_app.settings.quietEndMinutes);
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

static void schedule_volume_preview(void) {
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

static void schedule_next_bell(void) {
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

static void ring_and_remember(void) {
    if (play_bell()) remember_last_ring();
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

static uint64_t current_version(void) {
    return ((uint64_t)VER_MAJOR << 48) | ((uint64_t)VER_MINOR << 32) | ((uint64_t)VER_PATCH << 16) | (uint64_t)VER_BUILD;
}

static void format_version(uint64_t version, wchar_t *buffer, size_t count) {
    swprintf_s(
        buffer,
        count,
        L"%u.%u.%u.%u",
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
    int installed = file_exists(g_app.installedExePath);
    g_app.installedVersion = installed ? executable_version(g_app.installedExePath) : 0;
    wchar_t startMenuShortcut[MAX_PATH];
    int shortcutsReady = known_folder_file_path(&FOLDERID_Programs, L"Bellwin.lnk", startMenuShortcut, MAX_PATH)
        && file_exists(startMenuShortcut);
    g_app.updateAvailable = installed && g_app.installedVersion < current_version();
    g_app.showInstall = !installed || g_app.updateAvailable || !shortcutsReady;
    if (!g_app.showInstall) g_app.hoverInstall = 0;
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

static void fill_rect_color(HDC dc, const RECT *rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, rect, brush);
    DeleteObject(brush);
}

static void rounded_rect(HDC dc, const RECT *rect, int radius, COLORREF fill, COLORREF border) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, rect->left, rect->top, rect->right, rect->bottom, px(radius), px(radius));
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

static void draw_text(HDC dc, const wchar_t *text, RECT rect, HFONT font, COLORREF color, UINT format) {
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, text, -1, &rect, format);
    SelectObject(dc, oldFont);
}

static void draw_focus_outline(HDC dc, const RECT *rect, int radius) {
    HPEN pen = CreatePen(PS_SOLID, px(2), RGB(0, 95, 184));
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, rect->left, rect->top, rect->right, rect->bottom, px(radius), px(radius));
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

static int control_has_visible_focus(ControlId control) {
    return g_app.windowFocused
        && g_app.focusVisibility == FOCUS_VISIBLE
        && g_app.focusedControl == control;
}

static int control_has_focus(ControlId control) {
    return g_app.windowFocused && g_app.focusedControl == control;
}

typedef struct PixelSurface {
    uint32_t *pixels;
    int width;
    int height;
} PixelSurface;

static unsigned blend_coverage_channel(
    unsigned background,
    unsigned fill,
    unsigned border,
    unsigned fillCoverage,
    unsigned borderCoverage
) {
    unsigned outside = BELLWIN_AA_SAMPLE_COUNT - fillCoverage - borderCoverage;
    return (
        background * outside + fill * fillCoverage + border * borderCoverage
        + BELLWIN_AA_SAMPLE_COUNT / 2
    ) / BELLWIN_AA_SAMPLE_COUNT;
}

static void blend_coverage_pixel(
    PixelSurface *surface,
    int x,
    int y,
    COLORREF fill,
    COLORREF border,
    unsigned fillCoverage,
    unsigned borderCoverage
) {
    uint32_t background = surface->pixels[y * surface->width + x];
    unsigned red = blend_coverage_channel(
        (background >> 16) & 0xff,
        GetRValue(fill),
        GetRValue(border),
        fillCoverage,
        borderCoverage
    );
    unsigned green = blend_coverage_channel(
        (background >> 8) & 0xff,
        GetGValue(fill),
        GetGValue(border),
        fillCoverage,
        borderCoverage
    );
    unsigned blue = blend_coverage_channel(
        background & 0xff,
        GetBValue(fill),
        GetBValue(border),
        fillCoverage,
        borderCoverage
    );
    surface->pixels[y * surface->width + x] =
        (background & 0xff000000) | (red << 16) | (green << 8) | blue;
}

static void draw_antialiased_circle(
    PixelSurface *surface,
    int centerX,
    int centerY,
    int radius,
    int borderWidth,
    COLORREF fill,
    COLORREF border
) {
    if (!surface->pixels || radius <= 0) return;

    int left = centerX - radius;
    int top = centerY - radius;
    int right = centerX + radius;
    int bottom = centerY + radius;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > surface->width) right = surface->width;
    if (bottom > surface->height) bottom = surface->height;

    /* Finish pending GDI writes before reading and blending DIB pixels. */
    GdiFlush();
    for (int y = top; y < bottom; ++y) {
        for (int x = left; x < right; ++x) {
            BellwinCircleCoverage coverage = bellwin_circle_coverage(
                x, y, centerX, centerY, radius, borderWidth
            );
            if (coverage.fill == 0 && coverage.border == 0) continue;

            blend_coverage_pixel(
                surface, x, y, fill, border, coverage.fill, coverage.border
            );
        }
    }
}

static void draw_antialiased_horizontal_capsule(
    PixelSurface *surface,
    const RECT *rect,
    COLORREF color
) {
    if (!surface->pixels || rect->right <= rect->left || rect->bottom <= rect->top) return;

    int left = rect->left < 0 ? 0 : rect->left;
    int top = rect->top < 0 ? 0 : rect->top;
    int right = rect->right > surface->width ? surface->width : rect->right;
    int bottom = rect->bottom > surface->height ? surface->height : rect->bottom;

    GdiFlush();
    for (int y = top; y < bottom; ++y) {
        for (int x = left; x < right; ++x) {
            unsigned coverage = bellwin_horizontal_capsule_coverage(
                x, y, rect->left, rect->top, rect->right, rect->bottom
            );
            if (coverage == 0) continue;
            blend_coverage_pixel(surface, x, y, color, color, coverage, 0);
        }
    }
}

static void draw_slider(PixelSurface *surface, HDC dc, ControlId control, int y, int value, int minimum, int maximum, int ticks, const wchar_t *valueText) {
    const int left = SLIDER_LEFT;
    const int right = SLIDER_RIGHT;
    int position = left + MulDiv(value - minimum, right - left, maximum - minimum);
    RECT inactive = logical_rect(left, y - 2, right, y + 2);
    RECT active = logical_rect(left, y - 2, position, y + 2);
    fill_rect_color(dc, &inactive, RGB(210, 213, 218));
    fill_rect_color(dc, &active, RGB(0, 120, 212));

    if (ticks > 1) {
        HPEN tickPen = CreatePen(PS_SOLID, px(2), RGB(197, 200, 204));
        HGDIOBJ oldPen = SelectObject(dc, tickPen);
        for (int i = 0; i < ticks; ++i) {
            int x = left + MulDiv(i, right - left, ticks - 1);
            MoveToEx(dc, px(x), px(y - 7), NULL);
            LineTo(dc, px(x), px(y + 7));
        }
        SelectObject(dc, oldPen);
        DeleteObject(tickPen);
    }

    draw_antialiased_circle(
        surface,
        px(position),
        px(y),
        px(10),
        px(3),
        RGB(255, 255, 255),
        RGB(0, 120, 212)
    );

    RECT valueRect = logical_rect(590, y - 20, 690, y + 20);
    draw_text(dc, valueText, valueRect, g_app.bodyFont, RGB(96, 96, 96), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    if (control_has_visible_focus(control)) {
        RECT focus = logical_rect(315, y - 21, 700, y + 21);
        draw_focus_outline(dc, &focus, 6);
    }
}

static void format_interval(int minutes, wchar_t *buffer, size_t count) {
    if (minutes < 60) {
        swprintf_s(buffer, count, L"%d min", minutes);
    } else if (minutes % 60 == 0) {
        swprintf_s(buffer, count, L"%d hr", minutes / 60);
    } else {
        swprintf_s(buffer, count, L"%d.5 hr", minutes / 60);
    }
}

static void draw_triangle(HDC dc, POINT points[3], COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    Polygon(dc, points, 3);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

static void draw_time_box(HDC dc, ControlId control, int x, int y, int minuteOfDay) {
    RECT box = logical_rect(x, y, x + 110, y + 40);
    int focused = control_has_focus(control);
    rounded_rect(dc, &box, 3, RGB(255, 255, 255), RGB(194, 196, 200));

    RECT hoursRect = logical_rect(x + 6, y + 4, x + 38, y + 36);
    RECT colonRect = logical_rect(x + 38, y + 4, x + 48, y + 36);
    RECT minutesRect = logical_rect(x + 48, y + 4, x + 80, y + 36);
    RECT *selectedRect = g_app.timeEdit.segment == BELLWIN_TIME_HOURS ? &hoursRect : &minutesRect;
    if (focused) rounded_rect(dc, selectedRect, 3, RGB(0, 120, 212), RGB(0, 120, 212));

    wchar_t hoursText[3];
    wchar_t minutesText[3];
    swprintf_s(hoursText, 3, L"%02d", minuteOfDay / 60);
    swprintf_s(minutesText, 3, L"%02d", minuteOfDay % 60);
    COLORREF hoursColor = focused && g_app.timeEdit.segment == BELLWIN_TIME_HOURS ? RGB(255, 255, 255) : RGB(32, 32, 32);
    COLORREF minutesColor = focused && g_app.timeEdit.segment == BELLWIN_TIME_MINUTES ? RGB(255, 255, 255) : RGB(32, 32, 32);
    draw_text(dc, hoursText, hoursRect, g_app.bodyFont, hoursColor, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    draw_text(dc, L":", colonRect, g_app.bodyFont, RGB(32, 32, 32), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    draw_text(dc, minutesText, minutesRect, g_app.bodyFont, minutesColor, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    HPEN divider = CreatePen(PS_SOLID, 1, RGB(218, 220, 223));
    HGDIOBJ oldPen = SelectObject(dc, divider);
    MoveToEx(dc, px(x + 84), px(y), NULL);
    LineTo(dc, px(x + 84), px(y + 40));
    MoveToEx(dc, px(x + 84), px(y + 20), NULL);
    LineTo(dc, px(x + 110), px(y + 20));
    SelectObject(dc, oldPen);
    DeleteObject(divider);

    POINT up[3] = {{px(x + 91), px(y + 14)}, {px(x + 97), px(y + 8)}, {px(x + 103), px(y + 14)}};
    POINT down[3] = {{px(x + 91), px(y + 26)}, {px(x + 97), px(y + 32)}, {px(x + 103), px(y + 26)}};
    draw_triangle(dc, up, RGB(80, 80, 80));
    draw_triangle(dc, down, RGB(80, 80, 80));
}

static void draw_toggle(PixelSurface *surface, HDC dc, int x, int y, int on) {
    RECT track = logical_rect(x, y, x + 54, y + 30);
    draw_antialiased_horizontal_capsule(
        surface, &track, on ? RGB(0, 120, 212) : RGB(145, 149, 154)
    );
    int knobX = on ? x + 39 : x + 15;
    draw_antialiased_circle(
        surface,
        px(knobX),
        px(y + 15),
        px(11),
        0,
        RGB(255, 255, 255),
        RGB(255, 255, 255)
    );
    if (control_has_visible_focus(CONTROL_AUTOSTART)) {
        RECT focus = logical_rect(x - 5, y - 5, x + 59, y + 35);
        draw_focus_outline(dc, &focus, 20);
    }
}

static void draw_install_button(HDC dc) {
    if (!g_app.showInstall) return;
    RECT button = logical_rect(INSTALL_LEFT, INSTALL_TOP, INSTALL_RIGHT, INSTALL_BOTTOM);
    COLORREF fill = g_app.hoverInstall ? RGB(229, 241, 251) : RGB(255, 255, 255);
    rounded_rect(dc, &button, 5, fill, RGB(145, 149, 154));
    draw_text(
        dc,
        g_app.updateAvailable ? L"Update" : L"Install",
        button,
        g_app.smallFont,
        RGB(32, 32, 32),
        DT_CENTER | DT_VCENTER | DT_SINGLELINE
    );
    if (control_has_visible_focus(CONTROL_INSTALL)) draw_focus_outline(dc, &button, 5);
}

static void draw_update_tooltip(HDC dc) {
    if (!g_app.hoverInstall || !g_app.updateAvailable) return;

    wchar_t installed[24];
    wchar_t current[24];
    wchar_t text[80];
    format_version(g_app.installedVersion, installed, sizeof(installed) / sizeof(installed[0]));
    format_version(current_version(), current, sizeof(current) / sizeof(current[0]));
    swprintf_s(
        text,
        sizeof(text) / sizeof(text[0]),
        L"Update from %ls to %ls",
        installed,
        current
    );

    RECT measured = {0};
    HGDIOBJ oldFont = SelectObject(dc, g_app.smallFont);
    DrawTextW(dc, text, -1, &measured, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, oldFont);

    int horizontalPadding = px(9);
    int verticalPadding = px(6);
    int gap = px(8);
    RECT tooltip = {
        px(INSTALL_RIGHT) - (measured.right - measured.left) - horizontalPadding * 2,
        px(INSTALL_TOP) - gap - (measured.bottom - measured.top) - verticalPadding * 2,
        px(INSTALL_RIGHT),
        px(INSTALL_TOP) - gap,
    };
    rounded_rect(dc, &tooltip, 4, RGB(255, 255, 225), RGB(118, 118, 118));
    RECT content = tooltip;
    InflateRect(&content, -horizontalPadding, -verticalPadding);
    draw_text(
        dc,
        text,
        content,
        g_app.smallFont,
        RGB(32, 32, 32),
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX
    );
}

static void paint_ui(HWND window) {
    PAINTSTRUCT paint;
    HDC target = BeginPaint(window, &paint);
    RECT client;
    GetClientRect(window, &client);
    HDC dc = CreateCompatibleDC(target);
    BITMAPINFO bitmapInfo = {0};
    bitmapInfo.bmiHeader.biSize = sizeof(bitmapInfo.bmiHeader);
    bitmapInfo.bmiHeader.biWidth = client.right;
    bitmapInfo.bmiHeader.biHeight = -client.bottom;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;
    void *bitmapPixels = NULL;
    HBITMAP bitmap = CreateDIBSection(
        target, &bitmapInfo, DIB_RGB_COLORS, &bitmapPixels, NULL, 0
    );
    HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
    PixelSurface surface = {
        .pixels = bitmapPixels,
        .width = client.right,
        .height = client.bottom,
    };

    fill_rect_color(dc, &client, RGB(243, 243, 243));

    RECT title = logical_rect(0, 18, 760, 55);
    draw_text(dc, L"Settings", title, g_app.titleFont, RGB(50, 50, 50), DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    RECT card = logical_rect(40, 72, 720, 355);
    rounded_rect(dc, &card, 12, RGB(251, 251, 251), RGB(225, 225, 225));

    RECT label = logical_rect(78, 101, 310, 143);
    draw_text(dc, L"Bell volume", label, g_app.bodyFont, RGB(32, 32, 32), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    label = logical_rect(78, 157, 310, 199);
    draw_text(dc, L"Ring every", label, g_app.bodyFont, RGB(32, 32, 32), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    label = logical_rect(78, 213, 310, 255);
    draw_text(dc, L"to", label, g_app.bodyFont, RGB(32, 32, 32), DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    wchar_t valueText[32];
    swprintf_s(valueText, 32, L"%d%%", g_app.settings.volume);
    draw_slider(&surface, dc, CONTROL_VOLUME, VOLUME_SLIDER_Y, g_app.settings.volume, 0, 100, 0, valueText);
    format_interval(g_app.settings.minimumMinutes, valueText, 32);
    draw_slider(&surface, dc, CONTROL_MINIMUM_INTERVAL, MINIMUM_SLIDER_Y, g_app.settings.minimumMinutes, 30, 480, 16, valueText);
    format_interval(g_app.settings.maximumMinutes, valueText, 32);
    draw_slider(&surface, dc, CONTROL_MAXIMUM_INTERVAL, MAXIMUM_SLIDER_Y, g_app.settings.maximumMinutes, 30, 480, 16, valueText);

    RECT divider = logical_rect(78, 271, 682, 272);
    fill_rect_color(dc, &divider, RGB(220, 220, 220));
    label = logical_rect(78, 291, 310, 333);
    draw_text(dc, L"Quiet hours", label, g_app.bodyFont, RGB(32, 32, 32), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    draw_time_box(dc, CONTROL_QUIET_START, QUIET_START_X, QUIET_TIME_Y, g_app.settings.quietStartMinutes);
    RECT ellipsis = logical_rect(446, 292, 474, 332);
    draw_text(dc, L"…", ellipsis, g_app.bodyFont, RGB(100, 100, 100), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    draw_time_box(dc, CONTROL_QUIET_END, QUIET_END_X, QUIET_TIME_Y, g_app.settings.quietEndMinutes);

    label = logical_rect(40, 379, 185, 431);
    draw_text(dc, L"Launch at login", label, g_app.bodyFont, RGB(32, 32, 32), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    draw_toggle(&surface, dc, TOGGLE_X, TOGGLE_Y, g_app.autoStart);
    draw_install_button(dc);
    draw_update_tooltip(dc);

    BitBlt(target, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
    EndPaint(window, &paint);
}

static int is_slider_control(ControlId control) {
    return control >= CONTROL_VOLUME && control <= CONTROL_MAXIMUM_INTERVAL;
}

static int is_time_control(ControlId control) {
    return control == CONTROL_QUIET_START || control == CONTROL_QUIET_END;
}

static int slider_y(ControlId control) {
    if (control == CONTROL_VOLUME) return VOLUME_SLIDER_Y;
    if (control == CONTROL_MINIMUM_INTERVAL) return MINIMUM_SLIDER_Y;
    return MAXIMUM_SLIDER_Y;
}

static ControlId slider_at_point(int x, int y) {
    if (x < 315 || x >= 700) return CONTROL_NONE;
    ControlId sliders[] = {CONTROL_VOLUME, CONTROL_MINIMUM_INTERVAL, CONTROL_MAXIMUM_INTERVAL};
    for (size_t i = 0; i < sizeof(sliders) / sizeof(sliders[0]); ++i) {
        int rowY = slider_y(sliders[i]);
        if (y >= rowY - 21 && y < rowY + 21) return sliders[i];
    }
    return CONTROL_NONE;
}

static int time_x(ControlId control) {
    return control == CONTROL_QUIET_START ? QUIET_START_X : QUIET_END_X;
}

static int *time_value(ControlId control) {
    return control == CONTROL_QUIET_START
        ? &g_app.settings.quietStartMinutes
        : &g_app.settings.quietEndMinutes;
}

static int hit_time_segment(int x, int y, ControlId *control, BellwinTimeSegment *segment) {
    if (y < QUIET_TIME_Y || y >= QUIET_TIME_Y + TIME_BOX_HEIGHT) return 0;
    ControlId times[] = {CONTROL_QUIET_START, CONTROL_QUIET_END};
    for (size_t i = 0; i < sizeof(times) / sizeof(times[0]); ++i) {
        int left = time_x(times[i]);
        if (x >= left + 6 && x < left + 38) {
            *control = times[i];
            *segment = BELLWIN_TIME_HOURS;
            return 1;
        }
        if (x >= left + 48 && x < left + 80) {
            *control = times[i];
            *segment = BELLWIN_TIME_MINUTES;
            return 1;
        }
    }
    return 0;
}

static int hit_time_stepper(int x, int y, ControlId *control) {
    if (y < QUIET_TIME_Y || y >= QUIET_TIME_Y + TIME_BOX_HEIGHT) return 0;
    ControlId times[] = {CONTROL_QUIET_START, CONTROL_QUIET_END};
    for (size_t i = 0; i < sizeof(times) / sizeof(times[0]); ++i) {
        int left = time_x(times[i]);
        if (x >= left + TIME_STEPPER_X_OFFSET && x < left + TIME_BOX_WIDTH) {
            *control = times[i];
            return 1;
        }
    }
    return 0;
}

static int consume_wheel_steps(
    WheelTargetKind kind,
    ControlId control,
    BellwinTimeSegment segment,
    int delta
) {
    if (g_app.wheel.kind != kind
        || g_app.wheel.control != control
        || g_app.wheel.segment != segment) {
        g_app.wheel.kind = kind;
        g_app.wheel.control = control;
        g_app.wheel.segment = segment;
        g_app.wheel.remainder = 0;
    }
    g_app.wheel.remainder += delta;
    int steps = g_app.wheel.remainder / WHEEL_DELTA;
    g_app.wheel.remainder -= steps * WHEEL_DELTA;
    return steps;
}

static void set_focus_visibility(FocusVisibility visibility) {
    if (g_app.focusVisibility == visibility) return;
    g_app.focusVisibility = visibility;
    InvalidateRect(g_app.window, NULL, FALSE);
}

static void focus_control(ControlId control, FocusVisibility visibility) {
    if (control == CONTROL_INSTALL && !g_app.showInstall) control = CONTROL_AUTOSTART;
    if (GetFocus() != g_app.window) SetFocus(g_app.window);
    g_app.focusVisibility = visibility;
    if (g_app.focusedControl != control) {
        g_app.focusedControl = control;
        g_app.timeEdit.digitCount = 0;
        if (is_time_control(control)) g_app.timeEdit.segment = BELLWIN_TIME_HOURS;
    }
    InvalidateRect(g_app.window, NULL, FALSE);
}

static void move_focus(int direction) {
    int first = CONTROL_VOLUME;
    int last = g_app.showInstall ? CONTROL_INSTALL : CONTROL_AUTOSTART;
    int current = g_app.focusedControl;
    if (current < first || current > last) current = direction > 0 ? last : first;
    current += direction;
    if (current > last) current = first;
    if (current < first) current = last;
    focus_control((ControlId)current, FOCUS_VISIBLE);
}

static int slider_value_from_x(ControlId slider, int x) {
    int value;
    x = bellwin_clamp_int(x, SLIDER_LEFT, SLIDER_RIGHT);
    if (slider == CONTROL_VOLUME) {
        value = MulDiv(x - SLIDER_LEFT, 100, SLIDER_RIGHT - SLIDER_LEFT);
    } else {
        value = 30 + MulDiv(x - SLIDER_LEFT, 480 - 30, SLIDER_RIGHT - SLIDER_LEFT);
        value = ((value + 15) / 30) * 30;
        value = bellwin_clamp_int(value, 30, 480);
    }
    return value;
}

static void set_slider_value(ControlId slider, int value, int persist) {
    if (slider == CONTROL_VOLUME) {
        g_app.settings.volume = bellwin_clamp_int(value, 0, 100);
    } else if (slider == CONTROL_MINIMUM_INTERVAL) {
        g_app.settings.minimumMinutes = bellwin_clamp_int(value, 30, 480);
        if (g_app.settings.maximumMinutes < g_app.settings.minimumMinutes) {
            g_app.settings.maximumMinutes = g_app.settings.minimumMinutes;
        }
    } else if (slider == CONTROL_MAXIMUM_INTERVAL) {
        g_app.settings.maximumMinutes = bellwin_clamp_int(value, 30, 480);
        if (g_app.settings.minimumMinutes > g_app.settings.maximumMinutes) {
            g_app.settings.minimumMinutes = g_app.settings.maximumMinutes;
        }
    }
    if (persist) {
        save_settings();
        if (slider != CONTROL_VOLUME) schedule_next_bell();
    }
    InvalidateRect(g_app.window, NULL, FALSE);
}

static void update_slider_from_mouse(ControlId slider, int x) {
    int value = slider_value_from_x(slider, x);
    set_slider_value(slider, value, 0);
}

static void step_slider(ControlId slider, int direction, int wheel) {
    int step = slider == CONTROL_VOLUME ? (wheel ? 5 : 1) : 30;
    int current = slider == CONTROL_VOLUME
        ? g_app.settings.volume
        : slider == CONTROL_MINIMUM_INTERVAL
            ? g_app.settings.minimumMinutes
            : g_app.settings.maximumMinutes;
    set_slider_value(slider, current + direction * step, 1);
    if (slider == CONTROL_VOLUME) schedule_volume_preview();
}

static void wheel_logical_point(LPARAM lParam, int *x, int *y) {
    POINT point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    ScreenToClient(g_app.window, &point);
    *x = MulDiv(point.x, 96, g_app.dpi);
    *y = MulDiv(point.y, 96, g_app.dpi);
}

static int direct_gesture_delta(WPARAM wParam) {
    /* Treat touchpad scrolling as direct manipulation: up/right increases. */
    return -GET_WHEEL_DELTA_WPARAM(wParam);
}

static int step_hovered_slider(WheelTargetKind kind, int x, int y, int delta) {
    ControlId slider = slider_at_point(x, y);
    if (slider == CONTROL_NONE) return 0;
    int steps = consume_wheel_steps(kind, slider, BELLWIN_TIME_HOURS, delta);
    if (steps) step_slider(slider, steps, 1);
    return 1;
}

static void finish_slider_drag(void) {
    if (!g_app.draggingSlider) return;
    ControlId finished = g_app.draggingSlider;
    g_app.draggingSlider = CONTROL_NONE;
    save_settings();
    if (finished == CONTROL_VOLUME) schedule_volume_preview();
    else schedule_next_bell();
}

static void set_time_value(ControlId control, int value) {
    *time_value(control) = bellwin_normalize_day_minute(value);
    save_settings();
    schedule_next_bell();
    InvalidateRect(g_app.window, NULL, FALSE);
}

static void step_time_value(ControlId control, BellwinTimeSegment segment, int delta) {
    if (g_app.focusedControl == control && g_app.timeEdit.segment == segment) {
        g_app.timeEdit.digitCount = 0;
    }
    set_time_value(control, bellwin_step_time_segment(*time_value(control), segment, delta));
}

static void shift_time_minutes(ControlId control, int deltaMinutes) {
    if (g_app.focusedControl == control) g_app.timeEdit.digitCount = 0;
    set_time_value(control, *time_value(control) + deltaMinutes);
}

static void enter_time_digit(int digit) {
    if (!is_time_control(g_app.focusedControl)) return;
    int value;
    if (g_app.timeEdit.digitCount == 0) {
        g_app.timeEdit.firstDigit = digit;
        g_app.timeEdit.digitCount = 1;
        value = digit;
    } else {
        value = g_app.timeEdit.firstDigit * 10 + digit;
        g_app.timeEdit.digitCount = 0;
    }
    int updated = bellwin_set_time_segment(*time_value(g_app.focusedControl), g_app.timeEdit.segment, value);
    set_time_value(g_app.focusedControl, updated);
    if (g_app.timeEdit.digitCount == 0 && g_app.timeEdit.segment == BELLWIN_TIME_HOURS) {
        g_app.timeEdit.segment = BELLWIN_TIME_MINUTES;
        InvalidateRect(g_app.window, NULL, FALSE);
    }
}

static void activate_autostart(void) {
    int desired = !g_app.autoStart;
    if (set_autostart(desired)) {
        g_app.autoStart = desired;
        InvalidateRect(g_app.window, NULL, FALSE);
    } else {
        MessageBoxW(g_app.window, L"Could not change the startup setting.", APP_NAME, MB_OK | MB_ICONERROR);
    }
}

static void activate_install(void) {
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

static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (g_taskbarCreated && message == g_taskbarCreated) {
        add_tray_icon();
        return 0;
    }

    switch (message) {
    case WM_PAINT:
        paint_ui(window);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SETFOCUS:
        g_app.windowFocused = 1;
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
        RECT *suggested = (RECT *)lParam;
        SetWindowPos(window, NULL, suggested->left, suggested->top,
            suggested->right - suggested->left, suggested->bottom - suggested->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        int x = logical_x(lParam);
        int y = logical_y(lParam);

        ControlId slider = slider_at_point(x, y);
        if (slider != CONTROL_NONE) {
            focus_control(slider, FOCUS_HIDDEN);
            if (x >= SLIDER_LEFT - 10 && x < SLIDER_RIGHT + 10) {
                g_app.draggingSlider = slider;
                SetCapture(window);
                update_slider_from_mouse(slider, x);
            }
            return 0;
        }

        ControlId timeControl;
        BellwinTimeSegment timeSegment;
        if (hit_time_segment(x, y, &timeControl, &timeSegment)) {
            focus_control(timeControl, FOCUS_HIDDEN);
            g_app.timeEdit.segment = timeSegment;
            g_app.timeEdit.digitCount = 0;
            InvalidateRect(window, NULL, FALSE);
            return 0;
        }

        if (hit_time_stepper(x, y, &timeControl)) {
            focus_control(timeControl, FOCUS_HIDDEN);
            shift_time_minutes(timeControl, y < QUIET_TIME_Y + TIME_BOX_HEIGHT / 2 ? 30 : -30);
            return 0;
        }

        if (point_in(x, y, TOGGLE_X, TOGGLE_Y, TOGGLE_X + 54, TOGGLE_Y + 30)) {
            focus_control(CONTROL_AUTOSTART, FOCUS_HIDDEN);
            activate_autostart();
            return 0;
        }
        if (g_app.showInstall && point_in(x, y, INSTALL_LEFT, INSTALL_TOP, INSTALL_RIGHT, INSTALL_BOTTOM)) {
            focus_control(CONTROL_INSTALL, FOCUS_HIDDEN);
            activate_install();
            return 0;
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
        int x = logical_x(lParam);
        int y = logical_y(lParam);
        if (g_app.draggingSlider) update_slider_from_mouse(g_app.draggingSlider, x);
        int hover = g_app.showInstall && point_in(x, y, INSTALL_LEFT, INSTALL_TOP, INSTALL_RIGHT, INSTALL_BOTTOM);
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
        int x;
        int y;
        wheel_logical_point(lParam, &x, &y);
        int delta = direct_gesture_delta(wParam);

        if (step_hovered_slider(WHEEL_TARGET_SLIDER, x, y, delta)) return 0;

        ControlId timeControl;
        BellwinTimeSegment timeSegment;
        if (hit_time_segment(x, y, &timeControl, &timeSegment)) {
            int steps = consume_wheel_steps(WHEEL_TARGET_TIME_SEGMENT, timeControl, timeSegment, delta);
            if (steps) step_time_value(timeControl, timeSegment, steps);
            return 0;
        }

        if (hit_time_stepper(x, y, &timeControl)) {
            int steps = consume_wheel_steps(WHEEL_TARGET_TIME_STEPPER, timeControl, BELLWIN_TIME_MINUTES, delta);
            if (steps) shift_time_minutes(timeControl, steps * 30);
            return 0;
        }
        return 0;
    }
    case WM_MOUSEHWHEEL: {
        int x;
        int y;
        wheel_logical_point(lParam, &x, &y);
        step_hovered_slider(WHEEL_TARGET_SLIDER_HORIZONTAL, x, y, direct_gesture_delta(wParam));
        return 0;
    }
    case WM_SETCURSOR: {
        POINT point;
        GetCursorPos(&point);
        ScreenToClient(window, &point);
        int x = MulDiv(point.x, 96, g_app.dpi);
        int y = MulDiv(point.y, 96, g_app.dpi);
        ControlId timeControl;
        BellwinTimeSegment timeSegment;
        if (hit_time_segment(x, y, &timeControl, &timeSegment)) {
            SetCursor(LoadCursorW(NULL, IDC_IBEAM));
            return TRUE;
        }
        int overTimeStepper = hit_time_stepper(x, y, &timeControl);
        if (point_in(x, y, TOGGLE_X, TOGGLE_Y, TOGGLE_X + 54, TOGGLE_Y + 30)
            || (g_app.showInstall && point_in(x, y, INSTALL_LEFT, INSTALL_TOP, INSTALL_RIGHT, INSTALL_BOTTOM))
            || overTimeStepper) {
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
        if (trayEvent == WM_LBUTTONUP || trayEvent == WM_LBUTTONDBLCLK
            || trayEvent == NIN_SELECT || trayEvent == NIN_KEYSELECT) {
            show_window();
        } else if (trayEvent == WM_RBUTTONUP || trayEvent == WM_CONTEXTMENU) {
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
    case WM_KEYDOWN:
        if (wParam != VK_ESCAPE && g_app.focusedControl != CONTROL_NONE) {
            set_focus_visibility(FOCUS_VISIBLE);
        }
        if (wParam == VK_TAB) {
            move_focus((GetKeyState(VK_SHIFT) & 0x8000) ? -1 : 1);
            return 0;
        }
        if ((wParam == VK_LEFT || wParam == VK_RIGHT) && is_slider_control(g_app.focusedControl)) {
            step_slider(g_app.focusedControl, wParam == VK_RIGHT ? 1 : -1, 0);
            return 0;
        }
        if ((wParam == VK_LEFT || wParam == VK_RIGHT) && is_time_control(g_app.focusedControl)) {
            g_app.timeEdit.segment = bellwin_next_time_segment(g_app.timeEdit.segment);
            g_app.timeEdit.digitCount = 0;
            InvalidateRect(window, NULL, FALSE);
            return 0;
        }
        if ((wParam == VK_UP || wParam == VK_DOWN) && is_time_control(g_app.focusedControl)) {
            step_time_value(g_app.focusedControl, g_app.timeEdit.segment, wParam == VK_UP ? 1 : -1);
            return 0;
        }
        if ((wParam == VK_SPACE || wParam == VK_RETURN) && !(lParam & (1L << 30))) {
            if (g_app.focusedControl == CONTROL_AUTOSTART) {
                activate_autostart();
                return 0;
            }
            if (g_app.focusedControl == CONTROL_INSTALL) {
                activate_install();
                return 0;
            }
        }
        if (wParam == VK_ESCAPE) ShowWindow(window, SW_HIDE);
        return 0;
    case WM_CHAR:
        if (is_time_control(g_app.focusedControl) && wParam >= L'0' && wParam <= L'9') {
            enter_time_digit((int)(wParam - L'0'));
            return 0;
        }
        if ((g_app.focusedControl == CONTROL_AUTOSTART || g_app.focusedControl == CONTROL_INSTALL)
            && (wParam == L' ' || wParam == L'\r')) {
            return 0;
        }
        return 0;
    case WM_CLOSE:
        if (g_app.exiting) DestroyWindow(window);
        else ShowWindow(window, SW_HIDE);
        return 0;
    case WM_QUERYENDSESSION:
        return TRUE;
    case WM_DESTROY:
        KillTimer(window, TIMER_SCHEDULE);
        KillTimer(window, TIMER_VOLUME_PREVIEW);
        remove_tray_icon();
        mciSendStringW(L"close bellwin_sound", NULL, 0, NULL);
        delete_fonts();
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

static int command_line_has(const wchar_t *needle) {
    int count = 0;
    wchar_t **arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments) return 0;
    int found = 0;
    for (int i = 1; i < count; ++i) {
        if (_wcsicmp(arguments[i], needle) == 0) {
            found = 1;
            break;
        }
    }
    LocalFree(arguments);
    return found;
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

int main(void) {
    ZeroMemory(&g_app, sizeof(g_app));
    g_app.instance = GetModuleHandleW(NULL);
    int background = command_line_has(L"--background");

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
    RECT size = {0, 0, px(760), px(455)};
    AdjustWindowRectEx(&size, style, FALSE, 0);
    int width = size.right - size.left;
    int height = size.bottom - size.top;
    int x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;
    g_app.window = CreateWindowExW(
        0, APP_CLASS, APP_NAME, style,
        x, y, width, height,
        NULL, NULL, g_app.instance, NULL
    );
    if (!g_app.window) return 1;

    g_app.dpi = (int)current_dpi(g_app.window);
    RECT adjusted = {0, 0, px(760), px(455)};
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
    int cornerPreference = 2;
    DwmSetWindowAttribute(g_app.window, 33, &cornerPreference, sizeof(cornerPreference));
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
    if (g_app.largeIcon) DestroyIcon(g_app.largeIcon);
    if (g_app.smallIcon) DestroyIcon(g_app.smallIcon);
    if (g_app.pausedSmallIcon) DestroyIcon(g_app.pausedSmallIcon);
    if (g_app.mutex) CloseHandle(g_app.mutex);
    return (int)message.wParam;
}
