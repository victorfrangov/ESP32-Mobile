#ifndef WEATHER
#define WEATHER

#include "wifi.h"

//define the callback here instead of in wifi.h
esp_err_t weather_fetch_city(const char *city, update_screenf_callback_t update_screenf);

#endif /* WEATHER */
