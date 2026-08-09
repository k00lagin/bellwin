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

    float fraction = (float)(*widget->value - widget->minimum)
        / (float)(widget->maximum - widget->minimum);
    int left = (int)(box.x + 0.5f);
    int right = (int)(box.x + box.width + 0.5f);
    int centerY = (int)(box.y + box.height / 2.0f + 0.5f);
    int position = left + (int)(fraction * box.width + 0.5f);

    RECT inactive = {left, centerY - px(2), right, centerY + px(2)};
    RECT active = {left, centerY - px(2), position, centerY + px(2)};
    fill_rect_color(dc, &inactive, g_app.theme.palette.inactiveTrack);
    fill_rect_color(dc, &active, g_app.theme.palette.accent);

    if (widget->ticks > 1) {
        HPEN tickPen = CreatePen(PS_SOLID, px(2), g_app.theme.palette.tick);
        HGDIOBJ oldPen = SelectObject(dc, tickPen);
        for (int i = 0; i < widget->ticks; ++i) {
            int x = left + MulDiv(i, right - left, widget->ticks - 1);
            MoveToEx(dc, x, centerY - px(7), NULL);
            LineTo(dc, x, centerY + px(7));
        }
        SelectObject(dc, oldPen);
        DeleteObject(tickPen);
    }

    draw_antialiased_circle(
        surface,
        position,
        centerY,
        px(10),
        px(3),
        g_app.theme.palette.knob,
        g_app.theme.palette.accent
    );
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

static void render_time_box_custom(HDC dc, Clay_BoundingBox box, ControlId control) {
    const Widget *widget = widget_by_id(control);
    if (!widget) return;
    int minuteOfDay = *widget->value;
    int focused = control_has_focus(control);
    float scale = ui_scale();
    BellwinTimeBoxMetrics metrics = bellwin_time_box_metrics(box, scale);

    RECT boxRect = rect_from_box(box);
    rounded_rect(dc, &boxRect, px(3), g_app.theme.palette.controlBackground, g_app.theme.palette.controlBorder);

    RECT hoursRect = rect_from_box(metrics.hours);
    RECT colonRect = rect_from_box(metrics.colon);
    RECT minutesRect = rect_from_box(metrics.minutes);
    RECT *selectedRect = g_app.timeEdit.segment == BELLWIN_TIME_HOURS ? &hoursRect : &minutesRect;
    if (focused) rounded_rect(dc, selectedRect, px(3), g_app.theme.palette.accent, g_app.theme.palette.accent);

    wchar_t hoursText[3];
    wchar_t minutesText[3];
    swprintf_s(hoursText, 3, L"%02d", minuteOfDay / 60);
    swprintf_s(minutesText, 3, L"%02d", minuteOfDay % 60);
    COLORREF hoursColor = focused && g_app.timeEdit.segment == BELLWIN_TIME_HOURS
        ? g_app.theme.palette.accentText
        : g_app.theme.palette.controlText;
    COLORREF minutesColor = focused && g_app.timeEdit.segment == BELLWIN_TIME_MINUTES
        ? g_app.theme.palette.accentText
        : g_app.theme.palette.controlText;
    draw_text(dc, hoursText, hoursRect, g_app.bodyFont, hoursColor, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    draw_text(dc, L":", colonRect, g_app.bodyFont, g_app.theme.palette.controlText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    draw_text(dc, minutesText, minutesRect, g_app.bodyFont, minutesColor, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    int dividerX = (int)(metrics.dividerX + 0.5f);
    int stepperMidY = (int)(metrics.stepperMidY + 0.5f);
    HPEN divider = CreatePen(PS_SOLID, 1, g_app.theme.palette.divider);
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
    POINT up[3] = {
        {triangleLeft, (int)(box.y + 14.0f * scale + 0.5f)},
        {triangleMid, (int)(box.y + 8.0f * scale + 0.5f)},
        {triangleRight, (int)(box.y + 14.0f * scale + 0.5f)},
    };
    POINT down[3] = {
        {triangleLeft, (int)(box.y + 26.0f * scale + 0.5f)},
        {triangleMid, (int)(box.y + 32.0f * scale + 0.5f)},
        {triangleRight, (int)(box.y + 26.0f * scale + 0.5f)},
    };
    draw_triangle(dc, up, g_app.theme.palette.controlText);
    draw_triangle(dc, down, g_app.theme.palette.controlText);
}

static void render_toggle_custom(PixelSurface *surface, Clay_BoundingBox box) {
    int on = g_app.autoStart;
    float scale = ui_scale();
    RECT track = rect_from_box(box);
    draw_antialiased_horizontal_capsule(
        surface, &track, on ? g_app.theme.palette.accent : g_app.theme.palette.toggleOff
    );
    int knobX = (int)(box.x + (on ? 39.0f : 15.0f) * scale + 0.5f);
    int knobY = (int)(box.y + box.height / 2.0f + 0.5f);
    draw_antialiased_circle(
        surface,
        knobX,
        knobY,
        px(11),
        0,
        on ? g_app.theme.palette.knob : g_app.theme.palette.toggleOffKnob,
        on ? g_app.theme.palette.knob : g_app.theme.palette.toggleOffKnob
    );
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
        render_time_box_custom(dc, command->boundingBox, custom->control);
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
        InflateRect(&rect, px(5), px(5));
        radius = px(20);
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
