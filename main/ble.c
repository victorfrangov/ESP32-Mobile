#include "ble.h" // Assuming you have this header
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include "nvs_flash.h" // Needed for example structure, though not used heavily here

#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_gatt_defs.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#define BLE_TAG "BLE_DRIVER" // Use your existing tag
#define GATTC_TAG BLE_TAG    // Alias for consistency with example logs

// Scan parameters from example
#define EXT_SCAN_DURATION     0 // Scan indefinitely until stopped
#define EXT_SCAN_PERIOD       0 // Continuous scanning

#define APPLE_MANUFACTURER_ID 0x004C // Apple's Bluetooth Manufacturer ID

// --- Connection Parameters (Similar to the original example) ---
const esp_ble_conn_params_t phy_1m_conn_params = {
    .scan_interval = 0x40, // N * 0.625ms = 40ms
    .scan_window = 0x40,   // N * 0.625ms = 40ms
    .interval_min = 80,    // N * 1.25ms = 100ms
    .interval_max = 80,    // N * 1.25ms = 100ms
    .latency = 0,
    .supervision_timeout = 400, // N * 10ms = 4000ms
    .min_ce_len  = 0,
    .max_ce_len = 0,
};
// Define phy_2m_conn_params and phy_coded_conn_params if you intend to use those PHYs
// const esp_ble_conn_params_t phy_2m_conn_params = { ... };
// const esp_ble_conn_params_t phy_coded_conn_params = { ... };

// --- Globals adapted from example ---
#define PROFILE_NUM         1
#define PROFILE_A_APP_ID    0
#define INVALID_HANDLE      0

static void ble_gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);

struct gattc_profile_inst {
    esp_gattc_cb_t gattc_cb;
    uint16_t gattc_if;
    uint16_t app_id;
    uint16_t conn_id;
    esp_bd_addr_t remote_bda;
};

static struct gattc_profile_inst gl_profile_tab[PROFILE_NUM] = {
    [PROFILE_A_APP_ID] = {
        .gattc_cb = NULL, // Will be set later
        .gattc_if = ESP_GATT_IF_NONE,
    },
};

static bool is_connecting = false; // Flag to prevent multiple connection attempts

// Extended Scan Parameters (modify interval/window as needed)
static esp_ble_ext_scan_params_t ext_scan_params = {
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE, // Disable duplicate reports for simplicity
    .cfg_mask = ESP_BLE_GAP_EXT_SCAN_CFG_UNCODE_MASK, // Scan on 1M PHY only
    .uncoded_cfg = {BLE_SCAN_TYPE_ACTIVE, 80, 40}, // Active scan, Interval 50ms, Window 25ms
    // .coded_cfg = {BLE_SCAN_TYPE_ACTIVE, 80, 40}, // Coded PHY config (if needed)
};
// --- End Globals ---


// --- Forward declarations ---
static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);
static void esp_gattc_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param);
static void gattc_profile_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param);

