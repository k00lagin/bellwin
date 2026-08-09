#ifndef BELLWIN_LAYOUT_H
#define BELLWIN_LAYOUT_H

/* Declarative screen layout built on Clay. Pure layout logic: no windows.h,
   so the same tree can be built headless in tests. Rendering of the produced
   command list and of custom elements lives in render_gdi.c. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core.h"
#include "widgets.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wunused-variable"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#include "thirdparty/clay.h"
#pragma clang diagnostic pop

enum {
    BELLWIN_FONT_BODY = 0,
    BELLWIN_FONT_TITLE = 1,
    BELLWIN_FONT_SMALL = 2,
};

typedef struct BellwinUiColors {
    Clay_Color windowBackground;
    Clay_Color cardBackground;
    Clay_Color cardBorder;
    Clay_Color primaryText;
    Clay_Color secondaryText;
    Clay_Color controlBackground;
    Clay_Color controlBorder;
    Clay_Color controlText;
    Clay_Color divider;
    Clay_Color hoverBackground;
    Clay_Color tooltipBackground;
    Clay_Color tooltipBorder;
    Clay_Color tooltipText;
} BellwinUiColors;

typedef struct BellwinUiFrame {
    float scale;
    BellwinSettings settings;
    int showInstall;
    int updateAvailable;
    int hoverInstall;
    const char *installLabel;   /* must outlive the produced render commands */
    const char *updateTooltip;  /* shown while hoverInstall && updateAvailable */
    BellwinUiColors colors;
} BellwinUiFrame;

typedef enum BellwinUiCustomKind {
    BELLWIN_UI_CUSTOM_SLIDER,
    BELLWIN_UI_CUSTOM_TIME_BOX,
    BELLWIN_UI_CUSTOM_TOGGLE,
} BellwinUiCustomKind;

typedef struct BellwinUiCustom {
    BellwinUiCustomKind kind;
    ControlId control;
} BellwinUiCustom;

typedef struct BellwinTimeBoxMetrics {
    Clay_BoundingBox hours;
    Clay_BoundingBox colon;
    Clay_BoundingBox minutes;
    Clay_BoundingBox stepper;
    Clay_BoundingBox stepperUp;
    Clay_BoundingBox stepperDown;
    float dividerX;
    float stepperMidY;
} BellwinTimeBoxMetrics;


const BellwinUiCustom *bellwin_ui_custom_for(ControlId control);
Clay_ElementId bellwin_ui_hit_id(ControlId control);
Clay_ElementId bellwin_ui_track_id(ControlId control);
Clay_ElementId bellwin_ui_tooltip_id(void);
BellwinTimeBoxMetrics bellwin_time_box_metrics(Clay_BoundingBox box, float scale);
int bellwin_box_contains(Clay_BoundingBox box, float x, float y);
int bellwin_slider_value_from_position(int minimum, int maximum, int snapStep, Clay_BoundingBox track, float x);
int bellwin_ui_initialize(float width, float height);
Clay_RenderCommandArray bellwin_ui_build(const BellwinUiFrame *frame);
int bellwin_ui_is_ready(void);
void bellwin_ui_reset_measure_cache(void);
void bellwin_ui_shutdown(void);

#endif
