#include <windows.h>
#include <wchar.h>

#include "app_internal.h"
#include "render_gdi.h"
#include "rendering.h"
#include "ui.h"

#define px app_px
#define ui_scale app_ui_scale
#define rect_from_box app_rect_from_box

static void fill_rect_color(HDC dc, const RECT *rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, rect, brush);
    DeleteObject(brush);
}

static COLORREF interpolate_color(COLORREF from, COLORREF to, float progress) {
    progress = bellwin_ui_clamp01(progress);
    int red = (int)(GetRValue(from)
        + (GetRValue(to) - GetRValue(from)) * progress + 0.5f);
    int green = (int)(GetGValue(from)
        + (GetGValue(to) - GetGValue(from)) * progress + 0.5f);
    int blue = (int)(GetBValue(from)
        + (GetBValue(to) - GetBValue(from)) * progress + 0.5f);
    return RGB(red, green, blue);
}

static float ui_motion_value(ControlId control, UiVisualPart part, int pressed) {
    int slot = ui_motion_slot_for(control, part);
    if (slot < 0 || slot >= UI_MOTION_SLOT_COUNT) return 0.0f;
    return pressed
        ? g_app.partMotions[slot].pressed.value
        : g_app.partMotions[slot].hover.value;
}

/* radius is in physical pixels */
static void rounded_rect(HDC dc, const RECT *rect, int radius, COLORREF fill, COLORREF border) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, rect->left, rect->top, rect->right, rect->bottom, radius, radius);
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
            BellwinShapeCoverage coverage = bellwin_circle_coverage(
                x, y, centerX, centerY, radius, borderWidth
            );
            if (coverage.fill == 0 && coverage.border == 0) continue;

            blend_coverage_pixel(
                surface, x, y, fill, border, coverage.fill, coverage.border
            );
        }
    }
}

static void draw_antialiased_rounded_rect(
    PixelSurface *surface,
    const RECT *rect,
    int radius,
    int borderWidth,
    COLORREF fill,
    COLORREF border
) {
    if (!surface->pixels || rect->right <= rect->left || rect->bottom <= rect->top) return;

    int left = rect->left < 0 ? 0 : rect->left;
    int top = rect->top < 0 ? 0 : rect->top;
    int right = rect->right > surface->width ? surface->width : rect->right;
    int bottom = rect->bottom > surface->height ? surface->height : rect->bottom;
    GdiFlush();
    for (int y = top; y < bottom; ++y) {
        for (int x = left; x < right; ++x) {
            BellwinShapeCoverage coverage = bellwin_rounded_rect_coverage(
                x,
                y,
                rect->left,
                rect->top,
                rect->right,
                rect->bottom,
                radius,
                borderWidth
            );
            if (coverage.fill == 0 && coverage.border == 0) continue;
            blend_coverage_pixel(
                surface,
                x,
                y,
                fill,
                border,
                coverage.fill,
                coverage.border
            );
        }
    }
}

static void draw_clipped_antialiased_rounded_fill(
    PixelSurface *surface,
    const RECT *outer,
    const RECT *clip,
    int radius,
    COLORREF color
) {
    if (!surface->pixels || clip->right <= clip->left || clip->bottom <= clip->top) return;

    int left = clip->left < 0 ? 0 : clip->left;
    int top = clip->top < 0 ? 0 : clip->top;
    int right = clip->right > surface->width ? surface->width : clip->right;
    int bottom = clip->bottom > surface->height ? surface->height : clip->bottom;
    GdiFlush();
    for (int y = top; y < bottom; ++y) {
        for (int x = left; x < right; ++x) {
            BellwinShapeCoverage coverage = bellwin_rounded_rect_coverage(
                x,
                y,
                outer->left,
                outer->top,
                outer->right,
                outer->bottom,
                radius,
                0
            );
            if (coverage.fill == 0) continue;
            blend_coverage_pixel(surface, x, y, color, color, coverage.fill, 0);
        }
    }
}

