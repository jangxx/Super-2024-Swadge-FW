#ifndef _HDW_I2C_H_
#define _HDW_I2C_H_

#include <stdint.h>

#include <driver/i2c.h>
#include <esp_err.h>
#include <hal/gpio_types.h>

esp_err_t initI2CDriver(gpio_num_t sda, gpio_num_t scl, gpio_pullup_t pullup);
esp_err_t deinitI2CDriver(void);
esp_err_t SendI2CBuffer(int dev, char* buffer, int len);
esp_err_t ReadI2CBuffer(int dev, char* buffer, int len);

#endif
