#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <wchar.h>

#include "../core.h"
#include "../rendering.h"

static void test_overnight_quiet_hours(void) {
    assert(bellwin_is_quiet(22 * 60, 22 * 60, 10 * 60));
    assert(bellwin_is_quiet(5 * 60, 22 * 60, 10 * 60));
    assert(!bellwin_is_quiet(12 * 60, 22 * 60, 10 * 60));
    assert(!bellwin_is_quiet(10 * 60, 22 * 60, 10 * 60));
}

static void test_daytime_quiet_hours(void) {
    assert(bellwin_is_quiet(13 * 60, 12 * 60, 14 * 60));
    assert(!bellwin_is_quiet(11 * 60, 12 * 60, 14 * 60));
    assert(!bellwin_is_quiet(14 * 60, 12 * 60, 14 * 60));
    assert(!bellwin_is_quiet(0, 8 * 60, 8 * 60));
}

static void test_active_interval_skips_overnight_quiet_hours(void) {
    int beforeQuiet = 21 * 60 * 60 + 30 * 60;
    uint64_t secondsUntilQuiet = bellwin_seconds_until_minute_of_day(beforeQuiet, 22 * 60);
    assert(bellwin_limit_active_segment(60 * 60, secondsUntilQuiet) == 30 * 60);
    assert(bellwin_seconds_until_minute_of_day(22 * 60 * 60, 10 * 60) == 12 * 60 * 60);
    assert(bellwin_limit_active_segment(30 * 60, 12 * 60 * 60) == 30 * 60);

    secondsUntilQuiet = bellwin_seconds_until_minute_of_day(21 * 60 * 60 + 59 * 60 + 30, 22 * 60);
    assert(bellwin_limit_active_segment(60, secondsUntilQuiet) == 30);
    assert(bellwin_is_quiet(22 * 60, 22 * 60, 10 * 60));
}

static void test_active_interval_without_quiet_hours_is_unchanged(void) {
    assert(bellwin_limit_active_segment(90 * 60, UINT64_MAX) == 90 * 60);
    assert(bellwin_limit_active_segment(0, 60 * 60) == 0);
}

static void test_active_interval_skips_daytime_quiet_hours(void) {
    int beforeQuiet = 11 * 60 * 60 + 30 * 60;
    uint64_t secondsUntilQuiet = bellwin_seconds_until_minute_of_day(beforeQuiet, 12 * 60);
    assert(bellwin_limit_active_segment(60 * 60, secondsUntilQuiet) == 30 * 60);
    assert(bellwin_seconds_until_minute_of_day(12 * 60 * 60, 14 * 60) == 2 * 60 * 60);
    assert(bellwin_limit_active_segment(30 * 60, 22 * 60 * 60) == 30 * 60);
}

static void test_random_delay_is_inclusive(void) {
    assert(bellwin_random_delay_minutes(60, 240, 0) == 60);
    assert(bellwin_random_delay_minutes(60, 240, 180) == 240);
    assert(bellwin_random_delay_minutes(240, 60, 0) == 60);
}

static void test_clamping(void) {
    BellwinSettings settings = {-1, 500, 30, -30, 2000};
    bellwin_clamp_settings(&settings);
    assert(settings.volume == 0);
    assert(settings.minimumMinutes == 30);
    assert(settings.maximumMinutes == 480);
    assert(settings.quietStartMinutes == 1410);
    assert(settings.quietEndMinutes == 560);
}

static void test_time_segment_editing(void) {
    int time = 22 * 60 + 45;
    assert(bellwin_set_time_segment(time, BELLWIN_TIME_HOURS, 7) == 7 * 60 + 45);
    assert(bellwin_set_time_segment(time, BELLWIN_TIME_MINUTES, 9) == 22 * 60 + 9);
    assert(bellwin_set_time_segment(time, BELLWIN_TIME_HOURS, 99) == 23 * 60 + 45);
    assert(bellwin_set_time_segment(time, BELLWIN_TIME_MINUTES, 99) == 22 * 60 + 59);
}

static void test_time_segment_stepping_wraps_only_selected_pair(void) {
    assert(bellwin_step_time_segment(23 * 60 + 15, BELLWIN_TIME_HOURS, 1) == 15);
    assert(bellwin_step_time_segment(15, BELLWIN_TIME_HOURS, -1) == 23 * 60 + 15);
    assert(bellwin_step_time_segment(9 * 60 + 59, BELLWIN_TIME_MINUTES, 1) == 9 * 60);
    assert(bellwin_step_time_segment(9 * 60, BELLWIN_TIME_MINUTES, -1) == 9 * 60 + 59);
}

static void test_time_segment_selection_cycles(void) {
    assert(bellwin_next_time_segment(BELLWIN_TIME_HOURS) == BELLWIN_TIME_MINUTES);
    assert(bellwin_next_time_segment(BELLWIN_TIME_MINUTES) == BELLWIN_TIME_HOURS);
}

static void test_tray_click_actions(void) {
    int suppressLeftButtonUp = 0;
    assert(
        bellwin_tray_action(BELLWIN_TRAY_LEFT_BUTTON_UP, &suppressLeftButtonUp)
        == BELLWIN_TRAY_ACTION_DEFER_MENU
    );
    assert(
        bellwin_tray_action(BELLWIN_TRAY_SELECT, &suppressLeftButtonUp)
        == BELLWIN_TRAY_ACTION_NONE
    );

    assert(
        bellwin_tray_action(BELLWIN_TRAY_LEFT_BUTTON_DOUBLE_CLICK, &suppressLeftButtonUp)
        == BELLWIN_TRAY_ACTION_CANCEL_MENU_AND_SHOW_SETTINGS
    );
    assert(suppressLeftButtonUp);
    assert(
        bellwin_tray_action(BELLWIN_TRAY_LEFT_BUTTON_UP, &suppressLeftButtonUp)
        == BELLWIN_TRAY_ACTION_NONE
    );
    assert(!suppressLeftButtonUp);
    assert(
        bellwin_tray_action(BELLWIN_TRAY_KEY_SELECT, &suppressLeftButtonUp)
        == BELLWIN_TRAY_ACTION_SHOW_SETTINGS
    );
}

