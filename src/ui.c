#include "app_internal.h"
#include "ui.h"
#include "uia.h"
#include <windowsx.h>

#define ui_scale app_ui_scale

const Widget WIDGETS[WIDGET_COUNT] = {
    {
        .id = CONTROL_VOLUME,
        .role = BELLWIN_WIDGET_SLIDER,
        .name = L"Bell volume",
        .automationId = L"Volume",
        .minimum = 0,
        .maximum = 100,
        .keyStep = 1,
        .wheelStep = 5,
        .value = &g_app.settings.volume,
        .committed = schedule_volume_preview,
    },
    {
        .id = CONTROL_MINIMUM_INTERVAL,
        .role = BELLWIN_WIDGET_SLIDER,
        .name = L"Minimum interval",
        .automationId = L"MinimumInterval",
        .minimum = 30,
        .maximum = 480,
        .keyStep = 30,
        .wheelStep = 30,
        .ticks = 16,
        .value = &g_app.settings.minimumMinutes,
        .clamp = clamp_minimum_interval,
        .committed = schedule_next_bell,
    },
    {
        .id = CONTROL_MAXIMUM_INTERVAL,
        .role = BELLWIN_WIDGET_SLIDER,
        .name = L"Maximum interval",
        .automationId = L"MaximumInterval",
        .minimum = 30,
        .maximum = 480,
        .keyStep = 30,
        .wheelStep = 30,
        .ticks = 16,
        .value = &g_app.settings.maximumMinutes,
        .clamp = clamp_maximum_interval,
        .committed = schedule_next_bell,
    },
    {
        .id = CONTROL_QUIET_START,
        .role = BELLWIN_WIDGET_TIME_GROUP,
        .name = L"Quiet hours start",
        .automationId = L"QuietStart",
        .value = &g_app.settings.quietStartMinutes,
        .committed = schedule_next_bell,
    },
    {
        .id = CONTROL_QUIET_END,
        .role = BELLWIN_WIDGET_TIME_GROUP,
        .name = L"Quiet hours end",
        .automationId = L"QuietEnd",
        .value = &g_app.settings.quietEndMinutes,
        .committed = schedule_next_bell,
    },
    {
        .id = CONTROL_AUTOSTART,
        .role = BELLWIN_WIDGET_TOGGLE,
        .name = L"Launch at login",
        .automationId = L"Autostart",
        .value = &g_app.autoStart,
        .invoke = activate_autostart,
    },
    {
        .id = CONTROL_INSTALL,
        .role = BELLWIN_WIDGET_BUTTON,
        .name = L"Install",
        .automationId = L"Install",
        .invoke = activate_install,
        .visible = install_visible,
    },
};

const Widget *widget_by_id(ControlId id) {
    for (int i = 0; i < WIDGET_COUNT; ++i) {
        if (WIDGETS[i].id == id) return &WIDGETS[i];
    }
    return NULL;
}

int widget_index_of(ControlId id) {
    for (int i = 0; i < WIDGET_COUNT; ++i) {
        if (WIDGETS[i].id == id) return i;
    }
    return -1;
}

int widget_is_visible(const Widget *widget) {
    return !widget->visible || widget->visible();
}

int is_time_control(ControlId control) {
    const Widget *widget = widget_by_id(control);
    return widget && widget->role == BELLWIN_WIDGET_TIME_GROUP;
}

int control_has_visible_focus(ControlId control) {
    return g_app.windowFocused
        && g_app.focusVisibility == FOCUS_VISIBLE
        && g_app.focusedControl == control;
}

int control_has_focus(ControlId control) {
    return g_app.windowFocused && g_app.focusedControl == control;
}

void update_pointer_state(float x, float y, int down) {
    if (!bellwin_ui_is_ready()) return;
    Clay_SetPointerState((Clay_Vector2){x, y}, down != 0);
}

