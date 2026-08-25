#include <string.h>
#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/rmt_tx.h"
#include "driver/gpio.h"
#include "led_strip_encoder.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "mqtt_client.h"
#include "esp_now.h"
#include "ota_server.h"

/* ================================================================
   PINEO Y MATRIZ
   ================================================================ */
#define LED_GPIO_PIN                2
#define LED_NUM                     140
#define LED_ROWS                    5
#define LED_COLS                    28
#define RMT_LED_STRIP_RESOLUTION_HZ 10000000

/* ================================================================
   ZONAS DE LA MATRIZ (columnas)
   ================================================================ */
#define LEFT_COLS      9
#define MID_COLS       10
#define RIGHT_COLS     9

/* ================================================================
   COLORES PREDETERMINADOS (GRB)
   ================================================================ */
#define INTERMITENTE_R   255
#define INTERMITENTE_G   200
#define INTERMITENTE_B   0

#define BRAKE_R          255
#define BRAKE_G          0
#define BRAKE_B          0

/* ================================================================
   ANIMACION DE INTERMITENTES
   ================================================================ */
#define INTERMITENTE_FRAMES  14
#define INTERMITENTE_SHRINK  9

/* Crecimiento no lineal: 1 → 2 → 4 → 6 → 9 columnas */
static const int GROW_COLS[] = {1, 2, 4, 6, 9};

#define INTERMITENTE_ROWS_SHRINK  {1, 2, 3}
#define INTERMITENTE_ROW_GROW     2

/* ================================================================
   ANIMACION DE LUZ NOCTURNA
   ================================================================ */
#define NIGHT_PULSE_FRAMES    30
#define NIGHT_SATURATION      200
#define NIGHT_MIN_BRIGHT      5
#define NIGHT_MAX_BRIGHT      76
#define NIGHT_HUE_MIN         140
#define NIGHT_HUE_MAX         210
#define NIGHT_HUE_OFFSET_ROW  20

/* ================================================================
   TIEMPOS
   ================================================================ */
#define RENDER_MS      71

/* ================================================================
   WIFI / MQTT
   ================================================================ */
#define WIFI_SSID      "Motomami-net"
#define WIFI_PASS      "ktiarts123+++++/"
#define MQTT_BROKER    "mqtt://192.168.42.1"

#define WIFI_RETRY_US      (1000 * 1000)   /* backoff 1 s al reconectar WiFi */
#define MQTT_RECONNECT_MS  1000            /* reconexion MQTT rapida */

#define ESPNOW_CHANNEL 6

/* ================================================================
   ESTADO GLOBAL
   ================================================================ */
static const char *TAG = "neo";

static uint8_t led_strip_pixels[LED_NUM * 3];
static rmt_channel_handle_t led_chan;
static rmt_encoder_handle_t led_encoder;

static bool left_active   = false;
static bool right_active  = false;
static bool hazard_active = false;
static bool brake_active  = false;
static bool night_active  = false;

static uint8_t night_intensity = 100;
static uint8_t main_intensity  = 100;

static uint32_t frame = 0;

static volatile uint32_t espnow_last_id = 0;

#define RMT_TX_TIMEOUT_MS 500
#define WDT_TIMEOUT_US    (5 * 1000 * 1000)
static volatile int64_t wdt_last_kick_us = 0;

static esp_mqtt_client_handle_t mqtt_client;
static esp_netif_t *netif = NULL;
static esp_timer_handle_t wifi_retry_timer = NULL;

/* Contador de mensajes publicados (parametro de control "ID") */
static uint32_t msg_id = 0;

/* ================================================================
   PIXELES — funciones de bajo nivel
   ================================================================ */
static uint32_t index_of(uint32_t row, uint32_t col)
{
    if (row % 2 == 0)
        return row * LED_COLS + (LED_COLS - 1 - col);
    else
        return row * LED_COLS + col;
}

static void set_pixel(uint32_t row, uint32_t col, uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t idx = index_of(row, col);
    led_strip_pixels[idx * 3 + 0] = g;
    led_strip_pixels[idx * 3 + 1] = r;
    led_strip_pixels[idx * 3 + 2] = b;
}

