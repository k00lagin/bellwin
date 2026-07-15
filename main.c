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
#include <time.h>
#include <wchar.h>

#include "core.h"
#include "resource.h"
#include "version.h"

#define APP_CLASS L"Bellwin.Settings.Window"
#define APP_NAME L"Bellwin"
#define APP_MUTEX L"Local\\Bellwin.SingleInstance"
#define TIMER_SCHEDULE 1
#define WM_TRAY (WM_APP + 1)
#define WM_SHOW_BELLWIN (WM_APP + 2)
#define CMD_TRAY_SHOW 1001
#define CMD_TRAY_RING 1002
#define CMD_TRAY_EXIT 1003

typedef BOOL (WINAPI *SetProcessDpiAwarenessContextFn)(HANDLE);
typedef UINT (WINAPI *GetDpiForWindowFn)(HWND);

typedef struct AppState {
    HINSTANCE instance;
    HWND window;
    HANDLE mutex;
    HICON largeIcon;
    HICON smallIcon;
    NOTIFYICONDATAW tray;
    BellwinSettings settings;
    HFONT titleFont;
    HFONT bodyFont;
    HFONT smallFont;
    int dpi;
    int draggingSlider;
    int hoverInstall;
    int autoStart;
    int showInstall;
    int updateAvailable;
    int exiting;
    ULONGLONG nextBellTick;
    wchar_t appDataDirectory[MAX_PATH];
    wchar_t settingsPath[MAX_PATH];
    wchar_t soundPath[MAX_PATH];
    wchar_t installedExePath[MAX_PATH];
} AppState;

static AppState g_app;
static UINT g_taskbarCreated;

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

static void save_settings(void) {
    write_setting(L"Volume", g_app.settings.volume);
    write_setting(L"MinimumMinutes", g_app.settings.minimumMinutes);
    write_setting(L"MaximumMinutes", g_app.settings.maximumMinutes);
    write_setting(L"QuietStartMinutes", g_app.settings.quietStartMinutes);
    write_setting(L"QuietEndMinutes", g_app.settings.quietEndMinutes);
}