HitInfo hit_test_pointer(float x, float y) {
    HitInfo hit = {HIT_NONE, CONTROL_NONE, BELLWIN_TIME_HOURS, 0};
    if (!bellwin_ui_is_ready()) return hit;
    for (int i = 0; i < WIDGET_COUNT; ++i) {
        const Widget *widget = &WIDGETS[i];
        if (!widget_is_visible(widget)) continue;
        if (!Clay_PointerOver(bellwin_ui_hit_id(widget->id))) continue;
        hit.control = widget->id;
        switch (widget->role) {
        case BELLWIN_WIDGET_SLIDER:
            hit.kind = HIT_SLIDER;
            return hit;
        case BELLWIN_WIDGET_TIME_GROUP: {
            Clay_ElementData data = Clay_GetElementData(bellwin_ui_hit_id(widget->id));
            if (!data.found) break;
            BellwinTimeBoxMetrics metrics = bellwin_time_box_metrics(data.boundingBox, ui_scale());
            if (bellwin_box_contains(metrics.hours, x, y)) {
                hit.kind = HIT_TIME_SEGMENT;
                hit.segment = BELLWIN_TIME_HOURS;
            } else if (bellwin_box_contains(metrics.minutes, x, y)) {
                hit.kind = HIT_TIME_SEGMENT;
                hit.segment = BELLWIN_TIME_MINUTES;
            } else if (bellwin_box_contains(metrics.stepper, x, y)) {
                hit.kind = HIT_TIME_STEPPER;
                hit.stepperUp = y < metrics.stepperMidY;
            } else {
                hit.control = CONTROL_NONE; /* colon and box edges are inert */
            }
            return hit;
        }
        case BELLWIN_WIDGET_TOGGLE:
            hit.kind = HIT_TOGGLE;
            return hit;
        case BELLWIN_WIDGET_BUTTON:
            hit.kind = HIT_INSTALL;
            return hit;
        }
    }
    hit.control = CONTROL_NONE;
    return hit;
}

static int ui_pointer_state_equal(UiPointerState left, UiPointerState right) {
    return left.control == right.control && left.part == right.part;
}

static UiPointerState ui_pointer_state_from_hit(HitInfo hit) {
    UiPointerState state = {CONTROL_NONE, UI_PART_NONE};
    state.control = hit.control;
    if (hit.kind == HIT_SLIDER) state.part = UI_PART_SLIDER;
    else if (hit.kind == HIT_TIME_STEPPER) {
        state.part = hit.stepperUp ? UI_PART_TIME_UP : UI_PART_TIME_DOWN;
    } else if (hit.kind == HIT_TOGGLE) state.part = UI_PART_TOGGLE;
    else state.control = CONTROL_NONE;
    return state;
}

static int ui_animation_policy_enabled(void) {
    BOOL enabled = TRUE;
    if (!SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0)) {
        enabled = TRUE;
    }
    return enabled && g_app.theme.mode != BELLWIN_THEME_HIGH_CONTRAST;
}

static int ui_motion_allowed(void) {
    return g_app.uiMotionInitialized
        && g_app.uiAnimationsEnabled
        && g_app.window
        && IsWindowVisible(g_app.window);
}

static int ui_any_motion_active(void) {
    if (!g_app.uiMotionInitialized) return 0;
    for (int i = 0; i < UI_MOTION_SLOT_COUNT; ++i) {
        if (g_app.partMotions[i].hover.active
            || g_app.partMotions[i].pressed.active) return 1;
    }
    return g_app.toggleStateMotion.active
        || bellwin_ui_switch_motion_active(&g_app.toggleMotion);
}

static void ui_finish_all_motion(void) {
    if (!g_app.uiMotionInitialized) return;
    for (int i = 0; i < UI_MOTION_SLOT_COUNT; ++i) {
        bellwin_ui_motion_finish(&g_app.partMotions[i].hover);
        bellwin_ui_motion_finish(&g_app.partMotions[i].pressed);
    }
    bellwin_ui_motion_finish(&g_app.toggleStateMotion);
    bellwin_ui_switch_motion_finish(&g_app.toggleMotion);
    if (g_app.uiAnimationTimerActive && g_app.window) {
        KillTimer(g_app.window, BELLWIN_UI_ANIMATION_TIMER_ID);
        g_app.uiAnimationTimerActive = 0;
    }
}