static void test_pause_state(void) {
    assert(!bellwin_pause_is_active(1000, 0, 0));
    assert(bellwin_pause_is_active(1000, 2000, 0));
    assert(!bellwin_pause_is_active(2000, 2000, 0));
    assert(!bellwin_pause_is_active(3000, 2000, 0));
    assert(bellwin_pause_is_active(3000, 0, 1));
    assert(bellwin_pause_is_active(3000, 2000, 1));
}

static void test_persisted_timed_pause_validation(void) {
    assert(bellwin_timed_pause_is_valid(1000, 1000 + 30 * 60, 30, 100000));
    assert(bellwin_timed_pause_is_valid(1000, 1000 + 120 * 60, 120, 100000));
    assert(!bellwin_timed_pause_is_valid(0, 1000 + 30 * 60, 30, 100000));
    assert(!bellwin_timed_pause_is_valid(1000, 1000 + 30 * 60, 45, 100000));
    assert(!bellwin_timed_pause_is_valid(1000, 1000 + 29 * 60, 30, 100000));
    assert(!bellwin_timed_pause_is_valid(1000, 100001, 30, 100000));
}

static void test_last_ring_relative_time_formatting(void) {
    wchar_t text[64];

    bellwin_format_last_ring(0, text, 64);
    assert(wcscmp(text, L"Rang just now") == 0);
    bellwin_format_last_ring(45, text, 64);
    assert(wcscmp(text, L"Rang 45 seconds ago") == 0);
    bellwin_format_last_ring(60, text, 64);
    assert(wcscmp(text, L"Rang 1 minute ago") == 0);
    bellwin_format_last_ring(5 * 60, text, 64);
    assert(wcscmp(text, L"Rang 5 minutes ago") == 0);
    bellwin_format_last_ring(60 * 60, text, 64);
    assert(wcscmp(text, L"Rang 1 hour ago") == 0);
    bellwin_format_last_ring(23 * 60 * 60, text, 64);
    assert(wcscmp(text, L"Rang 23 hours ago") == 0);
    bellwin_format_last_ring(24 * 60 * 60, text, 64);
    assert(wcscmp(text, L"Rang yesterday") == 0);
    bellwin_format_last_ring(3 * 24 * 60 * 60, text, 64);
    assert(wcscmp(text, L"Rang 3 days ago") == 0);
}

static void test_antialiased_circle_coverage(void) {
    BellwinCircleCoverage center = bellwin_circle_coverage(9, 9, 10, 10, 10, 3);
    assert(center.fill == BELLWIN_AA_SAMPLE_COUNT);
    assert(center.border == 0);

    BellwinCircleCoverage edge = bellwin_circle_coverage(0, 9, 10, 10, 10, 3);
    assert(edge.fill == 0);
    assert(edge.border == BELLWIN_AA_SAMPLE_COUNT);

    BellwinCircleCoverage corner = bellwin_circle_coverage(0, 0, 10, 10, 10, 3);
    assert(corner.fill == 0);
    assert(corner.border == 0);

    BellwinCircleCoverage partial = bellwin_circle_coverage(1, 4, 10, 10, 10, 3);
    assert(partial.fill == 0);
    assert(partial.border > 0);
    assert(partial.border < BELLWIN_AA_SAMPLE_COUNT);

    BellwinCircleCoverage mirror = bellwin_circle_coverage(18, 4, 10, 10, 10, 3);
    assert(partial.fill == mirror.fill);
    assert(partial.border == mirror.border);

    BellwinCircleCoverage fillOnly = bellwin_circle_coverage(1, 4, 10, 10, 10, 0);
    assert(fillOnly.fill == partial.border);
    assert(fillOnly.border == 0);
}

static void test_antialiased_capsule_coverage(void) {
    assert(
        bellwin_horizontal_capsule_coverage(27, 14, 0, 0, 54, 30)
        == BELLWIN_AA_SAMPLE_COUNT
    );
    assert(
        bellwin_horizontal_capsule_coverage(0, 14, 0, 0, 54, 30)
        == BELLWIN_AA_SAMPLE_COUNT
    );
    assert(bellwin_horizontal_capsule_coverage(0, 0, 0, 0, 54, 30) == 0);

    uint8_t partial = bellwin_horizontal_capsule_coverage(3, 5, 0, 0, 54, 30);
    assert(partial > 0);
    assert(partial < BELLWIN_AA_SAMPLE_COUNT);
    assert(partial == bellwin_horizontal_capsule_coverage(50, 5, 0, 0, 54, 30));
}

int main(void) {
    test_overnight_quiet_hours();
    test_daytime_quiet_hours();
    test_active_interval_skips_overnight_quiet_hours();
    test_active_interval_without_quiet_hours_is_unchanged();
    test_active_interval_skips_daytime_quiet_hours();
    test_random_delay_is_inclusive();
    test_clamping();
    test_time_segment_editing();
    test_time_segment_stepping_wraps_only_selected_pair();
    test_time_segment_selection_cycles();
    test_tray_click_actions();
    test_pause_state();
    test_persisted_timed_pause_validation();
    test_last_ring_relative_time_formatting();
    test_antialiased_circle_coverage();
    test_antialiased_capsule_coverage();
    puts("core tests passed");
    return 0;
}
