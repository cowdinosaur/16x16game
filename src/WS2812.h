#ifndef WS2812_H
#define WS2812_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "driver/rmt.h"

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} ws2812_pixel_t;

typedef struct {
    rmt_channel_t channel;
    gpio_num_t gpio;
    uint16_t pixel_count;
    ws2812_pixel_t *pixels;
    uint8_t brightness;
} ws2812_t;

// Initialize WS2812 LED strip
ws2812_t* ws2812_init(uint16_t pixel_count, gpio_num_t gpio, rmt_channel_t channel);

// Free WS2812 resources
void ws2812_free(ws2812_t *strip);

// Set brightness (0-255)
void ws2812_set_brightness(ws2812_t *strip, uint8_t brightness);

// Set pixel color
void ws2812_set_pixel(ws2812_t *strip, uint16_t index, uint8_t r, uint8_t g, uint8_t b);

// Set pixel color from ws2812_pixel_t
void ws2812_set_pixel_rgb(ws2812_t *strip, uint16_t index, ws2812_pixel_t color);

// Get pixel color
ws2812_pixel_t ws2812_get_pixel(ws2812_t *strip, uint16_t index);

// Clear all pixels
void ws2812_clear(ws2812_t *strip);

// Send data to LEDs
void ws2812_show(ws2812_t *strip);

// Helper function to create color from HSV
ws2812_pixel_t ws2812_hsv_to_rgb(uint8_t h, uint8_t s, uint8_t v);

// Helper function for rainbow effect
ws2812_pixel_t ws2812_wheel(uint8_t pos);

#ifdef __cplusplus
}
#endif

#endif // WS2812_H