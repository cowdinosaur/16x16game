#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "WS2812.h"
#include "VL53L0X.h"

#define TAG "LED_GAME"

// Configuration
#define MATRIX_WIDTH 16
#define MATRIX_HEIGHT 16
#define LED_COUNT (MATRIX_WIDTH * MATRIX_HEIGHT)
#define LED_PIN GPIO_NUM_10  // Changed from GPIO_NUM_2 based on your setup
#define BRIGHTNESS 50
#define RMT_CHANNEL RMT_CHANNEL_0

// I2C Configuration for ToF sensor
#define I2C_MASTER_SCL_IO 9
#define I2C_MASTER_SDA_IO 8
#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_FREQ_HZ 400000
#define VL53L0X_ADDR 0x29

typedef enum {
    MENU,
    PONG,
    FLAPPY,
    CATCH,
    INVADERS
} game_mode_t;

// Global variables
static ws2812_t *strip = nullptr;
static VL53L0X *tof_sensor = nullptr;
static uint16_t sensor_distance = 200;
static game_mode_t current_mode = MENU;  // Start with menu
static int menu_selection = -1;  // -1 means no selection (out of range)
static int last_stable_selection = -1;
static uint32_t selection_start_time = 0;
static bool sensor_initialized = false;
static bool tof_debug_mode = false;  // Set to true for detailed sensor output

// Menu selection configuration
#define MIN_SELECTION_DISTANCE 100  // Minimum distance for valid selection (mm)
#define MAX_SELECTION_DISTANCE 350  // Maximum distance for valid selection (mm)
#define SELECTION_HOLD_TIME 5000    // Time to hold selection to confirm (ms)

// Function prototypes
void init_hardware(void);
void init_tof_sensor(void);
void clear_display(void);
void set_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b);
void show_display(void);
uint16_t read_tof_sensor(void);
int get_sensor_position(void);
void draw_menu(void);
void print_game_legend(game_mode_t game);
void test_matrix_mapping(void);
void show_transition_screen(const char* text, uint8_t r, uint8_t g, uint8_t b, int duration_ms);
void show_game_over_screen(int score, int high_score);
void run_pong(void);
void run_flappy(void);
void run_catch(void);
void run_invaders(void);

void init_tof_sensor(void) {
    ESP_LOGI(TAG, "Initializing VL53L0X ToF sensor");

    // Create VL53L0X instance
    tof_sensor = new VL53L0X(I2C_MASTER_NUM);

    // Initialize I2C master
    tof_sensor->i2cMasterInit(static_cast<gpio_num_t>(I2C_MASTER_SDA_IO),
                              static_cast<gpio_num_t>(I2C_MASTER_SCL_IO));

    // Initialize sensor
    if (!tof_sensor->init()) {
        ESP_LOGE(TAG, "Failed to initialize VL53L0X");
        delete tof_sensor;
        tof_sensor = nullptr;
        sensor_initialized = false;
    } else {
        sensor_initialized = true;
        ESP_LOGI(TAG, "VL53L0X initialized successfully");
    }
}

void init_hardware(void) {
    ESP_LOGI(TAG, "Initializing hardware...");

    // Initialize WS2812 LED strip
    strip = ws2812_init(LED_COUNT, LED_PIN, RMT_CHANNEL);
    if (!strip) {
        ESP_LOGE(TAG, "Failed to initialize WS2812 strip");
        return;
    }
    ws2812_set_brightness(strip, BRIGHTNESS);

    // Initialize ToF sensor
    init_tof_sensor();

    ESP_LOGI(TAG, "Hardware initialized");
}

void clear_display(void) {
    ws2812_clear(strip);
}

int get_index(int x, int y) {
    if (x < 0 || x >= MATRIX_WIDTH || y < 0 || y >= MATRIX_HEIGHT) {
        return -1;
    }
    return y * MATRIX_WIDTH + (MATRIX_WIDTH - 1 - x);

    // Zigzag pattern for LED matrix
    // Try flipping: even rows reversed, odd rows normal
    if (y % 2 == 0) {
        // Even rows (0, 2, 4...): right to left
        return y * MATRIX_WIDTH + (MATRIX_WIDTH - 1 - x);
    } else {
        // Odd rows (1, 3, 5...): left to right
        return y * MATRIX_WIDTH + x;
    }
}

