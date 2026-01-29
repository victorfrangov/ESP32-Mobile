#ifndef WIFI
#define WIFI

#include <string.h>               // For functions like `bzero`
#include <lwip/err.h>
#include <lwip/sockets.h>
#include <lwip/sys.h>
#include <lwip/netdb.h>
#include <lwip/dns.h>

#include <esp_wifi.h>             // For Wi-Fi functions and configurations
#include <esp_event.h>            // For event handling
#include <esp_log.h>              // For logging

#include <freertos/FreeRTOS.h>    // For FreeRTOS functions
#include <freertos/event_groups.h>// For event group handling
#include <freertos/task.h>        // For task delay (`vTaskDelay`)

#include "u8g2_esp32_hal.h" 
#include "wifi_config.h"

static const char *WIFI_TAG = "WIFI";
typedef void (*update_screenf_callback_t)(const char* fmt, ...);

typedef enum {
    WIFI_SUCCESS = 1 << 0,
    WIFI_FAILURE = 1 << 1,
    TCP_SUCCESS = 1 << 0,
    TCP_FAILURE = 1 << 1,
    MAX_FAILURES = 10
} wifi_status_t;

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
static void ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
esp_err_t connect_wifi(void);
// esp_err_t connect_tcp_server(update_screenf_callback_t update_screenf);
// static void handle_server_data(int sock, update_screenf_callback_t update_screenf);

#endif /* WIFI */
