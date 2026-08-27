#ifndef WIFI
#define WIFI

#include <esp_wifi.h>             // For Wi-Fi functions and configurations
#include <esp_log.h>              // For logging

#define WIFI_TAG "WIFI"

typedef enum {
    WIFI_SUCCESS = 1 << 0,
    WIFI_FAILURE = 1 << 1,
    MAX_FAILURES = 10
} wifi_status_t;

esp_err_t wifi_init(void);
esp_err_t connect_wifi(void);
esp_err_t wifi_reset_provisioning(void);
bool wifi_is_connected(void);

#endif /* WIFI */
