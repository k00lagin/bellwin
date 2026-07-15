#ifndef BELLWIN_CORE_H
#define BELLWIN_CORE_H

#include <stdint.h>

typedef struct BellwinSettings {
    int volume;
    int minimumMinutes;
    int maximumMinutes;
    int quietStartMinutes;
    int quietEndMinutes;
} BellwinSettings;

typedef enum BellwinTimeSegment {
    BELLWIN_TIME_HOURS,
    BELLWIN_TIME_MINUTES,
} BellwinTimeSegment;

static int bellwin_clamp_int(int value, int minimum, int maximum) {
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

static int bellwin_normalize_day_minute(int value) {
    value %= 24 * 60;
    return value < 0 ? value + 24 * 60 : value;
}

static void bellwin_clamp_settings(BellwinSettings *settings) {
    settings->volume = bellwin_clamp_int(settings->volume, 0, 100);
    settings->minimumMinutes = bellwin_clamp_int(settings->minimumMinutes, 30, 480);
    settings->maximumMinutes = bellwin_clamp_int(settings->maximumMinutes, 30, 480);
    if (settings->minimumMinutes > settings->maximumMinutes) {
        int swap = settings->minimumMinutes;
        settings->minimumMinutes = settings->maximumMinutes;
        settings->maximumMinutes = swap;
    }
    settings->quietStartMinutes = bellwin_normalize_day_minute(settings->quietStartMinutes);
    settings->quietEndMinutes = bellwin_normalize_day_minute(settings->quietEndMinutes);
}

static int bellwin_is_quiet(int minuteOfDay, int quietStart, int quietEnd) {
    minuteOfDay = bellwin_normalize_day_minute(minuteOfDay);
    quietStart = bellwin_normalize_day_minute(quietStart);
    quietEnd = bellwin_normalize_day_minute(quietEnd);
    if (quietStart == quietEnd) return 0;
    if (quietStart < quietEnd) {
        return minuteOfDay >= quietStart && minuteOfDay < quietEnd;
    }
    return minuteOfDay >= quietStart || minuteOfDay < quietEnd;
}

static int bellwin_minutes_until_quiet_end(int minuteOfDay, int quietStart, int quietEnd) {
    minuteOfDay = bellwin_normalize_day_minute(minuteOfDay);
    quietStart = bellwin_normalize_day_minute(quietStart);
    quietEnd = bellwin_normalize_day_minute(quietEnd);
    if (!bellwin_is_quiet(minuteOfDay, quietStart, quietEnd)) return 0;
    if (quietEnd > minuteOfDay) return quietEnd - minuteOfDay;
    return 24 * 60 - minuteOfDay + quietEnd;
}

static int bellwin_random_delay_minutes(int minimumMinutes, int maximumMinutes, uint32_t randomValue) {
    minimumMinutes = bellwin_clamp_int(minimumMinutes, 30, 480);
    maximumMinutes = bellwin_clamp_int(maximumMinutes, 30, 480);
    if (minimumMinutes > maximumMinutes) {
        int swap = minimumMinutes;
        minimumMinutes = maximumMinutes;
        maximumMinutes = swap;
    }
    return minimumMinutes + (int)(randomValue % (uint32_t)(maximumMinutes - minimumMinutes + 1));
}

static int bellwin_set_time_segment(int minuteOfDay, BellwinTimeSegment segment, int value) {
    minuteOfDay = bellwin_normalize_day_minute(minuteOfDay);
    int hours = minuteOfDay / 60;
    int minutes = minuteOfDay % 60;
    if (segment == BELLWIN_TIME_HOURS) hours = bellwin_clamp_int(value, 0, 23);
    else minutes = bellwin_clamp_int(value, 0, 59);
    return hours * 60 + minutes;
}

static int bellwin_step_time_segment(int minuteOfDay, BellwinTimeSegment segment, int delta) {
    minuteOfDay = bellwin_normalize_day_minute(minuteOfDay);
    int hours = minuteOfDay / 60;
    int minutes = minuteOfDay % 60;
    if (segment == BELLWIN_TIME_HOURS) {
        hours = (hours + delta) % 24;
        if (hours < 0) hours += 24;
    } else {
        minutes = (minutes + delta) % 60;
        if (minutes < 0) minutes += 60;
    }
    return hours * 60 + minutes;
}

#endif