static void ui_sync_animation_timer(void) {
    int active = ui_any_motion_active();
    if (active && !g_app.uiAnimationTimerActive && g_app.window) {
        if (SetTimer(g_app.window, BELLWIN_UI_ANIMATION_TIMER_ID, 16, NULL)) {
            g_app.uiAnimationTimerActive = 1;
        } else {
            ui_finish_all_motion();
        }
    } else if (!active && g_app.uiAnimationTimerActive && g_app.window) {
        KillTimer(g_app.window, BELLWIN_UI_ANIMATION_TIMER_ID);
        g_app.uiAnimationTimerActive = 0;
    }
}

static void ui_motion_changed(void) {
    if (g_app.window) InvalidateRect(g_app.window, NULL, FALSE);
    ui_sync_animation_timer();
}

static uint32_t ui_slot_duration(int slot, int entering) {
    if (slot >= UI_MOTION_VOLUME_SLIDER && slot <= UI_MOTION_MAXIMUM_SLIDER) {
        return entering ? BELLWIN_UI_MOTION_NORMAL_MS : BELLWIN_UI_MOTION_FAST_MS;
    }
    return BELLWIN_UI_MOTION_FASTER_MS;
}

static void ui_set_hovered_pointer(UiPointerState state) {
    if (!g_app.uiMotionInitialized
        || ui_pointer_state_equal(g_app.hoveredPointer, state)) return;

    g_app.hoveredPointer = state;
    int selectedSlot = ui_motion_slot_for(state.control, state.part);
    uint64_t now = GetTickCount64();
    int animate = ui_motion_allowed();
    for (int slot = 0; slot < UI_MOTION_SLOT_COUNT; ++slot) {
        int selected = slot == selectedSlot;
        bellwin_ui_motion_start(
            &g_app.partMotions[slot].hover,
            selected ? 1.0f : 0.0f,
            now,
            ui_slot_duration(slot, selected),
            animate
        );
    }
    bellwin_ui_switch_motion_set_hover(
        &g_app.toggleMotion,
        selectedSlot == UI_MOTION_AUTOSTART,
        now,
        animate
    );
    ui_motion_changed();
}

void ui_update_hover_from_hit(HitInfo hit) {
    ui_set_hovered_pointer(ui_pointer_state_from_hit(hit));
}

static void ui_set_pressed_pointer(UiPointerState state) {
    if (!g_app.uiMotionInitialized
        || ui_pointer_state_equal(g_app.pressedPointer, state)) return;

    g_app.pressedPointer = state;
    int selectedSlot = ui_motion_slot_for(state.control, state.part);
    uint64_t now = GetTickCount64();
    int animate = ui_motion_allowed();
    for (int slot = 0; slot < UI_MOTION_SLOT_COUNT; ++slot) {
        int selected = slot == selectedSlot;
        bellwin_ui_motion_start(
            &g_app.partMotions[slot].pressed,
            selected ? 1.0f : 0.0f,
            now,
            ui_slot_duration(slot, selected),
            animate
        );
    }
    ui_motion_changed();
}

void ui_begin_pointer_press(HitInfo hit) {
    UiPointerState state = ui_pointer_state_from_hit(hit);
    ui_set_pressed_pointer(state);
    if (state.part == UI_PART_TOGGLE) {
        bellwin_ui_switch_motion_press(
            &g_app.toggleMotion,
            GetTickCount64(),
            ui_motion_allowed()
        );
        ui_motion_changed();
    }
}

void ui_end_pointer_press(void) {
    ui_set_pressed_pointer((UiPointerState){CONTROL_NONE, UI_PART_NONE});
}

void ui_cancel_toggle_visual(void) {
    if (!g_app.uiMotionInitialized) return;
    int hovered = g_app.hoveredPointer.control == CONTROL_AUTOSTART
        && g_app.hoveredPointer.part == UI_PART_TOGGLE;
    bellwin_ui_switch_motion_cancel(
        &g_app.toggleMotion,
        hovered,
        GetTickCount64(),
        ui_motion_allowed()
    );
    ui_motion_changed();
}

