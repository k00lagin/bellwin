#ifndef BELLWIN_APP_INTERNAL_H
#define BELLWIN_APP_INTERNAL_H

#define COBJMACROS
#include <windows.h>
#include <shellapi.h>
#include <stdint.h>

#include "core.h"
#include "layout.h"
#include "theme.h"
#include "ui_motion.h"
#include "widgets.h"

#define APP_CLASS L"Bellwin.Settings.Window"
#define APP_NAME L"Bellwin"
#define WM_UIA_INVOKE (WM_APP + 3)

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

typedef struct AppState {
    HINSTANCE instance;
    HWND window;
    HANDLE mutex;
    HICON largeIcon;
    HICON smallIcon;
    HICON pausedSmallIcon;
    NOTIFYICONDATAW tray;
    BellwinSettings settings;
    BellwinThemeState theme;
    HFONT titleFont;
    HFONT bodyFont;
    HFONT smallFont;
    HDC measureDc;
    int dpi;
    ControlId draggingSlider;
    ControlId focusedControl;
    FocusVisibility focusVisibility;
    TimeEditState timeEdit;
    WheelState wheel;
    UiPointerState hoveredPointer;
    UiPointerState pressedPointer;
    UiPartMotion partMotions[UI_MOTION_SLOT_COUNT];
    BellwinUiMotionValue toggleStateMotion;
    BellwinUiSwitchMotion toggleMotion;
    BellwinTogglePointerInteraction togglePointer;
    int windowFocused;
    int hoverInstall;
    int trackingMouseLeave;
    int uiMotionInitialized;
    int uiAnimationsEnabled;
    int uiAnimationTimerActive;
    int autoStart;
    int showInstall;
    int updateAvailable;
    uint64_t installedVersion;
    int exiting;
    int suppressTrayLeftButtonUp;
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

typedef struct Widget {
    ControlId id;
    BellwinWidgetRole role;
    const wchar_t *name;
    const wchar_t *automationId;
    int minimum;
    int maximum;
    int keyStep;
    int wheelStep;
    int ticks;
    int *value;
    void (*clamp)(void);
    void (*committed)(void);
    void (*invoke)(void);
    int (*visible)(void);
} Widget;

enum { WIDGET_COUNT = 7 };

extern AppState g_app;
extern const Widget WIDGETS[WIDGET_COUNT];

const Widget *widget_by_id(ControlId id);
int widget_index_of(ControlId id);
int widget_is_visible(const Widget *widget);
int is_time_control(ControlId control);

int app_px(int logical);
float app_ui_scale(void);
RECT app_rect_from_box(Clay_BoundingBox box);
void schedule_volume_preview(void);
void schedule_next_bell(void);
void activate_autostart(void);
void activate_install(void);
void clamp_minimum_interval(void);
void clamp_maximum_interval(void);
int install_visible(void);
void save_settings(void);
void format_version_utf8(uint64_t version, char *buffer, size_t count);
uint64_t current_version(void);

#endif
