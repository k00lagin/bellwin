#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CLAY_IMPLEMENTATION
#include "layout.h"

static const BellwinUiCustom BELLWIN_UI_CUSTOM_ELEMENTS[] = {
    {BELLWIN_UI_CUSTOM_SLIDER, CONTROL_VOLUME},
    {BELLWIN_UI_CUSTOM_SLIDER, CONTROL_MINIMUM_INTERVAL},
    {BELLWIN_UI_CUSTOM_SLIDER, CONTROL_MAXIMUM_INTERVAL},
    {BELLWIN_UI_CUSTOM_TIME_BOX, CONTROL_QUIET_START},
    {BELLWIN_UI_CUSTOM_TIME_BOX, CONTROL_QUIET_END},
    {BELLWIN_UI_CUSTOM_TOGGLE, CONTROL_AUTOSTART},
};

const BellwinUiCustom *bellwin_ui_custom_for(ControlId control) {
    size_t count = sizeof(BELLWIN_UI_CUSTOM_ELEMENTS) / sizeof(BELLWIN_UI_CUSTOM_ELEMENTS[0]);
    for (size_t i = 0; i < count; ++i) {
        if (BELLWIN_UI_CUSTOM_ELEMENTS[i].control == control) return &BELLWIN_UI_CUSTOM_ELEMENTS[i];
    }
    return NULL;
}

/* The interactive element for a control: hit testing and the focus ring. */
Clay_ElementId bellwin_ui_hit_id(ControlId control) {
    switch (control) {
    case CONTROL_VOLUME: return Clay_GetElementId(CLAY_STRING("VolumeRow"));
    case CONTROL_MINIMUM_INTERVAL: return Clay_GetElementId(CLAY_STRING("MinimumRow"));
    case CONTROL_MAXIMUM_INTERVAL: return Clay_GetElementId(CLAY_STRING("MaximumRow"));
    case CONTROL_QUIET_START: return Clay_GetElementId(CLAY_STRING("QuietStartBox"));
    case CONTROL_QUIET_END: return Clay_GetElementId(CLAY_STRING("QuietEndBox"));
    case CONTROL_AUTOSTART: return Clay_GetElementId(CLAY_STRING("AutostartToggle"));
    case CONTROL_INSTALL: return Clay_GetElementId(CLAY_STRING("InstallButton"));
    default: return Clay_GetElementId(CLAY_STRING("Root"));
    }
}

/* The draggable track region of a slider. */
Clay_ElementId bellwin_ui_track_id(ControlId control) {
    switch (control) {
    case CONTROL_VOLUME: return Clay_GetElementId(CLAY_STRING("VolumeTrack"));
    case CONTROL_MINIMUM_INTERVAL: return Clay_GetElementId(CLAY_STRING("MinimumTrack"));
    case CONTROL_MAXIMUM_INTERVAL: return Clay_GetElementId(CLAY_STRING("MaximumTrack"));
    default: return Clay_GetElementId(CLAY_STRING("Root"));
    }
}

Clay_ElementId bellwin_ui_tooltip_id(void) {
    return Clay_GetElementId(CLAY_STRING("UpdateTooltip"));
}

BellwinTimeBoxMetrics bellwin_time_box_metrics(Clay_BoundingBox box, float scale) {
    BellwinTimeBoxMetrics metrics;
    float contentTop = box.y + 4.0f * scale;
    float contentHeight = 32.0f * scale;
    metrics.hours = (Clay_BoundingBox){box.x + 6.0f * scale, contentTop, 32.0f * scale, contentHeight};
    metrics.colon = (Clay_BoundingBox){box.x + 38.0f * scale, contentTop, 10.0f * scale, contentHeight};
    metrics.minutes = (Clay_BoundingBox){box.x + 48.0f * scale, contentTop, 32.0f * scale, contentHeight};
    metrics.dividerX = box.x + 84.0f * scale;
    metrics.stepperMidY = box.y + 20.0f * scale;
    metrics.stepper = (Clay_BoundingBox){metrics.dividerX, box.y, 26.0f * scale, box.height};
    metrics.stepperUp = (Clay_BoundingBox){metrics.dividerX, box.y, 26.0f * scale, metrics.stepperMidY - box.y};
    metrics.stepperDown = (Clay_BoundingBox){metrics.dividerX, metrics.stepperMidY, 26.0f * scale, box.y + box.height - metrics.stepperMidY};
    return metrics;
}

