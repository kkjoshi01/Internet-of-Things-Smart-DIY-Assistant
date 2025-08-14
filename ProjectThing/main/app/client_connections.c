#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h" // ESP-IDF's certificate bundle for HTTPS requests
#include "cJSON.h"
#include "ui_main.h"
#include "audio_manager.h"
#include "app_net.h"
#include "lwip/netdb.h"
#include "ui_conversation.h"
#include "esp_sntp.h"
#include "esp_spiffs.h"

#define WIT_TOKEN ""
#define GPT_TOKEN ""
#define MAX_RESPONSE_SIZE 16384
#define WEATHER_BUFFER_SIZE 256

static const char *TAG = "HTTP_CLIENT";
static char response_buffer[MAX_RESPONSE_SIZE];
static int response_offset = 0;

static char final_text[256];
static char final_intent[64];
static char final_gpt[512];


void play_audio_response(void *param) {

    play_audio("/spiffs/response.wav");
    vTaskDelete(NULL); // Delete the task after playing audio
}


static FILE *tts_file = NULL; 

static esp_err_t tts_handler(esp_http_client_event_t *evt) { // GPT Text to Speech Handler
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGE(TAG, "HTTP Event Error");
            break;
        case HTTP_EVENT_ON_HEADER: // Saves the response to spiffs/response.wav
            if (!tts_file) {
                tts_file = fopen("/spiffs/response.wav", "wb");
                if (!tts_file) {
                    ESP_LOGE(TAG, "Failed to open file for writing");
                    return ESP_FAIL;
                }
            }
            break;
        case HTTP_EVENT_ON_DATA:
            if (tts_file && evt->data_len > 0) {
                fwrite(evt->data, 1, evt->data_len, tts_file);
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            if (tts_file) {
                fclose(tts_file);
                tts_file = NULL; // Close file after writing
            }
            ESP_LOGI(TAG, "TTS Response Finished");
            break;
        default:
            break;
    }
    return ESP_OK;
}

esp_err_t gpt_to_speech(const char *prompt, const char *instructions) { // GPT Text to Speech Function

    unlink("/spiffs/response.wav"); // Remove old response file if exists

    esp_http_client_config_t config = {
        .url = "https://api.openai.com/v1/audio/speech",
        .method = HTTP_METHOD_POST,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = tts_handler,
        .timeout_ms = 30000, // 30 Second Timeout
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_http_client_set_header(client, "Authorization", "Bearer " GPT_TOKEN);
    esp_http_client_set_header(client, "Content-Type", "application/json");

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", "gpt-4.0-mini-tts");
    cJSON_AddStringToObject(root, "prompt", prompt);
    cJSON_AddStringToObject(root, "voice", "alloy");
    if (instructions && strlen(instructions) > 0) {
        cJSON_AddStringToObject(root, "instructions", instructions);
    }
    cJSON_AddStringToObject(root, "response_format", "wav");
    char *body = cJSON_PrintUnformatted(root);

    esp_http_client_set_post_field(client, body, strlen(body)); // Set the request body
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "TTS Response Status: %d", status);
        if (status == 200) {
            ESP_LOGI(TAG, "TTS Response written to /spiffs/response.wav");
            xTaskCreate(play_audio_response, "play_audio_response", 2048*2, NULL, 5, NULL); // Play the audio response
        } else {
            ESP_LOGE(TAG, "TTS request failed with status: %d", status);
            play_audio("/spiffs/not_read.wav"); // Play error audio
        }
    } else {
        ESP_LOGE(TAG, "Failed to send TTS request: %s", esp_err_to_name(err));
        play_audio("/spiffs/not_read.wav"); // Play error audio
    }

    cJSON_Delete(root);
    free(body); // Free the JSON body string
    esp_http_client_cleanup(client); // Cleanup HTTP client
    return err;
}

