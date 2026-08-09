#define BELLWIN_THEME_INTERNALS
#include "theme.h"

#include <dwmapi.h>
#include <math.h>

typedef LONG (WINAPI *BellwinRtlGetVersionFn)(OSVERSIONINFOW *);

typedef struct BellwinWindowsCapabilities {
    int windows10OrNewer;
    int windows11FrameSupported;
} BellwinWindowsCapabilities;

/* Documented for Windows 11 build 22000; local values keep the Windows 7 SDK floor. */
enum BellwinDwmAttribute {
    BELLWIN_DWMWA_USE_IMMERSIVE_DARK_MODE = 20,
    BELLWIN_DWMWA_WINDOW_CORNER_PREFERENCE = 33,
};

static double bellwin_linear_channel(BYTE channel) {
    double value = channel / 255.0;
    if (value <= 0.04045) return value / 12.92;
    return pow((value + 0.055) / 1.055, 2.4);
}

static double bellwin_relative_luminance(COLORREF color) {
    return 0.2126 * bellwin_linear_channel(GetRValue(color))
        + 0.7152 * bellwin_linear_channel(GetGValue(color))
        + 0.0722 * bellwin_linear_channel(GetBValue(color));
}

static double bellwin_contrast_ratio(COLORREF left, COLORREF right) {
    double leftLuminance = bellwin_relative_luminance(left);
    double rightLuminance = bellwin_relative_luminance(right);
    double lighter = leftLuminance > rightLuminance ? leftLuminance : rightLuminance;
    double darker = leftLuminance > rightLuminance ? rightLuminance : leftLuminance;
    return (lighter + 0.05) / (darker + 0.05);
}

static COLORREF bellwin_contrasting_text(COLORREF background) {
    COLORREF black = RGB(0, 0, 0);
    COLORREF white = RGB(255, 255, 255);
    return bellwin_contrast_ratio(background, black) >= bellwin_contrast_ratio(background, white)
        ? black
        : white;
}

static COLORREF bellwin_blend_color(COLORREF base, COLORREF overlay, BYTE overlayAlpha) {
    unsigned int baseWeight = 255U - overlayAlpha;
    BYTE red = (BYTE)((GetRValue(base) * baseWeight
        + GetRValue(overlay) * overlayAlpha + 127U) / 255U);
    BYTE green = (BYTE)((GetGValue(base) * baseWeight
        + GetGValue(overlay) * overlayAlpha + 127U) / 255U);
    BYTE blue = (BYTE)((GetBValue(base) * baseWeight
        + GetBValue(overlay) * overlayAlpha + 127U) / 255U);
    return RGB(red, green, blue);
}

static int bellwin_has_control_contrast(
    const BellwinThemePalette *palette,
    COLORREF color
) {
    return bellwin_contrast_ratio(color, palette->windowBackground) >= 3.0
        && bellwin_contrast_ratio(color, palette->cardBackground) >= 3.0;
}

static COLORREF bellwin_accent_foreground(COLORREF accent) {
    COLORREF white = RGB(255, 255, 255);
    return bellwin_contrast_ratio(accent, white) >= 3.0
        ? white
        : RGB(0, 0, 0);
}

static void bellwin_apply_accent_control_tokens(BellwinThemePalette *palette) {
    COLORREF active = bellwin_has_control_contrast(palette, palette->accent)
        ? palette->accent
        : RGB(0, 120, 212);
    COLORREF hover = bellwin_blend_color(active, RGB(255, 255, 255), 15);
    COLORREF pressed = bellwin_relative_luminance(palette->windowBackground) < 0.5
        ? bellwin_blend_color(active, RGB(255, 255, 255), 28)
        : bellwin_blend_color(active, RGB(0, 0, 0), 28);
    if (!bellwin_has_control_contrast(palette, hover)) hover = active;
    if (!bellwin_has_control_contrast(palette, pressed)) pressed = active;

    palette->sliderTrackActive = active;
    palette->sliderTrackActiveHover = hover;
    palette->sliderTrackActivePressed = pressed;
    palette->sliderThumbInner = active;
    palette->sliderThumbInnerHover = hover;
    palette->sliderThumbInnerPressed = pressed;
    palette->toggleTrackOnFill = active;
    palette->toggleTrackOnFillHover = hover;
    palette->toggleTrackOnFillPressed = pressed;
    palette->toggleThumbOn = bellwin_accent_foreground(active);
}

