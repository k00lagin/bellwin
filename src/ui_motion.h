#ifndef BELLWIN_UI_MOTION_H
#define BELLWIN_UI_MOTION_H

#include <stdint.h>
#include "widgets.h"

enum {
    BELLWIN_UI_MOTION_FASTER_MS = 83,
    BELLWIN_UI_MOTION_FAST_MS = 167,
    BELLWIN_UI_MOTION_NORMAL_MS = 250,
};

typedef enum UiVisualPart {
    UI_PART_NONE,
    UI_PART_SLIDER,
    UI_PART_TIME_UP,
    UI_PART_TIME_DOWN,
    UI_PART_TOGGLE,
} UiVisualPart;

typedef struct UiPointerState {
    ControlId control;
    UiVisualPart part;
} UiPointerState;

typedef enum UiMotionSlot {
    UI_MOTION_VOLUME_SLIDER,
    UI_MOTION_MINIMUM_SLIDER,
    UI_MOTION_MAXIMUM_SLIDER,
    UI_MOTION_QUIET_START_UP,
    UI_MOTION_QUIET_START_DOWN,
    UI_MOTION_QUIET_END_UP,
    UI_MOTION_QUIET_END_DOWN,
    UI_MOTION_AUTOSTART,
    UI_MOTION_SLOT_COUNT,
} UiMotionSlot;

static inline int ui_motion_slot_for(ControlId control, UiVisualPart part) {
    if (part == UI_PART_SLIDER) {
        if (control == CONTROL_VOLUME) return UI_MOTION_VOLUME_SLIDER;
        if (control == CONTROL_MINIMUM_INTERVAL) return UI_MOTION_MINIMUM_SLIDER;
        if (control == CONTROL_MAXIMUM_INTERVAL) return UI_MOTION_MAXIMUM_SLIDER;
    } else if (part == UI_PART_TIME_UP) {
        if (control == CONTROL_QUIET_START) return UI_MOTION_QUIET_START_UP;
        if (control == CONTROL_QUIET_END) return UI_MOTION_QUIET_END_UP;
    } else if (part == UI_PART_TIME_DOWN) {
        if (control == CONTROL_QUIET_START) return UI_MOTION_QUIET_START_DOWN;
        if (control == CONTROL_QUIET_END) return UI_MOTION_QUIET_END_DOWN;
    } else if (part == UI_PART_TOGGLE && control == CONTROL_AUTOSTART) {
        return UI_MOTION_AUTOSTART;
    }
    return -1;
}

typedef struct BellwinUiMotionValue {
    float from;
    float to;
    float value;
    uint64_t startedAtMs;
    uint32_t durationMs;
    int active;
} BellwinUiMotionValue;

typedef struct UiPartMotion {
    BellwinUiMotionValue hover;
    BellwinUiMotionValue pressed;
} UiPartMotion;

typedef struct BellwinUiRect {
    float x;
    float y;
    float width;
    float height;
} BellwinUiRect;

typedef enum BellwinUiSwitchInteraction {
    BELLWIN_UI_SWITCH_NORMAL,
    BELLWIN_UI_SWITCH_HOVERED,
    BELLWIN_UI_SWITCH_PRESSED,
} BellwinUiSwitchInteraction;

typedef struct BellwinUiRectMotion {
    BellwinUiMotionValue x;
    BellwinUiMotionValue y;
    BellwinUiMotionValue width;
    BellwinUiMotionValue height;
} BellwinUiRectMotion;

typedef struct BellwinUiSwitchMotion {
    BellwinUiRectMotion knob;
    int checked;
    int hovered;
    int pressed;
} BellwinUiSwitchMotion;

