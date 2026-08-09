#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../layout.h"

static Clay_Dimensions stub_measure(Clay_StringSlice text, Clay_TextElementConfig *config, void *userData) {
    (void)userData;
    return (Clay_Dimensions){
        (float)text.length * (float)config->fontSize * 0.5f,
        (float)config->fontSize,
    };
}

static BellwinUiFrame test_frame(float scale) {
    BellwinUiFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.scale = scale;
    frame.settings.volume = 50;
    frame.settings.minimumMinutes = 60;
    frame.settings.maximumMinutes = 240;
    frame.settings.quietStartMinutes = 22 * 60;
    frame.settings.quietEndMinutes = 10 * 60;
    frame.showInstall = 1;
    frame.installLabel = "Install";
    return frame;
}

static Clay_BoundingBox box_of(Clay_ElementId id) {
    Clay_ElementData data = Clay_GetElementData(id);
    assert(data.found);
    assert(data.boundingBox.width > 0.0f);
    assert(data.boundingBox.height > 0.0f);
    return data.boundingBox;
}

static int boxes_overlap(Clay_BoundingBox a, Clay_BoundingBox b) {
    return a.x < b.x + b.width && b.x < a.x + a.width
        && a.y < b.y + b.height && b.y < a.y + a.height;
}

static int box_inside(Clay_BoundingBox inner, Clay_BoundingBox outer) {
    return inner.x >= outer.x && inner.y >= outer.y
        && inner.x + inner.width <= outer.x + outer.width + 0.5f
        && inner.y + inner.height <= outer.y + outer.height + 0.5f;
}

static void test_layout_structure(void) {
    BellwinUiFrame frame = test_frame(1.0f);
    Clay_RenderCommandArray commands = bellwin_ui_build(&frame);
    assert(commands.length > 0);

    Clay_BoundingBox card = box_of(Clay_GetElementId(CLAY_STRING("Card")));
    Clay_BoundingBox volumeRow = box_of(bellwin_ui_hit_id(CONTROL_VOLUME));
    Clay_BoundingBox minimumRow = box_of(bellwin_ui_hit_id(CONTROL_MINIMUM_INTERVAL));
    Clay_BoundingBox maximumRow = box_of(bellwin_ui_hit_id(CONTROL_MAXIMUM_INTERVAL));
    Clay_BoundingBox volumeTrack = box_of(bellwin_ui_track_id(CONTROL_VOLUME));
    Clay_BoundingBox quietStart = box_of(bellwin_ui_hit_id(CONTROL_QUIET_START));
    Clay_BoundingBox quietEnd = box_of(bellwin_ui_hit_id(CONTROL_QUIET_END));
    Clay_BoundingBox toggle = box_of(bellwin_ui_hit_id(CONTROL_AUTOSTART));
    Clay_BoundingBox install = box_of(bellwin_ui_hit_id(CONTROL_INSTALL));

    /* Slider rows are stacked in order without overlap, inside the card. */
    assert(volumeRow.y + volumeRow.height <= minimumRow.y);
    assert(minimumRow.y + minimumRow.height <= maximumRow.y);
    assert(box_inside(volumeRow, card));
    assert(box_inside(maximumRow, card));
    assert(box_inside(volumeTrack, volumeRow));

    /* Quiet-hours boxes sit side by side on one line, inside the card. */
    assert(quietStart.x + quietStart.width <= quietEnd.x);
    assert(fabsf(quietStart.y - quietEnd.y) < 0.5f);
    assert(!boxes_overlap(quietStart, quietEnd));
    assert(box_inside(quietStart, card));
    assert(box_inside(quietEnd, card));

    /* The bottom row sits below the card, toggle left of the install button,
       everything within the window. */
    assert(toggle.y >= card.y + card.height);
    assert(toggle.x + toggle.width <= install.x);
    assert(install.x + install.width <= 760.0f + 0.5f);
    assert(install.y + install.height <= 407.0f + 0.5f);
}

