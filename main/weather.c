#include "weather.h"
#include <esp_http_client.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include <esp_log_buffer.h>

#define WEATHER_API_KEY "03ee26f075e8945928ceb0a0bfb00535"

esp_err_t weather_fetch_city(const char *city, update_screenf_callback_t update_screenf) {
    if (!city || !update_screenf) return ESP_ERR_INVALID_ARG;

    char url[256];
    // TODO: URL-encode `city` if needed.
    snprintf(url, sizeof(url),
             "http://api.openweathermap.org/data/2.5/weather?q=%s&units=metric&appid=%s",
             city, WEATHER_API_KEY);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return err;
    }

    int status = esp_http_client_get_status_code(client);
    esp_http_client_fetch_headers(client);

    char *buffer = (char *)calloc(1, 2048 + 1);
    if (!buffer) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }

    int total = 0;
    while (1) {
        int r = esp_http_client_read(client, buffer + total, 2048 - total);
        if (r <= 0) break;
        total += r;
        if (total >= 2048) break;
    }

    if (total > 0) {
        buffer[total] = '\0';
        ESP_LOG_BUFFER_CHAR("WEATHER_JSON", buffer, total);
        cJSON *root = cJSON_Parse(buffer);
        if (root) {
            cJSON *name = cJSON_GetObjectItem(root, "name");
            cJSON *main = cJSON_GetObjectItem(root, "main");
            cJSON *temp = main ? cJSON_GetObjectItem(main, "temp") : NULL;
            cJSON *hum  = main ? cJSON_GetObjectItem(main, "humidity") : NULL;

            cJSON *wind = cJSON_GetObjectItem(root, "wind");
            cJSON *wspd = wind ? cJSON_GetObjectItem(wind, "speed") : NULL;

            cJSON *weather = cJSON_GetObjectItem(root, "weather");
            cJSON *w0 = (weather && cJSON_IsArray(weather)) ? cJSON_GetArrayItem(weather, 0) : NULL;
            cJSON *desc = w0 ? cJSON_GetObjectItem(w0, "main") : NULL;

            char line[64];
            snprintf(line, sizeof(line),
                     "%s %.1fC H:%d%% W:%.1f %s",
                     (name && cJSON_IsString(name)) ? name->valuestring : "City",
                     (temp && cJSON_IsNumber(temp)) ? temp->valuedouble : 0.0,
                     (hum && cJSON_IsNumber(hum)) ? hum->valueint : 0,
                     (wspd && cJSON_IsNumber(wspd)) ? wspd->valuedouble : 0.0,
                     (desc && cJSON_IsString(desc)) ? desc->valuestring : "");

            update_screenf("%s", line);
            cJSON_Delete(root);
        } else {
            update_screenf("JSON parse error");
        }
    } else {
        update_screenf("HTTP %d no body", status);
    }

    free(buffer);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ESP_OK;
}