static inline float bellwin_ui_clamp01(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static inline float bellwin_ui_lerp(float from, float to, float progress) {
    return from + (to - from) * progress;
}

static inline float bellwin_ui_motion_progress(
    uint64_t startedAtMs,
    uint32_t durationMs,
    uint64_t nowMs
) {
    if (durationMs == 0) return 1.0f;
    if (nowMs <= startedAtMs) return 0.0f;
    uint64_t elapsedMs = nowMs - startedAtMs;
    if (elapsedMs >= durationMs) return 1.0f;
    return (float)elapsedMs / (float)durationMs;
}

/* ControlFastOutSlowInKeySpline is cubic-bezier(0, 0, 0, 1).
   Its time coordinate is parameter^3. A fixed-iteration bisection avoids a
   libm dependency and gives deterministic results on the supported C toolchain. */
static inline float bellwin_ui_fast_out_slow_in(float progress) {
    progress = bellwin_ui_clamp01(progress);
    if (progress == 0.0f || progress == 1.0f) return progress;

    float low = 0.0f;
    float high = 1.0f;
    for (int iteration = 0; iteration < 20; ++iteration) {
        float parameter = (low + high) * 0.5f;
        float time = parameter * parameter * parameter;
        if (time < progress) low = parameter;
        else high = parameter;
    }

    float parameter = (low + high) * 0.5f;
    return parameter * parameter * (3.0f - 2.0f * parameter);
}

static inline void bellwin_ui_motion_init(BellwinUiMotionValue *motion, float value) {
    motion->from = value;
    motion->to = value;
    motion->value = value;
    motion->startedAtMs = 0;
    motion->durationMs = 0;
    motion->active = 0;
}

static inline void bellwin_ui_motion_snap(BellwinUiMotionValue *motion, float value) {
    motion->from = value;
    motion->to = value;
    motion->value = value;
    motion->durationMs = 0;
    motion->active = 0;
}

static inline void bellwin_ui_motion_finish(BellwinUiMotionValue *motion) {
    bellwin_ui_motion_snap(motion, motion->to);
}

static inline int bellwin_ui_motion_tick(BellwinUiMotionValue *motion, uint64_t nowMs) {
    if (!motion->active) return 0;

    float progress = bellwin_ui_motion_progress(
        motion->startedAtMs,
        motion->durationMs,
        nowMs
    );
    if (progress >= 1.0f) {
        bellwin_ui_motion_snap(motion, motion->to);
        return 0;
    }

    motion->value = bellwin_ui_lerp(
        motion->from,
        motion->to,
        bellwin_ui_fast_out_slow_in(progress)
    );
    return 1;
}

static inline void bellwin_ui_motion_start(
    BellwinUiMotionValue *motion,
    float target,
    uint64_t nowMs,
    uint32_t durationMs,
    int animationsEnabled
) {
    bellwin_ui_motion_tick(motion, nowMs);

    if (!animationsEnabled || durationMs == 0 || motion->value == target) {
        bellwin_ui_motion_snap(motion, target);
        return;
    }
    if (motion->active && motion->to == target) return;

    motion->from = motion->value;
    motion->to = target;
    motion->startedAtMs = nowMs;
    motion->durationMs = durationMs;
    motion->active = 1;
}

static inline void bellwin_ui_rect_motion_init(
    BellwinUiRectMotion *motion,
    BellwinUiRect rect
) {
    bellwin_ui_motion_init(&motion->x, rect.x);
    bellwin_ui_motion_init(&motion->y, rect.y);
    bellwin_ui_motion_init(&motion->width, rect.width);
    bellwin_ui_motion_init(&motion->height, rect.height);
}

static inline BellwinUiRect bellwin_ui_rect_motion_rect(
    const BellwinUiRectMotion *motion
) {
    BellwinUiRect rect = {
        motion->x.value,
        motion->y.value,
        motion->width.value,
        motion->height.value,
    };
    return rect;
}

static inline int bellwin_ui_rect_motion_active(const BellwinUiRectMotion *motion) {
    return motion->x.active
        || motion->y.active
        || motion->width.active
        || motion->height.active;
}

static inline int bellwin_ui_rect_motion_tick(
    BellwinUiRectMotion *motion,
    uint64_t nowMs
) {
    int active = bellwin_ui_motion_tick(&motion->x, nowMs);
    active |= bellwin_ui_motion_tick(&motion->y, nowMs);
    active |= bellwin_ui_motion_tick(&motion->width, nowMs);
    active |= bellwin_ui_motion_tick(&motion->height, nowMs);
    return active;
}

static inline void bellwin_ui_rect_motion_start(
    BellwinUiRectMotion *motion,
    BellwinUiRect target,
    uint64_t nowMs,
    uint32_t durationMs,
    int animationsEnabled
) {
    bellwin_ui_motion_start(&motion->x, target.x, nowMs, durationMs, animationsEnabled);
    bellwin_ui_motion_start(&motion->y, target.y, nowMs, durationMs, animationsEnabled);
    bellwin_ui_motion_start(
        &motion->width,
        target.width,
        nowMs,
        durationMs,
        animationsEnabled
    );
    bellwin_ui_motion_start(
        &motion->height,
        target.height,
        nowMs,
        durationMs,
        animationsEnabled
    );
}

static inline void bellwin_ui_rect_motion_finish(BellwinUiRectMotion *motion) {
    bellwin_ui_motion_finish(&motion->x);
    bellwin_ui_motion_finish(&motion->y);
    bellwin_ui_motion_finish(&motion->width);
    bellwin_ui_motion_finish(&motion->height);
}

/* Returns track-local geometry for the fixed 40 x 20 switch visual. */
static inline BellwinUiRect bellwin_ui_switch_knob_target(
    int checked,
    BellwinUiSwitchInteraction interaction
) {
    BellwinUiRect rect;
    if (interaction == BELLWIN_UI_SWITCH_PRESSED) {
        rect.x = checked ? 20.0f : 3.0f;
        rect.y = 3.0f;
        rect.width = 17.0f;
        rect.height = 14.0f;
    } else if (interaction == BELLWIN_UI_SWITCH_HOVERED) {
        rect.x = checked ? 23.0f : 3.0f;
        rect.y = 3.0f;
        rect.width = 14.0f;
        rect.height = 14.0f;
    } else {
        rect.x = checked ? 24.0f : 4.0f;
        rect.y = 4.0f;
        rect.width = 12.0f;
        rect.height = 12.0f;
    }
    return rect;
}

static inline BellwinUiSwitchInteraction bellwin_ui_switch_rest_interaction(
    const BellwinUiSwitchMotion *motion
) {
    return motion->hovered ? BELLWIN_UI_SWITCH_HOVERED : BELLWIN_UI_SWITCH_NORMAL;
}

static inline void bellwin_ui_switch_motion_init(
    BellwinUiSwitchMotion *motion,
    int checked,
    int hovered
) {
    motion->checked = checked != 0;
    motion->hovered = hovered != 0;
    motion->pressed = 0;
    bellwin_ui_rect_motion_init(
        &motion->knob,
        bellwin_ui_switch_knob_target(
            motion->checked,
            bellwin_ui_switch_rest_interaction(motion)
        )
    );
}

static inline BellwinUiRect bellwin_ui_switch_motion_rect(
    const BellwinUiSwitchMotion *motion
) {
    return bellwin_ui_rect_motion_rect(&motion->knob);
}

static inline int bellwin_ui_switch_motion_active(const BellwinUiSwitchMotion *motion) {
    return bellwin_ui_rect_motion_active(&motion->knob);
}

static inline int bellwin_ui_switch_motion_tick(
    BellwinUiSwitchMotion *motion,
    uint64_t nowMs
) {
    return bellwin_ui_rect_motion_tick(&motion->knob, nowMs);
}

static inline void bellwin_ui_switch_motion_set_hover(
    BellwinUiSwitchMotion *motion,
    int hovered,
    uint64_t nowMs,
    int animationsEnabled
) {
    motion->hovered = hovered != 0;
    if (motion->pressed) return;
    bellwin_ui_rect_motion_start(
        &motion->knob,
        bellwin_ui_switch_knob_target(
            motion->checked,
            bellwin_ui_switch_rest_interaction(motion)
        ),
        nowMs,
        BELLWIN_UI_MOTION_FASTER_MS,
        animationsEnabled
    );
}

static inline void bellwin_ui_switch_motion_press(
    BellwinUiSwitchMotion *motion,
    uint64_t nowMs,
    int animationsEnabled
) {
    motion->pressed = 1;
    bellwin_ui_rect_motion_start(
        &motion->knob,
        bellwin_ui_switch_knob_target(motion->checked, BELLWIN_UI_SWITCH_PRESSED),
        nowMs,
        BELLWIN_UI_MOTION_FASTER_MS,
        animationsEnabled
    );
}

static inline void bellwin_ui_switch_motion_release(
    BellwinUiSwitchMotion *motion,
    int checkedAfterCommit,
    int hovered,
    uint64_t nowMs,
    int animationsEnabled
) {
    motion->checked = checkedAfterCommit != 0;
    motion->hovered = hovered != 0;
    motion->pressed = 0;
    bellwin_ui_rect_motion_start(
        &motion->knob,
        bellwin_ui_switch_knob_target(
            motion->checked,
            bellwin_ui_switch_rest_interaction(motion)
        ),
        nowMs,
        BELLWIN_UI_MOTION_FAST_MS,
        animationsEnabled
    );
}

static inline void bellwin_ui_switch_motion_cancel(
    BellwinUiSwitchMotion *motion,
    int hovered,
    uint64_t nowMs,
    int animationsEnabled
) {
    motion->hovered = hovered != 0;
    motion->pressed = 0;
    bellwin_ui_rect_motion_start(
        &motion->knob,
        bellwin_ui_switch_knob_target(
            motion->checked,
            bellwin_ui_switch_rest_interaction(motion)
        ),
        nowMs,
        BELLWIN_UI_MOTION_FASTER_MS,
        animationsEnabled
    );
}

static inline void bellwin_ui_switch_motion_set_checked(
    BellwinUiSwitchMotion *motion,
    int checked,
    uint64_t nowMs,
    int animationsEnabled
) {
    motion->checked = checked != 0;
    motion->pressed = 0;
    bellwin_ui_rect_motion_start(
        &motion->knob,
        bellwin_ui_switch_knob_target(
            motion->checked,
            bellwin_ui_switch_rest_interaction(motion)
        ),
        nowMs,
        BELLWIN_UI_MOTION_FAST_MS,
        animationsEnabled
    );
}

static inline void bellwin_ui_switch_motion_finish(BellwinUiSwitchMotion *motion) {
    bellwin_ui_rect_motion_finish(&motion->knob);
}

#endif