void set_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    int idx = get_index(x, y);
    if (idx >= 0 && idx < LED_COUNT) {
        ws2812_set_pixel(strip, idx, r, g, b);
    }
}

void show_display(void) {
    ws2812_show(strip);
}

uint16_t read_tof_sensor(void) {
    static int reading_count = 0;

    if (sensor_initialized && tof_sensor) {
        uint16_t distance_mm = 0;
        if (tof_sensor->read(&distance_mm)) {
            // Print raw reading every 10th reading to avoid spam
            if (reading_count++ % 10 == 0) {
                printf("ToF Raw: %d mm", distance_mm);
                if (distance_mm < 50) {
                    printf(" (below min, clamping to 50)");
                } else if (distance_mm > 400) {
                    printf(" (above max, clamping to 400)");
                }
                printf("\n");
            }

            // Clamp to expected range
            if (distance_mm < 50) distance_mm = 50;
            if (distance_mm > 400) distance_mm = 400;
            return distance_mm;
        } else {
            if (reading_count++ % 20 == 0) {
                printf("ToF Error: Failed to read sensor\n");
            }
        }
    }

    // Fallback to simulated sensor movement for testing
    static uint16_t sim_dist = 200;
    static int dir = 1;
    sim_dist += dir * 5;
    if (sim_dist > 350) dir = -1;
    if (sim_dist < 100) dir = 1;

    if (reading_count++ % 10 == 0) {
        printf("ToF Simulated: %d mm (sensor not connected)\n", sim_dist);
    }

    return sim_dist;
}

int get_sensor_position(void) {
    uint16_t dist = read_tof_sensor();
    int pos = ((dist - 50) * 15) / 350;
    if (pos < 0) pos = 0;
    if (pos > 15) pos = 15;

    if (tof_debug_mode) {
        static int debug_count = 0;
        if (debug_count++ % 5 == 0) {  // Print every 5th call
            printf("ToF Debug - Distance: %d mm -> Position: %d (0-15)\n", dist, pos);
        }
    }

    return pos;
}

void draw_rect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, bool filled) {
    if (filled) {
        for (int i = 0; i < h; i++) {
            for (int j = 0; j < w; j++) {
                set_pixel(x + j, y + i, r, g, b);
            }
        }
    } else {
        for (int i = 0; i < w; i++) {
            set_pixel(x + i, y, r, g, b);
            set_pixel(x + i, y + h - 1, r, g, b);
        }
        for (int i = 0; i < h; i++) {
            set_pixel(x, y + i, r, g, b);
            set_pixel(x + w - 1, y + i, r, g, b);
        }
    }
}