int bellwin_box_contains(Clay_BoundingBox box, float x, float y) {
    return x >= box.x && x < box.x + box.width && y >= box.y && y < box.y + box.height;
}

int bellwin_slider_value_from_position(int minimum, int maximum, int snapStep, Clay_BoundingBox track, float x) {
    if (track.width <= 0.0f) return minimum;
    float fraction = (x - track.x) / track.width;
    if (fraction < 0.0f) fraction = 0.0f;
    if (fraction > 1.0f) fraction = 1.0f;
    int value = minimum + (int)(fraction * (float)(maximum - minimum) + 0.5f);
    return bellwin_snap_value(value, minimum, maximum, snapStep);
}

static void *bellwin_ui_memory;
static int bellwin_ui_ready;

int bellwin_ui_initialize(float width, float height) {
    if (bellwin_ui_ready) {
        Clay_SetLayoutDimensions((Clay_Dimensions){width, height});
        return 1;
    }
    Clay_SetMaxElementCount(512);
    uint32_t size = Clay_MinMemorySize();
    bellwin_ui_memory = malloc(size);
    if (!bellwin_ui_memory) return 0;
    Clay_Initialize(
        Clay_CreateArenaWithCapacityAndMemory(size, bellwin_ui_memory),
        (Clay_Dimensions){width, height},
        (Clay_ErrorHandler){0}
    );
    bellwin_ui_ready = 1;
    return 1;
}

static Clay_String bellwin_ui_string(const char *text) {
    Clay_String result = {false, (int32_t)strlen(text), text};
    return result;
}

static uint16_t bellwin_ui_pad(float value, float scale) {
    return (uint16_t)(value * scale + 0.5f);
}

static uint16_t bellwin_ui_font_size(float size, float scale) {
    return (uint16_t)(size * scale + 0.5f);
}

static Clay_TextElementConfig *bellwin_ui_text_config(uint16_t fontId, float fontSize, float scale, Clay_Color color) {
    return CLAY_TEXT_CONFIG({
        .textColor = color,
        .fontId = fontId,
        .fontSize = bellwin_ui_font_size(fontSize, scale),
        .wrapMode = CLAY_TEXT_WRAP_NONE,
    });
}

static void bellwin_ui_spacer_height(float height) {
    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(height)}}}) {}
}

static void bellwin_ui_spacer_width(float width) {
    CLAY({.layout = {.sizing = {CLAY_SIZING_FIXED(width), CLAY_SIZING_FIXED(0)}}}) {}
}

/* Label column + focusable row [track | value text]. The row box doubles as
   the wheel target and keyboard focus outline. */
static void bellwin_ui_slider_row(
    const BellwinUiFrame *frame,
    Clay_String label,
    Clay_ElementId rowId,
    Clay_ElementId trackId,
    const char *valueText,
    const BellwinUiCustom *custom
) {
    float s = frame->scale;
    CLAY({.layout = {
        .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(42.0f * s)},
        .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
    }}) {
        CLAY({.layout = {
            .sizing = {CLAY_SIZING_FIXED(232.0f * s), CLAY_SIZING_GROW(0)},
            .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
        }}) {
            CLAY_TEXT(label, bellwin_ui_text_config(BELLWIN_FONT_BODY, 18.0f, s, frame->colors.primaryText));
        }
        bellwin_ui_spacer_width(5.0f * s);
        CLAY({.id = rowId, .layout = {
            .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
            .padding = {bellwin_ui_pad(15.0f, s), bellwin_ui_pad(10.0f, s), 0, 0},
            .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
        }}) {
            CLAY({.id = trackId,
                .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}},
                .custom = {.customData = (void *)custom},
            }) {}
            bellwin_ui_spacer_width(25.0f * s);
            CLAY({.layout = {
                .sizing = {CLAY_SIZING_FIXED(100.0f * s), CLAY_SIZING_GROW(0)},
                .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
            }}) {
                CLAY_TEXT(
                    bellwin_ui_string(valueText),
                    bellwin_ui_text_config(BELLWIN_FONT_BODY, 18.0f, s, frame->colors.secondaryText)
                );
            }
        }
    }
}

static void bellwin_ui_time_box(float scale, Clay_ElementId boxId, const BellwinUiCustom *custom) {
    CLAY({.id = boxId,
        .layout = {.sizing = {CLAY_SIZING_FIXED(110.0f * scale), CLAY_SIZING_FIXED(40.0f * scale)}},
        .custom = {.customData = (void *)custom},
    }) {}
}

