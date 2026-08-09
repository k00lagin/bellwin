#define COBJMACROS
#include <windows.h>
#include <uiautomationcore.h>
#include <uiautomationcoreapi.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "app_internal.h"
#include "ui.h"
#include "uia.h"

#ifndef UIA_InvokePatternId
#define UIA_InvokePatternId 10000
#define UIA_ValuePatternId 10002
#define UIA_RangeValuePatternId 10003
#define UIA_TogglePatternId 10015
#define UIA_AutomationFocusChangedEventId 20005
#define UIA_ControlTypePropertyId 30003
#define UIA_LocalizedControlTypePropertyId 30004
#define UIA_NamePropertyId 30005
#define UIA_HasKeyboardFocusPropertyId 30008
#define UIA_IsKeyboardFocusablePropertyId 30009
#define UIA_IsEnabledPropertyId 30010
#define UIA_AutomationIdPropertyId 30011
#define UIA_ClassNamePropertyId 30012
#define UIA_IsControlElementPropertyId 30016
#define UIA_IsContentElementPropertyId 30017
#define UIA_IsOffscreenPropertyId 30022
#define UIA_FrameworkIdPropertyId 30024
#define UIA_ValueValuePropertyId 30045
#define UIA_ValueIsReadOnlyPropertyId 30046
#define UIA_RangeValueValuePropertyId 30047
#define UIA_ToggleToggleStatePropertyId 30086
#define UIA_ButtonControlTypeId 50000
#define UIA_SliderControlTypeId 50015
#define UIA_SpinnerControlTypeId 50016
#define UIA_GroupControlTypeId 50026
#endif
#ifndef UIA_E_INVALIDOPERATION
#define UIA_E_INVALIDOPERATION ((HRESULT)0x80131509L)
#endif

#define ui_scale app_ui_scale

/* ------------------------- UI Automation provider -------------------------
   Exposes the semantic widget table to Narrator and other UIA clients: a
   fragment root over the window with one fragment per visible widget.
   ProviderOptions_UseComThreading routes all client calls through the STA
   message loop, so provider code touches g_app without extra locking. */

#ifndef UiaAppendRuntimeId
#define UiaAppendRuntimeId 3
#endif

typedef enum UiaNodeKind {
    UIA_NODE_WIDGET,
    UIA_NODE_HOURS,
    UIA_NODE_MINUTES,
    UIA_NODE_COUNT,
} UiaNodeKind;

typedef struct UiaWidgetProvider {
    const IRawElementProviderSimpleVtbl *simpleVtbl;
    const IRawElementProviderFragmentVtbl *fragmentVtbl;
    const IValueProviderVtbl *valueVtbl;
    const IRangeValueProviderVtbl *rangeVtbl;
    const IToggleProviderVtbl *toggleVtbl;
    const IInvokeProviderVtbl *invokeVtbl;
    LONG references;
    ControlId control;
    UiaNodeKind node;
} UiaWidgetProvider;

typedef struct UiaRootProvider {
    const IRawElementProviderSimpleVtbl *simpleVtbl;
    const IRawElementProviderFragmentVtbl *fragmentVtbl;
    const IRawElementProviderFragmentRootVtbl *fragmentRootVtbl;
    LONG references;
} UiaRootProvider;

static UiaRootProvider *g_uiaRoot;
static UiaWidgetProvider *g_uiaWidgets[WIDGET_COUNT][UIA_NODE_COUNT];

static UiaWidgetProvider *uia_widget_provider(int index, UiaNodeKind node);
static UiaRootProvider *uia_root_provider(void);
static ULONG uia_root_addref(UiaRootProvider *provider);

#define UIA_WIDGET_FROM(iface, member) CONTAINING_RECORD((void *)(iface), UiaWidgetProvider, member)
#define UIA_ROOT_FROM(iface, member) CONTAINING_RECORD((void *)(iface), UiaRootProvider, member)

static IRawElementProviderSimple *uia_widget_simple(UiaWidgetProvider *provider) {
    return (IRawElementProviderSimple *)&provider->simpleVtbl;
}

static IRawElementProviderFragment *uia_widget_fragment(UiaWidgetProvider *provider) {
    return (IRawElementProviderFragment *)&provider->fragmentVtbl;
}

static IRawElementProviderSimple *uia_root_simple(UiaRootProvider *provider) {
    return (IRawElementProviderSimple *)&provider->simpleVtbl;
}

static IRawElementProviderFragment *uia_root_fragment(UiaRootProvider *provider) {
    return (IRawElementProviderFragment *)&provider->fragmentVtbl;
}

static IRawElementProviderFragmentRoot *uia_root_fragment_root(UiaRootProvider *provider) {
    return (IRawElementProviderFragmentRoot *)&provider->fragmentRootVtbl;
}

static int uia_node_is_segment(UiaNodeKind node) {
    return node == UIA_NODE_HOURS || node == UIA_NODE_MINUTES;
}

static BellwinTimeSegment uia_node_segment(UiaNodeKind node) {
    return node == UIA_NODE_MINUTES ? BELLWIN_TIME_MINUTES : BELLWIN_TIME_HOURS;
}

static void uia_format_interval(int minutes, wchar_t *buffer, size_t count) {
    char text[32];
    bellwin_format_interval_utf8(minutes, text, sizeof(text));
    size_t i = 0;
    if (count == 0) return;
    for (; i + 1 < count && text[i] != '\0'; ++i) {
        buffer[i] = (wchar_t)(unsigned char)text[i];
    }
    buffer[i] = L'\0';
}

static void uia_widget_value_text(const Widget *widget, int value, wchar_t *buffer, size_t count) {
    if (widget->id == CONTROL_VOLUME) {
        swprintf_s(buffer, count, L"%d%%", value);
    } else {
        uia_format_interval(value, buffer, count);
    }
}

static void uia_widget_name(
    const Widget *widget,
    UiaNodeKind node,
    wchar_t *buffer,
    size_t count
) {
    if (node == UIA_NODE_HOURS) {
        swprintf_s(buffer, count, L"%ls hours", widget->name);
    } else if (node == UIA_NODE_MINUTES) {
        swprintf_s(buffer, count, L"%ls minutes", widget->name);
    } else {
        swprintf_s(
            buffer,
            count,
            L"%ls",
            widget->id == CONTROL_INSTALL && g_app.updateAvailable ? L"Update" : widget->name
        );
    }
}

