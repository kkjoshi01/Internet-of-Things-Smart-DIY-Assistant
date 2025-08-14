#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

void ui_acquire(void);
void ui_release(void);
esp_err_t ui_main_start(void);
void ui_main_show_screen(void);
void ui_main_update_weather(const char* temperature, const char* city);
void ui_main_update_wifi_status(bool connected);
void ui_main_update_cloud_status(bool connected);
bool ui_main_is_wifi_connected(void);
void ui_main_set_time_synced(bool synced);