// --- GATTC Profile Handler ---
static void gattc_profile_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param)
{
    esp_ble_gattc_cb_param_t *p_data = (esp_ble_gattc_cb_param_t *)param;

    switch (event) {
    case ESP_GATTC_REG_EVT:
        ESP_LOGI(GATTC_TAG, "GATTC REG_EVT, status %d, app_id %d", p_data->reg.status, p_data->reg.app_id);
        if (p_data->reg.status == ESP_GATT_OK) {
            gl_profile_tab[p_data->reg.app_id].gattc_if = gattc_if;
        } else {
            ESP_LOGE(GATTC_TAG, "Reg app failed");
            return;
        }
        // Example configures privacy here, optional
        // esp_ble_gap_config_local_privacy(true);
        break;
    case ESP_GATTC_OPEN_EVT:
        if (param->open.status != ESP_GATT_OK){
            ESP_LOGE(GATTC_TAG, "connect failed, status %d", p_data->open.status);
            is_connecting = false; // Allow scanning/connecting again
            // Consider restarting scan here if needed
            // esp_ble_gap_start_ext_scan(EXT_SCAN_DURATION, EXT_SCAN_PERIOD);
            break;
        }
        ESP_LOGI(GATTC_TAG, "Connected successfully, conn_id %d, MTU %d", p_data->open.conn_id, p_data->open.mtu);
        gl_profile_tab[PROFILE_A_APP_ID].conn_id = p_data->open.conn_id;
        memcpy(gl_profile_tab[PROFILE_A_APP_ID].remote_bda, p_data->open.remote_bda, sizeof(esp_bd_addr_t));

        // Attempt to negotiate MTU for potentially better performance
        esp_err_t mtu_ret = esp_ble_gattc_send_mtu_req (gattc_if, p_data->open.conn_id);
        if (mtu_ret){
            ESP_LOGE(GATTC_TAG, "config MTU error, error code = %x", mtu_ret);
        }
        break;
    case ESP_GATTC_CFG_MTU_EVT:
        if (param->cfg_mtu.status != ESP_GATT_OK){
            ESP_LOGE(GATTC_TAG,"Config MTU failed, error status = %x", param->cfg_mtu.status);
        }
        ESP_LOGI(GATTC_TAG, "MTU configured: %d", param->cfg_mtu.mtu);
        break;
    case ESP_GATTC_DISCONNECT_EVT:
        ESP_LOGI(GATTC_TAG, "Disconnected, reason 0x%x", p_data->disconnect.reason);
        is_connecting = false; // Allow scanning/connecting again
        // Automatically restart scanning after disconnection
        esp_err_t scan_ret = esp_ble_gap_start_ext_scan(EXT_SCAN_DURATION, EXT_SCAN_PERIOD);
         if (scan_ret) {
            ESP_LOGE(GATTC_TAG, "Restart scan failed, error code = %x", scan_ret);
        }
        break;
    // Removed service discovery, characteristic, descriptor events
    default:
        ESP_LOGE(GATTC_TAG, "Unhandled GATTC event: %d", event);
        break;
    }
}