void print_game_legend(game_mode_t game) {
    printf("\n");
    printf("========================================\n");

    switch(game) {
        case PONG:
            printf("            🏓 PONG GAME 🏓            \n");
            printf("========================================\n");
            printf("OBJECTIVE: First to 5 points wins!\n");
            printf("\n");
            printf("CONTROLS:\n");
            printf("  • Move hand up/down (50-400mm range)\n");
            printf("  • Your paddle: BLUE (left side)\n");
            printf("  • AI paddle: RED (right side)\n");
            printf("\n");
            printf("DISPLAY:\n");
            printf("  • White ball\n");
            printf("  • Center line: Gray\n");
            printf("  • Scores shown as dots at top\n");
            printf("    - Your score: Blue dots (left)\n");
            printf("    - AI score: Red dots (right)\n");
            break;

        case FLAPPY:
            printf("          🐦 FLAPPY BIRD 🐦           \n");
            printf("========================================\n");
            printf("OBJECTIVE: Navigate through pipes!\n");
            printf("\n");
            printf("CONTROLS:\n");
            printf("  • Move hand UP quickly to jump\n");
            printf("  • Gravity pulls bird down\n");
            printf("  • Distance < 100mm = strong jump\n");
            printf("\n");
            printf("DISPLAY:\n");
            printf("  • Bird: YELLOW\n");
            printf("  • Pipes: GREEN\n");
            printf("  • Avoid hitting pipes or ground!\n");
            break;

        case CATCH:
            printf("           🧺 CATCH GAME 🧺           \n");
            printf("========================================\n");
            printf("OBJECTIVE: Catch good items, avoid bad!\n");
            printf("\n");
            printf("CONTROLS:\n");
            printf("  • Move hand left/right\n");
            printf("  • Position controls basket\n");
            printf("\n");
            printf("DISPLAY:\n");
            printf("  • Basket: BLUE (3 pixels wide)\n");
            printf("  • Good items: TEAL - CATCH THESE!\n");
            printf("  • Bad items: RED - AVOID THESE!\n");
            printf("  • Lives: Red dots at top (3 total)\n");
            printf("\n");
            printf("SCORING:\n");
            printf("  • Miss good item = -1 life\n");
            printf("  • Catch bad item = -1 life\n");
            break;

        case INVADERS:
            printf("        👾 SPACE INVADERS 👾          \n");
            printf("========================================\n");
            printf("OBJECTIVE: Destroy all invaders!\n");
            printf("\n");
            printf("CONTROLS:\n");
            printf("  • Move hand to position ship\n");
            printf("  • Quick movement (>3 pos) = SHOOT!\n");
            printf("\n");
            printf("DISPLAY:\n");
            printf("  • Your ship: CYAN (bottom)\n");
            printf("  • Invaders: GREEN\n");
            printf("  • Your bullets: YELLOW\n");
            printf("  • 20 invaders total (4 rows)\n");
            break;

        case MENU:
            printf("          🎮 GAME MENU 🎮            \n");
            printf("========================================\n");
            printf("SELECT A GAME:\n");
            printf("\n");
            printf("CONTROLS:\n");
            printf("  • Move hand to select quadrant:\n");
            printf("    - Top-left: PONG (Red)\n");
            printf("    - Top-right: FLAPPY (Yellow)\n");
            printf("    - Bottom-left: CATCH (Green)\n");
            printf("    - Bottom-right: INVADERS (Cyan)\n");
            printf("  • Wave hand quickly to SELECT\n");
            printf("\n");
            printf("SENSOR RANGE: 50-400mm\n");
            break;
    }

    printf("========================================\n");
    printf("\n");
}

void test_matrix_mapping(void) {
    printf("\n=== TESTING MATRIX MAPPING ===\n");
    printf("Drawing test pattern to diagnose zigzag issues\n\n");

    // Test 1: Light up first row only (y=0)
    printf("Test 1: First row (y=0) - Should be RED\n");
    printf("Indices for row 0: ");
    clear_display();
    for (int x = 0; x < 16; x++) {
        int idx = get_index(x, 0);
        printf("%d ", idx);
        set_pixel(x, 0, 255, 0, 0);  // Red
    }
    printf("\n");
    show_display();
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Test 2: Light up second row only (y=1)
    printf("Test 2: Second row (y=1) - Should be GREEN\n");
    printf("Indices for row 1: ");
    clear_display();
    for (int x = 0; x < 16; x++) {
        int idx = get_index(x, 1);
        printf("%d ", idx);
        set_pixel(x, 1, 0, 255, 0);  // Green
    }
    printf("\n");
    show_display();
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Test 3: Draw a vertical line at x=0
    printf("Test 3: Left edge (x=0) - Should be BLUE vertical line\n");
    clear_display();
    for (int y = 0; y < 16; y++) {
        set_pixel(0, y, 0, 0, 255);  // Blue
    }
    show_display();
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Test 4: Draw a vertical line at x=15
    printf("Test 4: Right edge (x=15) - Should be YELLOW vertical line\n");
    clear_display();
    for (int y = 0; y < 16; y++) {
        set_pixel(15, y, 255, 255, 0);  // Yellow
    }
    show_display();
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Test 5: Draw diagonal from top-left to bottom-right
    printf("Test 5: Diagonal TL to BR - Should be WHITE diagonal\n");
    clear_display();
    for (int i = 0; i < 16; i++) {
        set_pixel(i, i, 255, 255, 255);  // White
    }
    show_display();
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Test 6: Number pattern to see orientation
    printf("Test 6: Corner test pattern\n");
    clear_display();
    // Top-left corner (0,0) - RED
    set_pixel(0, 0, 255, 0, 0);
    set_pixel(1, 0, 255, 0, 0);
    set_pixel(0, 1, 255, 0, 0);

    // Top-right corner (15,0) - GREEN
    set_pixel(15, 0, 0, 255, 0);
    set_pixel(14, 0, 0, 255, 0);
    set_pixel(15, 1, 0, 255, 0);

    // Bottom-left corner (0,15) - BLUE
    set_pixel(0, 15, 0, 0, 255);
    set_pixel(1, 15, 0, 0, 255);
    set_pixel(0, 14, 0, 0, 255);

    // Bottom-right corner (15,15) - YELLOW
    set_pixel(15, 15, 255, 255, 0);
    set_pixel(14, 15, 255, 255, 0);
    set_pixel(15, 14, 255, 255, 0);

    show_display();
    printf("Corners: TL=RED, TR=GREEN, BL=BLUE, BR=YELLOW\n");
    vTaskDelay(pdMS_TO_TICKS(3000));

    printf("Matrix mapping test complete!\n");
    printf("Check if patterns appear correctly.\n");
    printf("If mirrored, we'll need to adjust the mapping.\n\n");
}

