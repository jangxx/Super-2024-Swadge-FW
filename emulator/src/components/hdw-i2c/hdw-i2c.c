#include "hdw-i2c.h"


esp_err_t initI2CDriver(gpio_num_t sda, gpio_num_t scl, gpio_pullup_t pullup)
{
	return ESP_OK;
}

esp_err_t deinitI2CDriver(void)
{
	return ESP_OK;
}

esp_err_t SendI2CBuffer(int dev, char* buffer, int len)
{
    return ESP_OK;
}

esp_err_t ReadI2CBuffer(int dev, char* buffer, int len)
{
	return ESP_OK;
}