static void clear_leds(void)
{
    memset(led_strip_pixels, 0, sizeof(led_strip_pixels));
}

static void send_leds(void)
{
    rmt_transmit_config_t tx = { .loop_count = 0 };
    rmt_transmit(led_chan, led_encoder, led_strip_pixels, sizeof(led_strip_pixels), &tx);
    if (rmt_tx_wait_all_done(led_chan, pdMS_TO_TICKS(RMT_TX_TIMEOUT_MS)) != ESP_OK) {
        ESP_LOGE(TAG, "RMT sin respuesta, reiniciando");
        esp_restart();
    }
}

/* ================================================================
   HSV → RGB
   ================================================================ */
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

/* ================================================================
   INTERMITENTES — animación de un lado
   ================================================================ */
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

static int intermitente_columnas_encendidas(uint32_t phase)
{
    if (phase < INTERMITENTE_SHRINK)
        return INTERMITENTE_SHRINK - (int)phase;
    else
        return GROW_COLS[phase - INTERMITENTE_SHRINK];
}

static void draw_intermitente_side(bool active, int ncols, int offset, uint32_t f, bool reverse)
{
    if (!active) return;

    uint32_t phase = f % INTERMITENTE_FRAMES;
    int lit = intermitente_columnas_encendidas(phase);

    uint8_t cr = (INTERMITENTE_R * main_intensity) / 100;
    uint8_t cg = (INTERMITENTE_G * main_intensity) / 100;
    uint8_t cb = (INTERMITENTE_B * main_intensity) / 100;

    if (phase < INTERMITENTE_SHRINK) {
        int rows[] = INTERMITENTE_ROWS_SHRINK;
        for (int i = 0; i < 3; i++)
            for (int c = 0; c < lit; c++)
                set_pixel(rows[i], reverse ? (offset + ncols - 1 - c) : (offset + c), cr, cg, cb);
    } else {
        for (int c = 0; c < lit; c++)
            set_pixel(INTERMITENTE_ROW_GROW, reverse ? (offset + ncols - 1 - c) : (offset + c), cr, cg, cb);
    }
}

static void draw_intermitentes(void)
{
    bool any = left_active || right_active || hazard_active;
    if (!any) return;

    draw_intermitente_side(left_active || hazard_active, LEFT_COLS, 0, frame, false);
    draw_intermitente_side(right_active || hazard_active, RIGHT_COLS, LED_COLS - RIGHT_COLS, frame, true);
}

/* ================================================================
   FRENADO
   ================================================================ */
void frenado(bool activar)
{
    brake_active = activar;
    ESP_LOGI(TAG, "frenado: %s", activar ? "ON" : "OFF");
}

static void draw_brake(void)
{
    uint8_t r_val = (BRAKE_R * main_intensity) / 100;
    uint8_t g_val = (BRAKE_G * main_intensity) / 100;
    uint8_t b_val = (BRAKE_B * main_intensity) / 100;

    bool any_dir = left_active || right_active || hazard_active;

    if (any_dir) {
        for (int r = 0; r < LED_ROWS; r++)
            for (int c = 9; c <= 18; c++)
                set_pixel(r, c, r_val, g_val, b_val);
    } else {
        for (int r = 0; r < LED_ROWS; r++)
            for (int c = 0; c < LED_COLS; c++)
                set_pixel(r, c, r_val, g_val, b_val);
    }
}

/* ================================================================
   LUZ NOCTURNA
   ================================================================ */
void luz_nocturna(bool activar)
{
    night_active = activar;
    ESP_LOGI(TAG, "luz_nocturna: %s", activar ? "ON" : "OFF");
}

static void espnow_recv_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    (void)info;
    if (len < 7 || len >= 24) return;              /* "id:LLLLL" */
    char buf[24];
    memcpy(buf, data, len);
    buf[len] = 0;

    char *colon = strchr(buf, ':');
    if (!colon) return;
    *colon = 0;
    uint32_t id = strtoul(buf, NULL, 10);
    int32_t diff = (int32_t)(id - espnow_last_id);
    if (diff <= 0 && diff > -10000) return;        /* duplicado/atrasado; -10000 tolera reboot del emisor */

    const char *s = colon + 1;
    if (strlen(s) < 5) return;

    espnow_last_id = id;
    left_active   = (s[0] == '0');
    right_active  = (s[1] == '0');
    hazard_active = (s[2] == '0');
    brake_active  = (s[3] == '0');
    night_active  = (s[4] == '0');
    ESP_LOGI(TAG, "ESP-NOW %lu:%c%c%c%c%c", id, s[0], s[1], s[2], s[3], s[4]);
}