// --- GAP Callback ---
static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    // --- Scan Related Events ---
    case ESP_GAP_BLE_SET_LOCAL_PRIVACY_COMPLETE_EVT: // Example starts scan setup here
        if (param->local_privacy_cmpl.status != ESP_BT_STATUS_SUCCESS){
            ESP_LOGE(GATTC_TAG, "Config local privacy failed, status %x", param->local_privacy_cmpl.status);
            break;
        }
        ESP_LOGI(GATTC_TAG, "Local privacy configured");
        esp_err_t scan_ret = esp_ble_gap_set_ext_scan_params(&ext_scan_params);
        if (scan_ret){
            ESP_LOGE(GATTC_TAG, "Set extend scan params error, error code = %x", scan_ret);
        }
        break;
    case ESP_GAP_BLE_SET_EXT_SCAN_PARAMS_COMPLETE_EVT: {
        if (param->set_ext_scan_params.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(GATTC_TAG, "Set extend scan params failed, error status = %x", param->set_ext_scan_params.status);
            break;
        }
        ESP_LOGI(GATTC_TAG, "Extended scan params set, starting scan...");
        esp_ble_gap_start_ext_scan(EXT_SCAN_DURATION, EXT_SCAN_PERIOD);
        break;
    }
    case ESP_GAP_BLE_EXT_SCAN_START_COMPLETE_EVT:
        if (param->ext_scan_start.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(GATTC_TAG, "Extended scan start failed, status %x", param->ext_scan_start.status);
            break;
        }
        ESP_LOGI(GATTC_TAG, "Extended scan started successfully");
        break;
    case ESP_GAP_BLE_EXT_ADV_REPORT_EVT: {
        uint8_t *mfg_data = NULL;
        uint8_t mfg_data_len = 0;

        // Parse Manufacturer Specific Data
        mfg_data = esp_ble_resolve_adv_data(param->ext_adv_report.params.adv_data,
                                        ESP_BLE_AD_MANUFACTURER_SPECIFIC_TYPE, &mfg_data_len);

        bool is_apple_device = false;
        if (mfg_data != NULL && mfg_data_len >= 2) {
            uint16_t manufacturer_id = (mfg_data[1] << 8) | mfg_data[0]; // Little-endian format

            if (manufacturer_id == APPLE_MANUFACTURER_ID) {
                ESP_LOGI(GATTC_TAG, "Ext Adv Report: Addr "ESP_BD_ADDR_STR", RSSI %d",
                                        ESP_BD_ADDR_HEX(param->ext_adv_report.params.addr),
                                        param->ext_adv_report.params.rssi);
                is_apple_device = true;
                ESP_LOGI(GATTC_TAG, "  -> Apple Device Found (Mfg ID: 0x%04X)", manufacturer_id);
                ESP_LOG_BUFFER_HEX(GATTC_TAG, mfg_data, mfg_data_len); // Log the specific data payload
            } else {
                ESP_LOGI(GATTC_TAG, "  -> Non-Apple Mfg Data Found (ID: 0x%04X)", manufacturer_id);
            }
        } else {
                ESP_LOGI(GATTC_TAG, "  -> Manufacturer data not found.");
                // Also check for name just in case
                uint8_t *adv_name = NULL;
                uint8_t adv_name_len = 0;
                adv_name = esp_ble_resolve_adv_data(param->ext_adv_report.params.adv_data, ESP_BLE_AD_TYPE_NAME_CMPL, &adv_name_len);
                if (adv_name != NULL) { ESP_LOGI(GATTC_TAG, "  -> Found Name: %.*s", adv_name_len, (char*)adv_name); }
        }

        // --- Connection Logic ---
        if (!is_connecting && is_apple_device)
        {
            ESP_LOGI(GATTC_TAG, ">>> APPLE DEVICE MATCH FOUND! Attempting connection to "ESP_BD_ADDR_STR, ESP_BD_ADDR_HEX(param->ext_adv_report.params.addr));
            is_connecting = true;
            esp_ble_gap_stop_ext_scan(); // Stop scanning

            // Initiate connection using enhanced open
            ESP_LOGI(GATTC_TAG, "Stopping scan and attempting enhanced connect...");

            esp_ble_gatt_creat_conn_params_t creat_conn_params = {0};
            memcpy(&creat_conn_params.remote_bda, param->ext_adv_report.params.addr, ESP_BD_ADDR_LEN);
            creat_conn_params.remote_addr_type = param->ext_adv_report.params.addr_type;
            creat_conn_params.own_addr_type = BLE_ADDR_TYPE_PUBLIC; // Or BLE_ADDR_TYPE_RPA_PUBLIC if using privacy
            creat_conn_params.is_direct = false; // Usually false for connections from general advertising
            // Set is_aux based on the report type if needed, true is often correct for extended reports
            creat_conn_params.is_aux = true;
            // Specify which PHYs to attempt connection on (1M is usually safest to start)
            creat_conn_params.phy_mask = ESP_BLE_PHY_1M_PREF_MASK; // | ESP_BLE_PHY_2M_PREF_MASK | ESP_BLE_PHY_CODED_PREF_MASK;
            creat_conn_params.phy_1m_conn_params = &phy_1m_conn_params;
            // Assign other PHY params if using them in the mask
            // creat_conn_params.phy_2m_conn_params = &phy_2m_conn_params;
            // creat_conn_params.phy_coded_conn_params = &phy_coded_conn_params;

            esp_err_t open_ret = esp_ble_gattc_enh_open(gl_profile_tab[PROFILE_A_APP_ID].gattc_if, &creat_conn_params);

            if (open_ret != ESP_OK) {
                ESP_LOGE(GATTC_TAG, "esp_ble_gattc_enh_open failed: %s", esp_err_to_name(open_ret));
                is_connecting = false; // Reset flag on immediate failure
                // Optionally restart scanning here
                // esp_ble_gap_start_ext_scan(EXT_SCAN_DURATION, EXT_SCAN_PERIOD);
            }
        }
        break;
    } // End ESP_GAP_BLE_EXT_ADV_REPORT_EVT case

    case ESP_GAP_BLE_EXT_SCAN_STOP_COMPLETE_EVT:
        if (param->ext_scan_stop.status != ESP_BT_STATUS_SUCCESS){
            ESP_LOGE(GATTC_TAG, "Extended scan stop failed, status %x", param->ext_scan_stop.status);
        } else {
            ESP_LOGI(GATTC_TAG, "Extended scan stopped successfully");
            // If stop was initiated for connection, connection attempt happens next via gattc_open
        }
        break;

    // --- Advertising Related Events (Keep if your ESP32 also advertises) ---
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT: // Assuming you might still advertise
         if (param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
             ESP_LOGI(BLE_TAG, "Advertising started successfully");
         } else {
             ESP_LOGE(BLE_TAG, "Failed to start advertising");
         }
         break;
    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT: // Assuming you might still advertise
         ESP_LOGI(BLE_TAG, "Advertising stopped");
         break;
    default:
        ESP_LOGE(GATTC_TAG, "Unhandled GAP event: %d", event);
        break;
    }
}

