#include <assert.h>
#include <stdio.h>

#include "../ui_motion.h"
#include "../widgets.h"

static int nearly_equal(float actual, float expected, float tolerance) {
    float difference = actual - expected;
    if (difference < 0.0f) difference = -difference;
    return difference <= tolerance;
}

static void test_ui_motion_uses_monotonic_time_and_winui_easing(void) {
    assert(nearly_equal(bellwin_ui_lerp(10.0f, 20.0f, 0.25f), 12.5f, 0.0001f));
    assert(nearly_equal(bellwin_ui_motion_progress(100, 100, 50), 0.0f, 0.0001f));
    assert(nearly_equal(bellwin_ui_motion_progress(100, 100, 150), 0.5f, 0.0001f));
    assert(nearly_equal(bellwin_ui_motion_progress(100, 100, 250), 1.0f, 0.0001f));

    assert(nearly_equal(bellwin_ui_fast_out_slow_in(0.0f), 0.0f, 0.0001f));
    assert(nearly_equal(bellwin_ui_fast_out_slow_in(0.5f), 0.8899f, 0.0002f));
    assert(nearly_equal(bellwin_ui_fast_out_slow_in(1.0f), 1.0f, 0.0001f));
}

static void test_ui_motion_reverses_from_the_current_visual_value(void) {
    BellwinUiMotionValue motion;
    bellwin_ui_motion_init(&motion, 0.0f);
    bellwin_ui_motion_start(&motion, 1.0f, 100, 100, 1);
    assert(motion.active);
    assert(nearly_equal(motion.value, 0.0f, 0.0001f));

    bellwin_ui_motion_tick(&motion, 150);
    float valueAtReverse = motion.value;
    assert(nearly_equal(valueAtReverse, 0.8899f, 0.0002f));

    bellwin_ui_motion_start(&motion, 0.0f, 150, 100, 1);
    assert(nearly_equal(motion.value, valueAtReverse, 0.0001f));
    assert(nearly_equal(motion.from, valueAtReverse, 0.0001f));
    assert(motion.active);

    bellwin_ui_motion_tick(&motion, 250);
    assert(!motion.active);
    assert(nearly_equal(motion.value, 0.0f, 0.0001f));
}

static void test_ui_motion_snaps_when_animation_is_disabled(void) {
    BellwinUiMotionValue motion;
    bellwin_ui_motion_init(&motion, 0.25f);
    bellwin_ui_motion_start(&motion, 1.0f, 100, 167, 0);
    assert(!motion.active);
    assert(nearly_equal(motion.value, 1.0f, 0.0001f));
}

static void test_toggle_pointer_commits_only_on_release_inside(void) {
    BellwinTogglePointerInteraction interaction = {0};
    int desired = -1;

    bellwin_toggle_pointer_begin(&interaction, 0);
    assert(interaction.pending);
    assert(interaction.desired == 1);
    assert(bellwin_toggle_pointer_release(&interaction, 0, &desired)
        == BELLWIN_TOGGLE_POINTER_CANCEL);
    assert(!interaction.pending);
    assert(desired == 1);

    bellwin_toggle_pointer_begin(&interaction, 1);
    assert(interaction.desired == 0);
    assert(bellwin_toggle_pointer_release(&interaction, 1, &desired)
        == BELLWIN_TOGGLE_POINTER_COMMIT);
    assert(!interaction.pending);
    assert(desired == 0);
}

static void test_toggle_pointer_capture_cancel_clears_pending_commit(void) {
    BellwinTogglePointerInteraction interaction = {0};
    bellwin_toggle_pointer_begin(&interaction, 0);
    assert(bellwin_toggle_pointer_cancel(&interaction)
        == BELLWIN_TOGGLE_POINTER_CANCEL);
    assert(!interaction.pending);
    assert(bellwin_toggle_pointer_cancel(&interaction)
        == BELLWIN_TOGGLE_POINTER_NONE);
}

static void assert_switch_knob_rect(
    BellwinUiRect rect,
    float x,
    float y,
    float width,
    float height
) {
    assert(nearly_equal(rect.x, x, 0.0001f));
    assert(nearly_equal(rect.y, y, 0.0001f));
    assert(nearly_equal(rect.width, width, 0.0001f));
    assert(nearly_equal(rect.height, height, 0.0001f));
    assert(nearly_equal(rect.y + rect.height * 0.5f, 10.0f, 0.0001f));
}

