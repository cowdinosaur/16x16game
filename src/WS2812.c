#include "WS2812.h"
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "WS2812"

// WS2812 timing specifications (in nanoseconds)
#define WS2812_T0H_NS 400
#define WS2812_T0L_NS 850
#define WS2812_T1H_NS 800
#define WS2812_T1L_NS 450
#define WS2812_RESET_US 50

ws2812_t* ws2812_init(uint16_t pixel_count, gpio_num_t gpio, rmt_channel_t channel) {
    ws2812_t *strip = (ws2812_t*)malloc(sizeof(ws2812_t));
    if (!strip) {
        ESP_LOGE(TAG, "Failed to allocate memory for WS2812 strip");
        return NULL;
    }

    strip->channel = channel;
    strip->gpio = gpio;
    strip->pixel_count = pixel_count;
    strip->brightness = 255;

    // Allocate pixel buffer
    strip->pixels = (ws2812_pixel_t*)calloc(pixel_count, sizeof(ws2812_pixel_t));
    if (!strip->pixels) {
        ESP_LOGE(TAG, "Failed to allocate memory for pixel buffer");
        free(strip);
        return NULL;
    }

    // Configure RMT
    rmt_config_t config = RMT_DEFAULT_CONFIG_TX(gpio, channel);
    config.clk_div = 2;  // 40MHz clock
    config.mem_block_num = 1;

    ESP_ERROR_CHECK(rmt_config(&config));
    ESP_ERROR_CHECK(rmt_driver_install(channel, 0, 0));

    ESP_LOGI(TAG, "WS2812 initialized: %d pixels on GPIO %d", pixel_count, gpio);
    return strip;
}

void ws2812_free(ws2812_t *strip) {
    if (strip) {
        rmt_driver_uninstall(strip->channel);
        if (strip->pixels) {
            free(strip->pixels);
        }
        free(strip);
    }
}

void ws2812_set_brightness(ws2812_t *strip, uint8_t brightness) {
    if (strip) {
        strip->brightness = brightness;
    }
}

void ws2812_set_pixel(ws2812_t *strip, uint16_t index, uint8_t r, uint8_t g, uint8_t b) {
    if (strip && index < strip->pixel_count) {
        strip->pixels[index].r = r;
        strip->pixels[index].g = g;
        strip->pixels[index].b = b;
    }
}

void ws2812_set_pixel_rgb(ws2812_t *strip, uint16_t index, ws2812_pixel_t color) {
    if (strip && index < strip->pixel_count) {
        strip->pixels[index] = color;
    }
}

ws2812_pixel_t ws2812_get_pixel(ws2812_t *strip, uint16_t index) {
    ws2812_pixel_t pixel = {0, 0, 0};
    if (strip && index < strip->pixel_count) {
        pixel = strip->pixels[index];
    }
    return pixel;
}

void ws2812_clear(ws2812_t *strip) {
    if (strip && strip->pixels) {
        memset(strip->pixels, 0, strip->pixel_count * sizeof(ws2812_pixel_t));
    }
}

static void ws2812_write_byte(rmt_item32_t *item, uint8_t byte) {
    for (int bit = 7; bit >= 0; bit--) {
        if (byte & (1 << bit)) {
            // Send 1
            item->level0 = 1;
            item->duration0 = WS2812_T1H_NS / 25;  // Convert ns to ticks (40MHz/2 = 20MHz = 50ns per tick)
            item->level1 = 0;
            item->duration1 = WS2812_T1L_NS / 25;
        } else {
            // Send 0
            item->level0 = 1;
            item->duration0 = WS2812_T0H_NS / 25;
            item->level1 = 0;
            item->duration1 = WS2812_T0L_NS / 25;
        }
        item++;
    }
}

void ws2812_show(ws2812_t *strip) {
    if (!strip || !strip->pixels) return;

    size_t num_items = strip->pixel_count * 3 * 8;  // 3 bytes per pixel, 8 bits per byte
    rmt_item32_t *items = (rmt_item32_t*)malloc(num_items * sizeof(rmt_item32_t));
    if (!items) {
        ESP_LOGE(TAG, "Failed to allocate RMT items");
        return;
    }

    rmt_item32_t *item = items;

    // Convert pixels to RMT items with brightness adjustment
    for (int i = 0; i < strip->pixel_count; i++) {
        uint8_t r = (strip->pixels[i].r * strip->brightness) / 255;
        uint8_t g = (strip->pixels[i].g * strip->brightness) / 255;
        uint8_t b = (strip->pixels[i].b * strip->brightness) / 255;

        // WS2812 expects GRB order
        ws2812_write_byte(item, g);
        item += 8;
        ws2812_write_byte(item, r);
        item += 8;
        ws2812_write_byte(item, b);
        item += 8;
    }

    // Send the data
    rmt_write_items(strip->channel, items, num_items, true);
    rmt_wait_tx_done(strip->channel, portMAX_DELAY);

    free(items);

    // Reset signal
    vTaskDelay(pdMS_TO_TICKS(1));
}

ws2812_pixel_t ws2812_hsv_to_rgb(uint8_t h, uint8_t s, uint8_t v) {
    ws2812_pixel_t rgb;
    uint8_t region, remainder, p, q, t;

    if (s == 0) {
        rgb.r = rgb.g = rgb.b = v;
        return rgb;
    }

    region = h / 43;
    remainder = (h - (region * 43)) * 6;

    p = (v * (255 - s)) >> 8;
    q = (v * (255 - ((s * remainder) >> 8))) >> 8;
    t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8;

    switch (region) {
        case 0:
            rgb.r = v; rgb.g = t; rgb.b = p;
            break;
        case 1:
            rgb.r = q; rgb.g = v; rgb.b = p;
            break;
        case 2:
            rgb.r = p; rgb.g = v; rgb.b = t;
            break;
        case 3:
            rgb.r = p; rgb.g = q; rgb.b = v;
            break;
        case 4:
            rgb.r = t; rgb.g = p; rgb.b = v;
            break;
        default:
            rgb.r = v; rgb.g = p; rgb.b = q;
            break;
    }

    return rgb;
}

ws2812_pixel_t ws2812_wheel(uint8_t pos) {
    ws2812_pixel_t color;

    if (pos < 85) {
        color.r = pos * 3;
        color.g = 255 - pos * 3;
        color.b = 0;
    } else if (pos < 170) {
        pos -= 85;
        color.r = 255 - pos * 3;
        color.g = 0;
        color.b = pos * 3;
    } else {
        pos -= 170;
        color.r = 0;
        color.g = pos * 3;
        color.b = 255 - pos * 3;
    }

    return color;
}