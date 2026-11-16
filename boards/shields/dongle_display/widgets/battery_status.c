/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/services/bas.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/battery.h>
#include <zmk/ble.h>
#include <zmk/display.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/event_manager.h>
#include <zmk/usb.h>

#include "battery_status.h"

#if IS_ENABLED(CONFIG_ZMK_DONGLE_DISPLAY_DONGLE_BATTERY)
    #define SOURCE_OFFSET 1
#else
    #define SOURCE_OFFSET 0
#endif

#ifndef ZMK_SPLIT_BLE_PERIPHERAL_COUNT
#  define ZMK_SPLIT_BLE_PERIPHERAL_COUNT 0
#endif

#define BUFFER_SIZE LV_CANVAS_BUF_SIZE(5, 8, LV_COLOR_FORMAT_GET_BPP(LV_COLOR_FORMAT_L8), LV_DRAW_BUF_STRIDE_ALIGN)
#define BATTERY_ROW_HEIGHT 10
/* "%c %3u%% " is at most seven 8px glyphs plus six 1px letter spaces and a 5px icon. */
#define BATTERY_WIDGET_WIDTH 72

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

#define BATTERY_SLOT_COUNT (ZMK_SPLIT_BLE_PERIPHERAL_COUNT + SOURCE_OFFSET)

struct battery_slot_state {
    uint8_t level;
    bool usb_present;
    char side; /* Zero means unknown, never infer side from allocation order. */
};

struct battery_state {
    struct battery_slot_state slots[BATTERY_SLOT_COUNT];
};

/* Experimental: this exact 50-position logical map is keyboard-specific.
 * Never classify local/synthetic positions or an unknown layout by parity. */
static char side_for_position(uint32_t position) {
    if (position <= 5 || (position >= 12 && position <= 17) ||
        (position >= 24 && position <= 30) || (position >= 38 && position <= 43)) {
        return 'L';
    }
    if ((position >= 6 && position <= 11) || (position >= 18 && position <= 23) ||
        (position >= 31 && position <= 37) || (position >= 44 && position <= 49)) {
        return 'R';
    }
    return 0;
}

struct battery_object {
    lv_obj_t *symbol;
    lv_obj_t *label;
} battery_objects[ZMK_SPLIT_BLE_PERIPHERAL_COUNT + SOURCE_OFFSET];
    
static lv_color_t battery_image_buffer[ZMK_SPLIT_BLE_PERIPHERAL_COUNT + SOURCE_OFFSET][BUFFER_SIZE];

static void draw_battery(lv_obj_t *canvas, uint8_t level, bool usb_present) {
    lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);
    
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);

    lv_draw_rect_dsc_t rect_fill_dsc;
    lv_draw_rect_dsc_init(&rect_fill_dsc);
    rect_fill_dsc.bg_color = lv_color_white();

    if (usb_present) {
        rect_fill_dsc.bg_opa = LV_OPA_TRANSP;
        rect_fill_dsc.border_color = lv_color_white();
        rect_fill_dsc.border_width = 1;
    }

    lv_canvas_set_px(canvas, 0, 0, lv_color_white(), LV_OPA_COVER);
    lv_canvas_set_px(canvas, 4, 0, lv_color_white(), LV_OPA_COVER);

    lv_area_t rect_coords;
    bool rect_draw = true;
    
    if (level <= 10 || usb_present) {
        rect_coords = (lv_area_t){1, 2, 3, 6};
    } else if (level <= 30) {
        rect_coords = (lv_area_t){1, 2, 3, 5};
    } else if (level <= 50) {
        rect_coords = (lv_area_t){1, 2, 3, 4};
    } else if (level <= 70) {
        rect_coords = (lv_area_t){1, 2, 3, 3};
    } else if (level <= 90) {
        rect_coords = (lv_area_t){1, 2, 3, 2};
    } else {
        rect_draw = false;
    }

    if (rect_draw) {
        lv_draw_rect(&layer, &rect_fill_dsc, &rect_coords);
    }

    lv_canvas_finish_layer(canvas, &layer);
}