static BellwinThemePalette bellwin_dark_palette(void) {
    BellwinThemePalette palette = {
        .windowBackground = RGB(32, 32, 32),
        .cardBackground = RGB(39, 39, 39),
        .cardBorder = RGB(68, 68, 68),
        .primaryText = RGB(242, 242, 242),
        .secondaryText = RGB(190, 190, 190),
        .controlBackground = RGB(48, 48, 48),
        .controlBorder = RGB(100, 100, 100),
        .controlText = RGB(242, 242, 242),
        .divider = RGB(72, 72, 72),
        .accent = RGB(0, 120, 212),
        .accentText = RGB(255, 255, 255),
        .focus = RGB(96, 205, 255),
        .hoverBackground = RGB(58, 58, 58),
        .tooltipBackground = RGB(55, 55, 55),
        .tooltipBorder = RGB(130, 130, 130),
        .tooltipText = RGB(242, 242, 242),
        .stepperHoverFill = RGB(64, 64, 64),
        .stepperPressedFill = RGB(73, 73, 73),
        .controlGlyphSecondary = RGB(190, 190, 190),
        .controlGlyphHover = RGB(190, 190, 190),
        .controlGlyphPressed = RGB(242, 242, 242),
        .sliderTrackInactive = RGB(161, 161, 161),
        .sliderTrackActive = RGB(0, 120, 212),
        .sliderTrackActiveHover = RGB(15, 128, 215),
        .sliderTrackActivePressed = RGB(28, 135, 217),
        .sliderTick = RGB(119, 119, 119),
        .sliderThumbSurface = RGB(48, 48, 48),
        .sliderThumbBorder = RGB(68, 68, 68),
        .sliderThumbInner = RGB(0, 120, 212),
        .sliderThumbInnerHover = RGB(15, 128, 215),
        .sliderThumbInnerPressed = RGB(28, 135, 217),
        .toggleTrackOffFill = RGB(29, 29, 29),
        .toggleTrackOffFillHover = RGB(42, 42, 42),
        .toggleTrackOffFillPressed = RGB(48, 48, 48),
        .toggleTrackOffStroke = RGB(154, 154, 154),
        .toggleTrackOnFill = RGB(0, 120, 212),
        .toggleTrackOnFillHover = RGB(15, 128, 215),
        .toggleTrackOnFillPressed = RGB(28, 135, 217),
        .toggleThumbOff = RGB(154, 154, 154),
        .toggleThumbOn = RGB(255, 255, 255),
    };
    return palette;
}

