#ifndef BELLWIN_CORE_H
#define BELLWIN_CORE_H

#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

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

typedef enum BellwinTrayEvent {
    BELLWIN_TRAY_LEFT_BUTTON_UP,
    BELLWIN_TRAY_LEFT_BUTTON_DOUBLE_CLICK,
    BELLWIN_TRAY_SELECT,
    BELLWIN_TRAY_KEY_SELECT,
    BELLWIN_TRAY_CONTEXT_MENU,
} BellwinTrayEvent;

typedef enum BellwinTrayAction {
    BELLWIN_TRAY_ACTION_NONE,
    BELLWIN_TRAY_ACTION_DEFER_MENU,
    BELLWIN_TRAY_ACTION_SHOW_MENU,
    BELLWIN_TRAY_ACTION_SHOW_SETTINGS,
    BELLWIN_TRAY_ACTION_CANCEL_MENU_AND_SHOW_SETTINGS,
} BellwinTrayAction;

static inline BellwinTrayAction bellwin_tray_action(BellwinTrayEvent event, int *suppressLeftButtonUp) {
    if (event == BELLWIN_TRAY_LEFT_BUTTON_UP) {
        if (*suppressLeftButtonUp) {
            *suppressLeftButtonUp = 0;
            return BELLWIN_TRAY_ACTION_NONE;
        }
        return BELLWIN_TRAY_ACTION_DEFER_MENU;
    }
    if (event == BELLWIN_TRAY_LEFT_BUTTON_DOUBLE_CLICK) {
        *suppressLeftButtonUp = 1;
        return BELLWIN_TRAY_ACTION_CANCEL_MENU_AND_SHOW_SETTINGS;
    }
    if (event == BELLWIN_TRAY_SELECT) return BELLWIN_TRAY_ACTION_NONE;
    if (event == BELLWIN_TRAY_KEY_SELECT) return BELLWIN_TRAY_ACTION_SHOW_SETTINGS;
    if (event == BELLWIN_TRAY_CONTEXT_MENU) return BELLWIN_TRAY_ACTION_SHOW_MENU;
    return BELLWIN_TRAY_ACTION_NONE;
}

static inline BellwinTimeSegment bellwin_next_time_segment(BellwinTimeSegment segment) {
    return segment == BELLWIN_TIME_HOURS ? BELLWIN_TIME_MINUTES : BELLWIN_TIME_HOURS;
}

static inline int bellwin_clamp_int(int value, int minimum, int maximum) {
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

static inline int bellwin_normalize_day_minute(int value) {
    value %= 24 * 60;
    return value < 0 ? value + 24 * 60 : value;
}

static inline void bellwin_clamp_settings(BellwinSettings *settings) {
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

static inline int bellwin_is_quiet(int minuteOfDay, int quietStart, int quietEnd) {
    minuteOfDay = bellwin_normalize_day_minute(minuteOfDay);
    quietStart = bellwin_normalize_day_minute(quietStart);
    quietEnd = bellwin_normalize_day_minute(quietEnd);
    if (quietStart == quietEnd) return 0;
    if (quietStart < quietEnd) {
        return minuteOfDay >= quietStart && minuteOfDay < quietEnd;
    }
    return minuteOfDay >= quietStart || minuteOfDay < quietEnd;
}

static inline uint64_t bellwin_seconds_until_minute_of_day(
    int currentSecondOfDay,
    int targetMinuteOfDay
) {
    currentSecondOfDay %= 24 * 60 * 60;
    if (currentSecondOfDay < 0) currentSecondOfDay += 24 * 60 * 60;
    int targetSecond = bellwin_normalize_day_minute(targetMinuteOfDay) * 60;
    int seconds = targetSecond - currentSecondOfDay;
    if (seconds <= 0) seconds += 24 * 60 * 60;
    return (uint64_t)seconds;
}

static inline uint64_t bellwin_limit_active_segment(
    uint64_t remainingActiveSeconds,
    uint64_t secondsUntilQuiet
) {
    return remainingActiveSeconds < secondsUntilQuiet ? remainingActiveSeconds : secondsUntilQuiet;
}

static inline int bellwin_random_delay_minutes(int minimumMinutes, int maximumMinutes, uint32_t randomValue) {
    minimumMinutes = bellwin_clamp_int(minimumMinutes, 30, 480);
    maximumMinutes = bellwin_clamp_int(maximumMinutes, 30, 480);
    if (minimumMinutes > maximumMinutes) {
        int swap = minimumMinutes;
        minimumMinutes = maximumMinutes;
        maximumMinutes = swap;
    }
    return minimumMinutes + (int)(randomValue % (uint32_t)(maximumMinutes - minimumMinutes + 1));
}

static inline int bellwin_set_time_segment(int minuteOfDay, BellwinTimeSegment segment, int value) {
    minuteOfDay = bellwin_normalize_day_minute(minuteOfDay);
    int hours = minuteOfDay / 60;
    int minutes = minuteOfDay % 60;
    if (segment == BELLWIN_TIME_HOURS) hours = bellwin_clamp_int(value, 0, 23);
    else minutes = bellwin_clamp_int(value, 0, 59);
    return hours * 60 + minutes;
}

static inline int bellwin_step_time_segment(int minuteOfDay, BellwinTimeSegment segment, int delta) {
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

static inline int bellwin_pause_is_active(uint64_t nowTick, uint64_t pauseUntilTick, int pauseIndefinitely) {
    return pauseIndefinitely || (pauseUntilTick != 0 && nowTick < pauseUntilTick);
}

static inline int bellwin_timed_pause_is_valid(
    uint64_t startedUnixSeconds,
    uint64_t untilUnixSeconds,
    int selectedMinutes,
    uint64_t maximumUnixSeconds
) {
    int supportedDuration = selectedMinutes == 30 || selectedMinutes == 60 || selectedMinutes == 120;
    return supportedDuration
        && startedUnixSeconds != 0
        && untilUnixSeconds >= startedUnixSeconds
        && untilUnixSeconds <= maximumUnixSeconds
        && untilUnixSeconds - startedUnixSeconds == (uint64_t)selectedMinutes * 60ULL;
}

static inline void bellwin_format_last_ring(uint64_t elapsedSeconds, wchar_t *buffer, size_t count) {
    if (!buffer || count == 0) return;

    if (elapsedSeconds < 10) {
        swprintf(buffer, count, L"Rang just now");
    } else if (elapsedSeconds < 60) {
        swprintf(
            buffer,
            count,
            elapsedSeconds == 1 ? L"Rang 1 second ago" : L"Rang %llu seconds ago",
            (unsigned long long)elapsedSeconds
        );
    } else if (elapsedSeconds < 60 * 60) {
        uint64_t minutes = elapsedSeconds / 60;
        swprintf(
            buffer,
            count,
            minutes == 1 ? L"Rang 1 minute ago" : L"Rang %llu minutes ago",
            (unsigned long long)minutes
        );
    } else if (elapsedSeconds < 24 * 60 * 60) {
        uint64_t hours = elapsedSeconds / (60 * 60);
        swprintf(
            buffer,
            count,
            hours == 1 ? L"Rang 1 hour ago" : L"Rang %llu hours ago",
            (unsigned long long)hours
        );
    } else if (elapsedSeconds < 2 * 24 * 60 * 60) {
        swprintf(buffer, count, L"Rang yesterday");
    } else {
        uint64_t days = elapsedSeconds / (24 * 60 * 60);
        swprintf(buffer, count, L"Rang %llu days ago", (unsigned long long)days);
    }
}

#endif