static void load_settings(void) {
    g_app.settings.volume = GetPrivateProfileIntW(L"Bellwin", L"Volume", 50, g_app.settingsPath);
    g_app.settings.minimumMinutes = GetPrivateProfileIntW(L"Bellwin", L"MinimumMinutes", 60, g_app.settingsPath);
    g_app.settings.maximumMinutes = GetPrivateProfileIntW(L"Bellwin", L"MaximumMinutes", 240, g_app.settingsPath);
    g_app.settings.quietStartMinutes = GetPrivateProfileIntW(L"Bellwin", L"QuietStartMinutes", 22 * 60, g_app.settingsPath);
    g_app.settings.quietEndMinutes = GetPrivateProfileIntW(L"Bellwin", L"QuietEndMinutes", 10 * 60, g_app.settingsPath);
    bellwin_clamp_settings(&g_app.settings);
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

static void play_bell(void) {
    wchar_t command[MAX_PATH + 96];
    mciSendStringW(L"close bellwin_sound", NULL, 0, NULL);
    if (swprintf_s(command, MAX_PATH + 96, L"open \"%ls\" type mpegvideo alias bellwin_sound", g_app.soundPath) < 0) return;
    if (mciSendStringW(command, NULL, 0, NULL) != 0) return;
    swprintf_s(command, MAX_PATH + 96, L"setaudio bellwin_sound volume to %d", g_app.settings.volume * 10);
    mciSendStringW(command, NULL, 0, NULL);
    mciSendStringW(L"play bellwin_sound from 0", NULL, 0, NULL);
}

static uint32_t random_u32(void) {
    uint32_t value = (uint32_t)rand();
    value = (value << 15) ^ (uint32_t)rand();
    value = (value << 2) ^ (uint32_t)rand();
    return value;
}

static int current_minute_of_day(void) {
    SYSTEMTIME now;
    GetLocalTime(&now);
    return now.wHour * 60 + now.wMinute;
}

static void schedule_next_bell(void) {
    int delay = bellwin_random_delay_minutes(
        g_app.settings.minimumMinutes,
        g_app.settings.maximumMinutes,
        random_u32()
    );
    int quietWait = bellwin_minutes_until_quiet_end(
        current_minute_of_day(),
        g_app.settings.quietStartMinutes,
        g_app.settings.quietEndMinutes
    );
    g_app.nextBellTick = GetTickCount64() + (ULONGLONG)(delay + quietWait) * 60ULL * 1000ULL;
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
    uint64_t installedVersion = installed ? executable_version(g_app.installedExePath) : 0;
    wchar_t desktopShortcut[MAX_PATH];
    wchar_t startMenuShortcut[MAX_PATH];
    int shortcutsReady = known_folder_file_path(&FOLDERID_Desktop, L"Bellwin.lnk", desktopShortcut, MAX_PATH)
        && known_folder_file_path(&FOLDERID_Programs, L"Bellwin.lnk", startMenuShortcut, MAX_PATH)
        && file_exists(desktopShortcut)
        && file_exists(startMenuShortcut);
    g_app.updateAvailable = installed && installedVersion < current_version();
    g_app.showInstall = !installed || g_app.updateAvailable || !shortcutsReady;
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

    HRESULT desktop = create_shortcut(&FOLDERID_Desktop, L"Bellwin.lnk", g_app.installedExePath);
    HRESULT startMenu = create_shortcut(&FOLDERID_Programs, L"Bellwin.lnk", g_app.installedExePath);
    if (FAILED(desktop) || FAILED(startMenu)) return 0;

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

static void draw_slider(HDC dc, int y, int value, int minimum, int maximum, int ticks, const wchar_t *valueText) {
    const int left = 330;
    const int right = 565;
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

    HBRUSH knobBrush = CreateSolidBrush(RGB(255, 255, 255));
    HPEN knobPen = CreatePen(PS_SOLID, px(3), RGB(0, 120, 212));
    HGDIOBJ oldBrush = SelectObject(dc, knobBrush);
    HGDIOBJ oldPen = SelectObject(dc, knobPen);
    Ellipse(dc, px(position - 10), px(y - 10), px(position + 10), px(y + 10));
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(knobBrush);
    DeleteObject(knobPen);

    RECT valueRect = logical_rect(590, y - 20, 690, y + 20);
    draw_text(dc, valueText, valueRect, g_app.bodyFont, RGB(96, 96, 96), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
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

static void draw_time_box(HDC dc, int x, int y, int minuteOfDay) {
    RECT box = logical_rect(x, y, x + 110, y + 40);
    rounded_rect(dc, &box, 3, RGB(255, 255, 255), RGB(194, 196, 200));
    wchar_t timeText[16];
    swprintf_s(timeText, 16, L"%02d:%02d", minuteOfDay / 60, minuteOfDay % 60);
    RECT textRect = logical_rect(x + 8, y, x + 84, y + 40);
    draw_text(dc, timeText, textRect, g_app.bodyFont, RGB(32, 32, 32), DT_LEFT | DT_VCENTER | DT_SINGLELINE);

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

static void draw_toggle(HDC dc, int x, int y, int on) {
    RECT track = logical_rect(x, y, x + 54, y + 30);
    rounded_rect(dc, &track, 30, on ? RGB(0, 120, 212) : RGB(145, 149, 154), on ? RGB(0, 120, 212) : RGB(145, 149, 154));
    int knobX = on ? x + 39 : x + 15;
    HBRUSH brush = CreateSolidBrush(RGB(255, 255, 255));
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    Ellipse(dc, px(knobX - 11), px(y + 4), px(knobX + 11), px(y + 26));
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

static void draw_install_button(HDC dc) {
    if (!g_app.showInstall) return;
    RECT button = logical_rect(610, 385, 710, 425);
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
}

static void paint_ui(HWND window) {
    PAINTSTRUCT paint;
    HDC target = BeginPaint(window, &paint);
    RECT client;
    GetClientRect(window, &client);
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, client.right, client.bottom);
    HGDIOBJ oldBitmap = SelectObject(dc, bitmap);

    fill_rect_color(dc, &client, RGB(243, 243, 243));

    RECT title = logical_rect(0, 18, 760, 55);
    draw_text(dc, L"Settings", title, g_app.titleFont, RGB(50, 50, 50), DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    RECT card = logical_rect(40, 72, 720, 355);
    rounded_rect(dc, &card, 12, RGB(251, 251, 251), RGB(225, 225, 225));

    RECT label = logical_rect(78, 101, 310, 143);
    draw_text(dc, L"Bell volume", label, g_app.bodyFont, RGB(32, 32, 32), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    label = logical_rect(78, 157, 310, 199);
    draw_text(dc, L"Ring from every", label, g_app.bodyFont, RGB(32, 32, 32), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    label = logical_rect(78, 213, 310, 255);
    draw_text(dc, L"To every", label, g_app.bodyFont, RGB(32, 32, 32), DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    wchar_t valueText[32];
    swprintf_s(valueText, 32, L"%d%%", g_app.settings.volume);
    draw_slider(dc, 122, g_app.settings.volume, 0, 100, 0, valueText);
    format_interval(g_app.settings.minimumMinutes, valueText, 32);
    draw_slider(dc, 178, g_app.settings.minimumMinutes, 30, 480, 16, valueText);
    format_interval(g_app.settings.maximumMinutes, valueText, 32);
    draw_slider(dc, 234, g_app.settings.maximumMinutes, 30, 480, 16, valueText);

    RECT divider = logical_rect(78, 271, 682, 272);
    fill_rect_color(dc, &divider, RGB(220, 220, 220));
    label = logical_rect(78, 291, 310, 333);
    draw_text(dc, L"Quiet hours", label, g_app.bodyFont, RGB(32, 32, 32), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    draw_time_box(dc, 330, 292, g_app.settings.quietStartMinutes);
    RECT ellipsis = logical_rect(446, 292, 474, 332);
    draw_text(dc, L"…", ellipsis, g_app.bodyFont, RGB(100, 100, 100), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    draw_time_box(dc, 480, 292, g_app.settings.quietEndMinutes);

    label = logical_rect(40, 379, 185, 431);
    draw_text(dc, L"Launch at login", label, g_app.bodyFont, RGB(32, 32, 32), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    draw_toggle(dc, 190, 390, g_app.autoStart);
    draw_install_button(dc);

    BitBlt(target, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
    EndPaint(window, &paint);
}

static int slider_value_from_x(int slider, int x) {
    int value;
    x = bellwin_clamp_int(x, 330, 565);
    if (slider == 1) {
        value = MulDiv(x - 330, 100, 565 - 330);
    } else {
        value = 30 + MulDiv(x - 330, 480 - 30, 565 - 330);
        value = ((value + 15) / 30) * 30;
        value = bellwin_clamp_int(value, 30, 480);
    }
    return value;
}

static void update_slider_from_mouse(int slider, int x) {
    int value = slider_value_from_x(slider, x);
    if (slider == 1) {
        g_app.settings.volume = value;
    } else if (slider == 2) {
        g_app.settings.minimumMinutes = value;
        if (g_app.settings.maximumMinutes < value) g_app.settings.maximumMinutes = value;
    } else if (slider == 3) {
        g_app.settings.maximumMinutes = value;
        if (g_app.settings.minimumMinutes > value) g_app.settings.minimumMinutes = value;
    }
    InvalidateRect(g_app.window, NULL, FALSE);
}

static void finish_slider_drag(void) {
    if (!g_app.draggingSlider) return;
    int finished = g_app.draggingSlider;
    g_app.draggingSlider = 0;
    save_settings();
    schedule_next_bell();
    if (finished == 1) play_bell();
}

static void change_quiet_time(int which, int delta) {
    int *value = which == 1 ? &g_app.settings.quietStartMinutes : &g_app.settings.quietEndMinutes;
    *value = bellwin_normalize_day_minute(*value + delta);
    save_settings();
    schedule_next_bell();
    InvalidateRect(g_app.window, NULL, FALSE);
}

static void add_tray_icon(void) {
    ZeroMemory(&g_app.tray, sizeof(g_app.tray));
    g_app.tray.cbSize = sizeof(g_app.tray);
    g_app.tray.hWnd = g_app.window;
    g_app.tray.uID = 1;
    g_app.tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_app.tray.uCallbackMessage = WM_TRAY;
    g_app.tray.hIcon = g_app.smallIcon;
    wcscpy_s(g_app.tray.szTip, sizeof(g_app.tray.szTip) / sizeof(g_app.tray.szTip[0]), L"Bellwin — mindfulness bell");
    Shell_NotifyIconW(NIM_ADD, &g_app.tray);
    g_app.tray.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &g_app.tray);
}

static void remove_tray_icon(void) {
    if (g_app.tray.cbSize) Shell_NotifyIconW(NIM_DELETE, &g_app.tray);
    ZeroMemory(&g_app.tray, sizeof(g_app.tray));
}

static void show_window(void) {
    ShowWindow(g_app.window, SW_RESTORE);
    SetForegroundWindow(g_app.window);
}

static void show_tray_menu(void) {
    POINT point;
    GetCursorPos(&point);
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, CMD_TRAY_SHOW, L"Settings");
    AppendMenuW(menu, MF_STRING, CMD_TRAY_RING, L"Ring now");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, CMD_TRAY_EXIT, L"Exit");
    SetForegroundWindow(g_app.window);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN, point.x, point.y, 0, g_app.window, NULL);
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
        if (point_in(x, y, 320, 107, 575, 137)) g_app.draggingSlider = 1;
        else if (point_in(x, y, 320, 163, 575, 193)) g_app.draggingSlider = 2;
        else if (point_in(x, y, 320, 219, 575, 249)) g_app.draggingSlider = 3;
        if (g_app.draggingSlider) {
            SetCapture(window);
            update_slider_from_mouse(g_app.draggingSlider, x);
            return 0;
        }
        if (point_in(x, y, 190, 390, 244, 420)) {
            int desired = !g_app.autoStart;
            if (set_autostart(desired)) {
                g_app.autoStart = desired;
                InvalidateRect(window, NULL, FALSE);
            } else {
                MessageBoxW(window, L"Could not change the startup setting.", APP_NAME, MB_OK | MB_ICONERROR);
            }
            return 0;
        }
        if (g_app.showInstall && point_in(x, y, 610, 385, 710, 425)) {
            if (install_app()) {
                MessageBoxW(window, L"Bellwin was installed. Shortcuts were added to the Desktop and Start menu.", APP_NAME, MB_OK | MB_ICONINFORMATION);
            } else {
                MessageBoxW(window, L"Bellwin could not be installed.", APP_NAME, MB_OK | MB_ICONERROR);
            }
            return 0;
        }
        if (point_in(x, y, 414, 292, 440, 332)) {
            change_quiet_time(1, y < 312 ? 30 : -30);
            return 0;
        }
        if (point_in(x, y, 564, 292, 590, 332)) {
            change_quiet_time(2, y < 312 ? 30 : -30);
            return 0;
        }
        return 0;
    }
    case WM_MOUSEMOVE: {
        int x = logical_x(lParam);
        int y = logical_y(lParam);
        if (g_app.draggingSlider) update_slider_from_mouse(g_app.draggingSlider, x);
        int hover = g_app.showInstall && point_in(x, y, 610, 385, 710, 425);
        if (hover != g_app.hoverInstall) {
            g_app.hoverInstall = hover;
            InvalidateRect(window, NULL, FALSE);
        }
        return 0;
    }
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
        POINT point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(window, &point);
        int x = MulDiv(point.x, 96, g_app.dpi);
        int y = MulDiv(point.y, 96, g_app.dpi);
        int delta = GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? 30 : -30;
        if (point_in(x, y, 330, 292, 440, 332)) change_quiet_time(1, delta);
        else if (point_in(x, y, 480, 292, 590, 332)) change_quiet_time(2, delta);
        return 0;
    }
    case WM_SETCURSOR: {
        POINT point;
        GetCursorPos(&point);
        ScreenToClient(window, &point);
        int x = MulDiv(point.x, 96, g_app.dpi);
        int y = MulDiv(point.y, 96, g_app.dpi);
        if (point_in(x, y, 190, 390, 244, 420)
            || (g_app.showInstall && point_in(x, y, 610, 385, 710, 425))
            || point_in(x, y, 414, 292, 440, 332)
            || point_in(x, y, 564, 292, 590, 332)) {
            SetCursor(LoadCursorW(NULL, IDC_HAND));
            return TRUE;
        }
        break;
    }
    case WM_TIMER:
        if (wParam == TIMER_SCHEDULE && GetTickCount64() >= g_app.nextBellTick) {
            if (!bellwin_is_quiet(current_minute_of_day(), g_app.settings.quietStartMinutes, g_app.settings.quietEndMinutes)) {
                play_bell();
            }
            schedule_next_bell();
        }
        return 0;
    case WM_TRAY:
        if (lParam == WM_LBUTTONDBLCLK) show_window();
        else if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) show_tray_menu();
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case CMD_TRAY_SHOW:
            show_window();
            return 0;
        case CMD_TRAY_RING:
            play_bell();
            schedule_next_bell();
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
        if (wParam == VK_ESCAPE) ShowWindow(window, SW_HIDE);
        return 0;
    case WM_CLOSE:
        if (g_app.exiting) DestroyWindow(window);
        else ShowWindow(window, SW_HIDE);
        return 0;
    case WM_QUERYENDSESSION:
        return TRUE;
    case WM_DESTROY:
        KillTimer(window, TIMER_SCHEDULE);
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
    SetTimer(g_app.window, TIMER_SCHEDULE, 1000, NULL);

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
    if (g_app.mutex) CloseHandle(g_app.mutex);
    return (int)message.wParam;
}