// Handler for HTTP events from Wit.ai
esp_err_t wit_handler(esp_http_client_event_t *evt) {
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGE(TAG, "HTTP Event Error");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG, "HTTP Event Connected");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGI(TAG, "HTTP Event Header Sent");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGI(TAG, "HTTP Event Header: %s: %s", evt->header_key, evt->header_value);
            // printf("%.*s", evt->data_len, (char*)evt->data);
            break;
        case HTTP_EVENT_ON_DATA:
            if (evt->data_len > 0) {
                if (response_offset + evt->data_len < MAX_RESPONSE_SIZE) {
                    memcpy(response_buffer + response_offset, evt->data, evt->data_len);
                    response_offset += evt->data_len;
                } else {
                    ESP_LOGE(TAG, "Response buffer overflow");
                }
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG, "HTTP Event Finished");
            response_buffer[response_offset] = '\0'; // Null-terminate the response
            ESP_LOGI(TAG, "Response: %s", response_buffer);

            
            final_text[0] = '\0'; // Clear final text
            final_intent[0] = '\0'; // Clear final intent
            
            size_t processed = 0;
            while (processed < (size_t)response_offset) {
                while (processed < (size_t)response_offset && response_buffer[processed] != '{') {
                    processed++;
                }
                if (processed >= (size_t)response_offset) {
                    break; // No more JSON objects found
                }
                int depth = 0;
                size_t position = processed;
                do {
                    if (response_buffer[position] == '{') {
                        depth++;
                    } else if (response_buffer[position] == '}') {
                        depth--;
                    }
                    position++;
                    if (position >= (size_t)response_offset && depth > 0) {
                        ESP_LOGE(TAG, "Malformed JSON object - no closing brace found");
                        depth = 1;
                        break;
                    }
                } while (depth > 0);

                if (depth != 0) {
                    ESP_LOGE(TAG, "Malformed JSON object - unmatched braces");
                    break;
                }

                size_t obj_len = position - processed; // Length of the JSON object

                char saved = response_buffer[processed + obj_len]; // Save the next character
                response_buffer[processed+ obj_len] = '\0'; // Null-terminate the JSON object
                cJSON *json = cJSON_Parse(response_buffer + processed);
                response_buffer[processed + obj_len] = saved; // Restore the next character

                if (json) {
                    cJSON *type = cJSON_GetObjectItem(json, "type");
                    cJSON *is_final = cJSON_GetObjectItem(json, "is_final");
                    cJSON *text = cJSON_GetObjectItem(json, "text");
                    cJSON *intents = cJSON_GetObjectItem(json, "intents");
                    if (type && strcmp(type->valuestring, "FINAL_UNDERSTANDING") == 0 && is_final && cJSON_IsTrue(is_final)) {
                        if (text && cJSON_IsString(text)) {
                            strncpy(final_text, text->valuestring, sizeof(final_text) - 1);
                            final_text[sizeof(final_text) - 1] = '\0'; // Ensure null termination
                        }
                        
                        if (intents && cJSON_IsArray(intents) && cJSON_GetArraySize(intents) > 0) {
                            cJSON *first_intent = cJSON_GetArrayItem(intents, 0);
                            cJSON *intent_name = cJSON_GetObjectItem(first_intent, "name");
                            if (intent_name && cJSON_IsString(intent_name)) {
                                strncpy(final_intent, intent_name->valuestring, sizeof(final_intent) - 1);
                                final_intent[sizeof(final_intent) - 1] = '\0'; // Ensure null termination
                            }
                        }
                    }
                    cJSON_Delete(json);
                }
                size_t remaining = response_offset - (processed + obj_len);
                memmove(response_buffer, response_buffer + processed + obj_len, remaining);
                response_offset = remaining;
                processed = 0; // Reset processed for next iteration
            }

            ESP_LOGI(TAG, "Final Text: %s", final_text);
            ESP_LOGI(TAG, "Final Intent: %s", final_intent);
            response_offset = 0; // Reset for next request
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP Event Disconnected");
            break;
        default:
            ESP_LOGW(TAG, "Unhandled HTTP event: %d", evt->event_id);
            break;
    }
    return ESP_OK;
}

// send text to OpenAI API
esp_err_t text_to_gpt(const char *text) {
    response_offset = 0; // Reset response buffer offset

    esp_http_client_config_t textConfig = {
        .url = "https://api.openai.com/v1/responses",
        .method = HTTP_METHOD_POST,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = wit_handler,
        .timeout_ms = 30000, // 30 Second Timeout
    };
    esp_http_client_handle_t client = esp_http_client_init(&textConfig);

    esp_http_client_set_header(client, "Authorization", "Bearer " GPT_TOKEN);
    esp_http_client_set_header(client, "Content-Type", "application/json");

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", "gpt-4.1");
    cJSON_AddStringToObject(root, "input", text);
    char *body = cJSON_PrintUnformatted(root);

    esp_http_client_set_post_field(client, body, strlen(body)); // Loading the body into the request
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "GPT Response Status: %d", status);
        ESP_LOGI(TAG, "GPT Response: %s", response_buffer);

        cJSON *arr = cJSON_Parse(response_buffer); // Parse the JSON response
        if (cJSON_IsArray(arr) && cJSON_GetArraySize(arr) > 0) {
            cJSON *msg = cJSON_GetArrayItem(arr, 0);
            cJSON *content = cJSON_GetObjectItem(msg, "content");
            if (cJSON_IsArray(content) && cJSON_GetArraySize(content) > 0) {
                cJSON *first = cJSON_GetArrayItem(content, 0);
                cJSON *content = cJSON_GetObjectItem(first, "text"); // Get the text content from the first item
                if (cJSON_IsString(content)) {
                    strncpy(final_gpt, content->valuestring, sizeof(final_gpt) - 1); // Copying the response to final GPT
                    final_gpt[sizeof(final_gpt) - 1] = '\0'; // Ensure null termination
                    ui_conversation_show_screen(text, final_gpt); // Show the response on UI
                    gpt_to_speech(final_gpt, "Please read this response and be fast but coherent"); // Convert the response to speech
                }
            } else {
                ESP_LOGE(TAG, "No content found in GPT response");
            }
        }  
    } else {
        ESP_LOGE(TAG, "Failed to send text to GPT: %s", esp_err_to_name(err));
    }
    cJSON_Delete(root);
    free(body);
    esp_http_client_cleanup(client);

    return err;
}