static void draw_antialiased_horizontal_capsule_subpixels(
    PixelSurface *surface,
    const BellwinSubpixelRect *rect,
    COLORREF color
) {
    if (!surface->pixels || rect->right <= rect->left || rect->bottom <= rect->top) return;

    const int scale = BELLWIN_AA_SUBPIXEL_SCALE;
    int clippedLeft = rect->left < 0 ? 0 : rect->left;
    int clippedTop = rect->top < 0 ? 0 : rect->top;
    int clippedRight = rect->right > surface->width * scale
        ? surface->width * scale
        : rect->right;
    int clippedBottom = rect->bottom > surface->height * scale
        ? surface->height * scale
        : rect->bottom;
    int left = clippedLeft / scale;
    int top = clippedTop / scale;
    int right = (clippedRight + scale - 1) / scale;
    int bottom = (clippedBottom + scale - 1) / scale;

    GdiFlush();
    for (int y = top; y < bottom; ++y) {
        for (int x = left; x < right; ++x) {
            unsigned coverage = bellwin_horizontal_capsule_subpixel_coverage(
                x, y, rect->left, rect->top, rect->right, rect->bottom
            );
            if (coverage == 0) continue;
            blend_coverage_pixel(surface, x, y, color, color, coverage, 0);
        }
    }
}

static void draw_antialiased_horizontal_capsule(
    PixelSurface *surface,
    const RECT *rect,
    COLORREF color
) {
    const int scale = BELLWIN_AA_SUBPIXEL_SCALE;
    BellwinSubpixelRect subpixelRect = {
        rect->left * scale,
        rect->top * scale,
        rect->right * scale,
        rect->bottom * scale,
    };
    draw_antialiased_horizontal_capsule_subpixels(surface, &subpixelRect, color);
}

static void draw_antialiased_rounded_outline(
    PixelSurface *surface,
    const RECT *rect,
    int radius,
    int borderWidth,
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
            BellwinShapeCoverage coverage = bellwin_rounded_rect_coverage(
                x,
                y,
                rect->left,
                rect->top,
                rect->right,
                rect->bottom,
                radius,
                borderWidth
            );
            if (coverage.border == 0) continue;
            blend_coverage_pixel(surface, x, y, color, color, 0, coverage.border);
        }
    }
}

static void render_slider_custom(PixelSurface *surface, HDC dc, Clay_BoundingBox box, ControlId control) {
    const Widget *widget = widget_by_id(control);
    if (!widget || widget->maximum <= widget->minimum) return;

    const BellwinThemePalette *palette = &g_app.theme.palette;
    float fraction = (float)(*widget->value - widget->minimum)
        / (float)(widget->maximum - widget->minimum);
    int left = (int)(box.x + 0.5f);
    int right = (int)(box.x + box.width + 0.5f);
    int centerY = (int)(box.y + box.height / 2.0f + 0.5f);
    int position = left + (int)(fraction * box.width + 0.5f);
    int railHeight = px(4);
    if (railHeight < 2) railHeight = 2;
    int railTop = centerY - railHeight / 2;
    int railBottom = railTop + railHeight;

    float hover = ui_motion_value(control, UI_PART_SLIDER, 0);
    float pressed = ui_motion_value(control, UI_PART_SLIDER, 1);
    COLORREF activeColor = interpolate_color(
        palette->sliderTrackActive,
        palette->sliderTrackActiveHover,
        hover
    );
    activeColor = interpolate_color(
        activeColor,
        palette->sliderTrackActivePressed,
        pressed
    );

    /* Outside ticks are drawn first so they remain physically separate from
       the rail. Their pen is one physical device pixel at every DPI. */
    if (widget->ticks > 1) {
        HPEN tickPen = CreatePen(PS_SOLID, 1, palette->sliderTick);
        HGDIOBJ oldPen = SelectObject(dc, tickPen);
        int gap = px(4);
        int length = px(4);
        for (int i = 0; i < widget->ticks; ++i) {
            int x = left + MulDiv(i, right - left, widget->ticks - 1);
            MoveToEx(dc, x, railTop - gap - length, NULL);
            LineTo(dc, x, railTop - gap);
            MoveToEx(dc, x, railBottom + gap, NULL);
            LineTo(dc, x, railBottom + gap + length);
        }
        SelectObject(dc, oldPen);
        DeleteObject(tickPen);
    }

    RECT inactive = {left, railTop, right, railBottom};
    draw_antialiased_horizontal_capsule(surface, &inactive, palette->sliderTrackInactive);
    if (position > left) {
        RECT active = {left, railTop, position, railBottom};
        draw_antialiased_horizontal_capsule(surface, &active, activeColor);
    }

    int outerRadius = px(11);
    int outerBorder = px(1);
    if (outerBorder < 1) outerBorder = 1;
    draw_antialiased_circle(
        surface,
        position,
        centerY,
        outerRadius,
        outerBorder,
        palette->sliderThumbSurface,
        palette->sliderThumbBorder
    );

    float innerDiameter = bellwin_ui_lerp(12.0f, 14.0f, hover);
    innerDiameter = bellwin_ui_lerp(innerDiameter, 10.0f, pressed);
    int innerRadius = (int)(innerDiameter * ui_scale() * 0.5f + 0.5f);
    if (innerRadius < 1) innerRadius = 1;
    COLORREF innerColor = interpolate_color(
        palette->sliderThumbInner,
        palette->sliderThumbInnerHover,
        hover
    );
    innerColor = interpolate_color(
        innerColor,
        palette->sliderThumbInnerPressed,
        pressed
    );
    draw_antialiased_circle(
        surface,
        position,
        centerY,
        innerRadius,
        0,
        innerColor,
        innerColor
    );
}

