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
#define BLINK_OFF_MS   2580
#define RENDER_MS      30

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
static uint8_t night_intensity = 100;
static uint8_t main_intensity = 100;
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
    led_strip_pixels[idx * 3 + 0] = r;
    led_strip_pixels[idx * 3 + 1] = g;
    led_strip_pixels[idx * 3 + 2] = b;
}

static void hsv_to_rgb(uint8_t h, uint8_t s, uint8_t v, uint8_t *r, uint8_t *g, uint8_t *b)
{
    uint8_t region, remainder, p, q, t;
    if (s == 0) { *r = v; *g = v; *b = v; return; }
    region = h / 43;
    remainder = (h - region * 43) * 6;
    p = (v * (255 - s)) >> 8;
    q = (v * (255 - ((s * remainder) >> 8))) >> 8;
    t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8;
    switch (region) {
        case 0: *r = v; *g = t; *b = p; break;
        case 1: *r = q; *g = v; *b = p; break;
        case 2: *r = p; *g = v; *b = t; break;
        case 3: *r = p; *g = q; *b = v; break;
        case 4: *r = t; *g = p; *b = v; break;
        default: *r = v; *g = p; *b = q; break;
    }
}

static void set_pixel_hsv(uint32_t row, uint32_t col, uint8_t h, uint8_t s, uint8_t v)
{
    uint8_t r, g, b;
    hsv_to_rgb(h, s, v, &r, &g, &b);
    set_pixel(row, col, r, g, b);
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
        esp_mqtt_client_subscribe(client, "motomami/luz_nocturna/intensidad", 1);
        esp_mqtt_client_subscribe(client, "motomami/intensidad", 1);
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
        else if (strcmp(topic, "motomami/luz_nocturna/intensidad") == 0) {
            int val = atoi(payload);
            if (val >= 0 && val <= 100) night_intensity = (uint8_t)val;
            ESP_LOGI(TAG, "night_intensity: %d%%", night_intensity);
        } else if (strcmp(topic, "motomami/intensidad") == 0) {
            int val = atoi(payload);
            if (val >= 0 && val <= 100) main_intensity = (uint8_t)val;
            ESP_LOGI(TAG, "main_intensity: %d%%", main_intensity);
        }
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

static void draw_directional_side(bool active, int ncols, int offset, uint32_t f, bool reverse)
{
    if (!active) return;

    uint32_t total_frames = 14;
    uint32_t off_frames = BLINK_OFF_MS / RENDER_MS;
    uint32_t phase = f % (total_frames + off_frames);

    if (phase >= total_frames) return;

    uint8_t cr = (255 * main_intensity) / 100;
    uint8_t cg = (200 * main_intensity) / 100;
    uint8_t cb = 0;
    int lit;

    if (phase < 9) {
        lit = 9 - (int)phase;
    } else {
        static const int grow[] = {1, 2, 4, 6, 9};
        lit = grow[phase - 9];
    }

    int rows_shrink[3] = {1, 2, 3};
    int n_shrink = 3;

    if (phase < 9) {
        for (int i = 0; i < n_shrink; i++) {
            for (int c = 0; c < lit; c++) {
                int col = reverse ? (offset + ncols - 1 - c) : (offset + c);
                set_pixel(rows_shrink[i], col, cr, cg, cb);
            }
        }
    } else {
        for (int c = 0; c < lit; c++) {
            int col = reverse ? (offset + ncols - 1 - c) : (offset + c);
            set_pixel(2, col, cr, cg, cb);
        }
    }
}

static void draw_night_light(uint32_t f)
{
    uint32_t period = 60;
    uint32_t bp = f % period;
    uint32_t half = period / 2;
    uint8_t v;
    if (bp < half)
        v = 5 + (bp * 71) / half;
    else
        v = 5 + ((period - 1 - bp) * 71) / half;

    v = (v * night_intensity) / 100;

    uint8_t h = (uint8_t)(f * 2);

    for (int c = 0; c < LED_COLS; c++) {
        set_pixel_hsv(0, c, h, 200, v);
        set_pixel_hsv(2, c, h, 200, v);
        set_pixel_hsv(4, c, (uint8_t)(h + 40), 200, v);
    }
}

static void render_task(void *arg)
{
    while (1) {
        bool any_dir = left_active || right_active || hazard_active;

        memset(led_strip_pixels, 0, sizeof(led_strip_pixels));

        if (brake_active) {
            uint8_t r_val = (255 * main_intensity) / 100;
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

        if (night_active && !brake_active) {
            draw_night_light(frame);
        }

        if (any_dir) {
            draw_directional_side(left_active || hazard_active, LEFT_COLS, 0, frame, false);
            draw_directional_side(right_active || hazard_active, RIGHT_COLS, LED_COLS - RIGHT_COLS, frame, true);
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