void ui_set_toggle_checked_visual(int checked) {
    if (!g_app.uiMotionInitialized) return;
    uint64_t now = GetTickCount64();
    int animate = ui_motion_allowed();
    int hovered = g_app.hoveredPointer.control == CONTROL_AUTOSTART
        && g_app.hoveredPointer.part == UI_PART_TOGGLE;
    if (g_app.toggleMotion.pressed) {
        bellwin_ui_switch_motion_release(
            &g_app.toggleMotion,
            checked,
            hovered,
            now,
            animate
        );
    } else {
        bellwin_ui_switch_motion_set_checked(
            &g_app.toggleMotion,
            checked,
            now,
            animate
        );
    }
    bellwin_ui_motion_start(
        &g_app.toggleStateMotion,
        checked ? 1.0f : 0.0f,
        now,
        BELLWIN_UI_MOTION_FASTER_MS,
        animate
    );
    ui_motion_changed();
}

void ui_animation_tick(void) {
    if (!g_app.uiMotionInitialized) return;
    uint64_t now = GetTickCount64();
    for (int i = 0; i < UI_MOTION_SLOT_COUNT; ++i) {
        bellwin_ui_motion_tick(&g_app.partMotions[i].hover, now);
        bellwin_ui_motion_tick(&g_app.partMotions[i].pressed, now);
    }
    bellwin_ui_motion_tick(&g_app.toggleStateMotion, now);
    bellwin_ui_switch_motion_tick(&g_app.toggleMotion, now);
    ui_motion_changed();
}

static void ui_reset_motion_values(void) {
    for (int i = 0; i < UI_MOTION_SLOT_COUNT; ++i) {
        bellwin_ui_motion_init(&g_app.partMotions[i].hover, 0.0f);
        bellwin_ui_motion_init(&g_app.partMotions[i].pressed, 0.0f);
    }
    g_app.hoveredPointer = (UiPointerState){CONTROL_NONE, UI_PART_NONE};
    g_app.pressedPointer = (UiPointerState){CONTROL_NONE, UI_PART_NONE};
    bellwin_ui_motion_init(&g_app.toggleStateMotion, g_app.autoStart ? 1.0f : 0.0f);
    bellwin_ui_switch_motion_init(&g_app.toggleMotion, g_app.autoStart, 0);
    g_app.togglePointer = (BellwinTogglePointerInteraction){0};
}

void ui_initialize_motion(void) {
    ui_reset_motion_values();
    g_app.uiAnimationsEnabled = ui_animation_policy_enabled();
    g_app.uiMotionInitialized = 1;
}

void ui_reset_motion_for_hidden_window(void) {
    if (!g_app.uiMotionInitialized) return;
    ui_finish_all_motion();
    ui_reset_motion_values();
    g_app.hoverInstall = 0;
}

void ui_refresh_animation_policy(void) {
    int enabled = ui_animation_policy_enabled();
    if (!g_app.uiMotionInitialized || enabled == g_app.uiAnimationsEnabled) return;
    g_app.uiAnimationsEnabled = enabled;
    if (!enabled) {
        ui_finish_all_motion();
        if (g_app.window) InvalidateRect(g_app.window, NULL, FALSE);
    }
}

int consume_wheel_steps(
    WheelTargetKind kind,
    ControlId control,
    BellwinTimeSegment segment,
    int delta
) {
    if (g_app.wheel.kind != kind
        || g_app.wheel.control != control
        || g_app.wheel.segment != segment) {
        g_app.wheel.kind = kind;
        g_app.wheel.control = control;
        g_app.wheel.segment = segment;
        g_app.wheel.remainder = 0;
    }
    g_app.wheel.remainder += delta;
    int steps = g_app.wheel.remainder / WHEEL_DELTA;
    g_app.wheel.remainder -= steps * WHEEL_DELTA;
    return steps;
}

void set_focus_visibility(FocusVisibility visibility) {
    if (g_app.focusVisibility == visibility) return;
    g_app.focusVisibility = visibility;
    InvalidateRect(g_app.window, NULL, FALSE);
}