static void draw_chevron(PixelSurface *surface, const POINT points[3], COLORREF color) {
    if (!surface->pixels) return;
    int width = px(1);
    if (width < 1) width = 1;
    int left = points[0].x;
    int right = points[0].x;
    int top = points[0].y;
    int bottom = points[0].y;
    for (int i = 1; i < 3; ++i) {
        if (points[i].x < left) left = points[i].x;
        if (points[i].x > right) right = points[i].x;
        if (points[i].y < top) top = points[i].y;
        if (points[i].y > bottom) bottom = points[i].y;
    }
    left -= width;
    top -= width;
    right += width + 1;
    bottom += width + 1;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > surface->width) right = surface->width;
    if (bottom > surface->height) bottom = surface->height;

    GdiFlush();
    for (int y = top; y < bottom; ++y) {
        for (int x = left; x < right; ++x) {
            unsigned first = bellwin_line_coverage(
                x, y, points[0].x, points[0].y, points[1].x, points[1].y, width
            );
            unsigned second = bellwin_line_coverage(
                x, y, points[1].x, points[1].y, points[2].x, points[2].y, width
            );
            unsigned coverage = first > second ? first : second;
            if (coverage == 0) continue;
            blend_coverage_pixel(surface, x, y, color, color, coverage, 0);
        }
    }
}

