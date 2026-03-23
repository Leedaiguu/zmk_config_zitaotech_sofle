/*
 * bbtrackball_input_handler.c
 * BB Trackball
 * - GPIO interrupt based movement
 * - regular mouse report
 * - hold Space(position 60) for scroll mode
 * - tuned for tiny Blackberry trackball:
 *   * slower scroll
 *   * faster initial response
 *   * mild capped acceleration
 *   * better response on very small movements
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_bbtrackball

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/input/input.h>
#include <zmk/events/position_state_changed.h>

LOG_MODULE_REGISTER(bbtrackball_input_handler, LOG_LEVEL_INF);

/* ==== GPIO Pins ==== */
#define DOWN_GPIO_PIN 9
#define LEFT_GPIO_PIN 12
#define UP_GPIO_PIN 5
#define RIGHT_GPIO_PIN 27

#define GPIO0_DEV DT_NODELABEL(gpio0)
#define GPIO1_DEV DT_NODELABEL(gpio1)

/* ==== Config ==== */
#define REPORT_INTERVAL_MS 10
#define SCROLL_DELAY_MS 40

/* pointer tuning for tiny Blackberry trackball */
#define BASE_MOVE_PIXELS 3

/* small movement / initial response tuning */
#define INITIAL_BOOST_WINDOW_MS 180
#define INITIAL_BOOST_PIXELS 1

/* mild capped acceleration thresholds */
#define FAST_START_THRESHOLD_MS 140
#define MID_SPEED_THRESHOLD_MS 70
#define HIGH_SPEED_THRESHOLD_MS 35

/* slower scroll tuning */
#define SCROLL_X_DIV 8
#define SCROLL_Y_DIV 8
#define SCROLL_MIN_STEP 1

/* ==== State ==== */
static bool moved = false;
static bool space_pressed = false;
static const struct device *trackball_dev_ref = NULL;
static int dx_acc = 0;
static int dy_acc = 0;

/* ==== GPIO callback-related ==== */
typedef struct {
    const struct device *gpio_dev;
    int pin;
    int last_state;
    uint32_t last_time;
    int sign; /* -1 or +1 */
} DirInput;

static DirInput dir_inputs[] = {
    {DEVICE_DT_GET(GPIO0_DEV), LEFT_GPIO_PIN, 1, 0, -1},
    {DEVICE_DT_GET(GPIO0_DEV), RIGHT_GPIO_PIN, 1, 0, +1},
    {DEVICE_DT_GET(GPIO0_DEV), UP_GPIO_PIN, 1, 0, -1},
    {DEVICE_DT_GET(GPIO1_DEV), DOWN_GPIO_PIN, 1, 0, +1},
};

static struct gpio_callback gpio_cbs[ARRAY_SIZE(dir_inputs)];

/* ==== Device Config/Data ==== */
struct bbtrackball_dev_config {
    uint16_t x_input_code;
    uint16_t y_input_code;
};

struct bbtrackball_data {
    const struct device *dev;
    struct k_work_delayable report_work;
    struct k_work_delayable arrow_repeat_work;
};

/* ==== External ==== */
bool trackball_is_moving(void) { return moved; }

/* ==== Helpers ==== */
static int scale_scroll(int v, int div) {
    if (v == 0) {
        return 0;
    }

    int out = v / div;

    if (out == 0) {
        out = (v > 0) ? SCROLL_MIN_STEP : -SCROLL_MIN_STEP;
    }

    return out;
}

static int calc_delta_px(uint32_t delta) {
    if (delta == 0) {
        delta = 1;
    }

    int delta_px = BASE_MOVE_PIXELS;

    /* tiny movement / first movement compensation */
    if (delta >= INITIAL_BOOST_WINDOW_MS) {
        delta_px += INITIAL_BOOST_PIXELS;
    }

    /* mild capped acceleration */
    if (delta < HIGH_SPEED_THRESHOLD_MS) {
        delta_px += 2;
    } else if (delta < MID_SPEED_THRESHOLD_MS) {
        delta_px += 1;
    } else if (delta < FAST_START_THRESHOLD_MS) {
        delta_px += 0;
    } else {
        /* very slow movement: keep only base + initial boost */
    }

    return delta_px;
}

/* ==== Space Listener ==== */
static int space_listener_cb(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (!ev) {
        return 0;
    }

    if (ev->position == 60) {
        space_pressed = ev->state;
        LOG_INF("Space %s", space_pressed ? "HELD" : "RELEASED");
    }

    return 0;
}

ZMK_LISTENER(space_listener, space_listener_cb);
ZMK_SUBSCRIPTION(space_listener, zmk_position_state_changed);

