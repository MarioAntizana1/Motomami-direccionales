#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/rmt_tx.h"
#include "led_strip_encoder.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "mqtt_client.h"

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

#define WIFI_SSID      "Mario-wifi"
#define WIFI_PASS      "572Huanuco321"
#define MQTT_BROKER    "mqtt://192.168.31.173"

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

static esp_mqtt_client_handle_t mqtt_client;
static bool wifi_connected = false;

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

static void mqtt_event_handler(void *args, esp_event_base_t base, int32_t event_id, void *data)
{
    esp_mqtt_event_t *event = data;
    esp_mqtt_client_handle_t client = event->client;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected");
        esp_mqtt_client_subscribe(client, "motomami/intermitente_izquierda", 1);
        esp_mqtt_client_subscribe(client, "motomami/intermitente_derecha", 1);
        esp_mqtt_client_subscribe(client, "motomami/intermitente_emergencia", 1);
        esp_mqtt_client_subscribe(client, "motomami/frenado", 1);
        esp_mqtt_client_subscribe(client, "motomami/luz_nocturna", 1);
        break;

    case MQTT_EVENT_DATA: {
        char topic[64];
        char payload[16];
        int tlen = event->topic_len;
        int plen = event->data_len;
        if (tlen >= sizeof(topic)) tlen = sizeof(topic) - 1;
        if (plen >= sizeof(payload)) plen = sizeof(payload) - 1;
        memcpy(topic, event->topic, tlen);
        topic[tlen] = 0;
        memcpy(payload, event->data, plen);
        payload[plen] = 0;

        bool on = (strcasecmp(payload, "ON") == 0);

        if (strcmp(topic, "motomami/intermitente_izquierda") == 0)
            intermitente_izquierda(on);
        else if (strcmp(topic, "motomami/intermitente_derecha") == 0)
            intermitente_derecha(on);
        else if (strcmp(topic, "motomami/intermitente_emergencia") == 0)
            intermitente_emergencia(on);
        else if (strcmp(topic, "motomami/frenado") == 0)
            frenado(on);
        else if (strcmp(topic, "motomami/luz_nocturna") == 0)
            luz_nocturna(on);
        break;
    }

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error");
        break;

    default:
        break;
    }
}

static void start_mqtt(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER,
    };
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_connected = false;
        esp_wifi_connect();
        ESP_LOGI(TAG, "WiFi reconnect...");
    } else if (id == IP_EVENT_STA_GOT_IP) {
        wifi_connected = true;
        ESP_LOGI(TAG, "WiFi connected, starting MQTT...");
        start_mqtt();
    }
}

static void wifi_init(void)
{
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_t instance_any;
    esp_event_handler_instance_t instance_got_ip;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();

    ESP_LOGI(TAG, "WiFi connecting to %s...", WIFI_SSID);
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
            for (int c = 9; c <= 18; c++) {
                float phase = (float)(c - 9) / (MID_COLS - 1) * 2.0f * 3.14159f;
                float wave = (sinf(phase + frame * 0.15f) + 1.0f) / 2.0f;
                uint8_t v = (uint8_t)(wave * 50.0f);
                for (int r = 0; r < LED_ROWS; r++)
                    set_pixel(r, c, v, 0, 0);
            }
        }

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
    ESP_ERROR_CHECK(nvs_flash_init());

    wifi_init();

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

    xTaskCreate(render_task, "render", 4096, NULL, 5, NULL);
}
