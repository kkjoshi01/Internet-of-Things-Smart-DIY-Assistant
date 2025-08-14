#pragma once
#include "esp_http_server.h"

void wifi_init(void);
// void scan_networks(char ssids[][33], int *networks);
bool is_wifi_connected(void);
esp_err_t wifiFormHandler(httpd_req_t *req);
esp_err_t connectHandler(httpd_req_t *req);
void start_web_server(void);
void start_dns(const char *ip);
void dns_task(void *param);

#define APP_WIFI_SSID "SmartESP"
#define APP_WIFI_PASS "dumbpassword"