// --- GATTC Callback Wrapper (from example) ---
static void esp_gattc_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param)
{
    ESP_LOGI(GATTC_TAG, "GATTC CB EVT %d, gattc if %d", event, gattc_if);

    /* If event is register event, store the gattc_if for each profile */
    if (event == ESP_GATTC_REG_EVT) {
        if (param->reg.status == ESP_GATT_OK) {
            gl_profile_tab[param->reg.app_id].gattc_if = gattc_if;
        } else {
            ESP_LOGE(GATTC_TAG, "Reg app failed, app_id %d, status %d",
                    param->reg.app_id, param->reg.status);
            return;
        }
    }

    /* Call handler for specific profile */
    do {
        int idx;
        for (idx = 0; idx < PROFILE_NUM; idx++) {
            if (gattc_if == ESP_GATT_IF_NONE || gattc_if == gl_profile_tab[idx].gattc_if) {
                if (gl_profile_tab[idx].gattc_cb) {
                    gl_profile_tab[idx].gattc_cb(event, gattc_if, param);
                }
            }
        }
    } while (0);
}

void ble_init(void) {
    esp_err_t ret;

    ESP_LOGI(BLE_TAG, "Starting BLE Initialization...");
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) { ESP_LOGE(GATTC_TAG, "Initialize controller failed: %s", esp_err_to_name(ret)); return; }
    ESP_LOGI(BLE_TAG, "BT Controller Initialized");

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) { ESP_LOGE(GATTC_TAG, "Enable controller failed: %s", esp_err_to_name(ret)); return; }
    ESP_LOGI(BLE_TAG, "BT Controller Enabled in BLE Mode");

    ret = esp_bluedroid_init();
    if (ret) { ESP_LOGE(GATTC_TAG, "Init bluedroid failed: %s", esp_err_to_name(ret)); return; }
    ESP_LOGI(BLE_TAG, "Bluedroid Initialized");

    ret = esp_bluedroid_enable();
    if (ret) { ESP_LOGE(GATTC_TAG, "Enable bluedroid failed: %s", esp_err_to_name(ret)); return; }
    ESP_LOGI(BLE_TAG, "Bluedroid Enabled");

    // --- ADD SECURITY INITIALIZATION ---
    ESP_LOGI(BLE_TAG, "Setting BLE Security Parameters...");
    esp_ble_auth_req_t auth_req = ESP_LE_AUTH_REQ_SC_MITM_BOND; // Use Secure Connections, MITM protection (if possible), and Bonding
    esp_ble_io_cap_t iocap = ESP_IO_CAP_NONE; // No Input, No Output capability
    uint8_t key_size = 16;      // Minimum encryption key size = 7, Max=16
    uint8_t init_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK; // Which keys to distribute
    uint8_t rsp_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;  // Which keys to accept
    esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &rsp_key, sizeof(uint8_t));
    // --- END SECURITY INITIALIZATION ---

    // Register GAP callback 
    ret = esp_ble_gap_register_callback(esp_gap_cb);
    if (ret){ ESP_LOGE(GATTC_TAG, "GAP register error, error code = %x", ret); return; }
    ESP_LOGI(BLE_TAG, "GAP Callback Registered");

    // Register GATTC callback function
    ret = esp_ble_gattc_register_callback(esp_gattc_cb);
    if(ret){ ESP_LOGE(GATTC_TAG, "GATTC register error, error code = %x", ret); return; }
    ESP_LOGI(BLE_TAG, "GATTC Callback Registered");

    // Register GATTC Application Profile
    gl_profile_tab[PROFILE_A_APP_ID].gattc_cb = gattc_profile_event_handler; // Assign handler
    ret = esp_ble_gattc_app_register(PROFILE_A_APP_ID);
    if (ret){ ESP_LOGE(GATTC_TAG, "GATTC app register error, error code = %x", ret); return; }
    ESP_LOGI(BLE_TAG, "GATTC App Registered");

    // Set local MTU size
    ret = esp_ble_gatt_set_local_mtu(200);
    if (ret){ ESP_LOGE(GATTC_TAG, "Set local MTU failed, error code = %x", ret); }
    ESP_LOGI(BLE_TAG, "Local MTU Set");

    ESP_LOGI(BLE_TAG, "Configuring local privacy to trigger scan setup...");
    esp_ble_gap_config_local_privacy(true); // Trigger the sequence leading to scanning

    ESP_LOGI(BLE_TAG, "BLE Initialization Complete. Waiting for scan to start...");
}