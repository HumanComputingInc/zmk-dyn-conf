/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_animation_ripple

#include <zephyr/device.h>

#include <stdlib.h>
#include <math.h>

#include <zephyr/logging/log.h>

#include <drivers/animation.h>

#include <zmk/animation.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct animation_ripple_event {
    size_t pixel_id;
    uint16_t distance;
    uint8_t counter;
};

struct animation_ripple_config {
    struct zmk_color_hsl *color_hsl;
    size_t *pixel_map;
    size_t pixel_map_size;
    size_t event_buffer_size;
    uint8_t blending_mode;
    uint8_t distance_per_frame;
    uint8_t ripple_width;
    uint8_t event_frames;
};

struct animation_ripple_data {
    struct zmk_color_rgb color_rgb;
    struct animation_ripple_event *event_buffer;
    size_t events_start;
    size_t events_end;
    size_t num_events;
    bool is_active;
};

static int animation_ripple_on_key_press(const struct device *dev, const zmk_event_t *event) {
    const struct animation_ripple_config *config = dev->config;
    struct animation_ripple_data *data = dev->data;

    const struct zmk_position_state_changed *pos_event;

    if (!data->is_active) {
        return 0;
    }

    if ((pos_event = as_zmk_position_state_changed(event)) == NULL) {
        // Event not supported.
        return -ENOTSUP;
    }

    if (!pos_event->state) {
        // Don't track key releases.
        return 0;
    }

    if (data->num_events == config->event_buffer_size) {
        // Event buffer is full - new key press events are dropped.
        return -ENOMEM;
    }

    data->event_buffer[data->events_end].pixel_id =
        zmk_animation_get_pixel_by_key_position(pos_event->position);
    data->event_buffer[data->events_end].distance = 0;
    data->event_buffer[data->events_end].counter = 0;

    data->events_end = (data->events_end + 1) % config->event_buffer_size;
    data->num_events += 1;

    zmk_animation_request_frames(config->event_frames);

    return 0;
}

static void animation_ripple_render_frame(const struct device *dev, struct animation_pixel *pixels,
                                          size_t num_pixels) {
    const struct animation_ripple_config *config = dev->config;
    struct animation_ripple_data *data = dev->data;

    size_t *pixel_map = config->pixel_map;

    size_t i = data->events_start;
    /* Used to detect case where data->events_start == data->events_end */
    bool events_full = (data->num_events == config->event_buffer_size);

    while (i != data->events_end || events_full) {
        struct animation_ripple_event *event = &data->event_buffer[i];

        // Render all pixels for each event
        for (int j = 0; j < config->pixel_map_size; ++j) {
            uint8_t pixel_distance = zmk_animation_get_pixel_distance(event->pixel_id, pixel_map[j]);

            if (config->ripple_width > abs(pixel_distance - event->distance)) {
                float intensity = 1.0f - (float)abs(pixel_distance - event->distance) /
                                             (float)config->ripple_width;

                struct zmk_color_rgb color = {
                    .r = intensity * data->color_rgb.r,
                    .g = intensity * data->color_rgb.g,
                    .b = intensity * data->color_rgb.b,
                };

                pixels[pixel_map[j]].value = zmk_apply_blending_mode(pixels[pixel_map[j]].value,
                                                                     color, config->blending_mode);
            }
        }

        // Update event counter
        if (event->counter < config->event_frames) {
            event->distance += config->distance_per_frame;
            event->counter += 1;
        } else {
            data->events_start = (data->events_start + 1) % config->event_buffer_size;
            data->num_events -= 1;
        }

        /* Clear event full flag, so we can exit loop */
        events_full = false;

        if (++i == config->event_buffer_size) {
            i = 0;
        }
    }
}

static void animation_ripple_start(const struct device *dev) {
    struct animation_ripple_data *data = dev->data;

    data->is_active = true;
}

static void animation_ripple_stop(const struct device *dev) {
    struct animation_ripple_data *data = dev->data;

    data->is_active = false;

    // Cancel processing of any ongoing events.
    data->num_events = 0;
    data->events_start = 0;
    data->events_end = 0;
}

static int animation_ripple_init(const struct device *dev) {
    const struct animation_ripple_config *config = dev->config;
    struct animation_ripple_data *data = dev->data;

    zmk_hsl_to_rgb(config->color_hsl, &data->color_rgb);

    return 0;
}

static const struct animation_api animation_ripple_api = {
    .on_start = animation_ripple_start,
    .on_stop = animation_ripple_stop,
    .render_frame = animation_ripple_render_frame,
};

#define ANIMATION_RIPPLE_DEVICE(idx)                                                               \
                                                                                                   \
    static struct animation_ripple_event                                                           \
        animation_ripple_##idx##_events[DT_INST_PROP(idx, buffer_size)];                           \
                                                                                                   \
    static struct animation_ripple_data animation_ripple_##idx##_data = {                          \
        .event_buffer = animation_ripple_##idx##_events,                                           \
        .events_start = 0,                                                                         \
        .events_end = 0,                                                                           \
        .num_events = 0,                                                                           \
    };                                                                                             \
                                                                                                   \
    static size_t animation_ripple_##idx##_pixel_map[] = DT_INST_PROP(idx, pixels);                \
                                                                                                   \
    static uint32_t animation_ripple_##idx##_color = DT_INST_PROP(idx, color);                     \
                                                                                                   \
    static struct animation_ripple_config animation_ripple_##idx##_config = {                      \
        .color_hsl = (struct zmk_color_hsl *)&animation_ripple_##idx##_color,                      \
        .pixel_map = &animation_ripple_##idx##_pixel_map[0],                                       \
        .pixel_map_size = DT_INST_PROP_LEN(idx, pixels),                                           \
        .event_buffer_size = DT_INST_PROP(idx, buffer_size),                                       \
        .blending_mode = DT_INST_PROP(idx, blending_mode),                                         \
        .distance_per_frame =                                                                      \
            (255 * 1000 / DT_INST_PROP(idx, duration)) / CONFIG_ZMK_ANIMATION_FPS,                 \
        .ripple_width = DT_INST_PROP(idx, ripple_width) / 2,                                       \
        .event_frames =                                                                            \
            255 / ((255 * 1000 / DT_INST_PROP(idx, duration)) / CONFIG_ZMK_ANIMATION_FPS),         \
    };                                                                                             \
                                                                                                   \
    DEVICE_DT_INST_DEFINE(idx, &animation_ripple_init, NULL, &animation_ripple_##idx##_data,       \
                          &animation_ripple_##idx##_config, POST_KERNEL,                           \
                          CONFIG_APPLICATION_INIT_PRIORITY, &animation_ripple_api);                \
                                                                                                   \
    static int animation_ripple_##idx##_event_handler(const zmk_event_t *event) {                  \
        const struct device *dev = DEVICE_DT_GET(DT_DRV_INST(idx));                                \
                                                                                                   \
        return animation_ripple_on_key_press(dev, event);                                          \
    }                                                                                              \
                                                                                                   \
    ZMK_LISTENER(animation_ripple_##idx, animation_ripple_##idx##_event_handler);                  \
    ZMK_SUBSCRIPTION(animation_ripple_##idx, zmk_position_state_changed);

DT_INST_FOREACH_STATUS_OKAY(ANIMATION_RIPPLE_DEVICE);
