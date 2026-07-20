#include <string.h>
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

static const char *TAG = "neo";

static uint8_t led_strip_pixels[LED_NUM * 3];

static uint32_t get_led_index(uint32_t row, uint32_t col)
{
    if (row % 2 == 0)
        return row * LED_COLS + (LED_COLS - 1 - col);
    else
        return row * LED_COLS + col;
}

void app_main(void)
{
    ESP_LOGI(TAG, "Create RMT TX channel");
    rmt_channel_handle_t led_chan = NULL;
    rmt_tx_channel_config_t tx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = LED_GPIO_PIN,
        .mem_block_symbols = 64,
        .resolution_hz = RMT_LED_STRIP_RESOLUTION_HZ,
        .trans_queue_depth = 4,
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &led_chan));

    ESP_LOGI(TAG, "Install led strip encoder");
    rmt_encoder_handle_t led_encoder = NULL;
    led_strip_encoder_config_t encoder_config = {
        .resolution = RMT_LED_STRIP_RESOLUTION_HZ,
    };
    ESP_ERROR_CHECK(rmt_new_led_strip_encoder(&encoder_config, &led_encoder));

    ESP_LOGI(TAG, "Enable RMT TX channel");
    ESP_ERROR_CHECK(rmt_enable(led_chan));

    ESP_LOGI(TAG, "Encendiendo todos los LEDs...");

    for (int row = 0; row < LED_ROWS; row++) {
        for (int col = 0; col < LED_COLS; col++) {
            uint32_t idx = get_led_index(row, col);
            led_strip_pixels[idx * 3 + 0] = 20; // G
            led_strip_pixels[idx * 3 + 1] = 20; // B
            led_strip_pixels[idx * 3 + 2] = 20; // R
        }
    }

    rmt_transmit_config_t tx_config = {
        .loop_count = 0,
    };
    ESP_ERROR_CHECK(rmt_transmit(led_chan, led_encoder, led_strip_pixels, sizeof(led_strip_pixels), &tx_config));
    ESP_ERROR_CHECK(rmt_tx_wait_all_done(led_chan, portMAX_DELAY));

    ESP_LOGI(TAG, "LEDs encendidos por 10 segundos...");
    vTaskDelay(pdMS_TO_TICKS(10000));

    memset(led_strip_pixels, 0, sizeof(led_strip_pixels));
    ESP_ERROR_CHECK(rmt_transmit(led_chan, led_encoder, led_strip_pixels, sizeof(led_strip_pixels), &tx_config));
    ESP_ERROR_CHECK(rmt_tx_wait_all_done(led_chan, portMAX_DELAY));

    ESP_LOGI(TAG, "Apagados");
}
