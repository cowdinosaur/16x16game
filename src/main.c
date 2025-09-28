#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/rmt.h"

#define TAG "LED_GAME"

// Configuration
#define MATRIX_WIDTH 16
#define MATRIX_HEIGHT 16
#define LED_COUNT (MATRIX_WIDTH * MATRIX_HEIGHT)
#define LED_PIN GPIO_NUM_2
#define BRIGHTNESS 50

// I2C Configuration for ToF sensor
#define I2C_MASTER_SCL_IO 9
#define I2C_MASTER_SDA_IO 8
#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_FREQ_HZ 400000
#define VL53L0X_ADDR 0x29

// RMT Configuration for WS2812
#define RMT_TX_CHANNEL RMT_CHANNEL_0

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} rgb_t;

typedef enum {
    MENU,
    PONG,
    FLAPPY,
    CATCH,
    INVADERS
} game_mode_t;

// Global variables
static rgb_t leds[LED_COUNT];
static uint16_t sensor_distance = 200;
static game_mode_t current_mode = MENU;
static int menu_selection = 0;

// Function prototypes
void init_hardware(void);
void init_i2c(void);
void init_ws2812(void);
void clear_display(void);
void set_pixel(int x, int y, rgb_t color);
void show_display(void);
uint16_t read_tof_sensor(void);
int get_sensor_position(void);
void draw_menu(void);
void run_pong(void);
void run_flappy(void);
void run_catch(void);
void run_invaders(void);

// WS2812 timing
#define WS2812_T0H_NS 400
#define WS2812_T0L_NS 850
#define WS2812_T1H_NS 800
#define WS2812_T1L_NS 450

static void ws2812_write(uint8_t *data, size_t len) {
    rmt_item32_t *items = malloc(len * 8 * sizeof(rmt_item32_t));
    if (!items) return;

    size_t item_idx = 0;
    for (size_t i = 0; i < len; i++) {
        for (int bit = 7; bit >= 0; bit--) {
            if (data[i] & (1 << bit)) {
                items[item_idx].level0 = 1;
                items[item_idx].duration0 = WS2812_T1H_NS / 10 * 8 / 100;
                items[item_idx].level1 = 0;
                items[item_idx].duration1 = WS2812_T1L_NS / 10 * 8 / 100;
            } else {
                items[item_idx].level0 = 1;
                items[item_idx].duration0 = WS2812_T0H_NS / 10 * 8 / 100;
                items[item_idx].level1 = 0;
                items[item_idx].duration1 = WS2812_T0L_NS / 10 * 8 / 100;
            }
            item_idx++;
        }
    }

    rmt_write_items(RMT_TX_CHANNEL, items, item_idx, true);
    free(items);
}

void init_ws2812(void) {
    rmt_config_t config = RMT_DEFAULT_CONFIG_TX(LED_PIN, RMT_TX_CHANNEL);
    config.clk_div = 1;

    ESP_ERROR_CHECK(rmt_config(&config));
    ESP_ERROR_CHECK(rmt_driver_install(config.channel, 0, 0));

    clear_display();
    show_display();
}

void init_i2c(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };

    ESP_ERROR_CHECK(i2c_param_config(I2C_MASTER_NUM, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0));

    // Initialize VL53L0X
    uint8_t init_seq[] = {0x00, 0x00};
    i2c_master_write_to_device(I2C_MASTER_NUM, VL53L0X_ADDR, init_seq, sizeof(init_seq), 1000 / portTICK_PERIOD_MS);
}

void init_hardware(void) {
    ESP_LOGI(TAG, "Initializing hardware...");
    init_ws2812();
    init_i2c();
    ESP_LOGI(TAG, "Hardware initialized");
}

void clear_display(void) {
    memset(leds, 0, sizeof(leds));
}

int get_index(int x, int y) {
    if (x < 0 || x >= MATRIX_WIDTH || y < 0 || y >= MATRIX_HEIGHT) {
        return -1;
    }
    // Zigzag pattern for LED matrix
    if (y % 2 == 0) {
        return y * MATRIX_WIDTH + x;
    } else {
        return y * MATRIX_WIDTH + (MATRIX_WIDTH - 1 - x);
    }
}

void set_pixel(int x, int y, rgb_t color) {
    int idx = get_index(x, y);
    if (idx >= 0 && idx < LED_COUNT) {
        leds[idx].r = (color.r * BRIGHTNESS) / 255;
        leds[idx].g = (color.g * BRIGHTNESS) / 255;
        leds[idx].b = (color.b * BRIGHTNESS) / 255;
    }
}