void draw_menu(void) {
    clear_display();

    // Game colors
    uint8_t colors[][3] = {
        {255, 0, 0},    // Red for Pong
        {255, 255, 0},  // Yellow for Flappy
        {0, 255, 0},    // Green for Catch
        {0, 255, 255}   // Cyan for Invaders
    };

    // Draw game icons
    for (int game = 0; game < 4; game++) {
        int x = (game % 2) * 8 + 2;
        int y = (game / 2) * 8 + 2;

        if (game == menu_selection && menu_selection != -1) {
            // Calculate brightness based on selection progress
            float progress = 0;
            if (selection_start_time > 0) {
                uint32_t elapsed = esp_timer_get_time() / 1000 - selection_start_time;
                progress = (float)elapsed / SELECTION_HOLD_TIME;
                if (progress > 1.0) progress = 1.0;
            }

            // Interpolate between dim and full brightness
            uint8_t r = (uint8_t)(colors[game][0] * (0.3 + 0.7 * progress));
            uint8_t g = (uint8_t)(colors[game][1] * (0.3 + 0.7 * progress));
            uint8_t b = (uint8_t)(colors[game][2] * (0.3 + 0.7 * progress));

            draw_rect(x, y, 4, 4, r, g, b, true);
        } else {
            draw_rect(x, y, 4, 4, 20, 20, 20, true);
        }
    }

    // Draw selection border (only if in valid range)
    if (menu_selection >= 0 && menu_selection < 4) {
        int x = (menu_selection % 2) * 8;
        int y = (menu_selection / 2) * 8;

        // Pulse the border brightness based on selection progress
        float progress = 0;
        if (selection_start_time > 0) {
            uint32_t elapsed = esp_timer_get_time() / 1000 - selection_start_time;
            progress = (float)elapsed / SELECTION_HOLD_TIME;
            if (progress > 1.0) progress = 1.0;
        }
        uint8_t brightness = (uint8_t)(100 + 155 * progress);
        draw_rect(x, y, 8, 8, brightness, brightness, brightness, false);
    }

    // Draw distance indicator (top row shows if in valid range)
    if (sensor_distance >= MIN_SELECTION_DISTANCE && sensor_distance <= MAX_SELECTION_DISTANCE) {
        // Green indicator for valid range
        set_pixel(0, 0, 0, 255, 0);
        set_pixel(15, 0, 0, 255, 0);
    } else {
        // Red indicator for out of range
        set_pixel(0, 0, 255, 0, 0);
        set_pixel(15, 0, 255, 0, 0);
    }

    show_display();
}