static void test_layout_scales(void) {
    BellwinUiFrame frame = test_frame(1.0f);
    bellwin_ui_build(&frame);
    Clay_BoundingBox card1 = box_of(Clay_GetElementId(CLAY_STRING("Card")));

    assert(bellwin_ui_initialize(1520.0f, 814.0f));
    frame = test_frame(2.0f);
    bellwin_ui_build(&frame);
    Clay_BoundingBox card2 = box_of(Clay_GetElementId(CLAY_STRING("Card")));
    assert(card2.width > card1.width * 1.9f);
    assert(card2.height > card1.height * 1.9f);

    assert(bellwin_ui_initialize(760.0f, 407.0f));
}

static void test_install_button_visibility(void) {
    /* Clay_GetElementData keeps returning the last known box for elements
       missing from the current frame, so visibility is asserted through
       Clay_PointerOver, which walks the current layout tree — the same way
       the application hit-tests. */
    BellwinUiFrame frame = test_frame(1.0f);
    bellwin_ui_build(&frame);
    Clay_BoundingBox install = box_of(bellwin_ui_hit_id(CONTROL_INSTALL));
    Clay_Vector2 center = {install.x + install.width / 2.0f, install.y + install.height / 2.0f};

    frame.showInstall = 0;
    bellwin_ui_build(&frame);
    Clay_SetPointerState(center, false);
    assert(!Clay_PointerOver(bellwin_ui_hit_id(CONTROL_INSTALL)));

    frame.showInstall = 1;
    bellwin_ui_build(&frame);
    Clay_SetPointerState(center, false);
    assert(Clay_PointerOver(bellwin_ui_hit_id(CONTROL_INSTALL)));
}

static void test_update_tooltip(void) {
    BellwinUiFrame frame = test_frame(1.0f);
    frame.updateAvailable = 1;
    frame.hoverInstall = 1;
    frame.installLabel = "Update";
    frame.updateTooltip = "Update from 1.0.0.0 to 2.0.0.0";
    bellwin_ui_build(&frame);
    Clay_BoundingBox install = box_of(bellwin_ui_hit_id(CONTROL_INSTALL));
    Clay_BoundingBox tooltip = box_of(bellwin_ui_tooltip_id());
    assert(tooltip.y + tooltip.height <= install.y);

    Clay_Vector2 tooltipCenter = {tooltip.x + tooltip.width / 2.0f, tooltip.y + tooltip.height / 2.0f};
    frame.hoverInstall = 0;
    bellwin_ui_build(&frame);
    Clay_SetPointerState(tooltipCenter, false);
    assert(!Clay_PointerOver(bellwin_ui_tooltip_id()));
}

static void test_pointer_hits_volume_row(void) {
    BellwinUiFrame frame = test_frame(1.0f);
    bellwin_ui_build(&frame);
    Clay_BoundingBox volumeRow = box_of(bellwin_ui_hit_id(CONTROL_VOLUME));
    Clay_Vector2 center = {volumeRow.x + volumeRow.width / 2.0f, volumeRow.y + volumeRow.height / 2.0f};
    Clay_SetPointerState(center, false);
    assert(Clay_PointerOver(bellwin_ui_hit_id(CONTROL_VOLUME)));
    assert(!Clay_PointerOver(bellwin_ui_hit_id(CONTROL_MINIMUM_INTERVAL)));
}