static void set_battery_symbol(size_t source, struct battery_slot_state state) {
    lv_obj_t *symbol = battery_objects[source].symbol;
    lv_obj_t *label = battery_objects[source].label;

    draw_battery(symbol, state.level, state.usb_present);
#if IS_ENABLED(CONFIG_ZMK_DONGLE_DISPLAY_DONGLE_BATTERY)
    if (source == 0) {
        /* The optional local battery is not a split half. */
        lv_label_set_text_fmt(label, "%4u%% ", state.level);
    } else
#endif
    {
        lv_label_set_text_fmt(label, "%c %3u%% ", state.side ? state.side : '?', state.level);
    }

    if (state.level > 0 || state.usb_present) {
        lv_obj_clear_flag(symbol, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(symbol);
        lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(label);
    } else {
        lv_obj_add_flag(symbol, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    }
}

static bool battery_slot_visible(struct battery_slot_state slot) {
    return slot.level > 0 || slot.usb_present;
}

static void position_battery_row(size_t source, size_t row) {
    lv_obj_t *symbol = battery_objects[source].symbol;
    lv_obj_t *label = battery_objects[source].label;

    lv_obj_align(symbol, LV_ALIGN_TOP_RIGHT, 0, row * BATTERY_ROW_HEIGHT);
    lv_obj_align_to(label, symbol, LV_ALIGN_OUT_LEFT_MID, 0, 0);
}

void battery_status_update_cb(struct battery_state state) {
    /* Called only on the display queue, including display initialization. */
    struct zmk_widget_dongle_battery_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        for (size_t source = 0; source < BATTERY_SLOT_COUNT; source++) {
            set_battery_symbol(source, state.slots[source]);
        }

        size_t row = 0;
#if IS_ENABLED(CONFIG_ZMK_DONGLE_DISPLAY_DONGLE_BATTERY)
        /* Keep the optional local battery separate from split-side ordering. */
        if (battery_slot_visible(state.slots[0])) {
            position_battery_row(0, row++);
        }
#endif
        /* Reorder positions, never source-indexed state, objects, or buffers.
         * Hidden slots consume no row; ties retain their source-slot order. */
        for (size_t rank = 0; rank < 3; rank++) {
            for (size_t source = SOURCE_OFFSET; source < BATTERY_SLOT_COUNT; source++) {
                struct battery_slot_state slot = state.slots[source];
                size_t side_rank = slot.side == 'L' ? 0 : slot.side == 'R' ? 1 : 2;
                if (battery_slot_visible(slot) && side_rank == rank) {
                    position_battery_row(source, row++);
                }
            }
        }
    }
}

static struct battery_state battery_status_get_state(const zmk_event_t *eh) {
    /* The display-listener macro holds its mutex across this entire function.
     * Keep ALL slots, not just the latest event: k_work submissions coalesce.
     * The display callback receives a locked copy and never reads this state.
     * These labels live in RAM only and are forgotten on slot release/reboot. */
    static struct battery_state state;

    /* ZMK's as_* helpers dereference eh, so NULL initialization must be guarded. */
    const struct zmk_peripheral_battery_state_changed *battery =
        eh ? as_zmk_peripheral_battery_state_changed(eh) : NULL;
    const struct zmk_position_state_changed *position =
        eh ? as_zmk_position_state_changed(eh) : NULL;

    if (battery) {
        /* Validate before adding the optional central-battery display offset. */
        if (battery->source < ZMK_SPLIT_BLE_PERIPHERAL_COUNT) {
            struct battery_slot_state *slot = &state.slots[battery->source + SOURCE_OFFSET];
            slot->level = battery->state_of_charge;
            if (slot->level == 0) {
                /* Existing transport uses zero for disconnect. Real 0% is also
                 * hidden/reset: there is no distinct disconnect flag here. */
                slot->side = 0;
            }
        }
    } else if (position) {
        /* Releases include synthetic disconnect releases; never learn from them.
         * Local events use UINT8_MAX, not a peripheral slot. Captured combo or
         * hold-tap events may not reach us: remain '?' until a press does. */
        if (position->state &&
            position->source != ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL &&
            position->source < ZMK_SPLIT_BLE_PERIPHERAL_COUNT) {
            struct battery_slot_state *slot = &state.slots[position->source + SOURCE_OFFSET];
            if (!slot->side) {
                slot->side = side_for_position(position->position);
            }
        }
    }
#if IS_ENABLED(CONFIG_ZMK_DONGLE_DISPLAY_DONGLE_BATTERY)
    else {
        const struct zmk_battery_state_changed *central =
            eh ? as_zmk_battery_state_changed(eh) : NULL;
        state.slots[0].level = central ? central->state_of_charge : zmk_battery_state_of_charge();
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
        state.slots[0].usb_present = zmk_usb_is_powered();
#endif
    }
#endif
    return state;
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_dongle_battery_status, struct battery_state,
                            battery_status_update_cb, battery_status_get_state)

ZMK_SUBSCRIPTION(widget_dongle_battery_status, zmk_peripheral_battery_state_changed);
ZMK_SUBSCRIPTION(widget_dongle_battery_status, zmk_position_state_changed);

#if IS_ENABLED(CONFIG_ZMK_DONGLE_DISPLAY_DONGLE_BATTERY)
#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

ZMK_SUBSCRIPTION(widget_dongle_battery_status, zmk_battery_state_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(widget_dongle_battery_status, zmk_usb_conn_state_changed);
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */
#endif /* !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) */
#endif /* IS_ENABLED(CONFIG_ZMK_DONGLE_DISPLAY_DONGLE_BATTERY) */

int zmk_widget_dongle_battery_status_init(struct zmk_widget_dongle_battery_status *widget, lv_obj_t *parent) {
    widget->obj = lv_obj_create(parent);

    /* The labels are aligned left of right-aligned canvases. LV_SIZE_CONTENT
     * ignores hidden children and left/top extents, yielding a zero/too-small
     * clip area after a redraw. Reserve the maximum label/icon footprint. */
    lv_obj_set_size(widget->obj, BATTERY_WIDGET_WIDTH,
                    BATTERY_SLOT_COUNT * BATTERY_ROW_HEIGHT);
    /* Also retain the full draw area if theme padding changes. */
    lv_obj_add_flag(widget->obj, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    for (int i = 0; i < ZMK_SPLIT_BLE_PERIPHERAL_COUNT + SOURCE_OFFSET; i++) {
        lv_obj_t *image_canvas = lv_canvas_create(widget->obj);
        lv_obj_t *battery_label = lv_label_create(widget->obj);

        lv_canvas_set_buffer(image_canvas, battery_image_buffer[i], 5, 8, LV_COLOR_FORMAT_L8);

        lv_obj_align(image_canvas, LV_ALIGN_TOP_RIGHT, 0, i * BATTERY_ROW_HEIGHT);
        lv_obj_align_to(battery_label, image_canvas, LV_ALIGN_OUT_LEFT_MID, 0, 0);

        lv_obj_add_flag(image_canvas, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(battery_label, LV_OBJ_FLAG_HIDDEN);
        
        battery_objects[i] = (struct battery_object){
            .symbol = image_canvas,
            .label = battery_label,
        };
    }

    sys_slist_append(&widgets, &widget->node);

    widget_dongle_battery_status_init();

    return 0;
}

lv_obj_t *zmk_widget_dongle_battery_status_obj(struct zmk_widget_dongle_battery_status *widget) {
    return widget->obj;
}
