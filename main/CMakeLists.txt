idf_component_register(SRCS "main.c" "wifi.c" "weather.c" "ble.c" "dht20.c"
                    INCLUDE_DIRS "." "${IDF_PATH}/components/bt/include/esp32c6/include/"
                                     "${IDF_PATH}/components/bt/host/nimble/nimble/nimble/include/"
                                     "${IDF_PATH}/components/bt/porting/npl/freertos/include/"
                   REQUIRES driver
                             esp_driver_spi
                             u8g2 
                             u8g2-hal-esp-idf 
                             esp_wifi
                             nvs_flash
                             bt
                             esp_http_client
                             cjson
                             )