// Transition screen with animated text
void show_transition_screen(const char* text, uint8_t r, uint8_t g, uint8_t b, int duration_ms) {
    int frames = duration_ms / 50;  // 50ms per frame

    for (int frame = 0; frame < frames; frame++) {
        clear_display();

        // Calculate fade effect
        float progress = (float)frame / frames;
        float brightness = 1.0;

        // Fade in for first 30%, full brightness for middle 40%, fade out for last 30%
        if (progress < 0.3) {
            brightness = progress / 0.3;
        } else if (progress > 0.7) {
            brightness = (1.0 - progress) / 0.3;
        }

        // Simple text display - show game name with animation
        if (strcmp(text, "PONG") == 0) {
            // Draw P-O-N-G letters
            int spacing = 3;
            int start_x = 2;

            // Animate letters appearing one by one
            int visible_letters = (frame * 4) / (frames / 2);
            if (visible_letters > 4) visible_letters = 4;

            for (int i = 0; i < visible_letters; i++) {
                int x = start_x + i * spacing;
                uint8_t br = r * brightness;
                uint8_t bg = g * brightness;
                uint8_t bb = b * brightness;

                // Draw simple 2x3 letters
                switch(text[i]) {
                    case 'P':
                        set_pixel(x, 6, br, bg, bb);
                        set_pixel(x, 7, br, bg, bb);
                        set_pixel(x, 8, br, bg, bb);
                        set_pixel(x+1, 6, br, bg, bb);
                        set_pixel(x+1, 7, br, bg, bb);
                        break;
                    case 'O':
                        set_pixel(x, 6, br, bg, bb);
                        set_pixel(x, 7, br, bg, bb);
                        set_pixel(x, 8, br, bg, bb);
                        set_pixel(x+1, 6, br, bg, bb);
                        set_pixel(x+1, 8, br, bg, bb);
                        break;
                    case 'N':
                        set_pixel(x, 6, br, bg, bb);
                        set_pixel(x, 7, br, bg, bb);
                        set_pixel(x, 8, br, bg, bb);
                        set_pixel(x+1, 6, br, bg, bb);
                        set_pixel(x+1, 8, br, bg, bb);
                        break;
                    case 'G':
                        set_pixel(x, 6, br, bg, bb);
                        set_pixel(x, 7, br, bg, bb);
                        set_pixel(x, 8, br, bg, bb);
                        set_pixel(x+1, 6, br, bg, bb);
                        set_pixel(x+1, 8, br, bg, bb);
                        break;
                }
            }
        } else {
            // Generic transition - expanding circle
            float radius = 8.0 * progress;
            for (int y = 0; y < 16; y++) {
                for (int x = 0; x < 16; x++) {
                    float dist = sqrt((x - 7.5) * (x - 7.5) + (y - 7.5) * (y - 7.5));
                    if (dist <= radius && dist >= radius - 1.5) {
                        set_pixel(x, y, r * brightness, g * brightness, b * brightness);
                    }
                }
            }
        }

        show_display();
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }

    clear_display();
    show_display();
}

// Game over screen with score display
void show_game_over_screen(int score, int high_score) {
    // Animate "GAME OVER" text with score
    for (int frame = 0; frame < 40; frame++) {  // 2 seconds at 50ms per frame
        clear_display();

        // Flash effect
        float brightness = (frame % 10 < 5) ? 1.0 : 0.5;

        // Draw "GAME" on top
        draw_rect(3, 2, 10, 3, 255 * brightness, 0, 0, false);

        // Draw "OVER" below
        draw_rect(3, 6, 10, 3, 255 * brightness, 0, 0, false);

        // Show score at bottom (simple number display)
        if (score >= 0 && frame > 10) {
            // Draw score indicator
            for (int i = 0; i < score && i < 16; i++) {
                set_pixel(i, 14, 0, 255, 0);  // Green dots for score
            }

            // Flash high score indicator if new high score
            if (score > high_score && (frame % 6 < 3)) {
                draw_rect(0, 12, 16, 1, 255, 255, 0, true);  // Yellow line for new high score
            }
        }

        show_display();
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }

    // Final fade out
    for (int brightness = 255; brightness >= 0; brightness -= 15) {
        clear_display();
        float b = brightness / 255.0;
        draw_rect(3, 2, 10, 3, 255 * b, 0, 0, false);
        draw_rect(3, 6, 10, 3, 255 * b, 0, 0, false);
        show_display();
        vTaskDelay(30 / portTICK_PERIOD_MS);
    }

    clear_display();
    show_display();
}