static void assert_autostart_composite_target(float scale, float width, float height) {
    assert(bellwin_ui_initialize(width, height));
    BellwinUiFrame frame = test_frame(scale);
    bellwin_ui_build(&frame);
    Clay_BoundingBox target = box_of(bellwin_ui_hit_id(CONTROL_AUTOSTART));
    Clay_BoundingBox visual = box_of(bellwin_ui_visual_id(CONTROL_AUTOSTART));

    /* The label, 12 epx gap, and 40 epx switch visual form one target. */
    assert(bellwin_ui_hit_id(CONTROL_AUTOSTART).id
        != bellwin_ui_visual_id(CONTROL_AUTOSTART).id);
    assert(fabsf(target.width - 197.0f * scale) < 0.5f);
    assert(fabsf(target.height - 40.0f * scale) < 0.5f);
    assert(fabsf(visual.width - 40.0f * scale) < 0.5f);
    assert(fabsf(visual.height - 20.0f * scale) < 0.5f);
    assert(fabsf(visual.x - target.x - 157.0f * scale) < 0.5f);
    assert(fabsf(
        visual.y + visual.height / 2.0f - target.y - target.height / 2.0f
    ) < 0.5f);
    assert(box_inside(visual, target));

    Clay_Vector2 labelPoint = {target.x + 4.0f * scale, target.y + target.height / 2.0f};
    Clay_SetPointerState(labelPoint, false);
    assert(Clay_PointerOver(bellwin_ui_hit_id(CONTROL_AUTOSTART)));

    Clay_Vector2 gapPoint = {target.x + 151.0f * scale, target.y + target.height / 2.0f};
    Clay_SetPointerState(gapPoint, false);
    assert(Clay_PointerOver(bellwin_ui_hit_id(CONTROL_AUTOSTART)));

    Clay_Vector2 visualPoint = {visual.x + visual.width / 2.0f, visual.y + visual.height / 2.0f};
    Clay_SetPointerState(visualPoint, false);
    assert(Clay_PointerOver(bellwin_ui_hit_id(CONTROL_AUTOSTART)));
}

static void test_autostart_composite_hit_target(void) {
    assert_autostart_composite_target(1.0f, 760.0f, 407.0f);
    assert_autostart_composite_target(2.0f, 1520.0f, 814.0f);
    assert(bellwin_ui_initialize(760.0f, 407.0f));
}

static void test_time_box_metrics(void) {
    Clay_BoundingBox box = {0.0f, 0.0f, 110.0f, 40.0f};
    BellwinTimeBoxMetrics metrics = bellwin_time_box_metrics(box, 1.0f);
    assert(metrics.hours.x == 6.0f && metrics.hours.width == 32.0f);
    assert(metrics.minutes.x == 48.0f && metrics.minutes.width == 32.0f);
    assert(metrics.dividerX == 84.0f);
    assert(metrics.stepper.x == 84.0f && metrics.stepper.width == 26.0f);
    assert(metrics.stepperMidY == 20.0f);
    assert(bellwin_box_contains(metrics.stepperUp, 90.0f, 10.0f));
    assert(bellwin_box_contains(metrics.stepperDown, 90.0f, 30.0f));
    assert(!bellwin_box_contains(metrics.hours, 40.0f, 10.0f));
}

static void test_slider_value_from_position(void) {
    Clay_BoundingBox track = {100.0f, 0.0f, 200.0f, 42.0f};
    assert(bellwin_slider_value_from_position(0, 100, 1, track, 100.0f) == 0);
    assert(bellwin_slider_value_from_position(0, 100, 1, track, 300.0f) == 100);
    assert(bellwin_slider_value_from_position(0, 100, 1, track, 50.0f) == 0);
    assert(bellwin_slider_value_from_position(0, 100, 1, track, 200.0f) == 50);
    assert(bellwin_slider_value_from_position(30, 480, 30, track, 150.0f) % 30 == 0);
    assert(bellwin_slider_value_from_position(30, 480, 30, track, 400.0f) % 30 == 0);
}

static void test_interval_formatting(void) {
    char buffer[16];
    bellwin_format_interval_utf8(45, buffer, sizeof(buffer));
    assert(strcmp(buffer, "45 min") == 0);
    bellwin_format_interval_utf8(120, buffer, sizeof(buffer));
    assert(strcmp(buffer, "2 hr") == 0);
    bellwin_format_interval_utf8(90, buffer, sizeof(buffer));
    assert(strcmp(buffer, "1.5 hr") == 0);
}

int main(void) {
    assert(bellwin_ui_initialize(760.0f, 407.0f));
    Clay_SetMeasureTextFunction(stub_measure, NULL);

    test_layout_structure();
    test_layout_scales();
    test_install_button_visibility();
    test_update_tooltip();
    test_pointer_hits_volume_row();
    test_autostart_composite_hit_target();
    test_time_box_metrics();
    test_slider_value_from_position();
    test_interval_formatting();
    printf("layout tests passed\n");
    return 0;
}