static void uia_widget_automation_id(
    const Widget *widget,
    UiaNodeKind node,
    wchar_t *buffer,
    size_t count
) {
    const wchar_t *suffix = node == UIA_NODE_HOURS
        ? L"Hours"
        : node == UIA_NODE_MINUTES ? L"Minutes" : L"";
    swprintf_s(buffer, count, L"%ls%ls", widget->automationId, suffix);
}

static int uia_widget_control_type(const Widget *widget, UiaNodeKind node) {
    if (uia_node_is_segment(node)) return UIA_SpinnerControlTypeId;
    switch (widget->role) {
    case BELLWIN_WIDGET_SLIDER: return UIA_SliderControlTypeId;
    case BELLWIN_WIDGET_TIME_GROUP: return UIA_GroupControlTypeId;
    case BELLWIN_WIDGET_TOGGLE:
    case BELLWIN_WIDGET_BUTTON: break;
    }
    return UIA_ButtonControlTypeId;
}

static int uia_widget_supports_value(const Widget *widget, UiaNodeKind node) {
    return node == UIA_NODE_WIDGET && widget->role == BELLWIN_WIDGET_SLIDER;
}

static int uia_widget_supports_range(const Widget *widget, UiaNodeKind node) {
    return (node == UIA_NODE_WIDGET && widget->role == BELLWIN_WIDGET_SLIDER)
        || (uia_node_is_segment(node) && widget->role == BELLWIN_WIDGET_TIME_GROUP);
}

static void uia_widget_bounds(ControlId control, UiaNodeKind node, struct UiaRect *rect) {
    rect->left = 0;
    rect->top = 0;
    rect->width = 0;
    rect->height = 0;
    if (!bellwin_ui_is_ready() || !g_app.window
            || !IsWindowVisible(g_app.window) || IsIconic(g_app.window)) return;
    Clay_ElementData data = Clay_GetElementData(bellwin_ui_hit_id(control));
    if (!data.found) return;
    Clay_BoundingBox box = data.boundingBox;
    if (uia_node_is_segment(node)) {
        BellwinTimeBoxMetrics metrics = bellwin_time_box_metrics(box, ui_scale());
        box = node == UIA_NODE_HOURS ? metrics.hours : metrics.minutes;
    }
    POINT origin = {0, 0};
    ClientToScreen(g_app.window, &origin);
    rect->left = (double)origin.x + (double)box.x;
    rect->top = (double)origin.y + (double)box.y;
    rect->width = (double)box.width;
    rect->height = (double)box.height;
}

static int uia_step_visible_index(int index, int direction) {
    for (index += direction; index >= 0 && index < WIDGET_COUNT; index += direction) {
        if (widget_is_visible(&WIDGETS[index])) return index;
    }
    return -1;
}

/* --- widget provider: shared IUnknown ---------------------------------- */

static ULONG uia_widget_addref(UiaWidgetProvider *provider) {
    return (ULONG)InterlockedIncrement(&provider->references);
}

static ULONG uia_widget_release(UiaWidgetProvider *provider) {
    LONG references = InterlockedDecrement(&provider->references);
    if (references == 0) free(provider);
    return (ULONG)references;
}

static void *uia_widget_pattern(UiaWidgetProvider *provider, PATTERNID patternId) {
    const Widget *widget = widget_by_id(provider->control);
    if (!widget) return NULL;
    if (patternId == UIA_ValuePatternId && uia_widget_supports_value(widget, provider->node)) {
        return (void *)&provider->valueVtbl;
    }
    if (patternId == UIA_RangeValuePatternId && uia_widget_supports_range(widget, provider->node)) {
        return (void *)&provider->rangeVtbl;
    }
    if (provider->node == UIA_NODE_WIDGET
            && patternId == UIA_TogglePatternId && widget->role == BELLWIN_WIDGET_TOGGLE) {
        return (void *)&provider->toggleVtbl;
    }
    if (provider->node == UIA_NODE_WIDGET
            && patternId == UIA_InvokePatternId && widget->role == BELLWIN_WIDGET_BUTTON) {
        return (void *)&provider->invokeVtbl;
    }
    return NULL;
}

static HRESULT uia_widget_query(UiaWidgetProvider *provider, REFIID riid, void **object) {
    void *result = NULL;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IRawElementProviderSimple)) {
        result = (void *)&provider->simpleVtbl;
    } else if (IsEqualIID(riid, &IID_IRawElementProviderFragment)) {
        result = (void *)&provider->fragmentVtbl;
    } else if (IsEqualIID(riid, &IID_IValueProvider)) {
        result = uia_widget_pattern(provider, UIA_ValuePatternId);
    } else if (IsEqualIID(riid, &IID_IRangeValueProvider)) {
        result = uia_widget_pattern(provider, UIA_RangeValuePatternId);
    } else if (IsEqualIID(riid, &IID_IToggleProvider)) {
        result = uia_widget_pattern(provider, UIA_TogglePatternId);
    } else if (IsEqualIID(riid, &IID_IInvokeProvider)) {
        result = uia_widget_pattern(provider, UIA_InvokePatternId);
    }
    *object = result;
    if (!result) return E_NOINTERFACE;
    uia_widget_addref(provider);
    return S_OK;
}

/* --- widget provider: IRawElementProviderSimple ------------------------ */

static HRESULT STDMETHODCALLTYPE uia_widget_simple_QueryInterface(IRawElementProviderSimple *iface, REFIID riid, void **object) {
    return uia_widget_query(UIA_WIDGET_FROM(iface, simpleVtbl), riid, object);
}

static ULONG STDMETHODCALLTYPE uia_widget_simple_AddRef(IRawElementProviderSimple *iface) {
    return uia_widget_addref(UIA_WIDGET_FROM(iface, simpleVtbl));
}

static ULONG STDMETHODCALLTYPE uia_widget_simple_Release(IRawElementProviderSimple *iface) {
    return uia_widget_release(UIA_WIDGET_FROM(iface, simpleVtbl));
}

