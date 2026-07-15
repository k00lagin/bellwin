#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "../core.h"

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

static void test_minutes_until_quiet_end(void) {
    assert(bellwin_minutes_until_quiet_end(23 * 60, 22 * 60, 10 * 60) == 11 * 60);
    assert(bellwin_minutes_until_quiet_end(5 * 60, 22 * 60, 10 * 60) == 5 * 60);
    assert(bellwin_minutes_until_quiet_end(13 * 60, 12 * 60, 14 * 60) == 60);
    assert(bellwin_minutes_until_quiet_end(15 * 60, 12 * 60, 14 * 60) == 0);
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

int main(void) {
    test_overnight_quiet_hours();
    test_daytime_quiet_hours();
    test_minutes_until_quiet_end();
    test_random_delay_is_inclusive();
    test_clamping();
    puts("core tests passed");
    return 0;
}
