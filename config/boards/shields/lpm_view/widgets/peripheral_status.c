/*
 * Copyright (c) 2023 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/battery.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/split/bluetooth/peripheral.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/usb.h>
#include <zmk/ble.h>
#include <zmk/wpm.h>

#include "peripheral_status.h"

/* util.h는 peripheral_status.h에서 이미 include됨 */

LV_IMG_DECLARE(spaceship1);
LV_IMG_DECLARE(spaceship2);
LV_IMG_DECLARE(spaceship3);
LV_IMG_DECLARE(spaceship4);
LV_IMG_DECLARE(spaceship5);
LV_IMG_DECLARE(spaceship6);
LV_IMG_DECLARE(spaceship7);
LV_IMG_DECLARE(spaceship8);

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

struct peripheral_status_state {
    bool connected;
};

struct art_state {
    lv_obj_t *art;
    lv_timer_t *timer;

    uint8_t frame_index;
    int x_pos;

    int star1_x[8];
    int star1_y[8];

    int star2_x[4];
    int star2_y[4];
};

static const lv_img_dsc_t *frames[] = {
    &spaceship1,
    &spaceship2,
    &spaceship3,
    &spaceship4,
    &spaceship5,
    &spaceship6,
    &spaceship7,
    &spaceship8,
};

#define FRAME_COUNT (sizeof(frames) / sizeof(frames[0]))

static void draw_stars(struct art_state *s) {

    lv_obj_t *parent = lv_obj_get_parent(s->art);
    lv_obj_t *canvas = lv_obj_get_child(parent, 0);

    lv_draw_rect_dsc_t star;
    init_rect_dsc(&star, LVGL_FOREGROUND);

    for (int i = 0; i < 8; i++) {
        lv_canvas_draw_rect(canvas, s->star1_x[i], s->star1_y[i], 1, 1, &star);
    }

    for (int i = 0; i < 4; i++) {
        lv_canvas_draw_rect(canvas, s->star2_x[i], s->star2_y[i], 2, 2, &star);
    }
}

static void art_anim_timer_cb(lv_timer_t *timer) {

    struct art_state *s = timer->user_data;

    uint8_t wpm = zmk_wpm_get_state();

    int step = 1;

    if (wpm > 80)
        step = 4;
    else if (wpm > 40)
        step = 3;
    else if (wpm > 10)
        step = 2;

    if (wpm > 0) {
        s->frame_index = (s->frame_index + step) % FRAME_COUNT;
        lv_img_set_src(s->art, frames[s->frame_index]);
    }

    if (wpm > 0)
        s->x_pos += step;
    else
        s->x_pos -= 1;

    if (s->x_pos > 80)
        s->x_pos = 80;

    if (s->x_pos < 0)
        s->x_pos = 0;

    lv_obj_set_pos(s->art, s->x_pos, 0);

    for (int i = 0; i < 8; i++) {

        s->star1_x[i] -= step;

        if (s->star1_x[i] < 0) {
            s->star1_x[i] = 120;
            s->star1_y[i] = sys_rand32_get() % 64;
        }
    }

    for (int i = 0; i < 4; i++) {

        s->star2_x[i] -= (step + 1);

        if (s->star2_x[i] < 0) {
            s->star2_x[i] = 120;
            s->star2_y[i] = sys_rand32_get() % 64;
        }
    }

    draw_stars(s);
}

int zmk_widget_status_init(struct zmk_widget_status *widget, lv_obj_t *parent) {

    widget->obj = lv_obj_create(parent);

    lv_obj_set_size(widget->obj, 144, 72);

    lv_obj_t *top = lv_canvas_create(widget->obj);

    lv_obj_align(top, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    lv_canvas_set_buffer(top,
                         widget->cbuf,
                         CANVAS_SIZE,
                         CANVAS_SIZE,
                         LV_IMG_CF_TRUE_COLOR);

    static struct art_state s;

    s.art = lv_img_create(widget->obj);

    s.frame_index = 0;
    s.x_pos = 0;

    lv_img_set_src(s.art, frames[0]);

    lv_obj_set_pos(s.art, 0, 0);

    for (int i = 0; i < 8; i++) {
        s.star1_x[i] = sys_rand32_get() % 120;
        s.star1_y[i] = sys_rand32_get() % 64;
    }

    for (int i = 0; i < 4; i++) {
        s.star2_x[i] = sys_rand32_get() % 120;
        s.star2_y[i] = sys_rand32_get() % 64;
    }

    s.timer = lv_timer_create(art_anim_timer_cb, 120, &s);

    lv_obj_set_user_data(widget->obj, &s);

    sys_slist_append(&widgets, &widget->node);

    widget_battery_status_init();
    widget_peripheral_status_init();

    return 0;
}

lv_obj_t *zmk_widget_status_obj(struct zmk_widget_status *widget) {
    return widget->obj;
}