// Pong game implementation
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
            pong.ball_vy += (pong.ball_y - (pong.player_y + 1)) * 0.2;
        } else {
            pong.ai_score++;
            pong.ball_x = 8;
            pong.ball_y = 8;
            pong.ball_vx = 1;
            pong.ball_vy = 0.5;
        }
    }

    if (pong.ball_x >= 14) {
        if (pong.ball_y >= pong.ai_y - 1 && pong.ball_y <= pong.ai_y + 2) {
            pong.ball_vx = -pong.ball_vx;
            pong.ball_vy += (pong.ball_y - (pong.ai_y + 1)) * 0.2;
        } else {
            pong.player_score++;
            pong.ball_x = 8;
            pong.ball_y = 8;
            pong.ball_vx = -1;
            pong.ball_vy = 0.5;
        }
    }

    if (pong.player_score >= 5 || pong.ai_score >= 5) {
        pong.game_over = true;
    }
}

void render_pong(void) {
    clear_display();

    // Draw paddles
    draw_rect(0, pong.player_y, 1, 3, 0, 0, 255, true);  // Blue player paddle
    draw_rect(15, pong.ai_y, 1, 3, 255, 0, 0, true);    // Red AI paddle

    // Draw ball
    set_pixel((int)pong.ball_x, (int)pong.ball_y, 255, 255, 255);

    // Draw center line
    for (int i = 0; i < 16; i += 2) {
        set_pixel(8, i, 40, 40, 40);
    }

    // Display scores as dots at the top
    for (int i = 0; i < pong.player_score && i < 5; i++) {
        set_pixel(3 + i, 0, 0, 0, 255);
    }
    for (int i = 0; i < pong.ai_score && i < 5; i++) {
        set_pixel(12 - i, 0, 255, 0, 0);
    }

    show_display();
}

void run_pong(void) {
    static bool initialized = false;
    static int high_score = 0;
    if (!initialized) {
        init_pong();
        print_game_legend(PONG);
        initialized = true;
    }

    update_pong();
    render_pong();

    if (pong.game_over) {
        // Determine winner and score
        int final_score = pong.player_score;
        if (final_score > high_score) {
            high_score = final_score;
        }
        show_game_over_screen(final_score, high_score);
        current_mode = MENU;
        initialized = false;
    }
}

// Flappy Bird implementation
void run_flappy(void) {
    static float bird_y = 8;
    static float bird_vy = 0;
    static int pipe_x = 16;
    static int pipe_gap_y = 8;
    static bool game_over = false;
    static bool initialized = false;

    if (!initialized) {
        print_game_legend(FLAPPY);
        initialized = true;
    }

    if (game_over) {
        static int score = 0;
        static int high_score = 0;
        if (score > high_score) high_score = score;
        show_game_over_screen(score, high_score);
        current_mode = MENU;
        bird_y = 8;
        bird_vy = 0;
        pipe_x = 16;
        game_over = false;
        score = 0;
        initialized = false;
        return;
    }

    // Update bird based on sensor
    int sensor_pos = get_sensor_position();
    if (sensor_pos < 5) {
        bird_vy = -1.5;
    }
    bird_vy += 0.15;
    bird_y += bird_vy;

    if (bird_y < 0) bird_y = 0;
    if (bird_y > 15) {
        game_over = true;
        return;
    }

    // Update pipe
    pipe_x--;
    if (pipe_x < -1) {
        pipe_x = 16;
        pipe_gap_y = rand() % 8 + 3;
    }

    // Check collision
    if (pipe_x == 4) {
        if (bird_y < pipe_gap_y || bird_y > pipe_gap_y + 3) {
            game_over = true;
            return;
        }
    }

    // Render
    clear_display();
    set_pixel(4, (int)bird_y, 255, 255, 0);  // Yellow bird

    if (pipe_x >= 0 && pipe_x < 16) {
        for (int y = 0; y < 16; y++) {
            if (y < pipe_gap_y || y > pipe_gap_y + 3) {
                set_pixel(pipe_x, y, 0, 255, 0);  // Green pipes
            }
        }
    }

    show_display();
}

