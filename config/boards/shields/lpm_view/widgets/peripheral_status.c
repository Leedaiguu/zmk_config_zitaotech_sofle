/*

* Copyright (c) 2023 The ZMK Contributors
* SPDX-License-Identifier: MIT
  */

#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/battery.h>
#include <zmk/display.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/split/bluetooth/peripheral.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/usb.h>
#include <zmk/ble.h>
#include <zmk/wpm.h>

#include "peripheral_status.h"

/* ================= 이미지 ================= */

LV_IMG_DECLARE(spaceship1);
LV_IMG_DECLARE(spaceship2);
LV_IMG_DECLARE(spaceship3);
LV_IMG_DECLARE(spaceship4);
LV_IMG_DECLARE(spaceship5);
LV_IMG_DECLARE(spaceship6);
LV_IMG_DECLARE(spaceship7);
LV_IMG_DECLARE(spaceship8);

/* ================= 상태 ================= */

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

struct peripheral_status_state {
bool connected;
};

/* ================= 애니메이션 상태 ================= */

struct art_state {
lv_obj_t *art;
lv_timer_t *timer;

```
uint8_t frame_index;
int x_pos;

int star1_x[8];
int star1_y[8];

int star2_x[4];
int star2_y[4];
```

};

/* ================= 프레임 ================= */

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

/* ================= 별 배경 ================= */

static void draw_stars(struct art_state *s) {

```
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
```

}

/* ================= 顶部 ================= */

static void draw_top(lv_obj_t *widget, lv_color_t cbuf[], const struct status_state *state) {

```
lv_obj_t *canvas = lv_obj_get_child(widget, 0);

lv_draw_label_dsc_t label_dsc;
init_label_dsc(&label_dsc, LVGL_FOREGROUND, &lv_font_montserrat_16, LV_TEXT_ALIGN_RIGHT);

lv_draw_rect_dsc_t rect;
init_rect_dsc(&rect, LVGL_BACKGROUND);

lv_canvas_draw_rect(canvas, 0, 0, CANVAS_SIZE, CANVAS_SIZE, &rect);

draw_battery(canvas, state);

lv_canvas_draw_text(canvas, 0, 0, CANVAS_SIZE, &label_dsc,
                    state->connected ? LV_SYMBOL_WIFI : LV_SYMBOL_CLOSE);

rotate_canvas(canvas, cbuf);
```

}

/* ================= 电池 ================= */

static void set_battery_status(struct zmk_widget_status *widget,
struct battery_status_state state) {

#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
widget->state.charging = state.usb_present;
#endif

```
widget->state.battery = state.level;

draw_top(widget->obj, widget->cbuf, &widget->state);
```

}

static void battery_status_update_cb(struct battery_status_state state) {

```
struct zmk_widget_status *widget;

SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
    set_battery_status(widget, state);
}
```

}

static struct battery_status_state battery_status_get_state(const zmk_event_t *eh) {

```
return (struct battery_status_state){
    .level = zmk_battery_state_of_charge(),
```

#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
.usb_present = zmk_usb_is_powered(),
#endif
};
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_battery_status, struct battery_status_state,
battery_status_update_cb, battery_status_get_state)

ZMK_SUBSCRIPTION(widget_battery_status, zmk_battery_state_changed);

#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(widget_battery_status, zmk_usb_conn_state_changed);
#endif

/* ================= 连接状态 ================= */

static struct peripheral_status_state get_state(const zmk_event_t *eh) {

```
return (struct peripheral_status_state){
    .connected = zmk_split_bt_peripheral_is_connected()
};
```

}

static void set_connection_status(struct zmk_widget_status *widget,
struct peripheral_status_state state) {

```
widget->state.connected = state.connected;

draw_top(widget->obj, widget->cbuf, &widget->state);
```

}

static void output_status_update_cb(struct peripheral_status_state state) {

```
struct zmk_widget_status *widget;

SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
    set_connection_status(widget, state);
}
```

}

ZMK_DISPLAY_WIDGET_LISTENER(widget_peripheral_status,
struct peripheral_status_state,
output_status_update_cb,
get_state)

ZMK_SUBSCRIPTION(widget_peripheral_status, zmk_split_peripheral_status_changed)

/* ================= 애니메이션 ================= */

static void art_anim_timer_cb(lv_timer_t *timer) {

```
struct art_state *s = timer->user_data;

uint8_t wpm = zmk_wpm_get_state();

int step = 1;

if (wpm > 80) step = 4;
else if (wpm > 40) step = 3;
else if (wpm > 10) step = 2;

/* 엔진 프레임 */

if (wpm > 0) {
    s->frame_index = (s->frame_index + step) % FRAME_COUNT;
    lv_img_set_src(s->art, frames[s->frame_index]);
}

/* 우주선 이동 */

if (wpm > 0)
    s->x_pos += step;
else
    s->x_pos -= 1;

if (s->x_pos > 80) s->x_pos = 80;
if (s->x_pos < 0) s->x_pos = 0;

lv_obj_set_pos(s->art, s->x_pos, 0);

/* Parallax stars */

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
```

}

/* ================= 初始化 ================= */

int zmk_widget_status_init(struct zmk_widget_status *widget,
lv_obj_t *parent) {

```
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

/* 별 초기화 */

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
```

}

lv_obj_t *zmk_widget_status_obj(struct zmk_widget_status *widget) {
return widget->obj;
}
