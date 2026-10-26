#include <stddef.h>
#include "bmp280.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define REG_STATUS     0xF3U
#define STATUS_ATTEMPTS 12U
#define REG_ID         0xD0U
#define REG_CALIB      0x88U
#define REG_CTRL_MEAS  0xF4U
#define REG_TEMP_MSB   0xFAU
#define CTRL_T1_FORCED 0x21U   /* temp x1, presion omitida, modo forzado */
#define I2C_TIMEOUT_MS 100
#define MEAS_WAIT_MS   20U
#define TEMP_SKIPPED   0x80000

static esp_err_t read_reg(bmp280_t *s, uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_master_transmit_receive(s->dev, &reg, 1, buf, len, I2C_TIMEOUT_MS);
}

/* Sondeo acotado: sin espera activa ni bucles infinitos sobre el periferico. */
static esp_err_t wait_clear(bmp280_t *s, uint8_t mask)
{
    for (uint8_t attempt = 0; attempt < STATUS_ATTEMPTS; ++attempt) {
        uint8_t status = 0;
        esp_err_t err = read_reg(s, REG_STATUS, &status, 1U);
        if (err != ESP_OK) return err;
        if ((status & mask) == 0U) return ESP_OK;
        vTaskDelay(pdMS_TO_TICKS(10U));
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t bmp280_init(bmp280_t *s, i2c_master_bus_handle_t bus, uint8_t addr)
{
    if (s == NULL || bus == NULL) return ESP_ERR_INVALID_ARG;
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 100000,
    };
    /* Se reserva una sola vez; los reintentos reutilizan el handle. Quien llama debe dejar s en cero. */
    esp_err_t err = ESP_OK;
    if (s->dev == NULL) err = i2c_master_bus_add_device(bus, &cfg, &s->dev);
    if (err != ESP_OK) {
        return err;
    }
    err = read_reg(s, REG_ID, &s->chip_id, 1U);
    if (err != ESP_OK) {
        return err;
    }
    if (s->chip_id != 0x58U && s->chip_id != 0x60U) return ESP_ERR_NOT_SUPPORTED;
    err = wait_clear(s, 0x01U); /* Espera a que termine la copia de calibracion desde la NVM. */
    if (err != ESP_OK) return err;
    uint8_t c[6];
    err = read_reg(s, REG_CALIB, c, sizeof c);
    if (err != ESP_OK) {
        return err;
    }
    s->t1 = (uint16_t)((uint16_t)c[0] | ((uint16_t)c[1] << 8));
    s->t2 = (int16_t)((uint16_t)c[2] | ((uint16_t)c[3] << 8));
    s->t3 = (int16_t)((uint16_t)c[4] | ((uint16_t)c[5] << 8));
    if (s->t1 == 0U || s->t1 == 0xFFFFU) return ESP_ERR_INVALID_RESPONSE;
    return ESP_OK;
}

esp_err_t bmp280_read_temp_centi(bmp280_t *s, int32_t *centi_c)
{
    if (s == NULL || s->dev == NULL || centi_c == NULL) return ESP_ERR_INVALID_ARG;
    const uint8_t cmd[2] = { REG_CTRL_MEAS, CTRL_T1_FORCED };
    esp_err_t err = i2c_master_transmit(s->dev, cmd, sizeof cmd, I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(MEAS_WAIT_MS));
    err = wait_clear(s, 0x09U); /* Espera a que measuring e im_update esten en 0 (conversion lista). */
    if (err != ESP_OK) return err;
    uint8_t d[3];
    err = read_reg(s, REG_TEMP_MSB, d, sizeof d);
    if (err != ESP_OK) {
        return err;
    }
    const int32_t adc = ((int32_t)d[0] << 12) | ((int32_t)d[1] << 4) | ((int32_t)d[2] >> 4);
    if (adc == TEMP_SKIPPED) {
        return ESP_ERR_INVALID_RESPONSE;   /* medicion no realizada: no inventamos un valor */
    }
    const int64_t t1 = (int64_t)s->t1;
    const int64_t v1 = (((adc >> 3) - (t1 << 1)) * (int32_t)s->t2) >> 11;
    const int64_t dt = (adc >> 4) - t1;
    const int64_t v2 = (((dt * dt) >> 12) * (int32_t)s->t3) >> 14;
    const int64_t result = (((v1 + v2) * 5) + 128) >> 8;
    if (result < -4000 || result > 8500) return ESP_ERR_INVALID_RESPONSE;
    *centi_c = (int32_t)result;
    return ESP_OK;
}