// Catch game implementation
void run_catch(void) {
    static int basket_x = 7;
    static float item_y = 0;
    static int item_x = 8;
    static bool item_is_good = true;
    static int lives = 3;
    static int score = 0;
    static bool initialized = false;
    static int high_score = 0;

    if (!initialized) {
        print_game_legend(CATCH);
        initialized = true;
    }

    if (lives <= 0) {
        if (score > high_score) high_score = score;
        show_game_over_screen(score, high_score);
        current_mode = MENU;
        lives = 3;
        score = 0;
        initialized = false;
        return;
    }

    // Update basket
    basket_x = get_sensor_position();
    if (basket_x > 13) basket_x = 13;

    // Update falling item
    item_y += 0.3;
    if (item_y >= 14) {
        if (item_x >= basket_x && item_x < basket_x + 3) {
            // Caught!
            if (item_is_good) {
                score++;
            } else {
                lives--;
            }
        } else if (item_is_good) {
            lives--;
        }
        item_y = 0;
        item_x = rand() % 16;
        item_is_good = (rand() % 3) != 0;  // 2/3 chance of good item
    }

    // Render
    clear_display();
    draw_rect(basket_x, 14, 3, 2, 0, 0, 255, true);  // Blue basket

    if (item_is_good) {
        set_pixel(item_x, (int)item_y, 0, 255, 128);  // Teal for good items
    } else {
        set_pixel(item_x, (int)item_y, 255, 0, 0);    // Red for bad items
    }

    // Draw lives
    for (int i = 0; i < lives && i < 3; i++) {
        set_pixel(i, 0, 255, 0, 0);
    }

    show_display();
}

// Space Invaders implementation
void run_invaders(void) {
    static int player_x = 7;
    static int invaders[20];
    static int bullet_x = -1;
    static int bullet_y = -1;
    static int invader_y = 0;
    static bool initialized = false;
    static int score = 0;
    static int high_score = 0;

    if (!initialized) {
        for (int i = 0; i < 20; i++) {
            invaders[i] = 1;
        }
        score = 0;
        print_game_legend(INVADERS);
        initialized = true;
    }

    // Update player
    player_x = get_sensor_position();
    if (player_x > 14) player_x = 14;

    // Fire bullet (simplified - fires when player moves quickly)
    static int last_player_x = 7;
    if (bullet_y < 0 && abs(player_x - last_player_x) > 3) {
        bullet_x = player_x + 1;
        bullet_y = 13;
    }
    last_player_x = player_x;

    // Update bullet
    if (bullet_y >= 0) {
        bullet_y--;
        // Check collision with invaders
        for (int i = 0; i < 20; i++) {
            if (invaders[i]) {
                int inv_x = (i % 5) * 3 + 1;
                int inv_y = (i / 5) * 2 + 1 + invader_y;
                if (bullet_x >= inv_x && bullet_x < inv_x + 2 &&
                    bullet_y >= inv_y && bullet_y < inv_y + 2) {
                    invaders[i] = 0;
                    bullet_y = -1;
                    score++;  // Increment score for each invader destroyed
                    break;
                }
            }
        }
    }

    // Check if all invaders destroyed
    bool any_alive = false;
    for (int i = 0; i < 20; i++) {
        if (invaders[i]) {
            any_alive = true;
            break;
        }
    }
    if (!any_alive) {
        // Victory! All invaders destroyed
        if (score > high_score) high_score = score;
        show_game_over_screen(score, high_score);
        current_mode = MENU;
        initialized = false;
        return;
    }

    // Render
    clear_display();
    draw_rect(player_x, 14, 2, 2, 0, 255, 255, true);  // Cyan player

    // Draw bullet
    if (bullet_y >= 0) {
        set_pixel(bullet_x, bullet_y, 255, 255, 0);  // Yellow bullet
    }

    // Draw invaders
    for (int i = 0; i < 20; i++) {
        if (invaders[i]) {
            int x = (i % 5) * 3 + 1;
            int y = (i / 5) * 2 + 1 + invader_y;
            if (y < 14) {
                draw_rect(x, y, 2, 1, 0, 255, 0, true);  // Green invaders
            }
        }
    }

    show_display();
}

