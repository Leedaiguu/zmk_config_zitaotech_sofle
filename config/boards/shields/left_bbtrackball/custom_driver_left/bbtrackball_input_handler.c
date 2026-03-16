/*
 * bbtrackball_input_handler.c
 * Blackberry Trackball – Optimized Quadrature Driver
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_bbtrackball

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/input/input.h>
#include <stdbool.h>

LOG_MODULE_REGISTER(bbtrackball_input_handler, LOG_LEVEL_INF);

/* ==== GPIO ==== */

#define LEFT_PIN   12
#define RIGHT_PIN  27
#define UP_PIN     5
#define DOWN_PIN   9

#define GPIO0_DEV DT_NODELABEL(gpio0)
#define GPIO1_DEV DT_NODELABEL(gpio1)

/* ==== tuning ==== */

#define BASE_STEP            1
#define SPEED_GAIN           25
#define MAX_ACCEL            6
#define REPORT_INTERVAL_MS   4

/* ==== accumulators ==== */

static float dx_acc = 0;
static float dy_acc = 0;

static uint32_t last_time = 0;

/* ==== quadrature state ==== */

static uint8_t last_state_x = 0;
static uint8_t last_state_y = 0;

/* state transition table */
static const int8_t quad_table[16] = {
     0,-1, 1, 0,
     1, 0, 0,-1,
    -1, 0, 0, 1,
     0, 1,-1, 0
};

struct bbtrackball_data {
    const struct device *dev;
    struct k_work_delayable report_work;
};

static struct gpio_callback gpio_cb;

/* ==== read helpers ==== */

static inline uint8_t read_x(void)
{
    int l = gpio_pin_get(DEVICE_DT_GET(GPIO0_DEV), LEFT_PIN);
    int r = gpio_pin_get(DEVICE_DT_GET(GPIO1_DEV), RIGHT_PIN);
    return (l << 1) | r;
}

static inline uint8_t read_y(void)
{
    int u = gpio_pin_get(DEVICE_DT_GET(GPIO0_DEV), UP_PIN);
    int d = gpio_pin_get(DEVICE_DT_GET(GPIO1_DEV), DOWN_PIN);
    return (u << 1) | d;
}

/* ==== ISR ==== */

static void edge_cb(const struct device *dev,
                    struct gpio_callback *cb,
                    uint32_t pins)
{
    uint32_t now = k_uptime_get_32();
    uint32_t dt = now - last_time;
    last_time = now;

    if (dt == 0) dt = 1;

    /* velocity estimate */
    float speed = (float)SPEED_GAIN / (float)dt;
    if (speed > MAX_ACCEL) speed = MAX_ACCEL;

    float step = BASE_STEP + speed;

    /* ==== X ==== */

    uint8_t nx = read_x();
    uint8_t idx = (last_state_x << 2) | nx;
    int8_t s = quad_table[idx];

    if (s)
        dx_acc += s * step;

    last_state_x = nx;

    /* ==== Y ==== */

    uint8_t ny = read_y();
    idx = (last_state_y << 2) | ny;
    s = quad_table[idx];

    if (s)
        dy_acc += s * step;

    last_state_y = ny;
}

/* ==== report ==== */

static void report_work_handler(struct k_work *work)
{
    struct k_work_delayable *dwork =
        CONTAINER_OF(work, struct k_work_delayable, work);

    struct bbtrackball_data *data =
        CONTAINER_OF(dwork, struct bbtrackball_data, report_work);

    const struct device *dev = data->dev;

    int dx = (int)dx_acc;
    int dy = (int)dy_acc;

    if (dx || dy) {

        input_report_rel(dev, INPUT_REL_HWHEEL, -dx, false, K_FOREVER);
        input_report_rel(dev, INPUT_REL_WHEEL, -dy, true, K_FOREVER);

        dx_acc -= dx;
        dy_acc -= dy;
    }

    k_work_schedule(&data->report_work, K_MSEC(REPORT_INTERVAL_MS));
}

/* ==== init ==== */

static int bbtrackball_init(const struct device *dev)
{
    struct bbtrackball_data *data = dev->data;

    const struct device *gpio0 = DEVICE_DT_GET(GPIO0_DEV);
    const struct device *gpio1 = DEVICE_DT_GET(GPIO1_DEV);

    gpio_pin_configure(gpio0, LEFT_PIN, GPIO_INPUT | GPIO_PULL_UP);
    gpio_pin_configure(gpio1, RIGHT_PIN, GPIO_INPUT | GPIO_PULL_UP);
    gpio_pin_configure(gpio0, UP_PIN, GPIO_INPUT | GPIO_PULL_UP);
    gpio_pin_configure(gpio1, DOWN_PIN, GPIO_INPUT | GPIO_PULL_UP);

    gpio_pin_interrupt_configure(gpio0, LEFT_PIN, GPIO_INT_EDGE_BOTH);
    gpio_pin_interrupt_configure(gpio1, RIGHT_PIN, GPIO_INT_EDGE_BOTH);
    gpio_pin_interrupt_configure(gpio0, UP_PIN, GPIO_INT_EDGE_BOTH);
    gpio_pin_interrupt_configure(gpio1, DOWN_PIN, GPIO_INT_EDGE_BOTH);

    gpio_init_callback(&gpio_cb, edge_cb,
        BIT(LEFT_PIN) | BIT(RIGHT_PIN) | BIT(UP_PIN) | BIT(DOWN_PIN));

    gpio_add_callback(gpio0, &gpio_cb);
    gpio_add_callback(gpio1, &gpio_cb);

    last_state_x = read_x();
    last_state_y = read_y();

    last_time = k_uptime_get_32();

    data->dev = dev;

    k_work_init_delayable(&data->report_work, report_work_handler);
    k_work_schedule(&data->report_work, K_MSEC(REPORT_INTERVAL_MS));

    LOG_INF("Blackberry Trackball Optimized Driver Init");

    return 0;
}

/* ==== device ==== */

#define BBTRACKBALL_INIT_PRIORITY CONFIG_INPUT_INIT_PRIORITY

#define BBTRACKBALL_DEFINE(inst)                              \
    static struct bbtrackball_data bbtrackball_data_##inst;   \
    DEVICE_DT_INST_DEFINE(inst,                               \
        bbtrackball_init,                                     \
        NULL,                                                 \
        &bbtrackball_data_##inst,                             \
        NULL,                                                 \
        POST_KERNEL,                                          \
        BBTRACKBALL_INIT_PRIORITY,                            \
        NULL);

DT_INST_FOREACH_STATUS_OKAY(BBTRACKBALL_DEFINE);

/* ==== movement detect ==== */

bool trackball_is_moving(void)
{
    return (dx_acc != 0) || (dy_acc != 0);
}
