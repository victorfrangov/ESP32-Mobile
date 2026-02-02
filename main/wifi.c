#include "wifi.h"

static uint8_t tries = 0;
static EventGroupHandle_t wifi_event_group;

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
static void ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data){
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START){
        ESP_LOGI(WIFI_TAG, "Connecting to AP...");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED){
        if (tries < MAX_FAILURES){
            ESP_LOGI(WIFI_TAG, "Reconnecting to AP...");
            esp_wifi_connect();
            tries++;
        } else {
            xEventGroupSetBits(wifi_event_group, WIFI_FAILURE);
        }
    }
}

//event handler for ip events
static void ip_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data){
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP){
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(WIFI_TAG, "STA IP: " IPSTR, IP2STR(&event->ip_info.ip));
        tries = 0;
        xEventGroupSetBits(wifi_event_group, WIFI_SUCCESS);
    }
}

//use ret and esp_loge to gracefeully handle errors, wifi errors are not fatal.
esp_err_t connect_wifi(void){
	int status = WIFI_FAILURE;

	/** INITIALIZE ALL THE THINGS **/
	//initialize the esp network interface
	ESP_ERROR_CHECK(esp_netif_init());

	//initialize default esp event loop
	ESP_ERROR_CHECK(esp_event_loop_create_default());

	//create wifi station in the wifi driver
	esp_netif_create_default_wifi_sta();

	//setup wifi station with the default wifi configuration
	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /** EVENT LOOP **/
	wifi_event_group = xEventGroupCreate();

    esp_event_handler_instance_t wifi_handler_event_instance;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &wifi_handler_event_instance));

    esp_event_handler_instance_t got_ip_event_instance;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &ip_event_handler,
                                                        NULL,
                                                        &got_ip_event_instance));

    /** START THE WIFI DRIVER **/
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
	     .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };

    esp_wifi_set_ps(WIFI_PS_NONE);

    // set the wifi controller to be a station
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    // set the wifi config
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    // set the bandwidth to HT40
    ESP_ERROR_CHECK(esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT40));

    // start the wifi driver
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(WIFI_TAG, "STA initialization complete");

    /** NOW WE WAIT **/
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
            WIFI_SUCCESS | WIFI_FAILURE,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_SUCCESS) {
        ESP_LOGI(WIFI_TAG, "Connected to ap");

        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            ESP_LOGI("WIFI", "RSSI: %d dBm", ap_info.rssi);
        }

        status = WIFI_SUCCESS;
    } else if (bits & WIFI_FAILURE) {
        ESP_LOGI(WIFI_TAG, "Failed to connect to ap");
        status = WIFI_FAILURE;
    } else {
        ESP_LOGE(WIFI_TAG, "UNEXPECTED EVENT");
        status = WIFI_FAILURE;
    }

    /* The event will not be processed after unregister */
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, got_ip_event_instance));
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_handler_event_instance));
    vEventGroupDelete(wifi_event_group);

    return status;
}

// esp_err_t connect_tcp_server(update_screenf_callback_t update_screenf){
// 	struct sockaddr_in serverInfo = {0};

//     serverInfo.sin_family = AF_INET;
//     serverInfo.sin_addr.s_addr = inet_addr("192.168.1.249");
//     serverInfo.sin_port = htons(12345);

//     int sock = socket(AF_INET, SOCK_STREAM, 0);
//     if (sock < 0) {
//         ESP_LOGE(WIFI_TAG, "Failed to create a socket");
//         return TCP_FAILURE;
//     }

//     if (connect(sock, (struct sockaddr *)&serverInfo, sizeof(serverInfo)) != 0) {
//         ESP_LOGE(WIFI_TAG, "Failed to connect to %s!", inet_ntoa(serverInfo.sin_addr.s_addr));
//         close(sock);
//         return TCP_FAILURE;
//     }

//     ESP_LOGI(WIFI_TAG, "Connected to TCP server.");
//     handle_server_data(sock, update_screenf);

//     close(sock);
//     return TCP_SUCCESS;
// }

// static void handle_server_data(int sock, update_screenf_callback_t update_screenf){
//     static char readBuffer[1440] = {0}; // Match the chunk size
//     uint32_t total_bytes = 0;
//     uint32_t start_time = xTaskGetTickCount();

//     while (1) {
//         bzero(readBuffer, sizeof(readBuffer));
//         int r = read(sock, readBuffer, sizeof(readBuffer));
//         if (r > 0) {
//             total_bytes += r;
//             ESP_LOGI(WIFI_TAG, "Received %d bytes from server", r);

//             // Send AAC data to the decoder (e.g., VS1053 or software decoder)
//             // Example: vs1053_send_data(readBuffer, r);

//             if (update_screenf) {
//                 update_screenf(&total_bytes);
//             }
//         } else if (r == 0) {
//             ESP_LOGI(WIFI_TAG, "Server closed connection");
//             break;
//         } else {
//             ESP_LOGE(WIFI_TAG, "Failed to read from server");
//             break;
//         }
//     }

//     // Calculate and log the final average download speed
//     uint32_t total_time = (xTaskGetTickCount() - start_time) * portTICK_PERIOD_MS; // Total time in milliseconds
//     unsigned int avg_speed_kbps;
//     if (total_time > 0) {
//         avg_speed_kbps = (total_bytes * 8.0) / total_time; // Average speed in kbps
//         ESP_LOGI(WIFI_TAG, "Average Download Speed: %u kbps. Total time: %u", avg_speed_kbps, (unsigned int)total_time / 1000);
//     } else {
//         ESP_LOGI(WIFI_TAG, "No data received or transfer time too short to calculate speed.");
//     }

//     ESP_LOGI(WIFI_TAG, "Total Bytes Received: %u", (unsigned int)total_bytes);
//     update_screenf(&avg_speed_kbps);
// }

