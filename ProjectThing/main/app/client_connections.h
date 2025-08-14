#pragma once
#include "esp_log.h"
#include "esp_http_client.h"

esp_err_t wit_event_handler(esp_http_client_event_t *event);
esp_err_t send_audio_to_wit(const char *file_path);
void get_local_city_and_weather(void *param);
// Add this near the other function prototypes
void sync_time_with_ntp(void);