static void test_switch_knob_targets_stretch_toward_travel(void) {
    BellwinUiRect offNormal = bellwin_ui_switch_knob_target(
        0,
        BELLWIN_UI_SWITCH_NORMAL
    );
    BellwinUiRect offHover = bellwin_ui_switch_knob_target(
        0,
        BELLWIN_UI_SWITCH_HOVERED
    );
    BellwinUiRect offPressed = bellwin_ui_switch_knob_target(
        0,
        BELLWIN_UI_SWITCH_PRESSED
    );
    assert_switch_knob_rect(offNormal, 4.0f, 4.0f, 12.0f, 12.0f);
    assert_switch_knob_rect(offHover, 3.0f, 3.0f, 14.0f, 14.0f);
    assert_switch_knob_rect(offPressed, 3.0f, 3.0f, 17.0f, 14.0f);
    assert(offPressed.x + offPressed.width > offHover.x + offHover.width);

    BellwinUiRect onNormal = bellwin_ui_switch_knob_target(
        1,
        BELLWIN_UI_SWITCH_NORMAL
    );
    BellwinUiRect onHover = bellwin_ui_switch_knob_target(
        1,
        BELLWIN_UI_SWITCH_HOVERED
    );
    BellwinUiRect onPressed = bellwin_ui_switch_knob_target(
        1,
        BELLWIN_UI_SWITCH_PRESSED
    );
    assert_switch_knob_rect(onNormal, 24.0f, 4.0f, 12.0f, 12.0f);
    assert_switch_knob_rect(onHover, 23.0f, 3.0f, 14.0f, 14.0f);
    assert_switch_knob_rect(onPressed, 20.0f, 3.0f, 17.0f, 14.0f);
    assert(onPressed.x < onHover.x);
}

static void test_switch_motion_runs_press_travel_and_settle_sequence(void) {
    BellwinUiSwitchMotion motion;
    bellwin_ui_switch_motion_init(&motion, 0, 0);
    assert_switch_knob_rect(
        bellwin_ui_switch_motion_rect(&motion),
        4.0f,
        4.0f,
        12.0f,
        12.0f
    );

    bellwin_ui_switch_motion_set_hover(&motion, 1, 0, 1);
    assert(!bellwin_ui_switch_motion_tick(&motion, BELLWIN_UI_MOTION_FASTER_MS));
    assert_switch_knob_rect(
        bellwin_ui_switch_motion_rect(&motion),
        3.0f,
        3.0f,
        14.0f,
        14.0f
    );

    bellwin_ui_switch_motion_press(&motion, 100, 1);
    bellwin_ui_switch_motion_tick(&motion, 100 + BELLWIN_UI_MOTION_FASTER_MS);
    assert_switch_knob_rect(
        bellwin_ui_switch_motion_rect(&motion),
        3.0f,
        3.0f,
        17.0f,
        14.0f
    );

    bellwin_ui_switch_motion_release(&motion, 1, 1, 200, 1);
    BellwinUiRect atRelease = bellwin_ui_switch_motion_rect(&motion);
    assert_switch_knob_rect(atRelease, 3.0f, 3.0f, 17.0f, 14.0f);

    bellwin_ui_switch_motion_tick(&motion, 283);
    BellwinUiRect duringTravel = bellwin_ui_switch_motion_rect(&motion);
    assert(duringTravel.x > 3.0f && duringTravel.x < 23.0f);
    assert(duringTravel.width > 14.0f && duringTravel.width < 17.0f);
    assert(nearly_equal(
        duringTravel.y + duringTravel.height * 0.5f,
        10.0f,
        0.0001f
    ));

    assert(!bellwin_ui_switch_motion_tick(&motion, 200 + BELLWIN_UI_MOTION_FAST_MS));
    assert_switch_knob_rect(
        bellwin_ui_switch_motion_rect(&motion),
        23.0f,
        3.0f,
        14.0f,
        14.0f
    );

    bellwin_ui_switch_motion_set_hover(&motion, 0, 400, 1);
    assert(!bellwin_ui_switch_motion_tick(&motion, 400 + BELLWIN_UI_MOTION_FASTER_MS));
    assert_switch_knob_rect(
        bellwin_ui_switch_motion_rect(&motion),
        24.0f,
        4.0f,
        12.0f,
        12.0f
    );
}