void focus_control_part(
    ControlId control,
    FocusVisibility visibility,
    int chooseTimeSegment,
    BellwinTimeSegment segment
) {
    int index = widget_index_of(control);
    while (index > 0 && !widget_is_visible(&WIDGETS[index])) --index;
    if (index >= 0) control = WIDGETS[index].id;
    int windowAlreadyFocused = GetFocus() == g_app.window;
    g_app.focusVisibility = visibility;
    int controlChanged = g_app.focusedControl != control;
    int segmentChanged = 0;
    if (controlChanged) {
        g_app.focusedControl = control;
        g_app.timeEdit.digitCount = 0;
        if (is_time_control(control)) {
            BellwinTimeSegment nextSegment = chooseTimeSegment ? segment : BELLWIN_TIME_HOURS;
            segmentChanged = g_app.timeEdit.segment != nextSegment;
            g_app.timeEdit.segment = nextSegment;
        }
    } else if (chooseTimeSegment && is_time_control(control)
            && g_app.timeEdit.segment != segment) {
        g_app.timeEdit.segment = segment;
        g_app.timeEdit.digitCount = 0;
        segmentChanged = 1;
    }
    if (!windowAlreadyFocused) SetFocus(g_app.window);
    if ((controlChanged || segmentChanged) && windowAlreadyFocused) {
        uia_notify_focus(control, g_app.timeEdit.segment);
    }
    InvalidateRect(g_app.window, NULL, FALSE);
}

void focus_control(ControlId control, FocusVisibility visibility) {
    focus_control_part(control, visibility, 0, BELLWIN_TIME_HOURS);
}

void select_time_segment(BellwinTimeSegment segment) {
    if (!is_time_control(g_app.focusedControl) || g_app.timeEdit.segment == segment) return;
    focus_control_part(g_app.focusedControl, g_app.focusVisibility, 1, segment);
}

void move_focus(int direction) {
    int focusable[WIDGET_COUNT];
    for (int i = 0; i < WIDGET_COUNT; ++i) focusable[i] = widget_is_visible(&WIDGETS[i]);
    int index = bellwin_next_focus_index(
        focusable, WIDGET_COUNT, widget_index_of(g_app.focusedControl), direction
    );
    if (index >= 0) focus_control(WIDGETS[index].id, FOCUS_VISIBLE);
}

static void widget_write_value(const Widget *widget, int value) {
    if (widget->role == BELLWIN_WIDGET_TIME_GROUP) {
        *widget->value = bellwin_normalize_day_minute(value);
    } else {
        *widget->value = bellwin_clamp_int(value, widget->minimum, widget->maximum);
    }
    if (widget->clamp) widget->clamp();
}

static void widget_commit(const Widget *widget) {
    save_settings();
    if (widget->committed) widget->committed();
}

void widget_set_value(const Widget *widget, int value, int persist) {
    int oldValues[WIDGET_COUNT] = {0};
    for (int i = 0; i < WIDGET_COUNT; ++i) {
        if (WIDGETS[i].value) oldValues[i] = *WIDGETS[i].value;
    }
    widget_write_value(widget, value);
    if (persist) widget_commit(widget);
    InvalidateRect(g_app.window, NULL, FALSE);
    for (int i = 0; i < WIDGET_COUNT; ++i) {
        if (WIDGETS[i].value && *WIDGETS[i].value != oldValues[i]) {
            uia_notify_value(&WIDGETS[i], oldValues[i], *WIDGETS[i].value);
        }
    }
}

void widget_step(const Widget *widget, int steps, int wheel) {
    int step = wheel ? widget->wheelStep : widget->keyStep;
    widget_set_value(widget, *widget->value + steps * step, 1);
}

void invoke_widget(ControlId id) {
    const Widget *widget = widget_by_id(id);
    if (widget && widget->invoke && widget_is_visible(widget)) widget->invoke();
}

