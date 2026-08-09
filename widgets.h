#ifndef BELLWIN_WIDGETS_H
#define BELLWIN_WIDGETS_H

#include <stdio.h>

/* Semantic widget vocabulary: pure role and action logic shared by input
   dispatch, painting, and the UI Automation provider. */

typedef enum ControlId {
    CONTROL_NONE,
    CONTROL_VOLUME,
    CONTROL_MINIMUM_INTERVAL,
    CONTROL_MAXIMUM_INTERVAL,
    CONTROL_QUIET_START,
    CONTROL_QUIET_END,
    CONTROL_AUTOSTART,
    CONTROL_INSTALL,
} ControlId;

typedef enum BellwinWidgetRole {
    BELLWIN_WIDGET_SLIDER,
    BELLWIN_WIDGET_TIME_GROUP,
    BELLWIN_WIDGET_TOGGLE,
    BELLWIN_WIDGET_BUTTON,
} BellwinWidgetRole;

typedef enum BellwinWidgetKey {
    BELLWIN_WIDGET_KEY_LEFT,
    BELLWIN_WIDGET_KEY_RIGHT,
    BELLWIN_WIDGET_KEY_UP,
    BELLWIN_WIDGET_KEY_DOWN,
    BELLWIN_WIDGET_KEY_ACTIVATE,
} BellwinWidgetKey;

typedef enum BellwinWidgetActionKind {
    BELLWIN_WIDGET_ACTION_NONE,
    BELLWIN_WIDGET_ACTION_STEP_VALUE,
    BELLWIN_WIDGET_ACTION_NEXT_SEGMENT,
    BELLWIN_WIDGET_ACTION_STEP_SEGMENT,
    BELLWIN_WIDGET_ACTION_INVOKE,
} BellwinWidgetActionKind;

typedef struct BellwinWidgetAction {
    BellwinWidgetActionKind kind;
    int amount;
} BellwinWidgetAction;

static inline BellwinWidgetAction bellwin_widget_key_action(BellwinWidgetRole role, BellwinWidgetKey key) {
    BellwinWidgetAction action = {BELLWIN_WIDGET_ACTION_NONE, 0};
    switch (role) {
    case BELLWIN_WIDGET_SLIDER:
        if (key == BELLWIN_WIDGET_KEY_LEFT) {
            action.kind = BELLWIN_WIDGET_ACTION_STEP_VALUE;
            action.amount = -1;
        } else if (key == BELLWIN_WIDGET_KEY_RIGHT) {
            action.kind = BELLWIN_WIDGET_ACTION_STEP_VALUE;
            action.amount = 1;
        }
        break;
    case BELLWIN_WIDGET_TIME_GROUP:
        if (key == BELLWIN_WIDGET_KEY_LEFT || key == BELLWIN_WIDGET_KEY_RIGHT) {
            action.kind = BELLWIN_WIDGET_ACTION_NEXT_SEGMENT;
        } else if (key == BELLWIN_WIDGET_KEY_UP) {
            action.kind = BELLWIN_WIDGET_ACTION_STEP_SEGMENT;
            action.amount = 1;
        } else if (key == BELLWIN_WIDGET_KEY_DOWN) {
            action.kind = BELLWIN_WIDGET_ACTION_STEP_SEGMENT;
            action.amount = -1;
        }
        break;
    case BELLWIN_WIDGET_TOGGLE:
    case BELLWIN_WIDGET_BUTTON:
        if (key == BELLWIN_WIDGET_KEY_ACTIVATE) {
            action.kind = BELLWIN_WIDGET_ACTION_INVOKE;
        }
        break;
    }
    return action;
}

static inline int bellwin_snap_value(int value, int minimum, int maximum, int step) {
    if (step > 1) value = ((value + step / 2) / step) * step;
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

static inline void bellwin_format_interval_utf8(int minutes, char *buffer, size_t count) {
    if (minutes < 60) {
        snprintf(buffer, count, "%d min", minutes);
    } else if (minutes % 60 == 0) {
        snprintf(buffer, count, "%d hr", minutes / 60);
    } else {
        snprintf(buffer, count, "%d.5 hr", minutes / 60);
    }
}

static inline int bellwin_next_focus_index(const int *focusable, int count, int currentIndex, int direction) {
    if (count <= 0) return -1;
    if (currentIndex < 0 || currentIndex >= count) {
        currentIndex = direction > 0 ? count - 1 : 0;
    }
    for (int attempt = 0; attempt < count; ++attempt) {
        currentIndex += direction;
        if (currentIndex >= count) currentIndex = 0;
        if (currentIndex < 0) currentIndex = count - 1;
        if (focusable[currentIndex]) return currentIndex;
    }
    return -1;
}

#endif