static BellwinThemePalette bellwin_high_contrast_palette(const BellwinSystemColors *colors) {
    BellwinThemePalette palette = {
        .windowBackground = colors->window,
        .cardBackground = colors->window,
        .cardBorder = colors->windowText,
        .primaryText = colors->windowText,
        .secondaryText = colors->windowText,
        .controlBackground = colors->buttonFace,
        .controlBorder = colors->buttonText,
        .controlText = colors->buttonText,
        .divider = colors->windowText,
        .accent = colors->highlight,
        .accentText = colors->highlightText,
        .focus = colors->highlight,
        .hoverBackground = colors->buttonFace,
        .tooltipBackground = colors->window,
        .tooltipBorder = colors->windowText,
        .tooltipText = colors->windowText,
        .stepperHoverFill = colors->highlight,
        .stepperPressedFill = colors->buttonText,
        .controlGlyphSecondary = colors->grayText,
        .controlGlyphHover = colors->highlightText,
        .controlGlyphPressed = colors->buttonFace,
        .sliderTrackInactive = colors->buttonText,
        .sliderTrackActive = colors->highlight,
        .sliderTrackActiveHover = colors->highlight,
        .sliderTrackActivePressed = colors->highlight,
        .sliderTick = colors->grayText,
        .sliderThumbSurface = colors->buttonFace,
        .sliderThumbBorder = colors->buttonText,
        .sliderThumbInner = colors->highlight,
        .sliderThumbInnerHover = colors->highlight,
        .sliderThumbInnerPressed = colors->highlight,
        .toggleTrackOffFill = colors->buttonFace,
        .toggleTrackOffFillHover = colors->buttonFace,
        .toggleTrackOffFillPressed = colors->buttonFace,
        .toggleTrackOffStroke = colors->buttonText,
        .toggleTrackOnFill = colors->highlight,
        .toggleTrackOnFillHover = colors->highlight,
        .toggleTrackOnFillPressed = colors->highlight,
        .toggleThumbOff = colors->buttonText,
        .toggleThumbOn = colors->highlightText,
    };
    return palette;
}

BellwinThemeState bellwin_resolve_theme(const BellwinThemeInputs *inputs) {
    BellwinThemeState theme = {
        .mode = BELLWIN_THEME_LIGHT,
        .palette = {
            .windowBackground = RGB(243, 243, 243),
            .cardBackground = RGB(251, 251, 251),
            .cardBorder = RGB(225, 225, 225),
            .primaryText = RGB(32, 32, 32),
            .secondaryText = RGB(96, 96, 96),
            .controlBackground = RGB(255, 255, 255),
            .controlBorder = RGB(194, 196, 200),
            .controlText = RGB(32, 32, 32),
            .divider = RGB(220, 220, 220),
            .accent = RGB(0, 120, 212),
            .accentText = RGB(255, 255, 255),
            .focus = RGB(0, 95, 184),
            .hoverBackground = RGB(229, 241, 251),
            .tooltipBackground = RGB(255, 255, 225),
            .tooltipBorder = RGB(118, 118, 118),
            .tooltipText = RGB(32, 32, 32),
            .stepperHoverFill = RGB(241, 241, 241),
            .stepperPressedFill = RGB(231, 231, 231),
            .controlGlyphSecondary = RGB(96, 96, 96),
            .controlGlyphHover = RGB(96, 96, 96),
            .controlGlyphPressed = RGB(32, 32, 32),
            .sliderTrackInactive = RGB(142, 142, 142),
            .sliderTrackActive = RGB(0, 120, 212),
            .sliderTrackActiveHover = RGB(15, 128, 215),
            .sliderTrackActivePressed = RGB(0, 107, 189),
            .sliderTick = RGB(169, 169, 169),
            .sliderThumbSurface = RGB(255, 255, 255),
            .sliderThumbBorder = RGB(225, 225, 225),
            .sliderThumbInner = RGB(0, 120, 212),
            .sliderThumbInnerHover = RGB(15, 128, 215),
            .sliderThumbInnerPressed = RGB(0, 107, 189),
            .toggleTrackOffFill = RGB(237, 237, 237),
            .toggleTrackOffFillHover = RGB(229, 229, 229),
            .toggleTrackOffFillPressed = RGB(220, 220, 220),
            .toggleTrackOffStroke = RGB(134, 134, 134),
            .toggleTrackOnFill = RGB(0, 120, 212),
            .toggleTrackOnFillHover = RGB(15, 128, 215),
            .toggleTrackOnFillPressed = RGB(0, 107, 189),
            .toggleThumbOff = RGB(134, 134, 134),
            .toggleThumbOn = RGB(255, 255, 255),
        },
        .windows11FrameSupported = inputs->windows11FrameSupported,
    };
    if (inputs->appLightThemeKnown && !inputs->appsUseLightTheme) {
        theme.mode = BELLWIN_THEME_DARK;
        theme.palette = bellwin_dark_palette();
    }
    if (inputs->highContrast) {
        theme.mode = BELLWIN_THEME_HIGH_CONTRAST;
        theme.palette = bellwin_high_contrast_palette(&inputs->systemColors);
        return theme;
    }
    if (inputs->accentKnown) {
        theme.palette.accent = RGB(
            (inputs->accentArgb >> 16) & 0xff,
            (inputs->accentArgb >> 8) & 0xff,
            inputs->accentArgb & 0xff
        );
    }
    theme.palette.accentText = bellwin_contrasting_text(theme.palette.accent);
    bellwin_apply_accent_control_tokens(&theme.palette);
    if (bellwin_contrast_ratio(theme.palette.accent, theme.palette.windowBackground) >= 3.0
        && bellwin_contrast_ratio(theme.palette.accent, theme.palette.cardBackground) >= 3.0) {
        theme.palette.focus = theme.palette.accent;
    }
    return theme;
}

