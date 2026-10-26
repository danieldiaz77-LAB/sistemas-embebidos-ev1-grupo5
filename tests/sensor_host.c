#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "bmp280.h"
static int failure=0;
static uint8_t status_byte=0;
static unsigned adds=0;
static uint32_t raw=519888;
static uint8_t id=0x58;
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t b,const i2c_device_config_t*c,i2c_master_dev_handle_t*d){(void)b;(void)c;adds++;*d=(void*)1;return ESP_OK;}
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t d,const uint8_t*r,size_t n,uint8_t*out,size_t len,int t){(void)d;(void)n;(void)t;if(failure)return 99;if(*r==0xF3){*out=status_byte;}else if(*r==0xD0){*out=id;}else if(*r==0x88){const uint8_t cal[]={0x70,0x6B,0x43,0x67,0x18,0xFC};assert(len==6);memcpy(out,cal,6);}else{out[0]=raw>>12;out[1]=raw>>4;out[2]=(raw&15)<<4;}return ESP_OK;}
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t d,const uint8_t*c,size_t n,int t){(void)d;(void)c;(void)n;(void)t;return failure?99:ESP_OK;}
int main(void){bmp280_t s={0};int32_t temp=123;
assert(bmp280_init(NULL,(void*)1,0x76)==ESP_ERR_INVALID_ARG);
assert(bmp280_init(&s,(void*)1,0x76)==ESP_OK);
assert(bmp280_read_temp_centi(&s,&temp)==ESP_OK);assert(temp==2508);
failure=1;temp=123;assert(bmp280_read_temp_centi(&s,&temp)==99);assert(temp==123);failure=0;
raw=0x80000;assert(bmp280_read_temp_centi(&s,&temp)==ESP_ERR_INVALID_RESPONSE);
id=0x60;assert(bmp280_init(&s,(void*)1,0x76)==ESP_OK);
id=0x00;assert(bmp280_init(&s,(void*)1,0x76)==ESP_ERR_NOT_SUPPORTED);
assert(bmp280_read_temp_centi(&s,NULL)==ESP_ERR_INVALID_ARG);
id=0x58;status_byte=1;assert(bmp280_init(&s,(void*)1,0x76)==ESP_ERR_TIMEOUT);
status_byte=0;assert(bmp280_init(&s,(void*)1,0x76)==ESP_OK);
status_byte=8;raw=519888;assert(bmp280_read_temp_centi(&s,&temp)==ESP_ERR_TIMEOUT);
status_byte=0;assert(bmp280_read_temp_centi(&s,&temp)==ESP_OK);assert(temp==2508);
assert(adds==1);
puts("PASS: compensation vector 25.08 C, invalid arguments, I2C error, skipped sample, chip IDs, bounded NVM/conversion wait, recovery, one allocation");}
