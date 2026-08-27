#include "main.h"
#include "esp_system.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "geolocation.h"
#include "u8g2.h"
#include "wifi.h"
#include <stdint.h>
#include "snake.h"

// Drawing functions
static void draw_wifi_info(void);
static void draw_time(void);
static void draw_geo(void);
static void draw_status_bar(void);
static void draw_wifi_bars(const int w, const int bars);

// Actions
static void action_games(void);
static void action_open_weather(void);
static void action_tnh(void);
static void action_time(void);
static void action_weather_mtl(void);
static void action_weather_nyc(void);
static void action_weather_lausanne(void);
static void action_geo(void);
static void action_open_settings(void);
static void action_wifi(void);
static void action_reset_wifi(void);
static void action_power(void);
static void action_shutdown(void);
static void action_restart(void);
static void action_flappybird(void);
static void action_snake(void);
static void action_minesweeper(void);

// Nav LIFO stack
static Screen s_nav_stack[MAX_NAV_DEPTH];
static int s_nav_top = -1; // -1 means empty stack

// Menu state model
static Screen current_screen = SCREEN_MAIN;
static const Menu* current_menu = NULL;

// Menu cursor. Which menu is selected?
static int main_selected = 0;
static int weather_selected = 0;
static int settings_selected = 0;
static int games_selected = 0;
static int power_selected = 0;

static bool sntp_started = false; // Time server
static GeoInfo geo_info = {0};
static bool s_last_wifi_connected = false;
static char s_last_bat_label[8] = "BAT?";
static int s_last_wifi_bars = -1;

static const MenuItem main_menu_items[] = {
    { "Games",       action_games },
    { "Weather",     action_open_weather },
    { "Time",        action_time },
    { "Settings",    action_open_settings },
    { "Power",       action_power }
};

static const MenuItem weather_menu_items[] = {
    { "Here",           action_tnh },
    { "Montreal",       action_weather_mtl },
    { "New York City",  action_weather_nyc },
    { "Lausanne",       action_weather_lausanne }
};

static const MenuItem settings_menu_items[] = {
    { "WiFi Info",        action_wifi },
    { "Reset WiFi",       action_reset_wifi },
    { "Geolocation",      action_geo }
};

static const MenuItem games_menu_items[] = {
    { "Flappy Bird",     action_flappybird },
    { "Snake",           action_snake },
    { "Minesweeper",     action_minesweeper }
};

static const MenuItem power_menu_items[] = {
    { "Shutdown",        action_shutdown },
    { "Restart",         action_restart  }
};

#define MAIN_MENU_COUNT (sizeof(main_menu_items) / sizeof(main_menu_items[0]))
#define WEATHER_MENU_COUNT (sizeof(weather_menu_items) / sizeof(weather_menu_items[0]))
#define SETTINGS_MENU_COUNT (sizeof(settings_menu_items) / sizeof(settings_menu_items[0]))
#define GAMES_MENU_COUNT (sizeof(games_menu_items) / sizeof(games_menu_items[0]))
#define POWER_MENU_COUNT (sizeof(power_menu_items) / sizeof(power_menu_items[0]))

static const Menu main_menu = { main_menu_items, MAIN_MENU_COUNT, &main_selected };
static const Menu weather_menu = { weather_menu_items, WEATHER_MENU_COUNT, &weather_selected };
static const Menu settings_menu = { settings_menu_items, SETTINGS_MENU_COUNT, &settings_selected };
static const Menu games_menu = { games_menu_items, GAMES_MENU_COUNT, &games_selected };
static const Menu power_menu = { power_menu_items, POWER_MENU_COUNT, &power_selected };

u8g2_t u8g2;

static void i2c_master_init(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = SDA_PIN,
        .scl_io_num = SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    i2c_param_config(I2C_MASTER_NUM, &conf);
    i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
}