static int bellwin_palette_equal(
    const BellwinThemePalette *left,
    const BellwinThemePalette *right
) {
    return left->windowBackground == right->windowBackground
        && left->cardBackground == right->cardBackground
        && left->cardBorder == right->cardBorder
        && left->primaryText == right->primaryText
        && left->secondaryText == right->secondaryText
        && left->controlBackground == right->controlBackground
        && left->controlBorder == right->controlBorder
        && left->controlText == right->controlText
        && left->divider == right->divider
        && left->accent == right->accent
        && left->accentText == right->accentText
        && left->focus == right->focus
        && left->hoverBackground == right->hoverBackground
        && left->tooltipBackground == right->tooltipBackground
        && left->tooltipBorder == right->tooltipBorder
        && left->tooltipText == right->tooltipText
        && left->stepperHoverFill == right->stepperHoverFill
        && left->stepperPressedFill == right->stepperPressedFill
        && left->controlGlyphSecondary == right->controlGlyphSecondary
        && left->controlGlyphHover == right->controlGlyphHover
        && left->controlGlyphPressed == right->controlGlyphPressed
        && left->sliderTrackInactive == right->sliderTrackInactive
        && left->sliderTrackActive == right->sliderTrackActive
        && left->sliderTrackActiveHover == right->sliderTrackActiveHover
        && left->sliderTrackActivePressed == right->sliderTrackActivePressed
        && left->sliderTick == right->sliderTick
        && left->sliderThumbSurface == right->sliderThumbSurface
        && left->sliderThumbBorder == right->sliderThumbBorder
        && left->sliderThumbInner == right->sliderThumbInner
        && left->sliderThumbInnerHover == right->sliderThumbInnerHover
        && left->sliderThumbInnerPressed == right->sliderThumbInnerPressed
        && left->toggleTrackOffFill == right->toggleTrackOffFill
        && left->toggleTrackOffFillHover == right->toggleTrackOffFillHover
        && left->toggleTrackOffFillPressed == right->toggleTrackOffFillPressed
        && left->toggleTrackOffStroke == right->toggleTrackOffStroke
        && left->toggleTrackOnFill == right->toggleTrackOnFill
        && left->toggleTrackOnFillHover == right->toggleTrackOnFillHover
        && left->toggleTrackOnFillPressed == right->toggleTrackOnFillPressed
        && left->toggleThumbOff == right->toggleThumbOff
        && left->toggleThumbOn == right->toggleThumbOn;
}

int bellwin_theme_equal(const BellwinThemeState *left, const BellwinThemeState *right) {
    return left->mode == right->mode
        && left->windows11FrameSupported == right->windows11FrameSupported
        && bellwin_palette_equal(&left->palette, &right->palette);
}