/* ==== GPIO interrupt callback ==== */
static void dir_edge_cb(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {
    ARG_UNUSED(cb);

    for (size_t i = 0; i < ARRAY_SIZE(dir_inputs); i++) {
        DirInput *d = &dir_inputs[i];

        if ((dev == d->gpio_dev) && (pins & BIT(d->pin))) {
            int val = gpio_pin_get(dev, d->pin);

            if (val != d->last_state) {
                uint32_t now = k_uptime_get_32();
                uint32_t delta = now - d->last_time;
                int delta_px = calc_delta_px(delta);

                if (i < 2) {
                    dx_acc += d->sign * delta_px;
                } else {
                    dy_acc += d->sign * delta_px;
                }

                d->last_state = val;
                d->last_time = now;
            }
        }
    }
}

/* ==== Scroll repeat task ==== */
static void arrow_repeat_work_handler(struct k_work *work) {
    struct k_work_delayable *dwork = CONTAINER_OF(work, struct k_work_delayable, work);
    struct bbtrackball_data *data = CONTAINER_OF(dwork, struct bbtrackball_data, arrow_repeat_work);

    if (!dx_acc && !dy_acc) {
        k_work_schedule(&data->arrow_repeat_work, K_MSEC(SCROLL_DELAY_MS));
        return;
    }

    int dx = -dx_acc;
    int dy = -dy_acc;

    /* Space held -> scroll mode */
    if (space_pressed) {
        int scroll_x = scale_scroll(dx, SCROLL_X_DIV);
        int scroll_y = scale_scroll(dy, SCROLL_Y_DIV);

        input_report_rel(data->dev, INPUT_REL_HWHEEL, scroll_x, false, K_FOREVER);
        input_report_rel(data->dev, INPUT_REL_WHEEL, -scroll_y, true, K_FOREVER);

        dx_acc = 0;
        dy_acc = 0;

        k_work_schedule(&data->arrow_repeat_work, K_MSEC(SCROLL_DELAY_MS));
        return;
    }

    /*
     * Not in scroll mode:
     * keep report task responsible for normal pointer movement.
     * Do not clear dx_acc/dy_acc here, otherwise movement can be lost.
     */
    k_work_schedule(&data->arrow_repeat_work, K_MSEC(10));
}

/* ==== HID report periodic task (regular mouse movement) ==== */
static void report_work_handler(struct k_work *work) {
    struct k_work_delayable *dwork = CONTAINER_OF(work, struct k_work_delayable, work);
    struct bbtrackball_data *data = CONTAINER_OF(dwork, struct bbtrackball_data, report_work);
    const struct device *dev = data->dev;

    trackball_dev_ref = dev;

    if (dx_acc || dy_acc) {
        moved = true;

        /* Space held -> disable mouse move, scroll handled elsewhere */
        if (!space_pressed) {
            int dx = -dx_acc;
            int dy = -dy_acc;

            input_report_rel(dev, INPUT_REL_X, dx, false, K_FOREVER);
            input_report_rel(dev, INPUT_REL_Y, dy, true, K_FOREVER);

            dx_acc = 0;
            dy_acc = 0;
        }
    } else {
        moved = false;
    }

    k_work_schedule(&data->report_work, K_MSEC(REPORT_INTERVAL_MS));
}

/* ==== Init ==== */
static int bbtrackball_init(const struct device *dev) {
    struct bbtrackball_data *data = dev->data;

    LOG_INF("Initializing BBtrackball (interrupt + workqueue + tuned tiny-ball scroll mode)...");

    for (size_t i = 0; i < ARRAY_SIZE(dir_inputs); i++) {
        DirInput *d = &dir_inputs[i];

        gpio_pin_configure(d->gpio_dev, d->pin, GPIO_INPUT | GPIO_PULL_UP | GPIO_INT_EDGE_BOTH);
        d->last_state = gpio_pin_get(d->gpio_dev, d->pin);
        d->last_time = k_uptime_get_32();

        gpio_init_callback(&gpio_cbs[i], dir_edge_cb, BIT(d->pin));
        gpio_add_callback(d->gpio_dev, &gpio_cbs[i]);
        gpio_pin_interrupt_configure(d->gpio_dev, d->pin, GPIO_INT_EDGE_BOTH);
    }

    data->dev = dev;
    trackball_dev_ref = dev;

    k_work_init_delayable(&data->report_work, report_work_handler);
    k_work_schedule(&data->report_work, K_MSEC(REPORT_INTERVAL_MS));

    k_work_init_delayable(&data->arrow_repeat_work, arrow_repeat_work_handler);
    k_work_schedule(&data->arrow_repeat_work, K_MSEC(SCROLL_DELAY_MS));

    return 0;
}

/* ==== Driver instance registration ==== */
#define BBTRACKBALL_INIT_PRIORITY CONFIG_INPUT_INIT_PRIORITY
#define BBTRACKBALL_DEFINE(inst)                                                                   \
    static struct bbtrackball_data bbtrackball_data_##inst;                                        \
    static const struct bbtrackball_dev_config bbtrackball_config_##inst = {                       \
        .x_input_code = DT_PROP_OR(DT_DRV_INST(inst), x_input_code, INPUT_REL_X),                  \
        .y_input_code = DT_PROP_OR(DT_DRV_INST(inst), y_input_code, INPUT_REL_Y),                  \
    };                                                                                             \
    DEVICE_DT_INST_DEFINE(inst, bbtrackball_init, NULL, &bbtrackball_data_##inst,                  \
                          &bbtrackball_config_##inst, POST_KERNEL, BBTRACKBALL_INIT_PRIORITY,      \
                          NULL);

DT_INST_FOREACH_STATUS_OKAY(BBTRACKBALL_DEFINE);