static void wdt_timer_cb(void *arg)
{
    (void)arg;
    if ((esp_timer_get_time() - wdt_last_kick_us) > WDT_TIMEOUT_US) {
        ESP_LOGE(TAG, "WDT: render loop colgado, reiniciando");
        esp_restart();
    }
}

static void draw_night_light(void)
{
    uint32_t bp = frame % NIGHT_PULSE_FRAMES;
    uint32_t half = NIGHT_PULSE_FRAMES / 2;

    uint8_t v;
    if (bp < half)
        v = NIGHT_MIN_BRIGHT + (bp * (NIGHT_MAX_BRIGHT - NIGHT_MIN_BRIGHT)) / half;
    else
        v = NIGHT_MIN_BRIGHT + ((NIGHT_PULSE_FRAMES - 1 - bp) * (NIGHT_MAX_BRIGHT - NIGHT_MIN_BRIGHT)) / half;

    v = (v * night_intensity) / 100;

    uint32_t hrange = NIGHT_HUE_MAX - NIGHT_HUE_MIN;
    uint32_t hp = frame % (hrange * 2);
    uint8_t hue;
    if (hp < hrange)
        hue = NIGHT_HUE_MIN + hp;
    else
        hue = NIGHT_HUE_MIN + (hrange * 2 - hp);

    for (int c = 0; c < LED_COLS; c++) {
        set_pixel_hsv(0, c, hue, NIGHT_SATURATION, v);
        set_pixel_hsv(2, c, hue, NIGHT_SATURATION, v);
        set_pixel_hsv(4, c, (uint8_t)(hue + NIGHT_HUE_OFFSET_ROW), NIGHT_SATURATION, v);
    }
}

/* ================================================================
   RENDER LOOP
   ================================================================ */
static void render_task(void *arg)
{
    while (1) {
        wdt_last_kick_us = esp_timer_get_time();

        if (frame % 300 == 0 && mqtt_client) {
            wifi_ap_record_t ap_info;
            if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                char rssi_str[8];
                sprintf(rssi_str, "%d", ap_info.rssi);
                esp_mqtt_client_publish(mqtt_client, "motomami/status/rssi", rssi_str, 0, 1, true);
                char id_str[12];
                sprintf(id_str, "%lu", ++msg_id);
                esp_mqtt_client_publish(mqtt_client, "motomami/status/id", id_str, 0, 1, true);
            }
        }

        clear_leds();

        if (brake_active)
            draw_brake();

        if (night_active && !brake_active)
            draw_night_light();

        draw_intermitentes();

        send_leds();
        frame++;
        vTaskDelay(pdMS_TO_TICKS(RENDER_MS));
    }
}

/* ================================================================
   MQTT
   ================================================================ */
