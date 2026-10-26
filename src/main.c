#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "nvs_flash.h"
#include "bmp280.h"
#include "web_page.h"

#define SAMPLE_MS 2000U
#define STALE_MS 6000
#define AP_SSID "SBS-Grupo5"
#define AP_PASS "ColectorG5!"

/* Direccion del modulo confirmado; cambiarla solo despues de escanear el bus real. */
#define SENSOR_ADDR 0x76U

static const char * const TAG = "G5-WEB";
static SemaphoreHandle_t s_lock;
static uint32_t s_boot;
static struct {
    bool has_sample;
    int32_t centi;
    int64_t sampled_ms;
    uint32_t sequence;
    uint32_t errors;
    uint8_t chip;
    esp_err_t error;
} s_data = { .error = ESP_ERR_NOT_FINISHED };

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

static void publish(esp_err_t error, int32_t centi, uint8_t chip)
{
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "snapshot mutex timeout; previous sample will expire");
        return;
    }
    s_data.error = error;
    s_data.chip = chip;
    if (error == ESP_OK) {
        s_data.centi = centi;
        s_data.sampled_ms = now_ms();
        s_data.has_sample = true;
        s_data.sequence++;
    } else { s_data.errors++; }
    if (xSemaphoreGive(s_lock) != pdTRUE) ESP_LOGE(TAG, "mutex release failed");
}

/* Unica duena del I2C: el servidor HTTP nunca espera una transaccion del sensor. */
static void sensor_task(void *arg)
{
    (void)arg;
    i2c_master_bus_handle_t bus = NULL;
    bmp280_t sensor = {0};
    bool ready = false;
    const i2c_master_bus_config_t cfg = {
        .i2c_port = I2C_NUM_0, .sda_io_num = GPIO_NUM_21,
        .scl_io_num = GPIO_NUM_22, .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7, .flags.enable_internal_pullup = false,
    };
    /* El bus se crea en la inicializacion, no dentro del ciclo de adquisicion. */
    const esp_err_t bus_error = i2c_new_master_bus(&cfg, &bus);
    if (bus_error != ESP_OK) {
        publish(bus_error, 0, 0);
        ESP_LOGE(TAG, "bus init: %s", esp_err_to_name(bus_error));
        vTaskDelete(NULL);
        return;
    }
    /* El dispositivo se crea una vez; si el sensor falta, se recupera con este mismo handle. */
    esp_err_t setup_error = bmp280_init(&sensor, bus, SENSOR_ADDR);
    ready = setup_error == ESP_OK;
    if (ready) ESP_LOGI(TAG, "chip_id=0x%02X", sensor.chip_id);
    if (sensor.dev == NULL) {
        publish(setup_error, 0, 0);
        vTaskDelete(NULL);
        return;
    }
    TickType_t next = xTaskGetTickCount();
    /* Tarea RTOS permanente: toda espera I2C es acotada y cada ciclo cede la CPU. */
    for (;;) {
        esp_err_t err = ESP_OK;
        if (err == ESP_OK && !ready) {
            err = bmp280_init(&sensor, bus, SENSOR_ADDR);
            ready = err == ESP_OK;
            if (ready) ESP_LOGI(TAG, "chip_id=0x%02X", sensor.chip_id);
        }
        int32_t centi = 0;
        if (err == ESP_OK) err = bmp280_read_temp_centi(&sensor, &centi);
        publish(err, centi, sensor.chip_id);
        if (err == ESP_OK) {
            int32_t magnitude = centi < 0 ? -centi : centi;
            ESP_LOGI(TAG, "temperatura=%s%"PRId32".%02"PRId32" C; heap=%"PRIu32,
                     centi < 0 ? "-" : "", magnitude / 100, magnitude % 100,
                     esp_get_free_heap_size());
        } else {
            ESP_LOGW(TAG, "sensor: %s", esp_err_to_name(err));
            ready = false; /* En el proximo ciclo se relee ID y calibracion, reutilizando el handle. */
        }
        if (xTaskDelayUntil(&next, pdMS_TO_TICKS(SAMPLE_MS)) != pdTRUE) {
            ESP_LOGW(TAG, "sample deadline missed");
            vTaskDelay(pdMS_TO_TICKS(1U));
            next = xTaskGetTickCount();
        }
    }
}

