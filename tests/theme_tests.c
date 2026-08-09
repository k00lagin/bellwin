#include <assert.h>
#include <stdio.h>

#include "../theme.h"

static void test_missing_system_inputs_use_light_fallback(void) {
    BellwinThemeInputs inputs = {0};
    BellwinThemeState theme = bellwin_resolve_theme(&inputs);

    assert(theme.mode == BELLWIN_THEME_LIGHT);
    assert(theme.palette.windowBackground == RGB(243, 243, 243));
    assert(theme.palette.cardBackground == RGB(251, 251, 251));
    assert(theme.palette.cardBorder == RGB(225, 225, 225));
    assert(theme.palette.primaryText == RGB(32, 32, 32));
    assert(theme.palette.secondaryText == RGB(96, 96, 96));
    assert(theme.palette.controlBackground == RGB(255, 255, 255));
    assert(theme.palette.controlBorder == RGB(194, 196, 200));
    assert(theme.palette.controlText == RGB(32, 32, 32));
    assert(theme.palette.divider == RGB(220, 220, 220));
    assert(theme.palette.accent == RGB(0, 120, 212));
    assert(theme.palette.accentText == RGB(0, 0, 0));
    assert(theme.palette.focus == RGB(0, 120, 212));
    assert(theme.palette.inactiveTrack == RGB(210, 213, 218));
    assert(theme.palette.tick == RGB(197, 200, 204));
    assert(theme.palette.toggleOff == RGB(145, 149, 154));
    assert(theme.palette.knob == RGB(255, 255, 255));
    assert(theme.palette.toggleOffKnob == RGB(255, 255, 255));
    assert(theme.palette.hoverBackground == RGB(229, 241, 251));
    assert(theme.palette.tooltipBackground == RGB(255, 255, 225));
    assert(theme.palette.tooltipBorder == RGB(118, 118, 118));
    assert(theme.palette.tooltipText == RGB(32, 32, 32));
}

static void test_dwm_argb_is_converted_to_colorref(void) {
    BellwinThemeInputs inputs = {
        .accentKnown = 1,
        .accentArgb = 0xff112233,
    };
    BellwinThemeState theme = bellwin_resolve_theme(&inputs);

    assert(GetRValue(theme.palette.accent) == 0x11);
    assert(GetGValue(theme.palette.accent) == 0x22);
    assert(GetBValue(theme.palette.accent) == 0x33);
}

static void test_known_app_preference_selects_dark_or_light(void) {
    BellwinThemeInputs inputs = {
        .appLightThemeKnown = 1,
        .appsUseLightTheme = 0,
    };
    BellwinThemeState dark = bellwin_resolve_theme(&inputs);
    assert(dark.mode == BELLWIN_THEME_DARK);
    assert(dark.palette.windowBackground == RGB(32, 32, 32));
    assert(dark.palette.primaryText == RGB(242, 242, 242));

    inputs.appsUseLightTheme = 1;
    assert(bellwin_resolve_theme(&inputs).mode == BELLWIN_THEME_LIGHT);

    inputs.appLightThemeKnown = 0;
    inputs.appsUseLightTheme = 0;
    assert(bellwin_resolve_theme(&inputs).mode == BELLWIN_THEME_LIGHT);
}

static void test_high_contrast_overrides_dark_and_accent(void) {
    BellwinThemeInputs inputs = {
        .highContrast = 1,
        .appLightThemeKnown = 1,
        .appsUseLightTheme = 0,
        .accentKnown = 1,
        .accentArgb = 0xff112233,
        .systemColors = {
            .window = RGB(1, 2, 3),
            .windowText = RGB(4, 5, 6),
            .buttonFace = RGB(7, 8, 9),
            .buttonText = RGB(10, 11, 12),
            .highlight = RGB(13, 14, 15),
            .highlightText = RGB(16, 17, 18),
            .grayText = RGB(19, 20, 21),
        },
    };
    BellwinThemeState theme = bellwin_resolve_theme(&inputs);

    assert(theme.mode == BELLWIN_THEME_HIGH_CONTRAST);
    assert(theme.palette.windowBackground == inputs.systemColors.window);
    assert(theme.palette.primaryText == inputs.systemColors.windowText);
    assert(theme.palette.controlBackground == inputs.systemColors.buttonFace);
    assert(theme.palette.controlBorder == inputs.systemColors.buttonText);
    assert(theme.palette.controlText == inputs.systemColors.buttonText);
    assert(theme.palette.accent == inputs.systemColors.highlight);
    assert(theme.palette.accentText == inputs.systemColors.highlightText);
    assert(theme.palette.secondaryText == inputs.systemColors.windowText);
    assert(theme.palette.tick == inputs.systemColors.grayText);
    assert(theme.palette.toggleOffKnob == inputs.systemColors.buttonFace);
}

static void test_accent_text_uses_the_higher_contrast_color(void) {
    BellwinThemeInputs inputs = {
        .accentKnown = 1,
        .accentArgb = 0xffffff00,
    };
    assert(bellwin_resolve_theme(&inputs).palette.accentText == RGB(0, 0, 0));

    inputs.accentArgb = 0xff000050;
    assert(bellwin_resolve_theme(&inputs).palette.accentText == RGB(255, 255, 255));
}

static void test_low_contrast_accent_is_not_used_for_focus(void) {
    BellwinThemeInputs inputs = {
        .accentKnown = 1,
        .accentArgb = 0xfff3f3f3,
    };
    BellwinThemeState theme = bellwin_resolve_theme(&inputs);

    assert(theme.palette.accent == RGB(243, 243, 243));
    assert(theme.palette.focus == RGB(0, 95, 184));
}

static void test_contrasting_accent_is_used_for_focus(void) {
    BellwinThemeInputs inputs = {
        .accentKnown = 1,
        .accentArgb = 0xff000000,
    };
    BellwinThemeState theme = bellwin_resolve_theme(&inputs);

    assert(theme.palette.focus == RGB(0, 0, 0));
}

static void test_theme_equality_compares_mode_palette_and_capability(void) {
    BellwinThemeInputs inputs = {0};
    BellwinThemeState left = bellwin_resolve_theme(&inputs);
    BellwinThemeState right = bellwin_resolve_theme(&inputs);
    assert(bellwin_theme_equal(&left, &right));

    right.palette.tooltipText = RGB(1, 2, 3);
    assert(!bellwin_theme_equal(&left, &right));
    right = left;
    right.mode = BELLWIN_THEME_DARK;
    assert(!bellwin_theme_equal(&left, &right));
    right = left;
    right.windows11FrameSupported = 1;
    assert(!bellwin_theme_equal(&left, &right));
}

int main(void) {
    test_missing_system_inputs_use_light_fallback();
    test_dwm_argb_is_converted_to_colorref();
    test_known_app_preference_selects_dark_or_light();
    test_high_contrast_overrides_dark_and_accent();
    test_accent_text_uses_the_higher_contrast_color();
    test_low_contrast_accent_is_not_used_for_focus();
    test_contrasting_accent_is_used_for_focus();
    test_theme_equality_compares_mode_palette_and_capability();
    puts("theme tests passed");
    return 0;
}
