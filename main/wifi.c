#include "wifi.h"
#include "esp_err.h"

#include <wifi_provisioning/manager.h>
#include <wifi_provisioning/scheme_ble.h>

static uint8_t tries = 0;
static EventGroupHandle_t wifi_event_group = NULL;
static bool s_wifi_inited = false;
static bool s_wifi_connected = false;

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data){
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START){
        ESP_LOGI(WIFI_TAG, "Wi-Fi STA started");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED){
        s_wifi_connected = false;
        if (tries < MAX_FAILURES){
            ESP_LOGI(WIFI_TAG, "Reconnecting to AP... (try %d/%d)", tries + 1, MAX_FAILURES);
            esp_wifi_connect();
            tries++;
        } else {
            if (wifi_event_group) {
                xEventGroupSetBits(wifi_event_group, WIFI_FAILURE);
            }
        }
    }
}

static void ip_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data){
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP){
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(WIFI_TAG, "STA IP: " IPSTR, IP2STR(&event->ip_info.ip));
        tries = 0;
        s_wifi_connected = true;
        if (wifi_event_group) {
            xEventGroupSetBits(wifi_event_group, WIFI_SUCCESS);
        }
    }
}

bool wifi_is_connected(void) {
    return s_wifi_connected;
}

esp_err_t wifi_init(void) {
    if (s_wifi_inited) return ESP_OK;

    // 1. Base network & event loop initialization
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // 2. Register Wi-Fi & IP event handlers
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
        ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
        IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL, NULL));

    // 3. Check Provisioning Manager
    wifi_prov_mgr_config_t prov_config = {
        .scheme = wifi_prov_scheme_ble,
        .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM
    };
    ESP_ERROR_CHECK(wifi_prov_mgr_init(prov_config));

    bool provisioned = false;
    ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned));

    if (!provisioned) {
        ESP_LOGI(WIFI_TAG, "Starting BLE provisioning as PROV_ESP32_MOBILE...");
        const char *service_name = "PROV_ESP32_MOBILE";
        wifi_prov_security_t security = WIFI_PROV_SECURITY_0;
        
        // Start BLE advertising for provisioning
        ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(security, NULL, service_name, NULL));

        // Wait until phone sends credentials and provisioning finishes
        wifi_prov_mgr_wait();
        wifi_prov_mgr_deinit();
    } else {
        ESP_LOGI(WIFI_TAG, "Already provisioned. Starting Wi-Fi...");
        wifi_prov_mgr_deinit();
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_start());
        esp_wifi_connect();
    }

    s_wifi_inited = true;
    return ESP_OK;
}

esp_err_t wifi_reset_provisioning(void){
    ESP_LOGI(WIFI_TAG, "Erasing saved WI-Fi creds...");
    wifi_prov_mgr_reset_provisioning();
    esp_wifi_restore();
    s_wifi_connected = false;
    return ESP_OK;
}


esp_err_t connect_wifi(void) {
    if (!s_wifi_inited) {
        esp_err_t err = wifi_init();
        if (err != ESP_OK) return err;
    }

    if (wifi_is_connected()) {
        return WIFI_SUCCESS;
    }

    tries = 0;
    xEventGroupClearBits(wifi_event_group, WIFI_SUCCESS | WIFI_FAILURE);
    ESP_LOGI(WIFI_TAG, "Connecting to AP...");
    esp_wifi_connect();

    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
            WIFI_SUCCESS | WIFI_FAILURE,
            pdFALSE,
            pdFALSE,
            pdMS_TO_TICKS(15000));

    if (bits & WIFI_SUCCESS) {
        ESP_LOGI(WIFI_TAG, "Connected to AP");
        return WIFI_SUCCESS;
    } else {
        ESP_LOGE(WIFI_TAG, "Failed to connect to AP");
        return WIFI_FAILURE;
    }
}