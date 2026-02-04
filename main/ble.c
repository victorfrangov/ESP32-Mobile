#include "ble.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
// #include "esp_nimble_hci.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

#define BLE_TAG "BLE_DRIVER"
#define APPLE_MANUFACTURER_ID 0x004C

static uint8_t own_addr_type;
static bool is_connecting = false;

static int ble_gap_event(struct ble_gap_event *event, void *arg);
static void ble_start_scan(void);

static void ble_on_sync(void)
{
    int rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        ESP_LOGE(BLE_TAG, "ble_hs_id_infer_auto failed: %d", rc);
        return;
    }

    ble_start_scan();
}

static void ble_start_scan(void)
{
    struct ble_gap_disc_params params = {
        .itvl = 0x50,
        .window = 0x30,
        .filter_policy = BLE_HCI_SCAN_FILT_NO_WL,
        .limited = 0,
        .passive = 0,
        .filter_duplicates = 1,
    };

    int rc = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &params, ble_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(BLE_TAG, "ble_gap_disc failed: %d", rc);
    } else {
        ESP_LOGI(BLE_TAG, "Scanning started");
    }
}

static void ble_log_adv_fields(const struct ble_hs_adv_fields *fields)
{
    if (fields->mfg_data != NULL && fields->mfg_data_len >= 2) {
        uint16_t manufacturer_id = (fields->mfg_data[1] << 8) | fields->mfg_data[0];
        ESP_LOGI(BLE_TAG, "Manufacturer ID: 0x%04X", manufacturer_id);
        ESP_LOG_BUFFER_HEX(BLE_TAG, fields->mfg_data, fields->mfg_data_len);
    }

    if (fields->name != NULL && fields->name_len > 0) {
        ESP_LOGI(BLE_TAG, "Name: %.*s", fields->name_len, fields->name);
    }
}

static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
        struct ble_hs_adv_fields fields;
        memset(&fields, 0, sizeof(fields));

        int rc = ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data);
        if (rc != 0) {
            return 0;
        }

        bool is_apple_device = false;
        if (fields.mfg_data != NULL && fields.mfg_data_len >= 2) {
            uint16_t manufacturer_id = (fields.mfg_data[1] << 8) | fields.mfg_data[0];
            if (manufacturer_id == APPLE_MANUFACTURER_ID) {
                is_apple_device = true;
                ESP_LOGI(BLE_TAG, "Apple device found, RSSI %d", event->disc.rssi);
                ble_log_adv_fields(&fields);
            }
        }

        if (!is_connecting && is_apple_device) {
            is_connecting = true;
            ble_gap_disc_cancel();

            int conn_rc = ble_gap_connect(own_addr_type, &event->disc.addr, 30000, NULL, ble_gap_event, NULL);
            if (conn_rc != 0) {
                ESP_LOGE(BLE_TAG, "ble_gap_connect failed: %d", conn_rc);
                is_connecting = false;
                ble_start_scan();
            }
        }
        return 0;
    }
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            ESP_LOGI(BLE_TAG, "Connected");
        } else {
            ESP_LOGE(BLE_TAG, "Connect failed: %d", event->connect.status);
            is_connecting = false;
            ble_start_scan();
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(BLE_TAG, "Disconnected, reason=%d", event->disconnect.reason);
        is_connecting = false;
        ble_start_scan();
        return 0;
    case BLE_GAP_EVENT_DISC_COMPLETE:
        ESP_LOGI(BLE_TAG, "Scan complete");
        if (!is_connecting) {
            ble_start_scan();
        }
        return 0;
    default:
        return 0;
    }
}

static void ble_host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void ble_init(void)
{
    esp_err_t ret = ESP_OK;

#if CONFIG_BT_CONTROLLER_ENABLED
    // ret = esp_nimble_hci_and_controller_init();
#else
    ret = esp_nimble_hci_init();
#endif
    if (ret != ESP_OK) {
        ESP_LOGE(BLE_TAG, "NimBLE HCI init failed: %s", esp_err_to_name(ret));
        return;
    }

    nimble_port_init();

    ble_hs_cfg.sync_cb = ble_on_sync;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set("ESP32-Mobile");

    nimble_port_freertos_init(ble_host_task);

    ESP_LOGI(BLE_TAG, "NimBLE initialized");
}

esp_err_t configure_ble5_advertising(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t start_ble5_advertising(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}