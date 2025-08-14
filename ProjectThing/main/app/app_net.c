// #include "wifi_web.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include <lwip/sockets.h>
#include <sys/time.h>
#include <string.h>
#include <ctype.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ui_main.h"
#include "client_connections.h"
#include "esp_sntp.h"
#include "audio_manager.h"

#define APP_WIFI_SSID "SmartESP"
#define APP_WIFI_PASS "dumbpassword"

static const char *TAG = "WIFI";
static httpd_handle_t server = NULL;
static uint32_t last_reply = 0;
static uint32_t last_client = 0;

// Connection status tracking
static bool wifi_connected = false;
static bool connection_in_progress = false;

void disconnectedNoise(void *param) {
    // Play a disconnected noise
    play_audio("/spiffs/wifi_disconnected.wav");
    vTaskDelete(NULL);
}

// WiFi event handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi station started");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (connection_in_progress) {
            ESP_LOGI(TAG, "Connection failed, retrying...");
            esp_wifi_connect();
        } else {
            xTaskCreate(disconnectedNoise, "disconnected_noise", 4096, NULL, 5, NULL);
            ESP_LOGI(TAG, "WiFi disconnected");
            wifi_connected = false;
            ui_main_update_wifi_status(false);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Connected! Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        wifi_connected = true;
        connection_in_progress = false;
        sync_time_with_ntp();

        
        // Delay before switching modes to allow web response to complete
        vTaskDelay(pdMS_TO_TICKS(2000));
        
        // Turn off AP mode since we're now connected to internet
        esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to switch to STA mode: %s", esp_err_to_name(err));
            return;
        }
        ESP_LOGI(TAG, "Switched to STA mode only");

        ui_main_update_wifi_status(true);
        xTaskCreate(get_local_city_and_weather, "get_local_city_and_weather", 4096, NULL, 5, NULL);
    }
}

// Function to check if WiFi is connected (for use in other file)
bool is_wifi_connected() {
    return wifi_connected;
}