static esp_err_t page_get(httpd_req_t *req)
{
    esp_err_t err = httpd_resp_set_type(req, "text/html; charset=utf-8");
    if (err != ESP_OK) return err;
    err = httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    if (err != ESP_OK) return err;
    return httpd_resp_send(req, (const char *)WEB_PAGE, WEB_PAGE_LEN);
}

static esp_err_t temp_get(httpd_req_t *req)
{
    char value[24] = "null";
    char age_text[32] = "null";
    char body[640];
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        esp_err_t status_error = httpd_resp_set_status(req, "503 Service Unavailable");
        if (status_error != ESP_OK) return status_error;
        return httpd_resp_send(req, "busy", HTTPD_RESP_USE_STRLEN);
    }
    const int64_t uptime = now_ms();
    const bool has = s_data.has_sample;
    const int64_t age = has ? uptime - s_data.sampled_ms : -1;
    const bool valid = has && s_data.error == ESP_OK && age >= 0 && age <= STALE_MS;
    const char *status = s_data.error == ESP_ERR_NOT_FINISHED ? "starting" :
        s_data.error != ESP_OK ? "sensor_error" : !valid ? "stale" : "ok";
    int value_len = valid ? snprintf(value, sizeof value, "%"PRId32, s_data.centi) : 4;
    int age_len = has ? snprintf(age_text, sizeof age_text, "%"PRId64, age) : 4;
    if (value_len < 0 || (size_t)value_len >= sizeof value ||
        age_len < 0 || (size_t)age_len >= sizeof age_text) {
        if (xSemaphoreGive(s_lock) != pdTRUE) ESP_LOGE(TAG, "mutex release failed");
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "format error");
    }
    int count = snprintf(body, sizeof body,
        "{\"boot_id\":\"%08"PRIx32"\",\"status\":\"%s\",\"valid\":%s,"
        "\"temperature_centi\":%s,\"age_ms\":%s,\"sample_ms\":%"PRId64","
        "\"uptime_ms\":%"PRId64",\"sequence\":%"PRIu32",\"errors\":%"PRIu32","
        "\"chip_id\":%u,\"error_code\":%d,\"free_heap\":%"PRIu32",\"period_ms\":%u}",
        s_boot, status, valid ? "true" : "false", value, age_text,
        s_data.sampled_ms, uptime, s_data.sequence, s_data.errors,
        (unsigned)s_data.chip, (int)s_data.error, esp_get_free_heap_size(), SAMPLE_MS);
    if (xSemaphoreGive(s_lock) != pdTRUE) ESP_LOGE(TAG, "mutex release failed");
    if (count < 0 || (size_t)count >= sizeof body)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON overflow");
    esp_err_t err = httpd_resp_set_type(req, "application/json");
    if (err != ESP_OK) return err;
    err = httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    if (err != ESP_OK) return err;
    return httpd_resp_send(req, body, count);
}

static void start_network(void)
{
    /* No se borra la particion NVS en silencio si falla la inicializacion. */
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *netif = esp_netif_create_default_wifi_ap();
    if (netif == NULL) { ESP_LOGE(TAG, "No AP netif"); abort(); }
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    wifi_config_t config = { .ap = {
        .ssid = AP_SSID, .ssid_len = sizeof(AP_SSID) - 1,
        .password = AP_PASS, .channel = 6, .max_connection = 3,
        .authmode = WIFI_AUTH_WPA2_PSK,
    }};
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &config));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_netif_ip_info_t ip;
    ESP_ERROR_CHECK(esp_netif_get_ip_info(netif, &ip));
    ESP_LOGI(TAG, "Wi-Fi: %s | abrir http://"IPSTR, AP_SSID, IP2STR(&ip.ip));
    httpd_handle_t server = NULL;
    httpd_config_t http = HTTPD_DEFAULT_CONFIG();
    http.lru_purge_enable = true;
    ESP_ERROR_CHECK(httpd_start(&server, &http));
    const httpd_uri_t page = { .uri = "/", .method = HTTP_GET, .handler = page_get };
    const httpd_uri_t api = { .uri = "/api/temp", .method = HTTP_GET, .handler = temp_get };
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &page));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &api));
}

void app_main(void)
{
    s_boot = esp_random();
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) { ESP_LOGE(TAG, "No mutex"); return; }
    if (xTaskCreate(sensor_task, "temperature", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "No sensor task"); return;
    }
    start_network();
}
