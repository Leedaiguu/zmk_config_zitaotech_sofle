#include <lvgl.h>

#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

#ifndef LV_ATTRIBUTE_IMG_SPACESHIP7
#define LV_ATTRIBUTE_IMG_SPACESHIP7
#endif

const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST LV_ATTRIBUTE_IMG_SPACESHIP7 uint8_t
spaceship7_map[] = {

#if CONFIG_NICE_VIEW_WIDGET_INVERTED
0xff,0xff,0xff,0xff,
0x00,0x00,0x00,0xff,
#else
0x00,0x00,0x00,0xff,
0xff,0xff,0xff,0xff,
#endif

/* BITMAP_DATA */

};

const lv_img_dsc_t spaceship7 = {
.header.cf = LV_IMG_CF_INDEXED_1BIT,
.header.always_zero = 0,
.header.reserved = 0,
.header.w = 120,
.header.h = 72,
.data_size = 1088,
.data = spaceship7_map,
};