static void mqtt_event_handler(void *args, esp_event_base_t base, int32_t event_id, void *data)
{
    esp_mqtt_event_t *event = data;
    esp_mqtt_client_handle_t client = event->client;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected");
        esp_mqtt_client_subscribe(client, "motomami/luz_nocturna/intensidad", 1);
        esp_mqtt_client_subscribe(client, "motomami/intensidad", 1);

        esp_mqtt_client_publish(client, "motomami/status", "online", 6, 1, true);

        {
            esp_netif_ip_info_t ip_info;
            if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
                char ip_str[16];
                sprintf(ip_str, IPSTR, IP2STR(&ip_info.ip));
                esp_mqtt_client_publish(client, "motomami/status/ip", ip_str, 0, 1, true);
                ESP_LOGI(TAG, "IP: %s", ip_str);
            }
        }

        {
            wifi_ap_record_t ap_info;
            if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                char rssi_str[8];
                sprintf(rssi_str, "%d", ap_info.rssi);
                esp_mqtt_client_publish(client, "motomami/status/rssi", rssi_str, 0, 1, true);
                ESP_LOGI(TAG, "RSSI: %d", ap_info.rssi);
            }
        }

        {
            char id_str[12];
            sprintf(id_str, "%lu", msg_id);
            esp_mqtt_client_publish(client, "motomami/status/id", id_str, 0, 1, true);
        }
        break;

    case MQTT_EVENT_DATA: {
        char topic[64];
        char payload[32];
        int tlen = event->topic_len;
        int plen = event->data_len;
        if (tlen >= sizeof(topic)) tlen = sizeof(topic) - 1;
        if (plen >= sizeof(payload)) plen = sizeof(payload) - 1;
        memcpy(topic, event->topic, tlen);
        topic[tlen] = 0;
        memcpy(payload, event->data, plen);
        payload[plen] = 0;

        if (strcmp(topic, "motomami/luz_nocturna/intensidad") == 0) {
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
    /* GOT_IP se dispara en cada reconexion WiFi: crear el cliente una sola vez.
     * esp_mqtt ya auto-reconecta por si solo. */
    if (mqtt_client != NULL) {
        return;
    }
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER,
        .network.reconnect_timeout_ms = MQTT_RECONNECT_MS,
        .session.last_will.topic = "motomami/status",
        .session.last_will.msg = "offline",
        .session.last_will.msg_len = 7,
        .session.last_will.qos = 1,
        .session.last_will.retain = true,
    };
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}

/* ================================================================
   WIFI
   ================================================================ */
static void wifi_retry_cb(void *arg)
{
    esp_wifi_connect();
}

static void espnow_init(void)
{
    esp_err_t err = esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "set_channel %d: %s", ESPNOW_CHANNEL, esp_err_to_name(err));
    }
    err = esp_now_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_now_init: %s (ESP-NOW desactivado)", esp_err_to_name(err));
        return;
    }
    esp_now_register_recv_cb(espnow_recv_cb);
    ESP_LOGI(TAG, "ESP-NOW receptor listo (canal %d)", ESPNOW_CHANNEL);
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == WIFI_EVENT_STA_START) {
        espnow_init();
        esp_wifi_connect();
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        /* backoff corto para no reconectar en rafaga si el AP esta caido */
        ESP_LOGI(TAG, "WiFi caido, reintento en 1 s...");
        if (wifi_retry_timer) {
            esp_timer_start_once(wifi_retry_timer, WIFI_RETRY_US);
        }
    } else if (id == IP_EVENT_STA_GOT_IP) {
        if (wifi_retry_timer) {
            esp_timer_stop(wifi_retry_timer);
        }
        ESP_LOGI(TAG, "WiFi connected, starting MQTT + OTA...");
        start_mqtt();
        ota_server_start();
    }
}

static void wifi_init(void)
{
    esp_netif_init();
    esp_event_loop_create_default();
    netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    /* Antena externa u.FL: GPIO14=HIGH activa el RF switch del XIAO C6 */
    gpio_set_direction(GPIO_NUM_14, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_14, 1);

    /* El AP (RPi) esta en la propia moto a <2 m: 10 dBm sobra.
     * Reduce el pico de corriente (tambien en la calibracion RF),
     * el consumo y el calor. */
    esp_wifi_set_max_tx_power(40);

    esp_event_handler_instance_t instance_any;
    esp_event_handler_instance_t instance_got_ip;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip);

    const esp_timer_create_args_t retry_args = {
        .callback = wifi_retry_cb,
        .name = "wifi_retry",
    };
    esp_timer_create(&retry_args, &wifi_retry_timer);

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

/* ================================================================
   MAIN
   ================================================================ */
void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());

    ota_boot_init();   // auto-validacion anti-rollback (30 s)

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

    esp_timer_create_args_t wdt_args = { .callback = wdt_timer_cb, .name = "sw_wdt" };
    esp_timer_handle_t wdt_timer;
    ESP_ERROR_CHECK(esp_timer_create(&wdt_args, &wdt_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(wdt_timer, 1000 * 1000));
}
