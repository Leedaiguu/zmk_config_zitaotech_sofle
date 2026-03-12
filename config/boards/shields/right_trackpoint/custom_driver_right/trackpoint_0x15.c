/*
 * TrackPoint HID over I2C Driver (Pointer Only)
 * Precision tuned version (sub-pixel optimized)
 */

#define DT_DRV_COMPAT zmk_trackpoint

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <stdlib.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>

#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>

#include <zephyr/input/input.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>

#include "custom_led.h"

LOG_MODULE_REGISTER(trackpoint, LOG_LEVEL_DBG);

/* ========= Motion GPIO ========= */

#define MOTION_GPIO_NODE DT_NODELABEL(gpio0)
#define MOTION_GPIO_PIN 14

static const struct device *motion_gpio_dev;

/* ========= TrackPoint constants ========= */

#define TRACKPOINT_PACKET_LEN 7
#define TRACKPOINT_MAGIC_BYTE0 0x50

/* ========= global state ========= */

uint32_t last_packet_time = 0;

static float frac_x = 0.0f;
static float frac_y = 0.0f;

static float smooth_dx = 0.0f;
static float smooth_dy = 0.0f;

/* ========= config ========= */

struct trackpoint_config {
    struct i2c_dt_spec i2c;
    struct gpio_dt_spec motion_gpio;
    uint16_t x_input_code;
    uint16_t y_input_code;
};

struct trackpoint_data {
    const struct device *dev;
    struct k_work_delayable poll_work;
};

/* ========= read packet ========= */

static int trackpoint_read_packet(
    const struct device *dev,
    int8_t *dx,
    int8_t *dy)
{
    const struct trackpoint_config *cfg = dev->config;

    uint8_t buf[TRACKPOINT_PACKET_LEN] = {0};

    int ret = i2c_read_dt(&cfg->i2c, buf, TRACKPOINT_PACKET_LEN);

    if (ret < 0) {
        LOG_ERR("I2C read failed: %d", ret);
        return ret;
    }

    if (buf[0] != TRACKPOINT_MAGIC_BYTE0) {
        LOG_WRN("Invalid packet header: 0x%02X", buf[0]);
        return -EIO;
    }

    *dx = (int8_t)buf[2];
    *dy = (int8_t)buf[3];

    return 0;
}

/* ========= acceleration ========= */

static inline float trackpoint_acceleration(int mag)
{
    float accel;

    if (mag <= 2) {
        accel = 0.70f;
    }
    else if (mag <= 5) {
        accel = 1.15f;
    }
    else if (mag <= 12) {
        accel = 1.80f;
    }
    else if (mag <= 24) {
        accel = 3.60f;
    }
    else {
        accel = 5.00f;
    }

    return accel;
}

/* ========= poll ========= */

static void trackpoint_poll_work(struct k_work *work)
{
    struct k_work_delayable *dwork =
        CONTAINER_OF(work, struct k_work_delayable, work);

    struct trackpoint_data *data =
        CONTAINER_OF(dwork, struct trackpoint_data, poll_work);

    const struct device *dev = data->dev;

    uint32_t now = k_uptime_get_32();

    int pin_state = gpio_pin_get(motion_gpio_dev, MOTION_GPIO_PIN);

    if (pin_state == 0) {

        int8_t dx = 0;
        int8_t dy = 0;

        if (trackpoint_read_packet(dev, &dx, &dy) == 0) {

            int ax = abs(dx);
            int ay = abs(dy);
            int mag = MAX(ax, ay);

            if (mag != 0) {

                float accel = trackpoint_acceleration(mag);

                float fx = (float)dx * accel;
                float fy = (float)dy * accel;

                /* ===== sub-pixel boost ===== */

                if (mag <= 2) {
                    fx *= 1.35f;
                    fy *= 1.35f;
                }

                /* ===== velocity smoothing (lighter) ===== */

                smooth_dx = smooth_dx * 0.40f + fx * 0.60f;
                smooth_dy = smooth_dy * 0.40f + fy * 0.60f;

                fx = smooth_dx;
                fy = smooth_dy;

                /* ===== fractional accumulation ===== */

                fx += frac_x;
                fy += frac_y;

                int outx = (int)fx;
                int outy = (int)fy;

                frac_x = fx - outx;
                frac_y = fy - outy;

                dx = outx;
                dy = outy;
            }

            input_report_rel(dev,
                             INPUT_REL_X,
                             -dx,
                             false,
                             K_FOREVER);

            input_report_rel(dev,
                             INPUT_REL_Y,
                             -dy,
                             true,
                             K_FOREVER);
        }

        last_packet_time = now;
    }

    k_work_schedule(&data->poll_work, K_MSEC(1));
}

/* ========= init ========= */

static int trackpoint_init(const struct device *dev)
{
    const struct trackpoint_config *cfg = dev->config;
    struct trackpoint_data *data = dev->data;

    k_sleep(K_MSEC(10));

    if (!device_is_ready(cfg->i2c.bus)) {
        LOG_ERR("I2C bus not ready");
        return -ENODEV;
    }

    motion_gpio_dev = DEVICE_DT_GET(MOTION_GPIO_NODE);

    if (!device_is_ready(motion_gpio_dev)) {
        LOG_ERR("Motion GPIO not ready");
        return -ENODEV;
    }

    gpio_pin_configure(motion_gpio_dev,
                       MOTION_GPIO_PIN,
                       GPIO_INPUT | GPIO_PULL_UP);

    data->dev = dev;

    k_work_init_delayable(&data->poll_work,
                          trackpoint_poll_work);

    k_work_schedule(&data->poll_work,
                    K_MSEC(1));

    LOG_DBG("TrackPoint initialized");

    return 0;
}

/* ========= device ========= */

#define TRACKPOINT_INIT_PRIORITY CONFIG_INPUT_INIT_PRIORITY

#define TRACKPOINT_DEFINE(inst)                                            \
    static struct trackpoint_data trackpoint_data_##inst;                  \
    static const struct trackpoint_config trackpoint_config_##inst = {     \
        .i2c = I2C_DT_SPEC_INST_GET(inst),                                 \
        .motion_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, motion_gpios, {0}),  \
        .x_input_code = DT_PROP_OR(DT_DRV_INST(inst), x_input_code, INPUT_REL_X), \
        .y_input_code = DT_PROP_OR(DT_DRV_INST(inst), y_input_code, INPUT_REL_Y), \
    };                                                                     \
    DEVICE_DT_INST_DEFINE(inst,                                            \
                          trackpoint_init,                                 \
                          NULL,                                            \
                          &trackpoint_data_##inst,                         \
                          &trackpoint_config_##inst,                       \
                          POST_KERNEL,                                     \
                          TRACKPOINT_INIT_PRIORITY,                        \
                          NULL);

DT_INST_FOREACH_STATUS_OKAY(TRACKPOINT_DEFINE);
