#ifndef BELLWIN_UIA_H
#define BELLWIN_UIA_H

#include "app_internal.h"

void uia_notify_focus(ControlId control, BellwinTimeSegment segment);
void uia_notify_install_state(int oldShowInstall, int oldUpdateAvailable);
void uia_notify_value(const Widget *widget, int oldValue, int newValue);
void uia_notify_toggle(int oldValue, int newValue);
LRESULT uia_handle_getobject(HWND window, WPARAM wParam, LPARAM lParam);
int uia_is_root_object(LPARAM lParam);
void uia_disconnect(HWND window);

#endif