void update_slider_from_mouse(const Widget *widget, float x) {
    if (!widget) return;
    Clay_ElementData track = Clay_GetElementData(bellwin_ui_track_id(widget->id));
    if (!track.found) return;
    int value = bellwin_slider_value_from_position(
        widget->minimum, widget->maximum, widget->keyStep, track.boundingBox, x
    );
    widget_set_value(widget, value, 0);
}

void wheel_client_point(LPARAM lParam, float *x, float *y) {
    POINT point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    ScreenToClient(g_app.window, &point);
    *x = (float)point.x;
    *y = (float)point.y;
}

int direct_gesture_delta(WPARAM wParam) {
    /* Treat touchpad scrolling as direct manipulation: up/right increases. */
    return -GET_WHEEL_DELTA_WPARAM(wParam);
}

void finish_slider_drag(void) {
    if (!g_app.draggingSlider) return;
    const Widget *finished = widget_by_id(g_app.draggingSlider);
    g_app.draggingSlider = CONTROL_NONE;
    if (finished) widget_commit(finished);
}

void step_time_value(const Widget *widget, BellwinTimeSegment segment, int delta) {
    if (g_app.focusedControl == widget->id && g_app.timeEdit.segment == segment) {
        g_app.timeEdit.digitCount = 0;
    }
    widget_set_value(widget, bellwin_step_time_segment(*widget->value, segment, delta), 1);
}

void shift_time_minutes(const Widget *widget, int deltaMinutes) {
    if (g_app.focusedControl == widget->id) g_app.timeEdit.digitCount = 0;
    widget_set_value(widget, *widget->value + deltaMinutes, 1);
}

void enter_time_digit(int digit) {
    const Widget *widget = widget_by_id(g_app.focusedControl);
    if (!widget || widget->role != BELLWIN_WIDGET_TIME_GROUP) return;
    int value;
    if (g_app.timeEdit.digitCount == 0) {
        g_app.timeEdit.firstDigit = digit;
        g_app.timeEdit.digitCount = 1;
        value = digit;
    } else {
        value = g_app.timeEdit.firstDigit * 10 + digit;
        g_app.timeEdit.digitCount = 0;
    }
    int updated = bellwin_set_time_segment(*widget->value, g_app.timeEdit.segment, value);
    widget_set_value(widget, updated, 1);
    if (g_app.timeEdit.digitCount == 0 && g_app.timeEdit.segment == BELLWIN_TIME_HOURS) {
        select_time_segment(BELLWIN_TIME_MINUTES);
    }
}

static int widget_key_from_vk(WPARAM vk, BellwinWidgetKey *key) {
    if (vk == VK_LEFT) *key = BELLWIN_WIDGET_KEY_LEFT;
    else if (vk == VK_RIGHT) *key = BELLWIN_WIDGET_KEY_RIGHT;
    else if (vk == VK_UP) *key = BELLWIN_WIDGET_KEY_UP;
    else if (vk == VK_DOWN) *key = BELLWIN_WIDGET_KEY_DOWN;
    else if (vk == VK_SPACE || vk == VK_RETURN) *key = BELLWIN_WIDGET_KEY_ACTIVATE;
    else return 0;
    return 1;
}

int handle_widget_key(const Widget *widget, WPARAM vk, int repeat) {
    BellwinWidgetKey key;
    if (!widget_key_from_vk(vk, &key)) return 0;
    BellwinWidgetAction action = bellwin_widget_key_action(widget->role, key);
    switch (action.kind) {
    case BELLWIN_WIDGET_ACTION_STEP_VALUE:
        widget_step(widget, action.amount, 0);
        return 1;
    case BELLWIN_WIDGET_ACTION_NEXT_SEGMENT:
        select_time_segment(bellwin_next_time_segment(g_app.timeEdit.segment));
        return 1;
    case BELLWIN_WIDGET_ACTION_STEP_SEGMENT:
        step_time_value(widget, g_app.timeEdit.segment, action.amount);
        return 1;
    case BELLWIN_WIDGET_ACTION_INVOKE:
        if (!repeat) invoke_widget(widget->id);
        return 1;
    case BELLWIN_WIDGET_ACTION_NONE:
        break;
    }
    return 0;
}