static void test_switch_motion_reverses_without_a_geometry_jump(void) {
    BellwinUiSwitchMotion motion;
    bellwin_ui_switch_motion_init(&motion, 0, 0);
    bellwin_ui_switch_motion_set_checked(&motion, 1, 0, 1);
    bellwin_ui_switch_motion_tick(&motion, 50);
    BellwinUiRect beforeReverse = bellwin_ui_switch_motion_rect(&motion);

    bellwin_ui_switch_motion_set_checked(&motion, 0, 50, 1);
    BellwinUiRect afterReverse = bellwin_ui_switch_motion_rect(&motion);
    assert_switch_knob_rect(
        afterReverse,
        beforeReverse.x,
        beforeReverse.y,
        beforeReverse.width,
        beforeReverse.height
    );

    assert(!bellwin_ui_switch_motion_tick(&motion, 50 + BELLWIN_UI_MOTION_FAST_MS));
    assert_switch_knob_rect(
        bellwin_ui_switch_motion_rect(&motion),
        4.0f,
        4.0f,
        12.0f,
        12.0f
    );
}

static void test_switch_motion_snaps_all_phases_when_disabled(void) {
    BellwinUiSwitchMotion motion;
    bellwin_ui_switch_motion_init(&motion, 0, 0);

    bellwin_ui_switch_motion_press(&motion, 0, 0);
    assert(!bellwin_ui_switch_motion_active(&motion));
    assert_switch_knob_rect(
        bellwin_ui_switch_motion_rect(&motion),
        3.0f,
        3.0f,
        17.0f,
        14.0f
    );

    bellwin_ui_switch_motion_release(&motion, 1, 0, 10, 0);
    assert(!bellwin_ui_switch_motion_active(&motion));
    assert_switch_knob_rect(
        bellwin_ui_switch_motion_rect(&motion),
        24.0f,
        4.0f,
        12.0f,
        12.0f
    );
}

static void test_switch_motion_cancel_and_policy_change_finish_cleanly(void) {
    BellwinUiSwitchMotion motion;
    bellwin_ui_switch_motion_init(&motion, 0, 1);
    bellwin_ui_switch_motion_press(&motion, 0, 1);
    bellwin_ui_switch_motion_tick(&motion, BELLWIN_UI_MOTION_FASTER_MS);

    bellwin_ui_switch_motion_cancel(&motion, 1, 100, 1);
    assert(!bellwin_ui_switch_motion_tick(&motion, 100 + BELLWIN_UI_MOTION_FASTER_MS));
    assert(!motion.checked);
    assert_switch_knob_rect(
        bellwin_ui_switch_motion_rect(&motion),
        3.0f,
        3.0f,
        14.0f,
        14.0f
    );

    bellwin_ui_switch_motion_set_checked(&motion, 1, 200, 1);
    bellwin_ui_switch_motion_tick(&motion, 250);
    assert(bellwin_ui_switch_motion_active(&motion));
    bellwin_ui_switch_motion_finish(&motion);
    assert(!bellwin_ui_switch_motion_active(&motion));
    assert_switch_knob_rect(
        bellwin_ui_switch_motion_rect(&motion),
        23.0f,
        3.0f,
        14.0f,
        14.0f
    );
}

static void test_slider_key_actions(void) {
    BellwinWidgetAction action = bellwin_widget_key_action(BELLWIN_WIDGET_SLIDER, BELLWIN_WIDGET_KEY_LEFT);
    assert(action.kind == BELLWIN_WIDGET_ACTION_STEP_VALUE);
    assert(action.amount == -1);
    action = bellwin_widget_key_action(BELLWIN_WIDGET_SLIDER, BELLWIN_WIDGET_KEY_RIGHT);
    assert(action.kind == BELLWIN_WIDGET_ACTION_STEP_VALUE);
    assert(action.amount == 1);
    assert(bellwin_widget_key_action(BELLWIN_WIDGET_SLIDER, BELLWIN_WIDGET_KEY_UP).kind == BELLWIN_WIDGET_ACTION_NONE);
    assert(bellwin_widget_key_action(BELLWIN_WIDGET_SLIDER, BELLWIN_WIDGET_KEY_ACTIVATE).kind == BELLWIN_WIDGET_ACTION_NONE);
}

