#ifndef BELLWIN_UI_H
#define BELLWIN_UI_H

#include "app_internal.h"

typedef enum HitKind {
    HIT_NONE,
    HIT_SLIDER,
    HIT_TIME_SEGMENT,
    HIT_TIME_STEPPER,
    HIT_TOGGLE,
    HIT_INSTALL,
} HitKind;

typedef struct HitInfo {
    HitKind kind;
    ControlId control;
    BellwinTimeSegment segment;
    int stepperUp;
} HitInfo;

enum { BELLWIN_UI_ANIMATION_TIMER_ID = 4 };

void update_pointer_state(float x, float y, int down);
HitInfo hit_test_pointer(float x, float y);
void ui_update_hover_from_hit(HitInfo hit);
void ui_begin_pointer_press(HitInfo hit);
void ui_end_pointer_press(void);
void ui_cancel_toggle_visual(void);
void ui_set_toggle_checked_visual(int checked);
void ui_animation_tick(void);
void ui_initialize_motion(void);
void ui_reset_motion_for_hidden_window(void);
void ui_refresh_animation_policy(void);
int consume_wheel_steps(WheelTargetKind kind, ControlId control, BellwinTimeSegment segment, int delta);
void set_focus_visibility(FocusVisibility visibility);
void focus_control_part(ControlId control, FocusVisibility visibility, int chooseTimeSegment, BellwinTimeSegment segment);
void focus_control(ControlId control, FocusVisibility visibility);
void select_time_segment(BellwinTimeSegment segment);
void move_focus(int direction);
void widget_set_value(const Widget *widget, int value, int persist);
void widget_step(const Widget *widget, int steps, int wheel);
void invoke_widget(ControlId id);
void update_slider_from_mouse(const Widget *widget, float x);
void wheel_client_point(LPARAM lParam, float *x, float *y);
int direct_gesture_delta(WPARAM wParam);
void finish_slider_drag(void);
void step_time_value(const Widget *widget, BellwinTimeSegment segment, int delta);
void shift_time_minutes(const Widget *widget, int deltaMinutes);
void enter_time_digit(int digit);
int handle_widget_key(const Widget *widget, WPARAM vk, int repeat);
int control_has_focus(ControlId control);
int control_has_visible_focus(ControlId control);

#endif