static void u8g2_init(void) {    
    u8g2_esp32_hal_t u8g2_esp32_hal = U8G2_ESP32_HAL_DEFAULT;
    u8g2_esp32_hal.clk   = PIN_CLK;
    u8g2_esp32_hal.mosi  = PIN_MOSI;
    u8g2_esp32_hal.cs    = PIN_CS;
    u8g2_esp32_hal.dc    = PIN_DC;
    u8g2_esp32_hal.reset = PIN_RESET;

    u8g2_esp32_hal_init(u8g2_esp32_hal);

    u8g2_Setup_ssd1309_128x64_noname0_f(&u8g2, U8G2_R0, u8g2_esp32_spi_byte_cb, u8g2_esp32_gpio_and_delay_cb);
    u8g2_InitDisplay(&u8g2);
    u8g2_SetPowerSave(&u8g2, 0);
}

static void uart_init(void) {
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    //Install UART driver, and get the queue.
    uart_driver_install(UART_NUM, BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(UART_NUM, &uart_config);
    //Set UART pins (using UART0 default pins)
    uart_set_pin(UART_NUM, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

static void nvs_init(void){
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

static void update_screenf_font_v(const uint8_t* font, const char* fmt, va_list args) {
    char text[256];
    vsnprintf(text, sizeof(text), fmt, args);

    u8g2_ClearBuffer(&u8g2);
    draw_status_bar();
    u8g2_SetFont(&u8g2, font ? font : u8g2_font_ncenB08_tr);

    const int max_w = u8g2_GetDisplayWidth(&u8g2);
    const int line_h = (u8g2_GetAscent(&u8g2) - u8g2_GetDescent(&u8g2) + 2);
    int y = STATUS_BAR_H + u8g2_GetAscent(&u8g2) + 2;

    char line[64] = {0};
    char word[32] = {0};
    const char* p = text;

    while (*p) {
        if (*p == '\n') {
            if (line[0]) {
                u8g2_DrawStr(&u8g2, 0, y, line);
                y += line_h;
                line[0] = '\0';
            } else {
                y += line_h; // blank line
            }
            p++;
            continue;
        }

        while (*p == ' ') p++;

        int wi = 0;
        while (*p && *p != ' ' && *p != '\n' && wi < (int)sizeof(word) - 1) {
            word[wi++] = *p++;
        }
        word[wi] = '\0';
        if (word[0] == '\0') break;

        char trial[64];
        if (line[0]) {
            strlcpy(trial, line, sizeof(trial));
            strlcat(trial, " ", sizeof(trial));
            strlcat(trial, word, sizeof(trial));
        } else {
            strlcpy(trial, word, sizeof(trial));
        }

        if (u8g2_GetStrWidth(&u8g2, trial) > max_w) {
            if (line[0]) {
                u8g2_DrawStr(&u8g2, 0, y, line);
                y += line_h;
                snprintf(line, sizeof(line), "%s", word);
            } else {
                u8g2_DrawStr(&u8g2, 0, y, word);
                y += line_h;
                line[0] = '\0';
            }
        } else {
            snprintf(line, sizeof(line), "%s", trial);
        }
    }

    if (line[0]) {
        u8g2_DrawStr(&u8g2, 0, y, line);
    }

    u8g2_SendBuffer(&u8g2);
}

void update_screenf_font(const uint8_t* font, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    update_screenf_font_v(font, fmt, args);
    va_end(args);
}

void update_screenf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    update_screenf_font_v(u8g2_font_ncenB08_tr, fmt, args);
    va_end(args);
}

static void weather_ui_update(const WeatherInfo* w) {
    u8g2_ClearBuffer(&u8g2);
    draw_status_bar();
    u8g2_SetFont(&u8g2, u8g2_font_ncenB08_tr);

    const int ascent = u8g2_GetAscent(&u8g2);
    const int line_h = (u8g2_GetAscent(&u8g2) - u8g2_GetDescent(&u8g2)) + 2;

    int y = STATUS_BAR_H + ascent;

    if (!w || !w->ok) {
        u8g2_DrawStr(&u8g2, 0, y, "Weather error"); y += line_h;
        u8g2_DrawStr(&u8g2, 0, y, (w && w->err[0]) ? w->err : "No details");
        u8g2_SendBuffer(&u8g2);
        return;
    }

    char msg[128];
    int n = snprintf(msg, sizeof(msg),
        "T:%dC F:%dC H:%u%%\nMin:%dC Max:%dC\nW:%u KM/H\n%s",
        w->temp_c, w->feels_c,
        w->hum_pct,
        w->tmin_c, w->tmax_c,
        w->wind_kmh,
        w->desc);
    ESP_LOGI("temp", "%d", n);
    if (n >= (int)sizeof(msg)) {
        // truncated (still safe)
    }
    update_screenf("%s", msg);
}

static void draw_wifi_info(void) {
    wifi_ap_record_t ap_info = {0};
    esp_err_t ap_ret = esp_wifi_sta_get_ap_info(&ap_info);

    esp_netif_ip_info_t ip_info = {0};
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif) {
        esp_netif_get_ip_info(netif, &ip_info);
    }

    char msg[192];

    if (ap_ret == ESP_OK) {
        snprintf(msg, sizeof(msg),
                 "WiFi CONNECTED\nSSID: %s\nRSSI: %d dBm\nIP: " IPSTR,
                 (char*)ap_info.ssid,
                 ap_info.rssi,
                 IP2STR(&ip_info.ip));
    } else {
        snprintf(msg, sizeof(msg), "WiFi not connected");
    }
    update_screenf("%s", msg);
}

static void draw_geo(void) {
    if (!geo_info.ok) {
        update_screenf("Geo: %s", geo_info.message[0] ? geo_info.message : "not ready");
        return;
    }
    update_screenf("Geo\n%s, %s\n%s\nUTC%+ld",
                   geo_info.city, geo_info.region,
                   geo_info.countryCode,
                   geo_info.offset_sec / 3600);
}

static void draw_time(void) {
    struct tm timeinfo = {0};
    if (!wifi_is_connected()) return; // Guard, not really needed.

    if (!sntp_started) {
        const char* ntpServer = "pool.ntp.org";
        esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, ntpServer);
        esp_sntp_init();
        sntp_started = true;
    }

    time_t n = time(NULL);
    if (n < 1609459200) {
        update_screenf("Time: syncing...");
        return;
    }

    time_t now = n + geo_info.offset_sec;
    gmtime_r(&now, &timeinfo);

    char time_msg[64] = {0};
    strftime(time_msg, sizeof(time_msg), "%A, %B %d %Y %H:%M:%S", &timeinfo);
    update_screenf("%s", time_msg);
}

// Generic menu draw helper
static void draw_menu(const Menu* menu) {
    u8g2_ClearBuffer(&u8g2);
    draw_status_bar();
    u8g2_SetFont(&u8g2, u8g2_font_ncenB08_tr);

    const int row_h = 12;
    const int top_y = STATUS_BAR_H + 10; // The bottom of the box.
    const int max_rows = (u8g2_GetDisplayHeight(&u8g2) - STATUS_BAR_H) / row_h;
    const int sel = *(menu->selected);

    int start = 0;
    if (sel >= max_rows) {
        start = sel - (max_rows - 1);
    }
    if (start < 0) start = 0;

    for (int i = 0; i < max_rows; i++) {
        int idx = start + i;
        if (idx >= menu->count) break;
        int row_y = top_y + row_h * i;

        if (idx == sel) {
            char line[32];
            snprintf(line, sizeof(line), "> %s", menu->items[idx].label);
            u8g2_DrawStr(&u8g2, 0, row_y, line);
        } else {
            u8g2_DrawStr(&u8g2, 10, row_y, menu->items[idx].label);
        }
    }
    u8g2_SendBuffer(&u8g2);
}

static Key decode_key(uint8_t b) {
    static int esc_state = 0;

    if (esc_state == 0) {
        if (b == 0x1B) { esc_state = 1; return KEY_NONE; }
        if (b == '\r' || b == '\n') return KEY_ENTER;
        
        // WASD keys (instant response, great for games)
        if (b == 'w' || b == 'W') return KEY_UP;
        if (b == 's' || b == 'S') return KEY_DOWN;
        if (b == 'a' || b == 'A') return KEY_LEFT;
        if (b == 'd' || b == 'D') return KEY_RIGHT;

        return KEY_NONE;
    }

    if (esc_state == 1) {
        if (b == '[') { esc_state = 2; return KEY_NONE; }
        esc_state = 0;
        return KEY_ESC;
    }

    // esc_state == 2 (Arrow key final byte)
    esc_state = 0;
    if (b == 'A') return KEY_UP;
    if (b == 'B') return KEY_DOWN;
    if (b == 'C') return KEY_RIGHT;
    if (b == 'D') return KEY_LEFT;

    return KEY_NONE;
}

// Handling input
static void handle_input(const uint8_t* data, int len) {
    for (int i = 0; i < len; i++) {
        Key k = decode_key(data[i]);
        if (k == KEY_NONE) continue;

        // -- Global keys -- (Can cleanup is_game_screen helper function)
        if (k == KEY_LEFT && 
            current_screen != SCREEN_SNAKE && 
            current_screen != SCREEN_MINESWEEPER && 
            current_screen != SCREEN_FLAPPYBIRD) {
            nav_pop();
            continue;
        }
        if (k == KEY_ESC){
            nav_reset();
            continue;
        }

        // -- Menu-only keys --
        if (current_menu) {
            if (k == KEY_UP && *(current_menu->selected) > 0) {
                (*(current_menu->selected))--;
                draw_menu(current_menu);
            } else if (k == KEY_DOWN && *(current_menu->selected) < (current_menu->count - 1)) {
                (*(current_menu->selected))++;
                draw_menu(current_menu);
            } else if (k == KEY_ENTER || k == KEY_RIGHT) {
                MenuAction action = current_menu->items[*(current_menu->selected)].action;
                if (action) action();
            }
        }

        if (current_screen == SCREEN_SNAKE){
            snake_handle_input(k);
        }
    }
}

static void set_screen(Screen s) {
    current_screen = s;
    switch (s) {
        case SCREEN_MAIN:
            current_menu = &main_menu;
            draw_menu(current_menu);
            break;
        case SCREEN_SETTINGS:
            current_menu = &settings_menu;
            draw_menu(current_menu);
            break;
        case SCREEN_WEATHER:
            current_menu = &weather_menu;
            draw_menu(current_menu);
            break;
        case SCREEN_GEO:
            current_menu = NULL;
            draw_geo();
            break;
        case SCREEN_GAMES:
            current_menu = &games_menu;
            draw_menu(current_menu);
            break;
        case SCREEN_POWER:
            current_menu = &power_menu;
            draw_menu(current_menu);
            break;
        default:
            current_menu = NULL;
            break;
    }
}

void nav_push(Screen next){
    if (s_nav_top < MAX_NAV_DEPTH - 1){
        s_nav_top++;
        s_nav_stack[s_nav_top] = next;
        set_screen(next);
    } else {
        ESP_LOGW("NAV", "Stack overflow! Cannot push to screen %d", next);
    }
}

void nav_pop(void){
    if (s_nav_top > 0){
        s_nav_top--;
        set_screen(s_nav_stack[s_nav_top]);
    }
}

void nav_reset(void){
    s_nav_top = 0;
    s_nav_stack[0] = SCREEN_MAIN;
    set_screen(SCREEN_MAIN);
}

static void get_battery_label(char* out, size_t out_sz) {
    // Placeholder until you have real battery data
    snprintf(out, out_sz, "BAT?");
}

static int get_wifi_bars(void) {
    if (!wifi_is_connected()) return 0;

    wifi_ap_record_t ap_info = {0};
    if (esp_wifi_sta_get_ap_info(&ap_info) != ESP_OK) {
        return 0;
    }

    int rssi = ap_info.rssi;
    if (rssi >= -67) return 3;
    if (rssi >= -75) return 2;
    if (rssi >= -85) return 1;
    return 0;
}

static void draw_wifi_bars(const int w, const int bars) {
    const int icon_w = (3 * 3) + (2 * 1) + 2; // 3 bars, bar_w=3, gap=1, offset=2
    const int x = w - icon_w;
    const int y_bottom = STATUS_BAR_H - 2;

    const int bar_w = 3;
    const int gap = 1;
    const int heights[3] = {3, 5, 7};

    for (int i = 0; i < 3; i++) {
        int h = heights[i];
        int bx = x + i * (bar_w + gap);
        int by = y_bottom - h;

        if (bars > i) {
            u8g2_DrawBox(&u8g2, bx, by, bar_w, h);
        } else {
            u8g2_DrawFrame(&u8g2, bx, by, bar_w, h);
        }
    }
}

static void status_bar_update_if_changed(void) {
    char bat[8];
    get_battery_label(bat, sizeof(bat));
    const bool wifi = wifi_is_connected();
    const int bars = get_wifi_bars();

    if (wifi == s_last_wifi_connected &&
        bars == s_last_wifi_bars &&
        strcmp(bat, s_last_bat_label) == 0) {
        return; // no change, no redraw
    }

    draw_status_bar();
    u8g2_SendBuffer(&u8g2);
}

static void draw_status_bar(void) {
    const int w = u8g2_GetDisplayWidth(&u8g2);
    const int bars = get_wifi_bars();

    u8g2_SetDrawColor(&u8g2, 0);
    u8g2_DrawBox(&u8g2, 0, 0, w, STATUS_BAR_H);
    u8g2_SetDrawColor(&u8g2, 1);

    u8g2_SetFont(&u8g2, u8g2_font_5x8_tr);
    u8g2_DrawHLine(&u8g2, 0, STATUS_BAR_H - 1, w);

    char bat[8];
    get_battery_label(bat, sizeof(bat));
    u8g2_DrawStr(&u8g2, 0, 8, bat);

    // Right: WiFi bars
    draw_wifi_bars(w, bars);

    s_last_wifi_connected = wifi_is_connected();
    s_last_wifi_bars = bars;
    strlcpy(s_last_bat_label, bat, sizeof(s_last_bat_label));
}

static void wifi_connect_task(void *pvParameters) {
    esp_err_t status = connect_wifi();
    if (status == WIFI_SUCCESS) {
        if (current_screen == SCREEN_WIFI) {
            draw_wifi_info();
        }
    } else {
        if (current_screen == SCREEN_WIFI) {
            update_screenf("WiFi connection failed");
        }
    }
    status_bar_update_if_changed();
    vTaskDelete(NULL);
}

static void weather_fetch_task(void *pvParameters) {
    const char* city = (const char*)pvParameters;
    weather_fetch_city(city, weather_ui_update);
    vTaskDelete(NULL);
}

static void geo_fetch_task(void *pvParameters) {
    geo_fetch_info(&geo_info);
    if (current_screen == SCREEN_GEO){
        draw_geo();
    }
    vTaskDelete(NULL);
}

static void action_flappybird(void) {nav_push(SCREEN_FLAPPYBIRD); }
static void action_snake(void) { nav_push(SCREEN_SNAKE); snake_init(); }
static void action_minesweeper(void) { nav_push(SCREEN_MINESWEEPER); }
static void action_games(void) { nav_push(SCREEN_GAMES); }
static void action_open_weather(void) { nav_push(SCREEN_WEATHER); }
static void action_tnh(void) { nav_push(SCREEN_TNH); }
static void action_open_settings(void) { nav_push(SCREEN_SETTINGS); }
static void action_power(void) { nav_push(SCREEN_POWER); }
static void action_shutdown(void) { 
    update_screenf("Shutting down...");
    vTaskDelay(pdMS_TO_TICKS(500));

    u8g2_ClearBuffer(&u8g2);
    u8g2_SendBuffer(&u8g2);
    u8g2_SetPowerSave(&u8g2, 1); // 1 = Enable power save

    esp_deep_sleep_start();
}
static void action_restart(void) {
    update_screenf("Restarting...");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}
static void action_time(void) { 
    nav_push(SCREEN_TIME); 
    if (!wifi_is_connected()) { 
        update_screenf("WiFi required"); 
        return;
    } 
    draw_time();
}
static void action_geo(void) { 
    nav_push(SCREEN_GEO);
    if (!wifi_is_connected()){
        update_screenf("Geo: WiFi required");
        return;
    }

    if (!geo_info.ok){
        update_screenf("Loading Geo...");
        xTaskCreate(geo_fetch_task, "geo_task", 4096, NULL, 5, NULL);
    } else {
        draw_geo(); // Already in ram, display immediatly
    }
}
static void action_wifi(void) {
    nav_push(SCREEN_WIFI);
    if (!wifi_is_connected()) {
        update_screenf("WiFi: connecting...");
        xTaskCreate(wifi_connect_task, "wifi_task", 4096, NULL, 5, NULL);
    } else {
        draw_wifi_info();
    }
}
static void action_weather_mtl(void) {
    nav_push(SCREEN_WEATHER_MTL);
    if (!wifi_is_connected()) {
        update_screenf("WiFi connection failed");
        return;
    }
    
    update_screenf("Loading weather...");
    xTaskCreate(weather_fetch_task, "weather_task_mtl", 4096, "Montreal", 5, NULL);
}
static void action_weather_nyc(void) {
    nav_push(SCREEN_WEATHER_NYC);
    if(!wifi_is_connected()) {
        update_screenf("WiFi connection failed");
        return;
    }

    update_screenf("Loading weather...");
    xTaskCreate(weather_fetch_task, "weather_task_nyc", 4096, "Manhattan", 5, NULL);
}
static void action_weather_lausanne(void) {
    nav_push(SCREEN_WEATHER_LAUSANNE);
    if(!wifi_is_connected()) {
        update_screenf("WiFi connection failed");
        return;
    }

    update_screenf("Loading weather...");
    xTaskCreate(weather_fetch_task, "weather_task_lausanne", 4096, "Lausanne", 5, NULL);
}
static void action_reset_wifi(void){
    update_screenf("Resetting Wi-Fi...\nRestarting device");
    wifi_reset_provisioning();
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
}

// static void log_mem_usage(void) {
//     // Heap
//     size_t free_heap = esp_get_free_heap_size();
//     size_t min_free_heap = esp_get_minimum_free_heap_size();
//     size_t free_8bit = heap_caps_get_free_size(MALLOC_CAP_8BIT);
//     size_t min_free_8bit = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
//
//     ESP_LOGI("mem", "heap free=%u min=%u, 8bit free=%u min=%u",
//              (unsigned)free_heap, (unsigned)min_free_heap,
//              (unsigned)free_8bit, (unsigned)min_free_8bit);
//
//     // Stack (current task)
//     UBaseType_t words = uxTaskGetStackHighWaterMark(NULL);
//     ESP_LOGI("mem", "stack high-water: %u bytes", (unsigned)(words * sizeof(StackType_t)));
// }

// Main app
void app_main(void) {
    i2c_master_init();
    u8g2_init();
    uart_init();
    nvs_init();
    nav_reset(); // Start on main menu
    wifi_init();

    uint8_t* data = (uint8_t*) malloc(BUF_SIZE);
    bool running = true;
    static uint32_t last_redraw = 0;

    while (running) {

        // Eventually
        // int len = uart_read_bytes(UART_NUM, data, BUF_SIZE - 1, pdMS_TO_TICKS(20));

        int len = read(STDIN_FILENO, data, BUF_SIZE - 1);
        if (len > 0) {
            data[len] = '\0';
            handle_input(data, len);
        }

        // Throttle live screen redraws to every 500ms so we don't spam SPI
        uint32_t now = xTaskGetTickCount();
        if (now - last_redraw >= pdMS_TO_TICKS(500)) {
            last_redraw = now;
            if (current_screen == SCREEN_TNH)  draw_dht20();
            if (current_screen == SCREEN_WIFI) draw_wifi_info();
            if (current_screen == SCREEN_TIME) draw_time();
        }
        if (current_screen == SCREEN_SNAKE) { snake_draw(); snake_update(); }
    
    vTaskDelay(pdMS_TO_TICKS(10));
    }
    free(data);
}