// URL decode function
void url_decode(char *dst, const char *src) {
    char a, b;
    while (*src) {
        if ((*src == '%') &&
            ((a = src[1]) && (b = src[2])) &&
            (isxdigit(a) && isxdigit(b))) {
            if (a >= 'a')
                a -= 'a'-'A';
            if (a >= 'A')
                a -= ('A' - 10);
            else
                a -= '0';
            if (b >= 'a')
                b -= 'a'-'A';
            if (b >= 'A')
                b -= ('A' - 10);
            else
                b -= '0';
            *dst++ = 16*a+b;
            src+=3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

void getLocalTime(void) {
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();
    
}

// void scan_networks(char ssids[][33], int *networks) {
//     uint16_t ap_count = 0;
//     wifi_scan_config_t scan_config = {0};
//     ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true));

//     ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));

//     if (ap_count == 0) {
//         ESP_LOGE(TAG, "No networks found");
//         *networks = 0;
//         return;
//     }

//     wifi_ap_record_t *ap_records = calloc(ap_count, sizeof(wifi_ap_record_t));
//     if (!ap_records) {
//         ESP_LOGE(TAG, "Failed to allocate memory for AP records");
//         *networks = 0;
//         return;
//     }
//     esp_err_t check = esp_wifi_scan_get_ap_records(&ap_count, ap_records);
//     if (check != ESP_OK) {
//         ESP_LOGE(TAG, "Failed to get AP records: %s", esp_err_to_name(check));
//         free(ap_records);
//         *networks = 0;
//         return;
//     }

    
//     *networks = 0;
//     for (int i = 0; i < ap_count && i<16; i++) {
//         strncpy(ssids[i], (char *)ap_records[i].ssid, 32);
//         ssids[i][32] = '\0'; // Ensure null termination
//         (*networks)++;
//     }

//     free(ap_records);
//     ESP_LOGI(TAG, "Found %d networks", *networks);
// }

static esp_err_t wifiFormHandler(httpd_req_t *req) {
    const char *response = 
        "<html>"
        "<head><title>WiFi Setup</title></head>"
        "<body style='font-family: Arial, sans-serif; max-width: 400px; margin: 50px auto; padding: 20px;'>"
        "<h1>WiFi Configuration</h1>"
        "<form method='POST' action='/connect'>"
        "<div style='margin: 20px 0;'>"
        "<label for='ssid' style='display: block; margin-bottom: 5px;'>SSID:</label>"
        "<input type='text' id='ssid' name='ssid' required style='width: 100%; padding: 8px; box-sizing: border-box;'>"
        "</div>"
        "<div style='margin: 20px 0;'>"
        "<label for='password' style='display: block; margin-bottom: 5px;'>Password:</label>"
        "<input type='password' id='password' name='password' required style='width: 100%; padding: 8px; box-sizing: border-box;'>"
        "</div>"
        "<input type='submit' value='Connect' style='background-color: #4CAF50; color: white; padding: 10px 20px; border: none; cursor: pointer; width: 100%;'>"
        "</form>"
        "</body>"
        "</html>";

    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t connectHandler(httpd_req_t *req) {
    // Allocate buffers on heap instead of stack to prevent overflow
    char *buf = malloc(512);
    char *ssid = malloc(33);
    char *password = malloc(65);
    char *ssid_encoded = malloc(100);
    char *password_encoded = malloc(100);
    char *response_buf = malloc(1200);
    
    if (!buf || !ssid || !password || !ssid_encoded || !password_encoded || !response_buf) {
        ESP_LOGE(TAG, "Failed to allocate memory for connection handler");
        // Clean up any successful allocations
        if (buf) free(buf);
        if (ssid) free(ssid);
        if (password) free(password);
        if (ssid_encoded) free(ssid_encoded);
        if (password_encoded) free(password_encoded);
        if (response_buf) free(response_buf);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
        return ESP_FAIL;
    }
    
    // Initialize buffers
    memset(buf, 0, 512);
    memset(ssid, 0, 33);
    memset(password, 0, 65);
    memset(ssid_encoded, 0, 100);
    memset(password_encoded, 0, 100);
    
    int ret = httpd_req_recv(req, buf, 511);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
        goto cleanup;
    }
    buf[ret] = 0;

    ESP_LOGI(TAG, "Received form data: %s", buf);

    // Parse the form data
    char *ssid_start = strstr(buf, "ssid=");
    char *password_start = strstr(buf, "password=");
    
    if (!ssid_start) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing SSID");
        goto cleanup;
    }

    // Extract SSID
    ssid_start += 5; // Skip "ssid="
    char *ssid_end = strchr(ssid_start, '&');
    if (ssid_end) {
        int ssid_len = ssid_end - ssid_start;
        if (ssid_len > 99) ssid_len = 99;
        strncpy(ssid_encoded, ssid_start, ssid_len);
        ssid_encoded[ssid_len] = '\0';
    } else {
        strncpy(ssid_encoded, ssid_start, 99);
        ssid_encoded[99] = '\0';
    }

    // Extract Password (if present)
    if (password_start) {
        password_start += 9; // Skip "password="
        strncpy(password_encoded, password_start, 99);
        password_encoded[99] = '\0';
    }

    // URL decode both SSID and password
    url_decode(ssid, ssid_encoded);
    url_decode(password, password_encoded);

    ESP_LOGI(TAG, "Attempting to connect to SSID: %s", ssid);

    if (strlen(ssid) == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID cannot be empty");
        goto cleanup;
    }

    // Send response FIRST before starting connection
    const char *response_template = 
        "<html>"
        "<head>"
        "<title>Connecting...</title>"
        "<meta http-equiv='refresh' content='10;url=/'>"
        "</head>"
        "<body style='font-family: Arial, sans-serif; max-width: 400px; margin: 50px auto; padding: 20px; text-align: center;'>"
        "<h1>Connecting to WiFi...</h1>"
        "<p>Attempting to connect to: <strong>%s</strong></p>"
        "<p>Please wait while the device connects to the network.</p>"
        "<p>This page will refresh in 10 seconds.</p>"
        "<p>If connection is successful, the access point will be disabled.</p>"
        "</body>"
        "</html>";

    snprintf(response_buf, 1199, response_template, ssid);
    
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    httpd_resp_send(req, response_buf, HTTPD_RESP_USE_STRLEN);

    // Now configure and start WiFi connection
    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);

    connection_in_progress = true;
    wifi_connected = false;

    // Set config and connect
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set WiFi config: %s", esp_err_to_name(err));
        goto cleanup;
    }
    
    err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFi connection: %s", esp_err_to_name(err));
        connection_in_progress = false;
    }

cleanup:
    // Free all allocated memory
    free(buf);
    free(ssid);
    free(password);
    free(ssid_encoded);
    free(password_encoded);
    free(response_buf);
    
    return ESP_OK;
}

static esp_err_t redirect(httpd_req_t *req) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, "Redirecting", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