static BellwinWindowsCapabilities bellwin_query_windows_capabilities(void) {
    BellwinWindowsCapabilities capabilities = {0};
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return capabilities;

    BellwinRtlGetVersionFn rtlGetVersion =
        (BellwinRtlGetVersionFn)(void *)GetProcAddress(ntdll, "RtlGetVersion");
    if (!rtlGetVersion) return capabilities;

    OSVERSIONINFOW version = {0};
    version.dwOSVersionInfoSize = sizeof(version);
    if (rtlGetVersion(&version) != 0) return capabilities;
    capabilities.windows10OrNewer = version.dwMajorVersion >= 10;
    capabilities.windows11FrameSupported = version.dwMajorVersion > 10
        || (version.dwMajorVersion == 10 && version.dwBuildNumber >= 22000);
    return capabilities;
}

BellwinThemeState bellwin_query_theme(void) {
    BellwinWindowsCapabilities capabilities = bellwin_query_windows_capabilities();
    BellwinThemeInputs inputs = {
        .windows11FrameSupported = capabilities.windows11FrameSupported,
    };

    HIGHCONTRASTW highContrast = {0};
    highContrast.cbSize = sizeof(highContrast);
    if (SystemParametersInfoW(
            SPI_GETHIGHCONTRAST,
            sizeof(highContrast),
            &highContrast,
            0
        ) && (highContrast.dwFlags & HCF_HIGHCONTRASTON)) {
        inputs.highContrast = 1;
        inputs.systemColors.window = GetSysColor(COLOR_WINDOW);
        inputs.systemColors.windowText = GetSysColor(COLOR_WINDOWTEXT);
        inputs.systemColors.buttonFace = GetSysColor(COLOR_BTNFACE);
        inputs.systemColors.buttonText = GetSysColor(COLOR_BTNTEXT);
        inputs.systemColors.highlight = GetSysColor(COLOR_HIGHLIGHT);
        inputs.systemColors.highlightText = GetSysColor(COLOR_HIGHLIGHTTEXT);
        inputs.systemColors.grayText = GetSysColor(COLOR_GRAYTEXT);
        return bellwin_resolve_theme(&inputs);
    }

    if (capabilities.windows10OrNewer) {
        DWORD appsUseLightTheme = 1;
        DWORD valueSize = sizeof(appsUseLightTheme);
        LSTATUS registryStatus = RegGetValueW(
            HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            L"AppsUseLightTheme",
            RRF_RT_REG_DWORD,
            NULL,
            &appsUseLightTheme,
            &valueSize
        );
        if (registryStatus == ERROR_SUCCESS
            && valueSize == sizeof(appsUseLightTheme)
            && (appsUseLightTheme == 0 || appsUseLightTheme == 1)) {
            inputs.appLightThemeKnown = 1;
            inputs.appsUseLightTheme = (int)appsUseLightTheme;
        }
    }

    DWORD accentArgb = 0;
    BOOL opaqueBlend = FALSE;
    if (SUCCEEDED(DwmGetColorizationColor(&accentArgb, &opaqueBlend))) {
        inputs.accentKnown = 1;
        inputs.accentArgb = accentArgb;
    }
    return bellwin_resolve_theme(&inputs);
}

void bellwin_apply_window_frame(HWND window, const BellwinThemeState *theme) {
    if (!window || !theme->windows11FrameSupported) return;

    BOOL dark = theme->mode == BELLWIN_THEME_DARK;
    int cornerPreference = 2;
    HRESULT darkResult = DwmSetWindowAttribute(
        window,
        BELLWIN_DWMWA_USE_IMMERSIVE_DARK_MODE,
        &dark,
        sizeof(dark)
    );
    if (FAILED(darkResult)) {
        /* Cosmetic failure: keep the system-default frame. */
    }
    HRESULT cornerResult = DwmSetWindowAttribute(
        window,
        BELLWIN_DWMWA_WINDOW_CORNER_PREFERENCE,
        &cornerPreference,
        sizeof(cornerPreference)
    );
    if (FAILED(cornerResult)) {
        /* Cosmetic failure: keep the system-default corner preference. */
    }
}
