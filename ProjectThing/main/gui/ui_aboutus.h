#pragma once

#include "esp_err.h"

esp_err_t ui_aboutus_start(void);
void ui_aboutus_show_screen(void);
void ui_aboutus_update_wifi_status(bool connected);
void ui_aboutus_update_cloud_status(bool connected);