static char bellwin_ui_volume_text[16];
static char bellwin_ui_minimum_text[16];
static char bellwin_ui_maximum_text[16];

Clay_RenderCommandArray bellwin_ui_build(const BellwinUiFrame *frame) {
    float s = frame->scale;
    const BellwinUiColors *colors = &frame->colors;

    snprintf(bellwin_ui_volume_text, sizeof(bellwin_ui_volume_text), "%d%%", frame->settings.volume);
    bellwin_format_interval_utf8(frame->settings.minimumMinutes, bellwin_ui_minimum_text, sizeof(bellwin_ui_minimum_text));
    bellwin_format_interval_utf8(frame->settings.maximumMinutes, bellwin_ui_maximum_text, sizeof(bellwin_ui_maximum_text));

    Clay_BeginLayout();
    CLAY({.id = CLAY_ID("Root"),
        .layout = {
            .layoutDirection = CLAY_TOP_TO_BOTTOM,
            .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
            .padding = {0, 0, bellwin_ui_pad(24.0f, s), 0},
        },
        .backgroundColor = colors->windowBackground,
    }) {
        CLAY({.layout = {
            .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
            .padding = {bellwin_ui_pad(40.0f, s), bellwin_ui_pad(40.0f, s), 0, 0},
        }}) {
            CLAY({.id = CLAY_ID("Card"),
                .layout = {
                    .layoutDirection = CLAY_TOP_TO_BOTTOM,
                    .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(283.0f * s)},
                    .padding = {
                        bellwin_ui_pad(38.0f, s),
                        bellwin_ui_pad(20.0f, s),
                        bellwin_ui_pad(29.0f, s),
                        bellwin_ui_pad(22.0f, s),
                    },
                },
                .backgroundColor = colors->cardBackground,
                .cornerRadius = CLAY_CORNER_RADIUS(12.0f * s),
                .border = {.color = colors->cardBorder, .width = {1, 1, 1, 1, 0}},
            }) {
                bellwin_ui_slider_row(
                    frame, CLAY_STRING("Bell volume"),
                    bellwin_ui_hit_id(CONTROL_VOLUME), bellwin_ui_track_id(CONTROL_VOLUME),
                    bellwin_ui_volume_text, bellwin_ui_custom_for(CONTROL_VOLUME)
                );
                bellwin_ui_spacer_height(14.0f * s);
                bellwin_ui_slider_row(
                    frame, CLAY_STRING("Ring every"),
                    bellwin_ui_hit_id(CONTROL_MINIMUM_INTERVAL), bellwin_ui_track_id(CONTROL_MINIMUM_INTERVAL),
                    bellwin_ui_minimum_text, bellwin_ui_custom_for(CONTROL_MINIMUM_INTERVAL)
                );
                bellwin_ui_spacer_height(14.0f * s);
                bellwin_ui_slider_row(
                    frame, CLAY_STRING("to"),
                    bellwin_ui_hit_id(CONTROL_MAXIMUM_INTERVAL), bellwin_ui_track_id(CONTROL_MAXIMUM_INTERVAL),
                    bellwin_ui_maximum_text, bellwin_ui_custom_for(CONTROL_MAXIMUM_INTERVAL)
                );
                bellwin_ui_spacer_height(16.0f * s);
                CLAY({.layout = {
                    .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)},
                    .padding = {0, bellwin_ui_pad(18.0f, s), 0, 0},
                }}) {
                    CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1)}},
                        .backgroundColor = colors->divider,
                    }) {}
                }
                bellwin_ui_spacer_height(19.0f * s);
                CLAY({.layout = {
                    .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(42.0f * s)},
                    .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
                }}) {
                    CLAY({.layout = {
                        .sizing = {CLAY_SIZING_FIXED(232.0f * s), CLAY_SIZING_GROW(0)},
                        .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
                    }}) {
                        CLAY_TEXT(
                            CLAY_STRING("Quiet hours"),
                            bellwin_ui_text_config(BELLWIN_FONT_BODY, 18.0f, s, colors->primaryText)
                        );
                    }
                    bellwin_ui_spacer_width(20.0f * s);
                    bellwin_ui_time_box(s, bellwin_ui_hit_id(CONTROL_QUIET_START), bellwin_ui_custom_for(CONTROL_QUIET_START));
                    bellwin_ui_spacer_width(6.0f * s);
                    CLAY({.layout = {
                        .sizing = {CLAY_SIZING_FIXED(28.0f * s), CLAY_SIZING_GROW(0)},
                        .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER},
                    }}) {
                        CLAY_TEXT(
                            CLAY_STRING("\xE2\x80\xA6"),
                            bellwin_ui_text_config(BELLWIN_FONT_BODY, 18.0f, s, colors->secondaryText)
                        );
                    }
                    bellwin_ui_spacer_width(6.0f * s);
                    bellwin_ui_time_box(s, bellwin_ui_hit_id(CONTROL_QUIET_END), bellwin_ui_custom_for(CONTROL_QUIET_END));
                }
            }
        }
        bellwin_ui_spacer_height(24.0f * s);

        CLAY({.layout = {
            .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(52.0f * s)},
            .padding = {bellwin_ui_pad(40.0f, s), bellwin_ui_pad(50.0f, s), 0, 0},
            .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
        }}) {
            CLAY({.layout = {
                .sizing = {CLAY_SIZING_FIXED(145.0f * s), CLAY_SIZING_GROW(0)},
                .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
            }}) {
                CLAY_TEXT(
                    CLAY_STRING("Launch at login"),
                    bellwin_ui_text_config(BELLWIN_FONT_BODY, 18.0f, s, colors->primaryText)
                );
            }
            bellwin_ui_spacer_width(5.0f * s);
            CLAY({.id = bellwin_ui_hit_id(CONTROL_AUTOSTART),
                .layout = {.sizing = {CLAY_SIZING_FIXED(54.0f * s), CLAY_SIZING_FIXED(30.0f * s)}},
                .custom = {.customData = (void *)bellwin_ui_custom_for(CONTROL_AUTOSTART)},
            }) {}
            CLAY({.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(0)}}}) {}
            if (frame->showInstall) {
                CLAY({.id = bellwin_ui_hit_id(CONTROL_INSTALL),
                    .layout = {
                        .sizing = {CLAY_SIZING_FIXED(100.0f * s), CLAY_SIZING_FIXED(40.0f * s)},
                        .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER},
                    },
                    .backgroundColor = frame->hoverInstall ? colors->hoverBackground : colors->controlBackground,
                    .cornerRadius = CLAY_CORNER_RADIUS(5.0f * s),
                    .border = {.color = colors->controlBorder, .width = {1, 1, 1, 1, 0}},
                }) {
                    CLAY_TEXT(
                        bellwin_ui_string(frame->installLabel ? frame->installLabel : "Install"),
                        bellwin_ui_text_config(BELLWIN_FONT_SMALL, 15.0f, s, colors->controlText)
                    );
                    if (frame->hoverInstall && frame->updateAvailable && frame->updateTooltip) {
                        CLAY({.id = bellwin_ui_tooltip_id(),
                            .floating = {
                                .attachTo = CLAY_ATTACH_TO_PARENT,
                                .attachPoints = {
                                    .element = CLAY_ATTACH_POINT_RIGHT_BOTTOM,
                                    .parent = CLAY_ATTACH_POINT_RIGHT_TOP,
                                },
                                .offset = {0, -8.0f * s},
                            },
                            .layout = {.padding = {
                                bellwin_ui_pad(9.0f, s),
                                bellwin_ui_pad(9.0f, s),
                                bellwin_ui_pad(6.0f, s),
                                bellwin_ui_pad(6.0f, s),
                            }},
                            .backgroundColor = colors->tooltipBackground,
                            .cornerRadius = CLAY_CORNER_RADIUS(4.0f * s),
                            .border = {.color = colors->tooltipBorder, .width = {1, 1, 1, 1, 0}},
                        }) {
                            CLAY_TEXT(
                                bellwin_ui_string(frame->updateTooltip),
                                bellwin_ui_text_config(BELLWIN_FONT_SMALL, 15.0f, s, colors->tooltipText)
                            );
                        }
                    }
                }
            }
        }
    }
    return Clay_EndLayout();
}

int bellwin_ui_is_ready(void) {
    return bellwin_ui_ready;
}

void bellwin_ui_reset_measure_cache(void) {
    if (bellwin_ui_ready) Clay_ResetMeasureTextCache();
}

void bellwin_ui_shutdown(void) {
    free(bellwin_ui_memory);
    bellwin_ui_memory = NULL;
    bellwin_ui_ready = 0;
}