void show_display(void) {
    uint8_t data[LED_COUNT * 3];
    for (int i = 0; i < LED_COUNT; i++) {
        // GRB order for WS2812
        data[i * 3] = leds[i].g;
        data[i * 3 + 1] = leds[i].r;
        data[i * 3 + 2] = leds[i].b;
    }
    ws2812_write(data, sizeof(data));
}

uint16_t read_tof_sensor(void) {
    // Simplified ToF reading - implement full VL53L0X protocol as needed
    uint8_t reg = 0x14;
    uint8_t data[2];

    i2c_master_write_read_device(I2C_MASTER_NUM, VL53L0X_ADDR, &reg, 1, data, 2, 1000 / portTICK_PERIOD_MS);

    uint16_t distance = (data[0] << 8) | data[1];
    if (distance < 50) distance = 50;
    if (distance > 400) distance = 400;

    return distance;
}

int get_sensor_position(void) {
    uint16_t dist = read_tof_sensor();
    int pos = ((dist - 50) * 15) / 350;
    if (pos < 0) pos = 0;
    if (pos > 15) pos = 15;
    return pos;
}

void draw_rect(int x, int y, int w, int h, rgb_t color, bool filled) {
    if (filled) {
        for (int i = 0; i < h; i++) {
            for (int j = 0; j < w; j++) {
                set_pixel(x + j, y + i, color);
            }
        }
    } else {
        for (int i = 0; i < w; i++) {
            set_pixel(x + i, y, color);
            set_pixel(x + i, y + h - 1, color);
        }
        for (int i = 0; i < h; i++) {
            set_pixel(x, y + i, color);
            set_pixel(x + w - 1, y + i, color);
        }
    }
}

void draw_menu(void) {
    clear_display();

    rgb_t colors[] = {
        {255, 0, 0},    // Red
        {255, 255, 0},  // Yellow
        {0, 255, 0},    // Green
        {0, 255, 255}   // Cyan
    };

    // Draw game icons
    for (int game = 0; game < 4; game++) {
        int x = (game % 2) * 8 + 2;
        int y = (game / 2) * 8 + 2;

        rgb_t color = (game == menu_selection) ? colors[game] : (rgb_t){20, 20, 20};
        draw_rect(x, y, 4, 4, color, true);
    }

    // Draw selection border
    if (menu_selection < 4) {
        int x = (menu_selection % 2) * 8;
        int y = (menu_selection / 2) * 8;
        draw_rect(x, y, 8, 8, (rgb_t){255, 255, 255}, false);
    }

    show_display();
}

// Simple Pong game implementation
typedef struct {
    float ball_x, ball_y;
    float ball_vx, ball_vy;
    int player_y;
    int ai_y;
    int player_score;
    int ai_score;
    bool game_over;
} pong_state_t;

static pong_state_t pong;

void init_pong(void) {
    pong.ball_x = 8;
    pong.ball_y = 8;
    pong.ball_vx = 1;
    pong.ball_vy = 0.5;
    pong.player_y = 7;
    pong.ai_y = 7;
    pong.player_score = 0;
    pong.ai_score = 0;
    pong.game_over = false;
}

void update_pong(void) {
    // Update player paddle
    pong.player_y = get_sensor_position();
    if (pong.player_y < 1) pong.player_y = 1;
    if (pong.player_y > 13) pong.player_y = 13;

    // Update AI paddle
    if (pong.ball_y < pong.ai_y + 1) {
        pong.ai_y--;
        if (pong.ai_y < 1) pong.ai_y = 1;
    } else if (pong.ball_y > pong.ai_y + 1) {
        pong.ai_y++;
        if (pong.ai_y > 13) pong.ai_y = 13;
    }

    // Update ball
    pong.ball_x += pong.ball_vx;
    pong.ball_y += pong.ball_vy;

    // Ball collision with top/bottom
    if (pong.ball_y <= 0 || pong.ball_y >= 15) {
        pong.ball_vy = -pong.ball_vy;
    }

    // Ball collision with paddles
    if (pong.ball_x <= 1) {
        if (pong.ball_y >= pong.player_y - 1 && pong.ball_y <= pong.player_y + 2) {
            pong.ball_vx = -pong.ball_vx;
        } else {
            pong.ai_score++;
            pong.ball_x = 8;
            pong.ball_y = 8;
            pong.ball_vx = 1;
        }
    }

    if (pong.ball_x >= 14) {
        if (pong.ball_y >= pong.ai_y - 1 && pong.ball_y <= pong.ai_y + 2) {
            pong.ball_vx = -pong.ball_vx;
        } else {
            pong.player_score++;
            pong.ball_x = 8;
            pong.ball_y = 8;
            pong.ball_vx = -1;
        }
    }

    if (pong.player_score >= 5 || pong.ai_score >= 5) {
        pong.game_over = true;
    }
}

