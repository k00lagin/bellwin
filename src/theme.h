#ifndef BELLWIN_THEME_H
#define BELLWIN_THEME_H

#include <windows.h>

typedef enum BellwinThemeMode {
    BELLWIN_THEME_LIGHT,
    BELLWIN_THEME_DARK,
    BELLWIN_THEME_HIGH_CONTRAST,
} BellwinThemeMode;

typedef struct BellwinThemePalette {
    COLORREF windowBackground;
    COLORREF cardBackground;
    COLORREF cardBorder;
    COLORREF primaryText;
    COLORREF secondaryText;
    COLORREF controlBackground;
    COLORREF controlBorder;
    COLORREF controlText;
    COLORREF divider;
    COLORREF accent;
    COLORREF accentText;
    COLORREF focus;
    COLORREF hoverBackground;
    COLORREF tooltipBackground;
    COLORREF tooltipBorder;
    COLORREF tooltipText;
    COLORREF stepperHoverFill;
    COLORREF stepperPressedFill;
    COLORREF controlGlyphSecondary;
    COLORREF controlGlyphHover;
    COLORREF controlGlyphPressed;
    COLORREF sliderTrackInactive;
    COLORREF sliderTrackActive;
    COLORREF sliderTrackActiveHover;
    COLORREF sliderTrackActivePressed;
    COLORREF sliderTick;
    COLORREF sliderThumbSurface;
    COLORREF sliderThumbBorder;
    COLORREF sliderThumbInner;
    COLORREF sliderThumbInnerHover;
    COLORREF sliderThumbInnerPressed;
    COLORREF toggleTrackOffFill;
    COLORREF toggleTrackOffFillHover;
    COLORREF toggleTrackOffFillPressed;
    COLORREF toggleTrackOffStroke;
    COLORREF toggleTrackOnFill;
    COLORREF toggleTrackOnFillHover;
    COLORREF toggleTrackOnFillPressed;
    COLORREF toggleThumbOff;
    COLORREF toggleThumbOn;
} BellwinThemePalette;

#if defined(BELLWIN_THEME_INTERNALS) || defined(BELLWIN_THEME_TESTING)
typedef struct BellwinSystemColors {
    COLORREF window;
    COLORREF windowText;
    COLORREF buttonFace;
    COLORREF buttonText;
    COLORREF highlight;
    COLORREF highlightText;
    COLORREF grayText;
} BellwinSystemColors;

typedef struct BellwinThemeInputs {
    int highContrast;
    int appLightThemeKnown;
    int appsUseLightTheme;
    int accentKnown;
    DWORD accentArgb;
    int windows11FrameSupported;
    BellwinSystemColors systemColors;
} BellwinThemeInputs;
#endif

typedef struct BellwinThemeState {
    BellwinThemeMode mode;
    BellwinThemePalette palette;
    int windows11FrameSupported;
} BellwinThemeState;

BellwinThemeState bellwin_query_theme(void);
int bellwin_theme_equal(const BellwinThemeState *left, const BellwinThemeState *right);
void bellwin_apply_window_frame(HWND window, const BellwinThemeState *theme);

#if defined(BELLWIN_THEME_INTERNALS) || defined(BELLWIN_THEME_TESTING)
BellwinThemeState bellwin_resolve_theme(const BellwinThemeInputs *inputs);
#endif

#endif
