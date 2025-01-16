#include "hdw-i2c.h"

#include "soc/rtc_cntl_reg.h"
#include "soc/gpio_reg.h"
#include "soc/io_mux_reg.h"
#include "rom/gpio.h"
#include "soc/gpio_struct.h"

#define DSCL_OUTPUT                             \
    {                                           \
        GPIO.enable1_w1ts.val = 1 << (41 - 32); \
    }
#define DSCL_INPUT                              \
    {                                           \
        GPIO.enable1_w1tc.val = 1 << (41 - 32); \
    }
#define DSDA_OUTPUT                  \
    {                                \
        GPIO.enable_w1ts = 1 << (3); \
    }
#define DSDA_INPUT                   \
    {                                \
        GPIO.enable_w1tc = 1 << (3); \
    }
#define READ_DSDA ((GPIO.in >> 3) & 1)

// 14 counts (1MHz) works most of the time, but no hurries, let's slow it down to ~800k.
void driver_i2c_delay(int x)
{
    int i;
    for (i = 0; i < 19 * x; i++)
        asm volatile("nop");
}
#define DELAY1 driver_i2c_delay(1);
#define DELAY2 driver_i2c_delay(2);

#include "static_i2c.h"


esp_err_t initI2CDriver(gpio_num_t sda, gpio_num_t scl, gpio_pullup_t pullup)
{
    gpio_config_t gsetup = {
        .pin_bit_mask = (1ULL << sda) | (1ULL << scl),
        .mode         = GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
    };

    gpio_config(&gsetup);

    // This will "shake loose" any devices stuck on the bus.
    GPIO.enable_w1ts      = 1 << (3);
    GPIO.enable1_w1ts.val = 1 << (41 - 32);
    esp_rom_delay_us(10);
    GPIO.out_w1tc = 1 << (3);
    for (int i = 0; i < 16; i++)
    {
        esp_rom_delay_us(10);
        GPIO.out1_w1ts.val = 1 << (41 - 32);
        esp_rom_delay_us(10);
        GPIO.out1_w1tc.val = 1 << (41 - 32);
    }
    esp_rom_delay_us(10);
    GPIO.out1_w1ts.val = 1 << (41 - 32);
    esp_rom_delay_us(10);
    GPIO.out_w1ts = 1 << (3);
    esp_rom_delay_us(10);
    GPIO.out1_w1ts.val = 1 << (41 - 32); // Send final stop

    // Prepare for normal open drain functionality.
    GPIO.enable1_w1tc.val = 1 << (41 - 32);
    GPIO.enable_w1tc      = 1 << (3);
    GPIO.out1_w1tc.val    = 1 << (41 - 32);
    GPIO.out_w1tc         = 1 << (3);

	return ESP_OK;
}

esp_err_t deinitI2CDriver(void)
{
	return ESP_OK;
}

esp_err_t SendI2CBuffer(int dev, char* buffer, int len)
{
    DriverSendStart();

    DriverSendByte(dev << 1); // set address and write mode (0)

    for (int i = 0; i < len; i++)
    {
        DriverSendByte(buffer[i]);
    }

    DriverSendStop();
    return ESP_OK;
}

esp_err_t ReadI2CBuffer(int dev, char* buffer, int len)
{
    DriverSendStart();

    DriverSendByte((dev << 1) | 1); // set address and read mode (1)

    for (int i = 0; i < len; i++)
    {
        buffer[i] = DriverGetByte(i == len - 1);

    }
    DriverSendStop();
    return len;
}