static void test_time_group_key_actions(void) {
    assert(bellwin_widget_key_action(BELLWIN_WIDGET_TIME_GROUP, BELLWIN_WIDGET_KEY_LEFT).kind == BELLWIN_WIDGET_ACTION_NEXT_SEGMENT);
    assert(bellwin_widget_key_action(BELLWIN_WIDGET_TIME_GROUP, BELLWIN_WIDGET_KEY_RIGHT).kind == BELLWIN_WIDGET_ACTION_NEXT_SEGMENT);
    BellwinWidgetAction action = bellwin_widget_key_action(BELLWIN_WIDGET_TIME_GROUP, BELLWIN_WIDGET_KEY_UP);
    assert(action.kind == BELLWIN_WIDGET_ACTION_STEP_SEGMENT);
    assert(action.amount == 1);
    action = bellwin_widget_key_action(BELLWIN_WIDGET_TIME_GROUP, BELLWIN_WIDGET_KEY_DOWN);
    assert(action.kind == BELLWIN_WIDGET_ACTION_STEP_SEGMENT);
    assert(action.amount == -1);
    assert(bellwin_widget_key_action(BELLWIN_WIDGET_TIME_GROUP, BELLWIN_WIDGET_KEY_ACTIVATE).kind == BELLWIN_WIDGET_ACTION_NONE);
}

static void test_activation_key_actions(void) {
    assert(bellwin_widget_key_action(BELLWIN_WIDGET_TOGGLE, BELLWIN_WIDGET_KEY_ACTIVATE).kind == BELLWIN_WIDGET_ACTION_INVOKE);
    assert(bellwin_widget_key_action(BELLWIN_WIDGET_BUTTON, BELLWIN_WIDGET_KEY_ACTIVATE).kind == BELLWIN_WIDGET_ACTION_INVOKE);
    assert(bellwin_widget_key_action(BELLWIN_WIDGET_TOGGLE, BELLWIN_WIDGET_KEY_LEFT).kind == BELLWIN_WIDGET_ACTION_NONE);
    assert(bellwin_widget_key_action(BELLWIN_WIDGET_BUTTON, BELLWIN_WIDGET_KEY_DOWN).kind == BELLWIN_WIDGET_ACTION_NONE);
}

static void test_snap_value(void) {
    assert(bellwin_snap_value(147, 30, 480, 30) == 150);
    assert(bellwin_snap_value(44, 30, 480, 30) == 30);
    assert(bellwin_snap_value(45, 30, 480, 30) == 60);
    assert(bellwin_snap_value(500, 30, 480, 30) == 480);
    assert(bellwin_snap_value(7, 30, 480, 30) == 30);
    assert(bellwin_snap_value(55, 0, 100, 1) == 55);
    assert(bellwin_snap_value(-3, 0, 100, 1) == 0);
    assert(bellwin_snap_value(120, 0, 100, 1) == 100);
}

static void test_focus_traversal_wraps(void) {
    int focusable[4] = {1, 1, 1, 1};
    assert(bellwin_next_focus_index(focusable, 4, -1, 1) == 0);
    assert(bellwin_next_focus_index(focusable, 4, -1, -1) == 3);
    assert(bellwin_next_focus_index(focusable, 4, 3, 1) == 0);
    assert(bellwin_next_focus_index(focusable, 4, 0, -1) == 3);
    assert(bellwin_next_focus_index(focusable, 4, 1, 1) == 2);
    assert(bellwin_next_focus_index(focusable, 4, 2, -1) == 1);
}

static void test_focus_traversal_skips_hidden(void) {
    int focusable[4] = {1, 1, 1, 0};
    assert(bellwin_next_focus_index(focusable, 4, 2, 1) == 0);
    assert(bellwin_next_focus_index(focusable, 4, 0, -1) == 2);
    assert(bellwin_next_focus_index(focusable, 4, -1, -1) == 2);
    int none[2] = {0, 0};
    assert(bellwin_next_focus_index(none, 2, 0, 1) == -1);
    assert(bellwin_next_focus_index(none, 2, -1, 1) == -1);
    assert(bellwin_next_focus_index(NULL, 0, -1, 1) == -1);
}

int main(void) {
    test_ui_motion_uses_monotonic_time_and_winui_easing();
    test_ui_motion_reverses_from_the_current_visual_value();
    test_ui_motion_snaps_when_animation_is_disabled();
    test_toggle_pointer_commits_only_on_release_inside();
    test_toggle_pointer_capture_cancel_clears_pending_commit();
    test_switch_knob_targets_stretch_toward_travel();
    test_switch_motion_runs_press_travel_and_settle_sequence();
    test_switch_motion_reverses_without_a_geometry_jump();
    test_switch_motion_snaps_all_phases_when_disabled();
    test_switch_motion_cancel_and_policy_change_finish_cleanly();
    test_slider_key_actions();
    test_time_group_key_actions();
    test_activation_key_actions();
    test_snap_value();
    test_focus_traversal_wraps();
    test_focus_traversal_skips_hidden();
    printf("widget tests passed\n");
    return 0;
}