void render_pong(void) {
    clear_display();

    // Draw paddles
    draw_rect(0, pong.player_y, 1, 3, (rgb_t){0, 0, 255}, true);
    draw_rect(15, pong.ai_y, 1, 3, (rgb_t){255, 0, 0}, true);

    // Draw ball
    set_pixel((int)pong.ball_x, (int)pong.ball_y, (rgb_t){255, 255, 255});

    // Draw center line
    for (int i = 0; i < 16; i += 2) {
        set_pixel(8, i, (rgb_t){40, 40, 40});
    }

    show_display();
}

void run_pong(void) {
    static bool initialized = false;
    if (!initialized) {
        init_pong();
        initialized = true;
    }

    update_pong();
    render_pong();

    if (pong.game_over) {
        vTaskDelay(3000 / portTICK_PERIOD_MS);
        current_mode = MENU;
        initialized = false;
    }
}

// Simplified other games
void run_flappy(void) {
    static float bird_y = 8;
    static float bird_vy = 0;
    static int pipe_x = 16;
    static int pipe_gap_y = 8;

    // Update bird based on sensor
    int sensor_pos = get_sensor_position();
    if (sensor_pos < 5) {
        bird_vy = -1;
    }
    bird_vy += 0.2;
    bird_y += bird_vy;

    if (bird_y < 0) bird_y = 0;
    if (bird_y > 15) {
        current_mode = MENU;
        bird_y = 8;
        return;
    }

    // Update pipe
    pipe_x--;
    if (pipe_x < -1) {
        pipe_x = 16;
        pipe_gap_y = rand() % 10 + 3;
    }

    // Render
    clear_display();
    set_pixel(4, (int)bird_y, (rgb_t){255, 255, 0});

    if (pipe_x >= 0 && pipe_x < 16) {
        for (int y = 0; y < 16; y++) {
            if (y < pipe_gap_y || y > pipe_gap_y + 3) {
                set_pixel(pipe_x, y, (rgb_t){0, 255, 0});
            }
        }
    }

    show_display();
}

void run_catch(void) {
    static int basket_x = 7;
    static float item_y = 0;
    static int item_x = 8;

    // Update basket
    basket_x = get_sensor_position();
    if (basket_x > 13) basket_x = 13;

    // Update falling item
    item_y += 0.3;
    if (item_y >= 14) {
        if (item_x >= basket_x && item_x < basket_x + 3) {
            // Caught!
        }
        item_y = 0;
        item_x = rand() % 16;
    }

    // Render
    clear_display();
    draw_rect(basket_x, 14, 3, 2, (rgb_t){0, 0, 255}, true);
    set_pixel(item_x, (int)item_y, (rgb_t){0, 255, 128});
    show_display();
}

void run_invaders(void) {
    static int player_x = 7;
    static int invaders[20];
    static bool initialized = false;

    if (!initialized) {
        for (int i = 0; i < 20; i++) {
            invaders[i] = 1;
        }
        initialized = true;
    }

    // Update player
    player_x = get_sensor_position();
    if (player_x > 14) player_x = 14;

    // Render
    clear_display();
    draw_rect(player_x, 14, 2, 2, (rgb_t){0, 255, 255}, true);

    for (int i = 0; i < 20; i++) {
        if (invaders[i]) {
            int x = (i % 5) * 3 + 1;
            int y = (i / 5) * 2 + 1;
            set_pixel(x, y, (rgb_t){0, 255, 0});
        }
    }

    show_display();
}

void game_task(void *pvParameters) {
    while (1) {
        sensor_distance = read_tof_sensor();

        switch (current_mode) {
            case MENU:
                menu_selection = get_sensor_position() / 4;
                if (menu_selection > 3) menu_selection = 3;
                draw_menu();

                // Simple selection: wave hand quickly to select
                static uint16_t last_dist = 200;
                if (abs(sensor_distance - last_dist) > 100) {
                    switch (menu_selection) {
                        case 0: current_mode = PONG; break;
                        case 1: current_mode = FLAPPY; break;
                        case 2: current_mode = CATCH; break;
                        case 3: current_mode = INVADERS; break;
                    }
                }
                last_dist = sensor_distance;
                break;

            case PONG:
                run_pong();
                break;

            case FLAPPY:
                run_flappy();
                break;

            case CATCH:
                run_catch();
                break;

            case INVADERS:
                run_invaders();
                break;
        }

        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "LED Matrix Game System Starting...");

    init_hardware();

    xTaskCreate(game_task, "game_task", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "System initialized!");
}