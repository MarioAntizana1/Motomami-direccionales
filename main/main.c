#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/rmt_tx.h"
#include "led_strip_encoder.h"

#define LED_GPIO_PIN                2
#define LED_NUM                     140
#define LED_ROWS                    5
#define LED_COLS                    28
#define RMT_LED_STRIP_RESOLUTION_HZ 10000000

#define LEFT_COLS      9
#define MID_COLS       10
#define RIGHT_COLS     9
#define BLINK_MS       300
#define RENDER_MS      50

static const char *TAG = "neo";

static uint8_t led_strip_pixels[LED_NUM * 3];
static rmt_channel_handle_t led_chan;
static rmt_encoder_handle_t led_encoder;

static bool left_active = false;
static bool right_active = false;
static bool hazard_active = false;
static bool brake_active = false;
static bool night_active = false;
static bool blink_on = true;
static uint32_t frame = 0;

static uint32_t get_led_index(uint32_t row, uint32_t col)
{
    if (row % 2 == 0)
        return row * LED_COLS + (LED_COLS - 1 - col);
    else
        return row * LED_COLS + col;
}

static void set_pixel(uint32_t row, uint32_t col, uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t idx = get_led_index(row, col);
    led_strip_pixels[idx * 3 + 0] = g;
    led_strip_pixels[idx * 3 + 1] = b;
    led_strip_pixels[idx * 3 + 2] = r;
}

void intermitente_izquierda(bool activar)
{
    left_active = activar;
    ESP_LOGI(TAG, "intermitente_izquierda: %s", activar ? "ON" : "OFF");
}

void intermitente_derecha(bool activar)
{
    right_active = activar;
    ESP_LOGI(TAG, "intermitente_derecha: %s", activar ? "ON" : "OFF");
}

void intermitente_emergencia(bool activar)
{
    hazard_active = activar;
    ESP_LOGI(TAG, "intermitente_emergencia: %s", activar ? "ON" : "OFF");
}

void frenado(bool activar)
{
    brake_active = activar;
    ESP_LOGI(TAG, "frenado: %s", activar ? "ON" : "OFF");
}

void luz_nocturna(bool activar)
{
    night_active = activar;
    ESP_LOGI(TAG, "luz_nocturna: %s", activar ? "ON" : "OFF");
}

static void render_task(void *arg)
{
    TickType_t last_blink = xTaskGetTickCount();

    while (1) {
        TickType_t now = xTaskGetTickCount();
        if (now - last_blink >= pdMS_TO_TICKS(BLINK_MS)) {
            blink_on = !blink_on;
            last_blink = now;
        }

        bool any_dir = left_active || right_active || hazard_active;
        bool l_on = (left_active || hazard_active) && blink_on;
        bool r_on = (right_active || hazard_active) && blink_on;

        memset(led_strip_pixels, 0, sizeof(led_strip_pixels));

        // Brake
        if (brake_active) {
            uint8_t r_val = 255;
            if (any_dir) {
                for (int r = 0; r < LED_ROWS; r++)
                    for (int c = 9; c <= 18; c++)
                        set_pixel(r, c, r_val, 0, 0);
            } else {
                for (int r = 0; r < LED_ROWS; r++)
                    for (int c = 0; c < LED_COLS; c++)
                        set_pixel(r, c, r_val, 0, 0);
            }
        }

        // Night light
        if (night_active) {
            uint8_t dim = 102;
            for (int r = 0; r < LED_ROWS; r += 4) {
                for (int c = 0; c < LED_COLS; c++) {
                    if (any_dir) {
                        bool in_left_blink = (c < LEFT_COLS) && l_on;
                        bool in_right_blink = (c >= LED_COLS - RIGHT_COLS) && r_on;
                        if (in_left_blink || in_right_blink)
                            continue;
                    }
                    set_pixel(r, c, dim, 0, 0);
                }
            }
            // Sinusoidal animation on middle columns
            for (int c = 9; c <= 18; c++) {
                float phase = (float)(c - 9) / (MID_COLS - 1) * 2.0f * 3.14159f;
                float wave = (sinf(phase + frame * 0.15f) + 1.0f) / 2.0f;
                uint8_t v = (uint8_t)(wave * 50.0f);
                for (int r = 0; r < LED_ROWS; r++)
                    set_pixel(r, c, v, 0, 0);
            }
        }

        // Directionals
        if (l_on) {
            for (int r = 0; r < LED_ROWS; r++)
                for (int c = 0; c < LEFT_COLS; c++)
                    set_pixel(r, c, 255, 80, 0);
        }
        if (r_on) {
            for (int r = 0; r < LED_ROWS; r++)
                for (int c = LED_COLS - RIGHT_COLS; c < LED_COLS; c++)
                    set_pixel(r, c, 255, 80, 0);
        }

        rmt_transmit_config_t tx = { .loop_count = 0 };
        rmt_transmit(led_chan, led_encoder, led_strip_pixels, sizeof(led_strip_pixels), &tx);
        rmt_tx_wait_all_done(led_chan, portMAX_DELAY);

        frame++;
        vTaskDelay(pdMS_TO_TICKS(RENDER_MS));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Create RMT TX channel");
    rmt_tx_channel_config_t tx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = LED_GPIO_PIN,
        .mem_block_symbols = 64,
        .resolution_hz = RMT_LED_STRIP_RESOLUTION_HZ,
        .trans_queue_depth = 4,
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &led_chan));

    ESP_LOGI(TAG, "Install led strip encoder");
    led_strip_encoder_config_t encoder_config = {
        .resolution = RMT_LED_STRIP_RESOLUTION_HZ,
    };
    ESP_ERROR_CHECK(rmt_new_led_strip_encoder(&encoder_config, &led_encoder));

    ESP_LOGI(TAG, "Enable RMT TX channel");
    ESP_ERROR_CHECK(rmt_enable(led_chan));

    ESP_LOGI(TAG, "Sistema iniciado. Funciones disponibles:");
    ESP_LOGI(TAG, "  intermitente_izquierda(bool)");
    ESP_LOGI(TAG, "  intermitente_derecha(bool)");
    ESP_LOGI(TAG, "  intermitente_emergencia(bool)");
    ESP_LOGI(TAG, "  frenado(bool)");
    ESP_LOGI(TAG, "  luz_nocturna(bool)");

    xTaskCreate(render_task, "render", 4096, NULL, 5, NULL);
}
