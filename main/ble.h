#ifndef BLE
#define BLE

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_log.h"

void ble_init(void);
esp_err_t configure_ble5_advertising(void);
esp_err_t start_ble5_advertising(void);

#endif /* BLE */
