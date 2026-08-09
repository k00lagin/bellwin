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
    assert(theme.palette.hoverBackground == RGB(229, 241, 251));
    assert(theme.palette.tooltipBackground == RGB(255, 255, 225));
    assert(theme.palette.tooltipBorder == RGB(118, 118, 118));
    assert(theme.palette.tooltipText == RGB(32, 32, 32));
}

static void test_stepper_tokens_follow_light_dark_and_high_contrast_palettes(void) {
    BellwinThemeState light = bellwin_resolve_theme(&(BellwinThemeInputs){0});
    assert(light.palette.stepperHoverFill == RGB(241, 241, 241));
    assert(light.palette.stepperPressedFill == RGB(231, 231, 231));
    assert(light.palette.controlGlyphSecondary == RGB(96, 96, 96));
    assert(light.palette.controlGlyphHover == RGB(96, 96, 96));
    assert(light.palette.controlGlyphPressed == RGB(32, 32, 32));

    BellwinThemeState dark = bellwin_resolve_theme(&(BellwinThemeInputs){
        .appLightThemeKnown = 1,
        .appsUseLightTheme = 0,
    });
    assert(dark.palette.stepperHoverFill == RGB(64, 64, 64));
    assert(dark.palette.stepperPressedFill == RGB(73, 73, 73));
    assert(dark.palette.controlGlyphSecondary == RGB(190, 190, 190));
    assert(dark.palette.controlGlyphHover == RGB(190, 190, 190));
    assert(dark.palette.controlGlyphPressed == RGB(242, 242, 242));

    BellwinThemeInputs highContrastInputs = {
        .highContrast = 1,
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
    BellwinThemeState highContrast = bellwin_resolve_theme(&highContrastInputs);
    assert(highContrast.palette.stepperHoverFill == highContrastInputs.systemColors.highlight);
    assert(highContrast.palette.stepperPressedFill == highContrastInputs.systemColors.buttonText);
    assert(highContrast.palette.controlGlyphSecondary == highContrastInputs.systemColors.grayText);
    assert(highContrast.palette.controlGlyphHover == highContrastInputs.systemColors.highlightText);
    assert(highContrast.palette.controlGlyphPressed == highContrastInputs.systemColors.buttonFace);
}

static void test_slider_tokens_follow_light_dark_and_high_contrast_palettes(void) {
    BellwinThemeState light = bellwin_resolve_theme(&(BellwinThemeInputs){0});
    assert(light.palette.sliderTrackInactive == RGB(142, 142, 142));
    assert(light.palette.sliderTrackActive == RGB(0, 120, 212));
    assert(light.palette.sliderTrackActiveHover == RGB(15, 128, 215));
    assert(light.palette.sliderTrackActivePressed == RGB(0, 107, 189));
    assert(light.palette.sliderTick == RGB(169, 169, 169));
    assert(light.palette.sliderThumbSurface == RGB(255, 255, 255));
    assert(light.palette.sliderThumbBorder == RGB(225, 225, 225));
    assert(light.palette.sliderThumbInner == light.palette.sliderTrackActive);
    assert(light.palette.sliderThumbInnerHover == light.palette.sliderTrackActiveHover);
    assert(light.palette.sliderThumbInnerPressed == light.palette.sliderTrackActivePressed);

    BellwinThemeState dark = bellwin_resolve_theme(&(BellwinThemeInputs){
        .appLightThemeKnown = 1,
        .appsUseLightTheme = 0,
    });
    assert(dark.palette.sliderTrackInactive == RGB(161, 161, 161));
    assert(dark.palette.sliderTick == RGB(119, 119, 119));
    assert(dark.palette.sliderThumbSurface == RGB(48, 48, 48));
    assert(dark.palette.sliderThumbBorder == RGB(68, 68, 68));
    assert(dark.palette.sliderThumbInner == RGB(0, 120, 212));
    assert(dark.palette.sliderThumbInnerHover == RGB(15, 128, 215));
    assert(dark.palette.sliderThumbInnerPressed == RGB(28, 135, 217));

    BellwinThemeInputs highContrastInputs = {
        .highContrast = 1,
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
    BellwinThemeState highContrast = bellwin_resolve_theme(&highContrastInputs);
    assert(highContrast.palette.sliderTrackInactive == highContrastInputs.systemColors.buttonText);
    assert(highContrast.palette.sliderTrackActive == highContrastInputs.systemColors.highlight);
    assert(highContrast.palette.sliderTrackActiveHover == highContrastInputs.systemColors.highlight);
    assert(highContrast.palette.sliderTrackActivePressed == highContrastInputs.systemColors.highlight);
    assert(highContrast.palette.sliderTick == highContrastInputs.systemColors.grayText);
    assert(highContrast.palette.sliderThumbSurface == highContrastInputs.systemColors.buttonFace);
    assert(highContrast.palette.sliderThumbBorder == highContrastInputs.systemColors.buttonText);
    assert(highContrast.palette.sliderThumbInner == highContrastInputs.systemColors.highlight);
    assert(highContrast.palette.sliderThumbInnerHover == highContrastInputs.systemColors.highlight);
    assert(highContrast.palette.sliderThumbInnerPressed == highContrastInputs.systemColors.highlight);
}

static void test_toggle_tokens_follow_light_dark_and_high_contrast_palettes(void) {
    BellwinThemeState light = bellwin_resolve_theme(&(BellwinThemeInputs){0});
    assert(light.palette.toggleTrackOffFill == RGB(237, 237, 237));
    assert(light.palette.toggleTrackOffFillHover == RGB(229, 229, 229));
    assert(light.palette.toggleTrackOffFillPressed == RGB(220, 220, 220));
    assert(light.palette.toggleTrackOffStroke == RGB(134, 134, 134));
    assert(light.palette.toggleTrackOnFill == RGB(0, 120, 212));
    assert(light.palette.toggleTrackOnFillHover == RGB(15, 128, 215));
    assert(light.palette.toggleTrackOnFillPressed == RGB(0, 107, 189));
    assert(light.palette.toggleThumbOff == light.palette.toggleTrackOffStroke);
    assert(light.palette.toggleThumbOn == RGB(255, 255, 255));

    BellwinThemeState dark = bellwin_resolve_theme(&(BellwinThemeInputs){
        .appLightThemeKnown = 1,
        .appsUseLightTheme = 0,
    });
    assert(dark.palette.toggleTrackOffFill == RGB(29, 29, 29));
    assert(dark.palette.toggleTrackOffFillHover == RGB(42, 42, 42));
    assert(dark.palette.toggleTrackOffFillPressed == RGB(48, 48, 48));
    assert(dark.palette.toggleTrackOffStroke == RGB(154, 154, 154));
    assert(dark.palette.toggleTrackOnFill == RGB(0, 120, 212));
    assert(dark.palette.toggleTrackOnFillHover == RGB(15, 128, 215));
    assert(dark.palette.toggleTrackOnFillPressed == RGB(28, 135, 217));
    assert(dark.palette.toggleThumbOff == dark.palette.toggleTrackOffStroke);
    assert(dark.palette.toggleThumbOn == RGB(255, 255, 255));

    BellwinThemeInputs highContrastInputs = {
        .highContrast = 1,
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
    BellwinThemeState highContrast = bellwin_resolve_theme(&highContrastInputs);
    assert(highContrast.palette.toggleTrackOffFill == highContrastInputs.systemColors.buttonFace);
    assert(highContrast.palette.toggleTrackOffFillHover == highContrastInputs.systemColors.buttonFace);
    assert(highContrast.palette.toggleTrackOffFillPressed == highContrastInputs.systemColors.buttonFace);
    assert(highContrast.palette.toggleTrackOffStroke == highContrastInputs.systemColors.buttonText);
    assert(highContrast.palette.toggleTrackOnFill == highContrastInputs.systemColors.highlight);
    assert(highContrast.palette.toggleTrackOnFillHover == highContrastInputs.systemColors.highlight);
    assert(highContrast.palette.toggleTrackOnFillPressed == highContrastInputs.systemColors.highlight);
    assert(highContrast.palette.toggleThumbOff == highContrastInputs.systemColors.buttonText);
    assert(highContrast.palette.toggleThumbOn == highContrastInputs.systemColors.highlightText);
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

static void test_dynamic_accent_updates_every_active_control_token(void) {
    BellwinThemeInputs inputs = {
        .accentKnown = 1,
        .accentArgb = 0xff197989,
    };
    BellwinThemeState theme = bellwin_resolve_theme(&inputs);

    COLORREF accent = RGB(25, 121, 137);
    COLORREF hover = RGB(39, 129, 144);
    COLORREF pressed = RGB(22, 108, 122);
    assert(theme.palette.sliderTrackActive == accent);
    assert(theme.palette.sliderTrackActiveHover == hover);
    assert(theme.palette.sliderTrackActivePressed == pressed);
    assert(theme.palette.sliderThumbInner == accent);
    assert(theme.palette.sliderThumbInnerHover == hover);
    assert(theme.palette.sliderThumbInnerPressed == pressed);
    assert(theme.palette.toggleTrackOnFill == accent);
    assert(theme.palette.toggleTrackOnFillHover == hover);
    assert(theme.palette.toggleTrackOnFillPressed == pressed);
    assert(theme.palette.toggleThumbOn == RGB(255, 255, 255));
}

static void test_dynamic_accent_uses_dark_theme_state_variants(void) {
    BellwinThemeInputs inputs = {
        .appLightThemeKnown = 1,
        .appsUseLightTheme = 0,
        .accentKnown = 1,
        .accentArgb = 0xff60cdff,
    };
    BellwinThemeState theme = bellwin_resolve_theme(&inputs);

    assert(theme.palette.sliderTrackActive == RGB(96, 205, 255));
    assert(theme.palette.sliderTrackActiveHover == RGB(105, 208, 255));
    assert(theme.palette.sliderTrackActivePressed == RGB(113, 210, 255));
    assert(theme.palette.toggleTrackOnFillPressed == RGB(113, 210, 255));
    assert(theme.palette.toggleThumbOn == RGB(0, 0, 0));
}

static void test_low_contrast_dynamic_accent_keeps_safe_active_control_tokens(void) {
    BellwinThemeInputs inputs = {
        .accentKnown = 1,
        .accentArgb = 0xfff3f3f3,
    };
    BellwinThemeState theme = bellwin_resolve_theme(&inputs);

    assert(theme.palette.accent == RGB(243, 243, 243));
    assert(theme.palette.sliderTrackActive == RGB(0, 120, 212));
    assert(theme.palette.sliderThumbInner == RGB(0, 120, 212));
    assert(theme.palette.toggleTrackOnFill == RGB(0, 120, 212));
}

static void test_accent_state_variants_do_not_drop_below_control_contrast(void) {
    BellwinThemeInputs inputs = {
        .accentKnown = 1,
        .accentArgb = 0xff888888,
    };
    BellwinThemeState theme = bellwin_resolve_theme(&inputs);

    assert(theme.palette.sliderTrackActive == RGB(136, 136, 136));
    assert(theme.palette.sliderTrackActiveHover == RGB(136, 136, 136));
    assert(theme.palette.toggleTrackOnFillHover == RGB(136, 136, 136));
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

static void test_theme_equality_compares_every_new_control_token(void) {
    BellwinThemeState left = bellwin_resolve_theme(&(BellwinThemeInputs){0});
    BellwinThemeState right;

#define ASSERT_TOKEN_IS_COMPARED(field) do { \
        right = left; \
        right.palette.field = left.palette.field == RGB(1, 2, 3) \
            ? RGB(4, 5, 6) \
            : RGB(1, 2, 3); \
        assert(!bellwin_theme_equal(&left, &right)); \
    } while (0)

    ASSERT_TOKEN_IS_COMPARED(stepperHoverFill);
    ASSERT_TOKEN_IS_COMPARED(stepperPressedFill);
    ASSERT_TOKEN_IS_COMPARED(controlGlyphSecondary);
    ASSERT_TOKEN_IS_COMPARED(controlGlyphHover);
    ASSERT_TOKEN_IS_COMPARED(controlGlyphPressed);
    ASSERT_TOKEN_IS_COMPARED(sliderTrackInactive);
    ASSERT_TOKEN_IS_COMPARED(sliderTrackActive);
    ASSERT_TOKEN_IS_COMPARED(sliderTrackActiveHover);
    ASSERT_TOKEN_IS_COMPARED(sliderTrackActivePressed);
    ASSERT_TOKEN_IS_COMPARED(sliderTick);
    ASSERT_TOKEN_IS_COMPARED(sliderThumbSurface);
    ASSERT_TOKEN_IS_COMPARED(sliderThumbBorder);
    ASSERT_TOKEN_IS_COMPARED(sliderThumbInner);
    ASSERT_TOKEN_IS_COMPARED(sliderThumbInnerHover);
    ASSERT_TOKEN_IS_COMPARED(sliderThumbInnerPressed);
    ASSERT_TOKEN_IS_COMPARED(toggleTrackOffFill);
    ASSERT_TOKEN_IS_COMPARED(toggleTrackOffFillHover);
    ASSERT_TOKEN_IS_COMPARED(toggleTrackOffFillPressed);
    ASSERT_TOKEN_IS_COMPARED(toggleTrackOffStroke);
    ASSERT_TOKEN_IS_COMPARED(toggleTrackOnFill);
    ASSERT_TOKEN_IS_COMPARED(toggleTrackOnFillHover);
    ASSERT_TOKEN_IS_COMPARED(toggleTrackOnFillPressed);
    ASSERT_TOKEN_IS_COMPARED(toggleThumbOff);
    ASSERT_TOKEN_IS_COMPARED(toggleThumbOn);

#undef ASSERT_TOKEN_IS_COMPARED
}

int main(void) {
    test_missing_system_inputs_use_light_fallback();
    test_stepper_tokens_follow_light_dark_and_high_contrast_palettes();
    test_slider_tokens_follow_light_dark_and_high_contrast_palettes();
    test_toggle_tokens_follow_light_dark_and_high_contrast_palettes();
    test_dwm_argb_is_converted_to_colorref();
    test_dynamic_accent_updates_every_active_control_token();
    test_dynamic_accent_uses_dark_theme_state_variants();
    test_low_contrast_dynamic_accent_keeps_safe_active_control_tokens();
    test_accent_state_variants_do_not_drop_below_control_contrast();
    test_known_app_preference_selects_dark_or_light();
    test_high_contrast_overrides_dark_and_accent();
    test_accent_text_uses_the_higher_contrast_color();
    test_low_contrast_accent_is_not_used_for_focus();
    test_contrasting_accent_is_used_for_focus();
    test_theme_equality_compares_mode_palette_and_capability();
    test_theme_equality_compares_every_new_control_token();
    puts("theme tests passed");
    return 0;
}