void start_web_server(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    
    // Increase stack size for HTTP server task to prevent overflow
    config.stack_size = 8192;  // Increase from default 4096 to 8192
    config.task_priority = 5;  // Lower priority to give more time to other tasks
    config.max_uri_handlers = 8;  // Reduce if not needed
    config.max_open_sockets = 4;  // Reduce concurrent connections
    config.uri_match_fn = httpd_uri_match_wildcard; // Use wildcard matching for URIs
    
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t form_uri = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = wifiFormHandler,
        };
        httpd_register_uri_handler(server, &form_uri);

        httpd_uri_t connect_uri = {
            .uri = "/connect",
            .method = HTTP_POST,
            .handler = connectHandler,
        };
        httpd_register_uri_handler(server, &connect_uri);

        httpd_uri_t uri_android = {
            .uri = "/generate_204",
            .method = HTTP_GET,
            .handler = wifiFormHandler,
        };
        httpd_register_uri_handler(server, &uri_android);

        httpd_uri_t uri_ios = {
            .uri = "/hotspot-detect.html",
            .method = HTTP_GET,
            .handler = wifiFormHandler,
        };
        httpd_register_uri_handler(server, &uri_ios);

        httpd_uri_t catchall_post = { // Catching all annoying VPNs
            .uri = "/*",
            .method = HTTP_POST,
            .handler = redirect,
        };
        httpd_register_uri_handler(server, &catchall_post);

        httpd_uri_t catchall_get = { // For those annoying VPNs etc
            .uri = "/*",
            .method = HTTP_GET,
            .handler = redirect,
        };
        httpd_register_uri_handler(server, &catchall_get);
        
        ESP_LOGI(TAG, "Web server started on %s", APP_WIFI_SSID);
    }
}

void wifi_init(void) {
    
    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {0};
    strcpy((char *) wifi_config.ap.ssid, APP_WIFI_SSID);
    strcpy((char *) wifi_config.ap.password, APP_WIFI_PASS);
    wifi_config.ap.ssid_len = strlen(APP_WIFI_SSID);
    wifi_config.ap.max_connection = 4;
    wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.ap.channel = 1;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "WiFi started: %s", APP_WIFI_SSID);
}




// void start_dns(const char *ip) {
//     int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
//     struct sockaddr_in server_addr, client_addr;

//     server_addr.sin_family = AF_INET;
//     server_addr.sin_port = htons(53);
//     server_addr.sin_addr.s_addr = INADDR_ANY;

//     if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
//         ESP_LOGE(TAG, "Failed to bind DNS socket");
//         close(sock);
//         return;
//     }
//     ESP_LOGI(TAG, "DNS server started on port 53");

//     char buffer[512];
//     while (true){
//         socklen_t addr_len = sizeof(client_addr);
//         int len = recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr *)&client_addr, &addr_len);
//         if (len < 0) {
//             ESP_LOGE(TAG, "Failed to receive DNS request");
//             continue;
//         }

//         uint32_t current_time = esp_timer_get_time() / 1000; // Convert to milliseconds
//         uint32_t current_ip = client_addr.sin_addr.s_addr;

//         if (current_time - last_reply < 2000 && current_ip == last_client) {
//             ESP_LOGI(TAG, "Ignoring duplicate DNS request");
//             vTaskDelay(pdMS_TO_TICKS(20));
//             continue;
//         }
//         last_reply = current_time;
//         last_client = current_ip;

//         char reply[512];
//         memcpy(reply, buffer, len);
//         reply[2] = 0x81; // Response Flag
//         reply[3] = 0x80; // Response Code

//         reply[7] = 0x01; // Answer Count

//         int i = 12;
//         while (i < len && buffer[i] != 0) {
//             i++;
//         }
//         i++; // Skip null byte
//         i +=4;

//         reply[i++] = 0xC0; reply[i++] = 0x0C;
//         reply[i++] = 0x00; reply[i++] = 0x01; // A
//         reply[i++] = 0x00; reply[i++] = 0x01;
//         reply[i++] = 0x00; reply[i++] = 0x00;
//         reply[i++] = 0x00; reply[i++] = 0x3C;
//         reply[i++] = 0x00; reply[i++] = 0x04; // IPv4 Address
        
//         struct in_addr ip_addr;
//         inet_aton(ip, &ip_addr);
//         memcpy(&reply[i], &ip_addr.s_addr, 4);
//         i += 4;

//         sendto(sock, reply, i, 0, (struct sockaddr *)&client_addr, addr_len);
//         ESP_LOGI(TAG, "DNS Sent Reply");
//         vTaskDelay(pdMS_TO_TICKS(20));
//     }
// }

// void dns_task(void *param) {
//     start_dns("192.168.4.1");
//     vTaskDelete(NULL);
// }

