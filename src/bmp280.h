#ifndef BMP280_H
#define BMP280_H

#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

typedef struct {
    i2c_master_dev_handle_t dev;
    uint16_t t1;
    int16_t t2;
    int16_t t3;
    uint8_t chip_id;
} bmp280_t;

esp_err_t bmp280_init(bmp280_t *s, i2c_master_bus_handle_t bus, uint8_t addr);
/* Temperatura en centesimas de grado Celsius (2534 = 25.34 C). */
esp_err_t bmp280_read_temp_centi(bmp280_t *s, int32_t *centi_c);

#endif