void game_task(void *pvParameters) {
    ESP_LOGI(TAG, "Game task started");

    static game_mode_t last_mode = (game_mode_t)-1;

    while (1) {
        sensor_distance = read_tof_sensor();

        // Print legend when mode changes
        if (current_mode != last_mode) {
            print_game_legend(current_mode);
            last_mode = current_mode;
        }

        switch (current_mode) {
            case MENU: {
                // Check if hand is in valid range
                if (sensor_distance >= MIN_SELECTION_DISTANCE && sensor_distance <= MAX_SELECTION_DISTANCE) {
                    // Calculate selection based on position
                    int new_selection = get_sensor_position() / 4;
                    if (new_selection > 3) new_selection = 3;

                    // Check if selection changed
                    if (new_selection != last_stable_selection) {
                        // Selection changed, reset timer
                        last_stable_selection = new_selection;
                        selection_start_time = esp_timer_get_time() / 1000;
                        menu_selection = new_selection;

                        if (tof_debug_mode) {
                            ESP_LOGI(TAG, "Menu selection started: %d (hold for 5s to confirm)", menu_selection);
                        }
                    } else {
                        // Selection stable, check if held long enough
                        uint32_t current_time = esp_timer_get_time() / 1000;
                        uint32_t elapsed = current_time - selection_start_time;

                        if (elapsed >= SELECTION_HOLD_TIME) {
                            // Selection confirmed!
                            ESP_LOGI(TAG, "Selection confirmed after 5 seconds!");

                            // Show transition screen based on selected game
                            const char* game_name = "";
                            uint8_t r = 255, g = 255, b = 255;

                            switch (menu_selection) {
                                case 0:
                                    game_name = "PONG";
                                    r = 255; g = 0; b = 0;  // Red
                                    current_mode = PONG;
                                    break;
                                case 1:
                                    game_name = "FLAPPY";
                                    r = 255; g = 255; b = 0;  // Yellow
                                    current_mode = FLAPPY;
                                    break;
                                case 2:
                                    game_name = "CATCH";
                                    r = 0; g = 255; b = 0;  // Green
                                    current_mode = CATCH;
                                    break;
                                case 3:
                                    game_name = "INVADERS";
                                    r = 0; g = 255; b = 255;  // Cyan
                                    current_mode = INVADERS;
                                    break;
                            }

                            // Show transition animation
                            show_transition_screen(game_name, r, g, b, 1500);

                            ESP_LOGI(TAG, "Starting game: %d", current_mode);

                            // Reset for next time
                            menu_selection = -1;
                            last_stable_selection = -1;
                            selection_start_time = 0;
                        } else if (elapsed % 1000 < 50) {  // Log progress every second
                            if (tof_debug_mode) {
                                ESP_LOGI(TAG, "Hold progress: %.1f seconds", elapsed / 1000.0);
                            }
                        }
                    }
                } else {
                    // Hand out of range - reset selection
                    if (menu_selection != -1) {
                        if (tof_debug_mode) {
                            ESP_LOGI(TAG, "Hand out of range (%dmm) - selection cancelled", sensor_distance);
                        }
                    }
                    menu_selection = -1;
                    last_stable_selection = -1;
                    selection_start_time = 0;
                }

                draw_menu();
                break;
            }

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

        vTaskDelay(50 / portTICK_PERIOD_MS);  // 20 FPS
    }
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "LED Matrix Game System Starting...");

    // Print welcome message
    printf("\n\n");
    printf("╔══════════════════════════════════════╗\n");
    printf("║   16x16 LED MATRIX GAMING SYSTEM    ║\n");
    printf("║         ESP32-C3 + WS2812B          ║\n");
    printf("║     VL53L0X Gesture Control         ║\n");
    printf("╚══════════════════════════════════════╝\n");
    printf("\n");
    printf("System initializing...\n");

    init_hardware();

    // Clear display initially
    clear_display();
    show_display();

    // Uncomment to test matrix mapping
    test_matrix_mapping();

    printf("Starting game system...\n");
    printf("Monitor @ 115200 baud for game info\n");
    printf("\n");
    printf("ToF Sensor Debug: %s\n", tof_debug_mode ? "ENABLED" : "DISABLED");
    printf("(Set tof_debug_mode = true in code for detailed output)\n");
    printf("\n");

    xTaskCreate(game_task, "game_task", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "System ready! Entering menu...");
}