// Send an audio file to Wit.ai for speech recognition
esp_err_t send_audio_to_wit(const char *file_path) {
    // play_audio(file_path); // Play the audio file before sending it
    if (!is_wifi_connected()) {
        ESP_LOGE(TAG, "WiFi not connected - cannot send audio to Wit.AI");
        return ESP_FAIL;
    }

    FILE *f = fopen(file_path, "rb");
    if (!f) {
        ESP_LOGW(TAG, "FILE DOESN'T EXIST");
        return ESP_FAIL;
    }

    // File Size
    fseek(f, 0, SEEK_END);
    size_t file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size <= 0) {
        ESP_LOGE(TAG, "Empty file?: %d", (int)file_size);
        fclose(f);
        return ESP_FAIL;
    }

    uint8_t *buffer = malloc(file_size);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate memory for file buffer");
        fclose(f);
        return ESP_ERR_NO_MEM;
    }

    fread(buffer, 1, file_size, f);
    fclose(f);
    
    esp_http_client_config_t config = {
        .url = "https://api.wit.ai/speech?v=20250528",
        .event_handler = wit_handler,
        .method = HTTP_METHOD_POST,
        .crt_bundle_attach = esp_crt_bundle_attach,
        // .buffer_size = 4096,
        // .buffer_size_tx = 4096,
        .timeout_ms = 30000, // 30 Second Timeout
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Authorization", "Bearer " WIT_TOKEN);
    esp_http_client_set_header(client, "Content-Type", "audio/wav"); //;audio/raw;encoding=unsigned-integer;bits=16;rate=16000;endian=little

    esp_http_client_set_post_field(client, (const char *)buffer, file_size);
    esp_err_t error = esp_http_client_perform(client);
    if (error == ESP_OK) {
        ESP_LOGI(TAG, "HTTP Status: %d", esp_http_client_get_status_code(client));
        if (strlen(final_text) > 0) {
            text_to_gpt(final_text); // Send the final text to GPT
        }
        
    } else {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(error));
        play_audio("not_read.wav");
        ui_conversation_show_screen("Failed to process audio", "Please try again."); // Show error message on UI
    }
    free(buffer);
    esp_http_client_cleanup(client);
    return error;
}

typedef struct { // Weather response structure for wttr.in
    char buffer[WEATHER_BUFFER_SIZE];
    int offset;
} weather_resp_t;

// Handler for HTTP events to fetch weather info
esp_err_t weather_handler(esp_http_client_event_t *evt) {
    weather_resp_t *weather = (weather_resp_t *)evt->user_data;
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (weather && evt->data_len + weather->offset < WEATHER_BUFFER_SIZE) {
                memcpy(weather->buffer + weather->offset, evt->data, evt->data_len);
                weather->offset += evt->data_len;
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

// Fetches weather and city information from wttr.in
void get_local_city_and_weather(void *param) {
    play_audio("/spiffs/wifi_connected.wav"); // Wifi has been connected
    weather_resp_t weather = { .offset = 0 };
    esp_http_client_config_t config = {
        .url = "https://wttr.in/?format=%l:+%t,+%C",
        .method = HTTP_METHOD_GET,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 5000,
        .event_handler = weather_handler,
        .user_data = &weather,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "User-Agent", "curl/7.68.0");

    esp_err_t err = esp_http_client_perform(client);

    if (err==ESP_OK) {
        weather.buffer[weather.offset] = '\0'; // Null-terminate the buffer
        ESP_LOGI(TAG, "Weather response: %s", weather.buffer);
        ESP_LOGI(TAG, "HTTP Status Code: %d", esp_http_client_get_status_code(client));

        char *sep = strchr(weather.buffer, ':');
        if (sep) {
            *sep = '\0';
            char *city = weather.buffer;
            char *weather_str = sep + 1;

            while (*weather_str == ' ' || *weather_str == '+') weather_str++; // Skip leading
            char *end = city + strlen(city) - 1;
            while (end > city && (*end == ' ' || *end == '+')) *end-- = '\0'; // Trailing

            ui_main_update_weather(weather_str, city);
        } else {
            ESP_LOGW(TAG, "Unexpected response format: %s", weather.buffer);
        }
    } else {
        ESP_LOGE(TAG, "Failed to get local city and weather: %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
    vTaskDelete(NULL); // Delete the task after completion
}

// Syncs the system clock with NTP server
void sync_time_with_ntp() {
    ESP_LOGI("TIME", "Initializing SNTP...");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();

    // Wait for time to be set
    time_t now = 0;
    struct tm timeinfo = { 0 };
    int retry = 0;
    const int retry_count = 10;
    while (timeinfo.tm_year < (2020 - 1900) && ++retry < retry_count) {
        ESP_LOGI("TIME", "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(pdMS_TO_TICKS(2000));
        time(&now);
        localtime_r(&now, &timeinfo);
    }
    if (timeinfo.tm_year < (2020 - 1900)) {
        ESP_LOGW("TIME", "Failed to sync time with NTP server.");
    } else {
        ESP_LOGI("TIME", "System time updated from NTP server.");
        ui_main_set_time_synced(true);
    }
}

