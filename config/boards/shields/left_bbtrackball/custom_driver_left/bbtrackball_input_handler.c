/*
 * bbtrackball_input_handler.c
 * Blackberry Micro Trackball – Stable Driver
 */

#define DT_DRV_COMPAT zmk_bbtrackball

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/input/input.h>

LOG_MODULE_REGISTER(bbtrackball_input_handler, LOG_LEVEL_INF);

/* ==== GPIO ==== */

#define LEFT_PIN   12
#define RIGHT_PIN  27
#define UP_PIN     5
#define DOWN_PIN   9

#define GPIO0_DEV DT_NODELABEL(gpio0)
#define GPIO1_DEV DT_NODELABEL(gpio1)

/* ==== tuned values ==== */

#define BASE_STEP          1.4f
#define SPEED_GAIN         22.0f
#define MAX_ACCEL          3.8f
#define DEBOUNCE_MS        2
#define REPORT_INTERVAL_MS 5

/* ==== accumulators ==== */

static float dx_acc = 0.0f;
static float dy_acc = 0.0f;

static uint32_t last_event_time = 0;

/* ==== direction input ==== */

typedef struct {
    const struct device *gpio;
    int pin;
    int last_state;
    int sign_x;
    int sign_y;
    uint32_t last_time;
} Dir;

static Dir dirs[] = {
    { DEVICE_DT_GET(GPIO0_DEV), LEFT_PIN,  1, -1,  0, 0 },
    { DEVICE_DT_GET(GPIO1_DEV), RIGHT_PIN, 1, +1,  0, 0 },
    { DEVICE_DT_GET(GPIO0_DEV), UP_PIN,    1,  0, -1, 0 },
    { DEVICE_DT_GET(GPIO1_DEV), DOWN_PIN,  1,  0, +1, 0 }
};

static struct gpio_callback gpio_cbs[4];

struct bbtrackball_data {
    const struct device *dev;
    struct k_work_delayable report_work;
};

/* ==== interrupt ==== */

static void edge_cb(const struct device *dev,
                    struct gpio_callback *cb,
                    uint32_t pins)
{
    uint32_t now = k_uptime_get_32();
    uint32_t dt = now - last_event_time;

    if (dt == 0)
        dt = 1;

    last_event_time = now;

    float speed = SPEED_GAIN / (float)dt;

    if (speed > MAX_ACCEL)
        speed = MAX_ACCEL;

    float step = BASE_STEP + speed;

    for (int i = 0; i < 4; i++) {

        Dir *d = &dirs[i];

        if (dev != d->gpio)
            continue;

        if (!(pins & BIT(d->pin)))
            continue;

        int val = gpio_pin_get(dev, d->pin);

        if (val == d->last_state)
            continue;

        if (now - d->last_time < DEBOUNCE_MS)
            continue;

        d->last_time = now;
        d->last_state = val;

        if (d->sign_x)
            dx_acc += d->sign_x * step;

        if (d->sign_y)
            dy_acc += d->sign_y * step;
    }
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

    for (int i = 0; i < 4; i++) {

        Dir *d = &dirs[i];

        gpio_pin_configure(d->gpio, d->pin,
            GPIO_INPUT | GPIO_PULL_UP | GPIO_INT_EDGE_BOTH);

        d->last_state = gpio_pin_get(d->gpio, d->pin);

        gpio_init_callback(&gpio_cbs[i], edge_cb, BIT(d->pin));

        gpio_add_callback(d->gpio, &gpio_cbs[i]);

        gpio_pin_interrupt_configure(d->gpio, d->pin,
            GPIO_INT_EDGE_BOTH);
    }

    last_event_time = k_uptime_get_32();

    data->dev = dev;

    k_work_init_delayable(&data->report_work, report_work_handler);
    k_work_schedule(&data->report_work, K_MSEC(REPORT_INTERVAL_MS));

    LOG_INF("Blackberry Trackball Driver Init");

    return 0;
}

/* ==== device ==== */

#define BBTRACKBALL_INIT_PRIORITY CONFIG_INPUT_INIT_PRIORITY

#define BBTRACKBALL_DEFINE(inst)                          \
    static struct bbtrackball_data bbtrackball_data_##inst; \
    DEVICE_DT_INST_DEFINE(inst,                           \
        bbtrackball_init,                                 \
        NULL,                                             \
        &bbtrackball_data_##inst,                         \
        NULL,                                             \
        POST_KERNEL,                                      \
        BBTRACKBALL_INIT_PRIORITY,                        \
        NULL);

DT_INST_FOREACH_STATUS_OKAY(BBTRACKBALL_DEFINE);

/* ==== movement detect ==== */

bool trackball_is_moving(void)
{
    return (dx_acc != 0) || (dy_acc != 0);
}
