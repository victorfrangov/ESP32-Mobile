#ifndef MAIN
#define MAIN

#include <lwip/err.h>
#include <lwip/sockets.h>
#include <lwip/sys.h>
#include <lwip/netdb.h>
#include <lwip/dns.h>

#include "sdkconfig.h"

#include <driver/i2c.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <driver/uart.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <nvs_flash.h>
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdarg.h>

#include <time.h>
#include <esp_sntp.h>

#include <u8g2.h>
#include "wifi.h"
#include "ble.h"
#include "dht20.h"
#include "weather.h"
#include "geolocation.h"

#include "u8g2_esp32_hal.h"

#define PIN_CLK 6
#define PIN_MOSI 7
#define PIN_CS 18
#define PIN_DC 19
#define PIN_RESET 20

#define SDA_PIN 10
#define SCL_PIN 11

#define I2C_MASTER_FREQ_HZ 100000
#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_TIMEOUT_MS 1000

#define UART_NUM UART_NUM_0
#define BUF_SIZE (1024)

#define STATUS_BAR_H 10
#define STATUS_BAR_UPDATE_MS 1000

typedef enum {
    SCREEN_MAIN,
    SCREEN_SETTINGS,
    SCREEN_WEATHER,
    SCREEN_WEATHER_MTL,
    SCREEN_TIME,
    SCREEN_TNH,
    SCREEN_WIFI,
    SCREEN_GEO,
    SCREEN_BT
} Screen;

typedef void (*MenuAction)(void);

typedef struct {
    const char* label;
    MenuAction action;
} MenuItem;

typedef struct {
    const MenuItem* items;
    int count;
    int* selected;
} Menu;

typedef enum {
    KEY_NONE,
    KEY_UP,
    KEY_LEFT,
    KEY_DOWN,
    KEY_RIGHT,
    KEY_ENTER,
    KEY_ESC
} Key;

void update_screenf(const char* fmt, ...);
void update_screenf_font(const uint8_t* font, const char* fmt, ...);
void weather_ui_update(const WeatherInfo* w);

void app_main(void);

#endif /* MAIN */