static void render_time_box_custom(
    PixelSurface *surface,
    HDC dc,
    Clay_BoundingBox box,
    ControlId control
) {
    const Widget *widget = widget_by_id(control);
    if (!widget) return;
    const BellwinThemePalette *palette = &g_app.theme.palette;
    int minuteOfDay = *widget->value;
    int focused = control_has_focus(control);
    float scale = ui_scale();
    BellwinTimeBoxMetrics metrics = bellwin_time_box_metrics(box, scale);

    RECT boxRect = rect_from_box(box);
    rounded_rect(dc, &boxRect, px(3), palette->controlBackground, palette->controlBorder);

    UiVisualPart stepperParts[2] = {UI_PART_TIME_UP, UI_PART_TIME_DOWN};
    Clay_BoundingBox stepperBoxes[2] = {metrics.stepperUp, metrics.stepperDown};
    for (int i = 0; i < 2; ++i) {
        float hover = ui_motion_value(control, stepperParts[i], 0);
        float pressed = ui_motion_value(control, stepperParts[i], 1);
        if (hover <= 0.0f && pressed <= 0.0f) continue;
        COLORREF fill = interpolate_color(
            palette->controlBackground,
            palette->stepperHoverFill,
            hover
        );
        fill = interpolate_color(fill, palette->stepperPressedFill, pressed);
        RECT clip = rect_from_box(stepperBoxes[i]);
        draw_clipped_antialiased_rounded_fill(surface, &boxRect, &clip, px(3), fill);
    }
    draw_antialiased_rounded_outline(
        surface,
        &boxRect,
        px(3),
        1,
        palette->controlBorder
    );

    RECT hoursRect = rect_from_box(metrics.hours);
    RECT colonRect = rect_from_box(metrics.colon);
    RECT minutesRect = rect_from_box(metrics.minutes);
    RECT *selectedRect = g_app.timeEdit.segment == BELLWIN_TIME_HOURS ? &hoursRect : &minutesRect;
    if (focused) rounded_rect(dc, selectedRect, px(3), palette->accent, palette->accent);

    wchar_t hoursText[3];
    wchar_t minutesText[3];
    swprintf_s(hoursText, 3, L"%02d", minuteOfDay / 60);
    swprintf_s(minutesText, 3, L"%02d", minuteOfDay % 60);
    COLORREF hoursColor = focused && g_app.timeEdit.segment == BELLWIN_TIME_HOURS
        ? palette->accentText
        : palette->controlText;
    COLORREF minutesColor = focused && g_app.timeEdit.segment == BELLWIN_TIME_MINUTES
        ? palette->accentText
        : palette->controlText;
    draw_text(dc, hoursText, hoursRect, g_app.bodyFont, hoursColor, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    draw_text(dc, L":", colonRect, g_app.bodyFont, palette->controlText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    draw_text(dc, minutesText, minutesRect, g_app.bodyFont, minutesColor, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    int dividerX = (int)(metrics.dividerX + 0.5f);
    int stepperMidY = (int)(metrics.stepperMidY + 0.5f);
    HPEN divider = CreatePen(PS_SOLID, 1, palette->divider);
    HGDIOBJ oldPen = SelectObject(dc, divider);
    MoveToEx(dc, dividerX, boxRect.top, NULL);
    LineTo(dc, dividerX, boxRect.bottom);
    MoveToEx(dc, dividerX, stepperMidY, NULL);
    LineTo(dc, boxRect.right, stepperMidY);
    SelectObject(dc, oldPen);
    DeleteObject(divider);

    int triangleLeft = (int)(box.x + 91.0f * scale + 0.5f);
    int triangleMid = (int)(box.x + 97.0f * scale + 0.5f);
    int triangleRight = (int)(box.x + 103.0f * scale + 0.5f);
    const POINT up[3] = {
        {triangleLeft, (int)(box.y + 14.0f * scale + 0.5f)},
        {triangleMid, (int)(box.y + 8.0f * scale + 0.5f)},
        {triangleRight, (int)(box.y + 14.0f * scale + 0.5f)},
    };
    const POINT down[3] = {
        {triangleLeft, (int)(box.y + 26.0f * scale + 0.5f)},
        {triangleMid, (int)(box.y + 32.0f * scale + 0.5f)},
        {triangleRight, (int)(box.y + 26.0f * scale + 0.5f)},
    };
    float upHover = ui_motion_value(control, UI_PART_TIME_UP, 0);
    float downHover = ui_motion_value(control, UI_PART_TIME_DOWN, 0);
    float upPressed = ui_motion_value(control, UI_PART_TIME_UP, 1);
    float downPressed = ui_motion_value(control, UI_PART_TIME_DOWN, 1);
    COLORREF upColor = interpolate_color(
        palette->controlGlyphSecondary,
        palette->controlGlyphHover,
        upHover
    );
    upColor = interpolate_color(upColor, palette->controlGlyphPressed, upPressed);
    COLORREF downColor = interpolate_color(
        palette->controlGlyphSecondary,
        palette->controlGlyphHover,
        downHover
    );
    downColor = interpolate_color(downColor, palette->controlGlyphPressed, downPressed);
    draw_chevron(
        surface,
        up,
        upColor
    );
    draw_chevron(
        surface,
        down,
        downColor
    );
}

static void render_toggle_custom(PixelSurface *surface, Clay_BoundingBox box) {
    const BellwinThemePalette *palette = &g_app.theme.palette;
    float scale = ui_scale();
    RECT track = rect_from_box(box);
    float hover = ui_motion_value(CONTROL_AUTOSTART, UI_PART_TOGGLE, 0);
    float pressed = ui_motion_value(CONTROL_AUTOSTART, UI_PART_TOGGLE, 1);
    float checked = g_app.toggleStateMotion.value;

    COLORREF offFill = interpolate_color(
        palette->toggleTrackOffFill,
        palette->toggleTrackOffFillHover,
        hover
    );
    offFill = interpolate_color(offFill, palette->toggleTrackOffFillPressed, pressed);
    COLORREF onFill = interpolate_color(
        palette->toggleTrackOnFill,
        palette->toggleTrackOnFillHover,
        hover
    );
    onFill = interpolate_color(onFill, palette->toggleTrackOnFillPressed, pressed);
    COLORREF trackFill = interpolate_color(offFill, onFill, checked);
    COLORREF trackBorder = interpolate_color(
        palette->toggleTrackOffStroke,
        onFill,
        checked
    );
    int borderWidth = px(1);
    if (borderWidth < 1) borderWidth = 1;
    draw_antialiased_rounded_rect(
        surface,
        &track,
        (track.bottom - track.top) / 2,
        borderWidth,
        trackFill,
        trackBorder
    );

    BellwinUiRect knobGeometry = bellwin_ui_switch_motion_rect(&g_app.toggleMotion);
    const int subpixelScale = BELLWIN_AA_SUBPIXEL_SCALE;
    int localLeftSubpixels = (int)(knobGeometry.x * scale * subpixelScale + 0.5f);
    int knobWidthSubpixels = (int)(
        knobGeometry.width * scale * subpixelScale + 0.5f
    );
    int knobHeightSubpixels = bellwin_even_subpixel_extent(
        knobGeometry.height * scale
    );
    BellwinSubpixelRect knob = bellwin_centered_capsule_subpixel_rect(
        track.left,
        track.top,
        track.bottom,
        localLeftSubpixels,
        knobWidthSubpixels,
        knobHeightSubpixels
    );
    COLORREF knobColor = interpolate_color(
        palette->toggleThumbOff,
        palette->toggleThumbOn,
        checked
    );
    draw_antialiased_horizontal_capsule_subpixels(surface, &knob, knobColor);
}

static HFONT font_for_id(uint16_t fontId) {
    if (fontId == BELLWIN_FONT_TITLE) return g_app.titleFont;
    if (fontId == BELLWIN_FONT_SMALL) return g_app.smallFont;
    return g_app.bodyFont;
}

static Clay_Dimensions measure_text_gdi(Clay_StringSlice text, Clay_TextElementConfig *config, void *userData) {
    (void)userData;
    Clay_Dimensions dimensions = {0, 0};
    wchar_t wide[256];
    int wideLength = MultiByteToWideChar(CP_UTF8, 0, text.chars, text.length, wide, 255);
    if (wideLength <= 0 || !g_app.measureDc) return dimensions;
    HGDIOBJ oldFont = SelectObject(g_app.measureDc, font_for_id(config->fontId));
    SIZE size = {0};
    if (GetTextExtentPoint32W(g_app.measureDc, wide, wideLength, &size)) {
        dimensions.width = (float)size.cx;
        dimensions.height = (float)size.cy;
    }
    SelectObject(g_app.measureDc, oldFont);
    return dimensions;
}

static Clay_Color ui_color(COLORREF color) {
    Clay_Color result = {(float)GetRValue(color), (float)GetGValue(color), (float)GetBValue(color), 255.0f};
    return result;
}

static COLORREF gdi_color(Clay_Color color) {
    return RGB((BYTE)(color.r + 0.5f), (BYTE)(color.g + 0.5f), (BYTE)(color.b + 0.5f));
}

static char g_updateTooltipText[96];

static BellwinUiFrame build_ui_frame(void) {
    const BellwinThemePalette *palette = &g_app.theme.palette;
    BellwinUiFrame frame;
    ZeroMemory(&frame, sizeof(frame));
    frame.scale = ui_scale();
    frame.settings = g_app.settings;
    frame.showInstall = g_app.showInstall;
    frame.updateAvailable = g_app.updateAvailable;
    frame.hoverInstall = g_app.hoverInstall;
    frame.installLabel = g_app.updateAvailable ? "Update" : "Install";
    if (g_app.updateAvailable) {
        char installed[24];
        char current[24];
        format_version_utf8(g_app.installedVersion, installed, sizeof(installed));
        format_version_utf8(current_version(), current, sizeof(current));
        snprintf(g_updateTooltipText, sizeof(g_updateTooltipText), "Update from %s to %s", installed, current);
        frame.updateTooltip = g_updateTooltipText;
    }
    frame.colors.windowBackground = ui_color(palette->windowBackground);
    frame.colors.cardBackground = ui_color(palette->cardBackground);
    frame.colors.cardBorder = ui_color(palette->cardBorder);
    frame.colors.primaryText = ui_color(palette->primaryText);
    frame.colors.secondaryText = ui_color(palette->secondaryText);
    frame.colors.controlBackground = ui_color(palette->controlBackground);
    frame.colors.controlBorder = ui_color(palette->controlBorder);
    frame.colors.controlText = ui_color(palette->controlText);
    frame.colors.divider = ui_color(palette->divider);
    frame.colors.hoverBackground = ui_color(palette->hoverBackground);
    frame.colors.tooltipBackground = ui_color(palette->tooltipBackground);
    frame.colors.tooltipBorder = ui_color(palette->tooltipBorder);
    frame.colors.tooltipText = ui_color(palette->tooltipText);
    return frame;
}

static int ensure_ui_ready(void) {
    static int measureBound = 0;
    RECT client;
    if (!g_app.window || !GetClientRect(g_app.window, &client)) return 0;
    if (!bellwin_ui_initialize((float)client.right, (float)client.bottom)) return 0;
    if (!measureBound) {
        Clay_SetMeasureTextFunction(measure_text_gdi, NULL);
        measureBound = 1;
    }
    return 1;
}

static void render_ui_text(HDC dc, const Clay_RenderCommand *command) {
    const Clay_TextRenderData *data = &command->renderData.text;
    wchar_t wide[256];
    int wideLength = MultiByteToWideChar(
        CP_UTF8, 0, data->stringContents.chars, data->stringContents.length, wide, 255
    );
    if (wideLength <= 0) return;
    wide[wideLength] = L'\0';
    RECT rect = rect_from_box(command->boundingBox);
    draw_text(
        dc, wide, rect, font_for_id(data->fontId), gdi_color(data->textColor),
        DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_NOCLIP
    );
}

static void render_ui_border(HDC dc, const Clay_RenderCommand *command) {
    const Clay_BorderRenderData *data = &command->renderData.border;
    RECT rect = rect_from_box(command->boundingBox);
    int width = data->width.left ? data->width.left : data->width.top;
    if (width <= 0) return;
    HPEN pen = CreatePen(PS_SOLID, width, gdi_color(data->color));
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    int radius = (int)(data->cornerRadius.topLeft + 0.5f);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

static void render_ui_custom(PixelSurface *surface, HDC dc, const Clay_RenderCommand *command) {
    const BellwinUiCustom *custom = (const BellwinUiCustom *)command->renderData.custom.customData;
    if (!custom) return;
    switch (custom->kind) {
    case BELLWIN_UI_CUSTOM_SLIDER:
        render_slider_custom(surface, dc, command->boundingBox, custom->control);
        break;
    case BELLWIN_UI_CUSTOM_TIME_BOX:
        render_time_box_custom(surface, dc, command->boundingBox, custom->control);
        break;
    case BELLWIN_UI_CUSTOM_TOGGLE:
        render_toggle_custom(surface, command->boundingBox);
        break;
    }
}

static void render_ui_commands(Clay_RenderCommandArray commands, PixelSurface *surface, HDC dc) {
    for (int32_t i = 0; i < commands.length; ++i) {
        Clay_RenderCommand *command = Clay_RenderCommandArray_Get(&commands, i);
        switch (command->commandType) {
        case CLAY_RENDER_COMMAND_TYPE_RECTANGLE: {
            RECT rect = rect_from_box(command->boundingBox);
            COLORREF fill = gdi_color(command->renderData.rectangle.backgroundColor);
            int radius = (int)(command->renderData.rectangle.cornerRadius.topLeft + 0.5f);
            if (radius > 0) rounded_rect(dc, &rect, radius, fill, fill);
            else fill_rect_color(dc, &rect, fill);
            break;
        }
        case CLAY_RENDER_COMMAND_TYPE_BORDER:
            render_ui_border(dc, command);
            break;
        case CLAY_RENDER_COMMAND_TYPE_TEXT:
            render_ui_text(dc, command);
            break;
        case CLAY_RENDER_COMMAND_TYPE_CUSTOM:
            render_ui_custom(surface, dc, command);
            break;
        default:
            break;
        }
    }
}

/* Keyboard focus is app state, not layout: drawn as an overlay from the
   focused widget's element box after the command list is rendered. */
static void render_focus_ring(PixelSurface *surface) {
    const Widget *widget = widget_by_id(g_app.focusedControl);
    if (!widget || !control_has_visible_focus(widget->id)) return;
    Clay_ElementData data = Clay_GetElementData(bellwin_ui_hit_id(widget->id));
    if (!data.found) return;
    RECT rect = rect_from_box(data.boundingBox);
    int radius;
    if (widget->role == BELLWIN_WIDGET_SLIDER) {
        radius = px(6);
    } else if (widget->role == BELLWIN_WIDGET_TOGGLE) {
        InflateRect(&rect, px(4), px(3));
        radius = px(5);
    } else if (widget->role == BELLWIN_WIDGET_BUTTON) {
        radius = px(5);
    } else {
        return; /* time boxes indicate focus with the segment highlight */
    }
    draw_antialiased_rounded_outline(
        surface, &rect, radius, px(2), g_app.theme.palette.focus
    );
}

void paint_ui(HWND window) {
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

    fill_rect_color(dc, &client, g_app.theme.palette.windowBackground);
    if (ensure_ui_ready()) {
        BellwinUiFrame frame = build_ui_frame();
        Clay_RenderCommandArray commands = bellwin_ui_build(&frame);
        render_ui_commands(commands, &surface, dc);
        render_focus_ring(&surface);
    }

    BitBlt(target, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
    EndPaint(window, &paint);
}
