#include <assert.h>
#include <stdio.h>

#include "../widgets.h"

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
    test_slider_key_actions();
    test_time_group_key_actions();
    test_activation_key_actions();
    test_snap_value();
    test_focus_traversal_wraps();
    test_focus_traversal_skips_hidden();
    printf("widget tests passed\n");
    return 0;
}