static HRESULT STDMETHODCALLTYPE uia_widget_get_ProviderOptions(IRawElementProviderSimple *iface, enum ProviderOptions *retVal) {
    (void)iface;
    *retVal = ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE uia_widget_GetPatternProvider(IRawElementProviderSimple *iface, PATTERNID patternId, IUnknown **retVal) {
    UiaWidgetProvider *provider = UIA_WIDGET_FROM(iface, simpleVtbl);
    *retVal = (IUnknown *)uia_widget_pattern(provider, patternId);
    if (*retVal) uia_widget_addref(provider);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE uia_widget_GetPropertyValue(IRawElementProviderSimple *iface, PROPERTYID propertyId, VARIANT *retVal) {
    UiaWidgetProvider *provider = UIA_WIDGET_FROM(iface, simpleVtbl);
    const Widget *widget = widget_by_id(provider->control);
    VariantInit(retVal);
    if (!widget) return S_OK;
    switch (propertyId) {
    case UIA_NamePropertyId: {
        wchar_t name[96];
        uia_widget_name(widget, provider->node, name, 96);
        retVal->vt = VT_BSTR;
        retVal->bstrVal = SysAllocString(name);
        break;
    }
    case UIA_AutomationIdPropertyId: {
        wchar_t automationId[64];
        uia_widget_automation_id(widget, provider->node, automationId, 64);
        retVal->vt = VT_BSTR;
        retVal->bstrVal = SysAllocString(automationId);
        break;
    }
    case UIA_ControlTypePropertyId:
        retVal->vt = VT_I4;
        retVal->lVal = uia_widget_control_type(widget, provider->node);
        break;
    case UIA_LocalizedControlTypePropertyId:
        if (widget->role == BELLWIN_WIDGET_TOGGLE) {
            retVal->vt = VT_BSTR;
            retVal->bstrVal = SysAllocString(L"toggle switch");
        }
        break;
    case UIA_FrameworkIdPropertyId:
        retVal->vt = VT_BSTR;
        retVal->bstrVal = SysAllocString(L"Bellwin");
        break;
    case UIA_IsKeyboardFocusablePropertyId:
        retVal->vt = VT_BOOL;
        retVal->boolVal = widget->role == BELLWIN_WIDGET_TIME_GROUP
            && provider->node == UIA_NODE_WIDGET ? VARIANT_FALSE : VARIANT_TRUE;
        break;
    case UIA_HasKeyboardFocusPropertyId:
        retVal->vt = VT_BOOL;
        retVal->boolVal = control_has_focus(provider->control)
            && (widget->role != BELLWIN_WIDGET_TIME_GROUP
                ? provider->node == UIA_NODE_WIDGET
                : uia_node_is_segment(provider->node)
                    && uia_node_segment(provider->node) == g_app.timeEdit.segment)
            ? VARIANT_TRUE
            : VARIANT_FALSE;
        break;
    case UIA_IsEnabledPropertyId:
    case UIA_IsControlElementPropertyId:
    case UIA_IsContentElementPropertyId:
        retVal->vt = VT_BOOL;
        retVal->boolVal = VARIANT_TRUE;
        break;
    case UIA_IsOffscreenPropertyId:
        retVal->vt = VT_BOOL;
        retVal->boolVal = (!g_app.window || !IsWindowVisible(g_app.window)
                || IsIconic(g_app.window) || !widget_is_visible(widget))
            ? VARIANT_TRUE
            : VARIANT_FALSE;
        break;
    default:
        break;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE uia_widget_get_HostRawElementProvider(IRawElementProviderSimple *iface, IRawElementProviderSimple **retVal) {
    (void)iface;
    *retVal = NULL;
    return S_OK;
}

/* --- widget provider: IRawElementProviderFragment ---------------------- */

static HRESULT STDMETHODCALLTYPE uia_widget_fragment_QueryInterface(IRawElementProviderFragment *iface, REFIID riid, void **object) {
    return uia_widget_query(UIA_WIDGET_FROM(iface, fragmentVtbl), riid, object);
}

static ULONG STDMETHODCALLTYPE uia_widget_fragment_AddRef(IRawElementProviderFragment *iface) {
    return uia_widget_addref(UIA_WIDGET_FROM(iface, fragmentVtbl));
}

static ULONG STDMETHODCALLTYPE uia_widget_fragment_Release(IRawElementProviderFragment *iface) {
    return uia_widget_release(UIA_WIDGET_FROM(iface, fragmentVtbl));
}

static HRESULT STDMETHODCALLTYPE uia_widget_Navigate(IRawElementProviderFragment *iface, enum NavigateDirection direction, IRawElementProviderFragment **retVal) {
    UiaWidgetProvider *provider = UIA_WIDGET_FROM(iface, fragmentVtbl);
    const Widget *widget = widget_by_id(provider->control);
    int widgetIndex = widget_index_of(provider->control);
    *retVal = NULL;
    if (direction == NavigateDirection_Parent) {
        if (uia_node_is_segment(provider->node)) {
            UiaWidgetProvider *parent = uia_widget_provider(widgetIndex, UIA_NODE_WIDGET);
            if (parent) {
                uia_widget_addref(parent);
                *retVal = uia_widget_fragment(parent);
            }
            return S_OK;
        }
        UiaRootProvider *root = uia_root_provider();
        if (root) {
            uia_root_addref(root);
            *retVal = uia_root_fragment(root);
        }
        return S_OK;
    }
    if (direction == NavigateDirection_NextSibling || direction == NavigateDirection_PreviousSibling) {
        UiaWidgetProvider *sibling = NULL;
        if (uia_node_is_segment(provider->node)) {
            UiaNodeKind siblingNode = direction == NavigateDirection_NextSibling
                ? UIA_NODE_MINUTES
                : UIA_NODE_HOURS;
            if (siblingNode != provider->node) {
                sibling = uia_widget_provider(widgetIndex, siblingNode);
            }
        } else {
            int step = direction == NavigateDirection_NextSibling ? 1 : -1;
            int index = uia_step_visible_index(widgetIndex, step);
            sibling = uia_widget_provider(index, UIA_NODE_WIDGET);
        }
        if (sibling) {
            uia_widget_addref(sibling);
            *retVal = uia_widget_fragment(sibling);
        }
        return S_OK;
    }
    if (widget && provider->node == UIA_NODE_WIDGET
            && widget->role == BELLWIN_WIDGET_TIME_GROUP
            && (direction == NavigateDirection_FirstChild || direction == NavigateDirection_LastChild)) {
        UiaNodeKind childNode = direction == NavigateDirection_FirstChild
            ? UIA_NODE_HOURS
            : UIA_NODE_MINUTES;
        UiaWidgetProvider *child = uia_widget_provider(widgetIndex, childNode);
        if (child) {
            uia_widget_addref(child);
            *retVal = uia_widget_fragment(child);
        }
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE uia_widget_GetRuntimeId(IRawElementProviderFragment *iface, SAFEARRAY **retVal) {
    UiaWidgetProvider *provider = UIA_WIDGET_FROM(iface, fragmentVtbl);
    int ids[2] = {UiaAppendRuntimeId, ((int)provider->control << 2) | (int)provider->node};
    *retVal = NULL;
    SAFEARRAY *runtimeId = SafeArrayCreateVector(VT_I4, 0, 2);
    if (!runtimeId) return E_OUTOFMEMORY;
    for (LONG i = 0; i < 2; ++i) {
        HRESULT result = SafeArrayPutElement(runtimeId, &i, &ids[i]);
        if (FAILED(result)) {
            SafeArrayDestroy(runtimeId);
            return result;
        }
    }
    *retVal = runtimeId;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE uia_widget_get_BoundingRectangle(IRawElementProviderFragment *iface, struct UiaRect *retVal) {
    UiaWidgetProvider *provider = UIA_WIDGET_FROM(iface, fragmentVtbl);
    uia_widget_bounds(provider->control, provider->node, retVal);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE uia_widget_GetEmbeddedFragmentRoots(IRawElementProviderFragment *iface, SAFEARRAY **retVal) {
    (void)iface;
    *retVal = NULL;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE uia_widget_SetFocus(IRawElementProviderFragment *iface) {
    UiaWidgetProvider *provider = UIA_WIDGET_FROM(iface, fragmentVtbl);
    const Widget *widget = widget_by_id(provider->control);
    if (!widget || !widget_is_visible(widget) || !g_app.window
            || !IsWindowVisible(g_app.window) || IsIconic(g_app.window)) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    if (widget->role == BELLWIN_WIDGET_TIME_GROUP && provider->node == UIA_NODE_WIDGET) {
        return UIA_E_INVALIDOPERATION;
    }
    focus_control_part(
        provider->control,
        FOCUS_VISIBLE,
        uia_node_is_segment(provider->node),
        uia_node_segment(provider->node)
    );
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE uia_widget_get_FragmentRoot(IRawElementProviderFragment *iface, IRawElementProviderFragmentRoot **retVal) {
    (void)iface;
    UiaRootProvider *root = uia_root_provider();
    *retVal = NULL;
    if (root) {
        uia_root_addref(root);
        *retVal = uia_root_fragment_root(root);
    }
    return S_OK;
}

/* --- widget provider: IValueProvider ----------------------------------- */

static HRESULT STDMETHODCALLTYPE uia_widget_value_QueryInterface(IValueProvider *iface, REFIID riid, void **object) {
    return uia_widget_query(UIA_WIDGET_FROM(iface, valueVtbl), riid, object);
}

static ULONG STDMETHODCALLTYPE uia_widget_value_AddRef(IValueProvider *iface) {
    return uia_widget_addref(UIA_WIDGET_FROM(iface, valueVtbl));
}

static ULONG STDMETHODCALLTYPE uia_widget_value_Release(IValueProvider *iface) {
    return uia_widget_release(UIA_WIDGET_FROM(iface, valueVtbl));
}

static HRESULT STDMETHODCALLTYPE uia_widget_value_SetValue(IValueProvider *iface, LPCWSTR value) {
    (void)iface;
    (void)value;
    return UIA_E_INVALIDOPERATION;
}

static HRESULT STDMETHODCALLTYPE uia_widget_value_get_Value(IValueProvider *iface, BSTR *retVal) {
    UiaWidgetProvider *provider = UIA_WIDGET_FROM(iface, valueVtbl);
    const Widget *widget = widget_by_id(provider->control);
    if (!widget) return E_FAIL;
    wchar_t text[32];
    uia_widget_value_text(widget, *widget->value, text, 32);
    *retVal = SysAllocString(text);
    return *retVal ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE uia_widget_value_get_IsReadOnly(IValueProvider *iface, BOOL *retVal) {
    (void)iface;
    *retVal = TRUE;
    return S_OK;
}

/* --- widget provider: IRangeValueProvider ------------------------------ */
static HRESULT STDMETHODCALLTYPE uia_widget_range_QueryInterface(IRangeValueProvider *iface, REFIID riid, void **object) {
    return uia_widget_query(UIA_WIDGET_FROM(iface, rangeVtbl), riid, object);
}

static ULONG STDMETHODCALLTYPE uia_widget_range_AddRef(IRangeValueProvider *iface) {
    return uia_widget_addref(UIA_WIDGET_FROM(iface, rangeVtbl));
}

static ULONG STDMETHODCALLTYPE uia_widget_range_Release(IRangeValueProvider *iface) {
    return uia_widget_release(UIA_WIDGET_FROM(iface, rangeVtbl));
}

static void uia_widget_range_limits(
    const UiaWidgetProvider *provider,
    const Widget *widget,
    int *minimum,
    int *maximum
) {
    if (provider->node == UIA_NODE_HOURS) {
        *minimum = 0;
        *maximum = 23;
    } else if (provider->node == UIA_NODE_MINUTES) {
        *minimum = 0;
        *maximum = 59;
    } else {
        *minimum = widget->minimum;
        *maximum = widget->maximum;
    }
}

static int uia_widget_range_value(const UiaWidgetProvider *provider, const Widget *widget) {
    if (provider->node == UIA_NODE_HOURS) return *widget->value / 60;
    if (provider->node == UIA_NODE_MINUTES) return *widget->value % 60;
    return *widget->value;
}

static HRESULT STDMETHODCALLTYPE uia_widget_range_SetValue(IRangeValueProvider *iface, double value) {
    UiaWidgetProvider *provider = UIA_WIDGET_FROM(iface, rangeVtbl);
    const Widget *widget = widget_by_id(provider->control);
    if (!widget || !uia_widget_supports_range(widget, provider->node)) return UIA_E_INVALIDOPERATION;
    int minimum;
    int maximum;
    uia_widget_range_limits(provider, widget, &minimum, &maximum);
    if (!(value >= (double)minimum && value <= (double)maximum)) return E_INVALIDARG;
    int rounded = (int)(value + 0.5);
    if (uia_node_is_segment(provider->node)) {
        int minuteOfDay = bellwin_set_time_segment(
            *widget->value,
            uia_node_segment(provider->node),
            rounded
        );
        widget_set_value(widget, minuteOfDay, 1);
    } else {
        widget_set_value(
            widget,
            bellwin_snap_value(rounded, widget->minimum, widget->maximum, widget->keyStep),
            1
        );
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE uia_widget_range_get_Value(IRangeValueProvider *iface, double *retVal) {
    UiaWidgetProvider *provider = UIA_WIDGET_FROM(iface, rangeVtbl);
    const Widget *widget = widget_by_id(provider->control);
    if (!widget) return E_FAIL;
    *retVal = (double)uia_widget_range_value(provider, widget);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE uia_widget_range_get_IsReadOnly(IRangeValueProvider *iface, BOOL *retVal) {
    (void)iface;
    *retVal = FALSE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE uia_widget_range_get_Maximum(IRangeValueProvider *iface, double *retVal) {
    UiaWidgetProvider *provider = UIA_WIDGET_FROM(iface, rangeVtbl);
    const Widget *widget = widget_by_id(provider->control);
    if (!widget) return E_FAIL;
    int minimum;
    int maximum;
    uia_widget_range_limits(provider, widget, &minimum, &maximum);
    (void)minimum;
    *retVal = (double)maximum;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE uia_widget_range_get_Minimum(IRangeValueProvider *iface, double *retVal) {
    UiaWidgetProvider *provider = UIA_WIDGET_FROM(iface, rangeVtbl);
    const Widget *widget = widget_by_id(provider->control);
    if (!widget) return E_FAIL;
    int minimum;
    int maximum;
    uia_widget_range_limits(provider, widget, &minimum, &maximum);
    (void)maximum;
    *retVal = (double)minimum;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE uia_widget_range_get_LargeChange(IRangeValueProvider *iface, double *retVal) {
    UiaWidgetProvider *provider = UIA_WIDGET_FROM(iface, rangeVtbl);
    *retVal = uia_node_is_segment(provider->node)
        ? 1.0
        : provider->control == CONTROL_VOLUME ? 10.0 : 60.0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE uia_widget_range_get_SmallChange(IRangeValueProvider *iface, double *retVal) {
    UiaWidgetProvider *provider = UIA_WIDGET_FROM(iface, rangeVtbl);
    const Widget *widget = widget_by_id(provider->control);
    *retVal = widget && !uia_node_is_segment(provider->node) ? (double)widget->keyStep : 1.0;
    return S_OK;
}

/* --- widget provider: IToggleProvider ---------------------------------- */

static HRESULT STDMETHODCALLTYPE uia_widget_toggle_QueryInterface(IToggleProvider *iface, REFIID riid, void **object) {
    return uia_widget_query(UIA_WIDGET_FROM(iface, toggleVtbl), riid, object);
}

static ULONG STDMETHODCALLTYPE uia_widget_toggle_AddRef(IToggleProvider *iface) {
    return uia_widget_addref(UIA_WIDGET_FROM(iface, toggleVtbl));
}

static ULONG STDMETHODCALLTYPE uia_widget_toggle_Release(IToggleProvider *iface) {
    return uia_widget_release(UIA_WIDGET_FROM(iface, toggleVtbl));
}

static HRESULT STDMETHODCALLTYPE uia_widget_toggle_Toggle(IToggleProvider *iface) {
    UiaWidgetProvider *provider = UIA_WIDGET_FROM(iface, toggleVtbl);
    const Widget *widget = widget_by_id(provider->control);
    if (!widget || !widget_is_visible(widget)) return UIA_E_ELEMENTNOTAVAILABLE;
    invoke_widget(provider->control);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE uia_widget_toggle_get_ToggleState(IToggleProvider *iface, enum ToggleState *retVal) {
    (void)iface;
    *retVal = g_app.autoStart ? ToggleState_On : ToggleState_Off;
    return S_OK;
}

/* --- widget provider: IInvokeProvider ---------------------------------- */

static HRESULT STDMETHODCALLTYPE uia_widget_invoke_QueryInterface(IInvokeProvider *iface, REFIID riid, void **object) {
    return uia_widget_query(UIA_WIDGET_FROM(iface, invokeVtbl), riid, object);
}

static ULONG STDMETHODCALLTYPE uia_widget_invoke_AddRef(IInvokeProvider *iface) {
    return uia_widget_addref(UIA_WIDGET_FROM(iface, invokeVtbl));
}

static ULONG STDMETHODCALLTYPE uia_widget_invoke_Release(IInvokeProvider *iface) {
    return uia_widget_release(UIA_WIDGET_FROM(iface, invokeVtbl));
}

static HRESULT STDMETHODCALLTYPE uia_widget_invoke_Invoke(IInvokeProvider *iface) {
    UiaWidgetProvider *provider = UIA_WIDGET_FROM(iface, invokeVtbl);
    const Widget *widget = widget_by_id(provider->control);
    if (!widget || !widget_is_visible(widget)) return UIA_E_ELEMENTNOTAVAILABLE;
    return PostMessageW(g_app.window, WM_UIA_INVOKE, (WPARAM)provider->control, 0)
        ? S_OK
        : HRESULT_FROM_WIN32(GetLastError());
}

/* --- root provider ------------------------------------------------------ */
static ULONG uia_root_addref(UiaRootProvider *provider) {
    return (ULONG)InterlockedIncrement(&provider->references);
}

static ULONG uia_root_release(UiaRootProvider *provider) {
    LONG references = InterlockedDecrement(&provider->references);
    if (references == 0) free(provider);
    return (ULONG)references;
}

static HRESULT uia_root_query(UiaRootProvider *provider, REFIID riid, void **object) {
    void *result = NULL;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IRawElementProviderSimple)) {
        result = (void *)&provider->simpleVtbl;
    } else if (IsEqualIID(riid, &IID_IRawElementProviderFragment)) {
        result = (void *)&provider->fragmentVtbl;
    } else if (IsEqualIID(riid, &IID_IRawElementProviderFragmentRoot)) {
        result = (void *)&provider->fragmentRootVtbl;
    }
    *object = result;
    if (!result) return E_NOINTERFACE;
    uia_root_addref(provider);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE uia_root_simple_QueryInterface(IRawElementProviderSimple *iface, REFIID riid, void **object) {
    return uia_root_query(UIA_ROOT_FROM(iface, simpleVtbl), riid, object);
}

static ULONG STDMETHODCALLTYPE uia_root_simple_AddRef(IRawElementProviderSimple *iface) {
    return uia_root_addref(UIA_ROOT_FROM(iface, simpleVtbl));
}

static ULONG STDMETHODCALLTYPE uia_root_simple_Release(IRawElementProviderSimple *iface) {
    return uia_root_release(UIA_ROOT_FROM(iface, simpleVtbl));
}

static HRESULT STDMETHODCALLTYPE uia_root_get_ProviderOptions(IRawElementProviderSimple *iface, enum ProviderOptions *retVal) {
    (void)iface;
    *retVal = ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE uia_root_GetPatternProvider(IRawElementProviderSimple *iface, PATTERNID patternId, IUnknown **retVal) {
    (void)iface;
    (void)patternId;
    *retVal = NULL;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE uia_root_GetPropertyValue(IRawElementProviderSimple *iface, PROPERTYID propertyId, VARIANT *retVal) {
    (void)iface;
    (void)propertyId;
    VariantInit(retVal);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE uia_root_get_HostRawElementProvider(IRawElementProviderSimple *iface, IRawElementProviderSimple **retVal) {
    (void)iface;
    return UiaHostProviderFromHwnd(g_app.window, retVal);
}

static HRESULT STDMETHODCALLTYPE uia_root_fragment_QueryInterface(IRawElementProviderFragment *iface, REFIID riid, void **object) {
    return uia_root_query(UIA_ROOT_FROM(iface, fragmentVtbl), riid, object);
}

static ULONG STDMETHODCALLTYPE uia_root_fragment_AddRef(IRawElementProviderFragment *iface) {
    return uia_root_addref(UIA_ROOT_FROM(iface, fragmentVtbl));
}

static ULONG STDMETHODCALLTYPE uia_root_fragment_Release(IRawElementProviderFragment *iface) {
    return uia_root_release(UIA_ROOT_FROM(iface, fragmentVtbl));
}

static HRESULT STDMETHODCALLTYPE uia_root_Navigate(IRawElementProviderFragment *iface, enum NavigateDirection direction, IRawElementProviderFragment **retVal) {
    (void)iface;
    *retVal = NULL;
    if (direction == NavigateDirection_FirstChild || direction == NavigateDirection_LastChild) {
        int index = direction == NavigateDirection_FirstChild
            ? uia_step_visible_index(-1, 1)
            : uia_step_visible_index(WIDGET_COUNT, -1);
        UiaWidgetProvider *child = uia_widget_provider(index, UIA_NODE_WIDGET);
        if (child) {
            uia_widget_addref(child);
            *retVal = uia_widget_fragment(child);
        }
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE uia_root_GetRuntimeId(IRawElementProviderFragment *iface, SAFEARRAY **retVal) {
    (void)iface;
    *retVal = NULL; /* the host hwnd provider supplies the runtime id */
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE uia_root_get_BoundingRectangle(IRawElementProviderFragment *iface, struct UiaRect *retVal) {
    (void)iface;
    retVal->left = 0;
    retVal->top = 0;
    retVal->width = 0;
    retVal->height = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE uia_root_GetEmbeddedFragmentRoots(IRawElementProviderFragment *iface, SAFEARRAY **retVal) {
    (void)iface;
    *retVal = NULL;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE uia_root_SetFocus(IRawElementProviderFragment *iface) {
    (void)iface;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE uia_root_get_FragmentRoot(IRawElementProviderFragment *iface, IRawElementProviderFragmentRoot **retVal) {
    UiaRootProvider *provider = UIA_ROOT_FROM(iface, fragmentVtbl);
    uia_root_addref(provider);
    *retVal = uia_root_fragment_root(provider);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE uia_root_root_QueryInterface(IRawElementProviderFragmentRoot *iface, REFIID riid, void **object) {
    return uia_root_query(UIA_ROOT_FROM(iface, fragmentRootVtbl), riid, object);
}

static ULONG STDMETHODCALLTYPE uia_root_root_AddRef(IRawElementProviderFragmentRoot *iface) {
    return uia_root_addref(UIA_ROOT_FROM(iface, fragmentRootVtbl));
}

static ULONG STDMETHODCALLTYPE uia_root_root_Release(IRawElementProviderFragmentRoot *iface) {
    return uia_root_release(UIA_ROOT_FROM(iface, fragmentRootVtbl));
}

static HRESULT STDMETHODCALLTYPE uia_root_ElementProviderFromPoint(IRawElementProviderFragmentRoot *iface, double x, double y, IRawElementProviderFragment **retVal) {
    (void)iface;
    *retVal = NULL;
    if (!bellwin_ui_is_ready() || !g_app.window
            || !IsWindowVisible(g_app.window) || IsIconic(g_app.window)) return S_OK;
    POINT point = {(int)x, (int)y};
    ScreenToClient(g_app.window, &point);
    for (int i = 0; i < WIDGET_COUNT; ++i) {
        if (!widget_is_visible(&WIDGETS[i])) continue;
        Clay_ElementData data = Clay_GetElementData(bellwin_ui_hit_id(WIDGETS[i].id));
        if (!data.found) continue;
        if (!bellwin_box_contains(data.boundingBox, (float)point.x, (float)point.y)) continue;
        UiaNodeKind node = UIA_NODE_WIDGET;
        if (WIDGETS[i].role == BELLWIN_WIDGET_TIME_GROUP) {
            BellwinTimeBoxMetrics metrics = bellwin_time_box_metrics(data.boundingBox, ui_scale());
            if (bellwin_box_contains(metrics.hours, (float)point.x, (float)point.y)) {
                node = UIA_NODE_HOURS;
            } else if (bellwin_box_contains(metrics.minutes, (float)point.x, (float)point.y)) {
                node = UIA_NODE_MINUTES;
            }
        }
        UiaWidgetProvider *provider = uia_widget_provider(i, node);
        if (provider) {
            uia_widget_addref(provider);
            *retVal = uia_widget_fragment(provider);
        }
        break;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE uia_root_GetFocus(IRawElementProviderFragmentRoot *iface, IRawElementProviderFragment **retVal) {
    (void)iface;
    *retVal = NULL;
    if (!g_app.windowFocused) return S_OK;
    const Widget *widget = widget_by_id(g_app.focusedControl);
    UiaNodeKind node = widget && widget->role == BELLWIN_WIDGET_TIME_GROUP
        ? g_app.timeEdit.segment == BELLWIN_TIME_MINUTES ? UIA_NODE_MINUTES : UIA_NODE_HOURS
        : UIA_NODE_WIDGET;
    UiaWidgetProvider *provider = uia_widget_provider(widget_index_of(g_app.focusedControl), node);
    if (provider) {
        uia_widget_addref(provider);
        *retVal = uia_widget_fragment(provider);
    }
    return S_OK;
}

/* --- vtables and construction ------------------------------------------ */

static const IRawElementProviderSimpleVtbl UIA_WIDGET_SIMPLE_VTBL = {
    uia_widget_simple_QueryInterface,
    uia_widget_simple_AddRef,
    uia_widget_simple_Release,
    uia_widget_get_ProviderOptions,
    uia_widget_GetPatternProvider,
    uia_widget_GetPropertyValue,
    uia_widget_get_HostRawElementProvider,
};

static const IRawElementProviderFragmentVtbl UIA_WIDGET_FRAGMENT_VTBL = {
    uia_widget_fragment_QueryInterface,
    uia_widget_fragment_AddRef,
    uia_widget_fragment_Release,
    uia_widget_Navigate,
    uia_widget_GetRuntimeId,
    uia_widget_get_BoundingRectangle,
    uia_widget_GetEmbeddedFragmentRoots,
    uia_widget_SetFocus,
    uia_widget_get_FragmentRoot,
};

static const IValueProviderVtbl UIA_WIDGET_VALUE_VTBL = {
    uia_widget_value_QueryInterface,
    uia_widget_value_AddRef,
    uia_widget_value_Release,
    uia_widget_value_SetValue,
    uia_widget_value_get_Value,
    uia_widget_value_get_IsReadOnly,
};

static const IRangeValueProviderVtbl UIA_WIDGET_RANGE_VTBL = {
    uia_widget_range_QueryInterface,
    uia_widget_range_AddRef,
    uia_widget_range_Release,
    uia_widget_range_SetValue,
    uia_widget_range_get_Value,
    uia_widget_range_get_IsReadOnly,
    uia_widget_range_get_Maximum,
    uia_widget_range_get_Minimum,
    uia_widget_range_get_LargeChange,
    uia_widget_range_get_SmallChange,
};

static const IToggleProviderVtbl UIA_WIDGET_TOGGLE_VTBL = {
    uia_widget_toggle_QueryInterface,
    uia_widget_toggle_AddRef,
    uia_widget_toggle_Release,
    uia_widget_toggle_Toggle,
    uia_widget_toggle_get_ToggleState,
};

static const IInvokeProviderVtbl UIA_WIDGET_INVOKE_VTBL = {
    uia_widget_invoke_QueryInterface,
    uia_widget_invoke_AddRef,
    uia_widget_invoke_Release,
    uia_widget_invoke_Invoke,
};

static const IRawElementProviderSimpleVtbl UIA_ROOT_SIMPLE_VTBL = {
    uia_root_simple_QueryInterface,
    uia_root_simple_AddRef,
    uia_root_simple_Release,
    uia_root_get_ProviderOptions,
    uia_root_GetPatternProvider,
    uia_root_GetPropertyValue,
    uia_root_get_HostRawElementProvider,
};

static const IRawElementProviderFragmentVtbl UIA_ROOT_FRAGMENT_VTBL = {
    uia_root_fragment_QueryInterface,
    uia_root_fragment_AddRef,
    uia_root_fragment_Release,
    uia_root_Navigate,
    uia_root_GetRuntimeId,
    uia_root_get_BoundingRectangle,
    uia_root_GetEmbeddedFragmentRoots,
    uia_root_SetFocus,
    uia_root_get_FragmentRoot,
};

static const IRawElementProviderFragmentRootVtbl UIA_ROOT_ROOT_VTBL = {
    uia_root_root_QueryInterface,
    uia_root_root_AddRef,
    uia_root_root_Release,
    uia_root_ElementProviderFromPoint,
    uia_root_GetFocus,
};
static UiaWidgetProvider *uia_widget_provider(int index, UiaNodeKind node) {
    if (index < 0 || index >= WIDGET_COUNT || node < 0 || node >= UIA_NODE_COUNT) return NULL;
    if (node != UIA_NODE_WIDGET && WIDGETS[index].role != BELLWIN_WIDGET_TIME_GROUP) return NULL;
    if (!g_uiaWidgets[index][node]) {
        UiaWidgetProvider *provider = (UiaWidgetProvider *)calloc(1, sizeof(*provider));
        if (!provider) return NULL;
        provider->simpleVtbl = &UIA_WIDGET_SIMPLE_VTBL;
        provider->fragmentVtbl = &UIA_WIDGET_FRAGMENT_VTBL;
        provider->valueVtbl = &UIA_WIDGET_VALUE_VTBL;
        provider->rangeVtbl = &UIA_WIDGET_RANGE_VTBL;
        provider->toggleVtbl = &UIA_WIDGET_TOGGLE_VTBL;
        provider->invokeVtbl = &UIA_WIDGET_INVOKE_VTBL;
        provider->references = 1;
        provider->control = WIDGETS[index].id;
        provider->node = node;
        g_uiaWidgets[index][node] = provider;
    }
    return g_uiaWidgets[index][node];
}

static UiaRootProvider *uia_root_provider(void) {
    if (!g_uiaRoot) {
        UiaRootProvider *provider = (UiaRootProvider *)calloc(1, sizeof(*provider));
        if (!provider) return NULL;
        provider->simpleVtbl = &UIA_ROOT_SIMPLE_VTBL;
        provider->fragmentVtbl = &UIA_ROOT_FRAGMENT_VTBL;
        provider->fragmentRootVtbl = &UIA_ROOT_ROOT_VTBL;
        provider->references = 1;
        g_uiaRoot = provider;
    }
    return g_uiaRoot;
}

/* --- change notifications ----------------------------------------------
   No-ops until the first UIA client connects (g_uiaRoot is created on the
   first WM_GETOBJECT) and while nobody is listening. */

void uia_notify_focus(ControlId control, BellwinTimeSegment segment) {
    if (!g_uiaRoot || !UiaClientsAreListening()) return;
    const Widget *widget = widget_by_id(control);
    UiaNodeKind node = widget && widget->role == BELLWIN_WIDGET_TIME_GROUP
        ? segment == BELLWIN_TIME_MINUTES ? UIA_NODE_MINUTES : UIA_NODE_HOURS
        : UIA_NODE_WIDGET;
    UiaWidgetProvider *provider = uia_widget_provider(widget_index_of(control), node);
    if (provider) {
        UiaRaiseAutomationEvent(uia_widget_simple(provider), UIA_AutomationFocusChangedEventId);
    }
}

void uia_notify_install_state(int oldShowInstall, int oldUpdateAvailable) {
    if (!g_uiaRoot || !UiaClientsAreListening()) return;
    if (oldShowInstall != g_app.showInstall) {
        UiaRaiseStructureChangedEvent(
            uia_root_simple(g_uiaRoot), StructureChangeType_ChildrenInvalidated, NULL, 0
        );
        return;
    }
    if (!g_app.showInstall || oldUpdateAvailable == g_app.updateAvailable) return;
    UiaWidgetProvider *provider = uia_widget_provider(
        widget_index_of(CONTROL_INSTALL), UIA_NODE_WIDGET
    );
    if (!provider) return;
    VARIANT oldVariant;
    VARIANT newVariant;
    VariantInit(&oldVariant);
    VariantInit(&newVariant);
    oldVariant.vt = VT_BSTR;
    oldVariant.bstrVal = SysAllocString(oldUpdateAvailable ? L"Update" : L"Install");
    newVariant.vt = VT_BSTR;
    newVariant.bstrVal = SysAllocString(g_app.updateAvailable ? L"Update" : L"Install");
    if (oldVariant.bstrVal && newVariant.bstrVal) {
        UiaRaiseAutomationPropertyChangedEvent(
            uia_widget_simple(provider), UIA_NamePropertyId, oldVariant, newVariant
        );
    }
    VariantClear(&oldVariant);
    VariantClear(&newVariant);
}

static void uia_notify_range_value(
    int widgetIndex,
    UiaNodeKind node,
    double oldValue,
    double newValue
) {
    if (oldValue == newValue) return;
    UiaWidgetProvider *provider = uia_widget_provider(widgetIndex, node);
    if (!provider) return;
    VARIANT oldVariant;
    VARIANT newVariant;
    VariantInit(&oldVariant);
    VariantInit(&newVariant);
    oldVariant.vt = VT_R8;
    oldVariant.dblVal = oldValue;
    newVariant.vt = VT_R8;
    newVariant.dblVal = newValue;
    UiaRaiseAutomationPropertyChangedEvent(
        uia_widget_simple(provider), UIA_RangeValueValuePropertyId, oldVariant, newVariant
    );
}

void uia_notify_value(const Widget *widget, int oldValue, int newValue) {
    if (!g_uiaRoot || !widget || !UiaClientsAreListening()) return;
    int widgetIndex = widget_index_of(widget->id);

    if (widget->role == BELLWIN_WIDGET_SLIDER) {
        UiaWidgetProvider *provider = uia_widget_provider(widgetIndex, UIA_NODE_WIDGET);
        if (!provider) return;
        VARIANT oldVariant;
        VARIANT newVariant;
        VariantInit(&oldVariant);
        VariantInit(&newVariant);
        wchar_t oldText[32];
        wchar_t newText[32];
        uia_widget_value_text(widget, oldValue, oldText, 32);
        uia_widget_value_text(widget, newValue, newText, 32);
        oldVariant.vt = VT_BSTR;
        oldVariant.bstrVal = SysAllocString(oldText);
        newVariant.vt = VT_BSTR;
        newVariant.bstrVal = SysAllocString(newText);
        if (oldVariant.bstrVal && newVariant.bstrVal) {
            UiaRaiseAutomationPropertyChangedEvent(
                uia_widget_simple(provider), UIA_ValueValuePropertyId, oldVariant, newVariant
            );
        }
        VariantClear(&oldVariant);
        VariantClear(&newVariant);
        uia_notify_range_value(widgetIndex, UIA_NODE_WIDGET, (double)oldValue, (double)newValue);
    } else if (widget->role == BELLWIN_WIDGET_TIME_GROUP) {
        uia_notify_range_value(
            widgetIndex,
            UIA_NODE_HOURS,
            (double)(oldValue / 60),
            (double)(newValue / 60)
        );
        uia_notify_range_value(
            widgetIndex,
            UIA_NODE_MINUTES,
            (double)(oldValue % 60),
            (double)(newValue % 60)
        );
    }
}

void uia_notify_toggle(int oldValue, int newValue) {
    if (!g_uiaRoot || !UiaClientsAreListening()) return;
    UiaWidgetProvider *provider = uia_widget_provider(
        widget_index_of(CONTROL_AUTOSTART), UIA_NODE_WIDGET
    );
    if (!provider) return;
    VARIANT oldVariant;
    VARIANT newVariant;
    VariantInit(&oldVariant);
    VariantInit(&newVariant);
    oldVariant.vt = VT_I4;
    oldVariant.lVal = oldValue ? ToggleState_On : ToggleState_Off;
    newVariant.vt = VT_I4;
    newVariant.lVal = newValue ? ToggleState_On : ToggleState_Off;
    UiaRaiseAutomationPropertyChangedEvent(
        uia_widget_simple(provider), UIA_ToggleToggleStatePropertyId, oldVariant, newVariant
    );
}

LRESULT uia_handle_getobject(HWND window, WPARAM wParam, LPARAM lParam) {
    UiaRootProvider *provider = uia_root_provider();
    if (!provider) return 0;
    return UiaReturnRawElementProvider(window, wParam, lParam, uia_root_simple(provider));
}
void uia_disconnect(HWND window) {
    UiaReturnRawElementProvider(window, 0, 0, NULL);
}

int uia_is_root_object(LPARAM lParam) {
    return (LONG)lParam == UiaRootObjectId;
}
