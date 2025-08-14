// Speech Recognition Main Handler
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_mn_speech_commands.h"
#include "esp_process_sdkconfig.h"
#include "esp_afe_sr_models.h"
#include "esp_mn_models.h"
#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "esp_afe_sr_iface.h"
#include "esp_mn_iface.h"
#include "esp_log.h"
#include "esp_timer.h"


#include "model_path.h"
#include "bsp_board.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ui_assistant.h"
#include "audio_manager.h"
#include "client_connections.h"
#include "app_net.h"

#define I2S_CHANNEL_NUM (2) 
#define FEED_CHANNELS 3
#define AUDIO_SAMPLE_RATE 16000 // The I2S sample rate was 8k, using a standard 16k messes with Audio
#define AUDIO_RECORD_TIME 3 // Max Record time based on memory buffer size
#define AUDIO_MAX_SAMPLES (AUDIO_SAMPLE_RATE * AUDIO_RECORD_TIME) // Max Samples (40,000)

static int16_t *record_buffer = NULL; // Buffer to record audio
static size_t record_offset = 0; // Current offset in the record buffer
static bool recording = false; // Recording flag to enable and disable audio recording
static int64_t record_end_time = 0; // End time for esp_timer (in milliseconds)

static const char *LOG_TAG = "SR_WAKE"; // Log tag for this file
typedef struct {
    const esp_afe_sr_iface_t *afe_handle;
    esp_afe_sr_data_t *afe_data;
    int16_t *afe_buffer;
    TaskHandle_t feed_task;
    TaskHandle_t detect_task;
    EventGroupHandle_t event_group;
} sr_data_t; // Data structure for AFE, based on factory demo

static sr_data_t *g_sr = NULL; // Global variable for speech recognition data
#define EVENT_STOP BIT0 // Stop Events for SR
#define EVENT_FEED_STOP BIT1
#define EVENT_DETECT_STOP BIT2
static bool audio_playing = false; // Flag to indicate if audio is currently playing

void start_recording() { // Simple method to generate a record buffer and start recording
    if (!record_buffer) {
        record_buffer = heap_caps_malloc(AUDIO_MAX_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    record_offset = 0;
    recording = true;
}

void stop_recording() { // End recording
    recording = false;
}

void wit_upload_task(void *param) {
    ESP_LOGI(LOG_TAG, "Wit AI Upload Task Started");
    esp_err_t err = send_audio_to_wit("/spiffs/last_query.wav"); // Uploads the recorded audio to Wit.AI
    if (err == ESP_OK) {
        ESP_LOGI(LOG_TAG, "Audio uploaded successfully to Wit.AI");
    } else {
        ESP_LOGE(LOG_TAG, "Failed to upload audio to Wit.AI: %s", esp_err_to_name(err));
        play_audio("/spiffs/not_read.wav");
    }
    vTaskDelete(NULL);
}

static void sr_task(void *param) { // Speech Recognition Task to detect wakeword and handle recording

    sr_data_t *sr = (sr_data_t *)param;

    while (true) {
        if (xEventGroupGetBits(sr->event_group) & EVENT_STOP) { // Stop Event Check
            ESP_LOGI(LOG_TAG, "Detect task stopping");
            xEventGroupSetBits(sr->event_group, EVENT_DETECT_STOP);
            break;
        }
        afe_fetch_result_t *result = sr->afe_handle->fetch(sr->afe_data); // AFE Data Result Fetching
        if (!result || result->ret_value == ESP_FAIL) {
            continue; // Skips when erroneous or blank result is returned (Audio Issue)
        }  

        if (result->wakeup_state == WAKENET_DETECTED && !recording) { // Wakeword Detection Check
            ESP_LOGI(LOG_TAG, "Wakeword detected!");
            if (!is_wifi_connected()) {
                ESP_LOGE(LOG_TAG, "WiFi not connected, cannot proceed with recording");
                play_audio("/spiffs/enable_wifi.wav"); // Play a prompt to enable WiFi
                continue;
            }

            ui_assistant_show_screen(); // Forces UI Screen
            // audio_playing = true; // Set audio playing flag
            // vTaskDelay(pdMS_TO_TICKS(50)); // Allow some time for tasks to suspend
            play_audio("/spiffs/echo_en_wake.wav"); // Play the audio file
            // audio_playing = false; // Reset audio playing flag after playback
            // vTaskDelay(pdMS_TO_TICKS(50)); // Allow some time for audio playback to finish

            // sr->afe_handle->disable_wakenet(sr->afe_data); // Disable Wakeword Detection
            start_recording();
            ESP_LOGI(LOG_TAG, "Recording started");
            record_end_time = esp_timer_get_time() + (AUDIO_RECORD_TIME * 1000000); // Setting end time for recording
        }
        
        if(recording && record_end_time > 0 && esp_timer_get_time() >= record_end_time) {
            stop_recording(); // Stop recording after the specified time (5 seconds)
            ESP_LOGI(LOG_TAG, "Recording stopped");
            // audio_playing = true;
            save_audio("/spiffs/last_query.wav", record_buffer, record_offset);
            save_raw_audio("/spiffs/last_query_test.raw", record_buffer, record_offset);
            // play_audio("/spiffs/last_query.wav");
            // audio_playing = false; // Reset audio playing flag
            ESP_LOGI(LOG_TAG, "Recording stopped, samples: %d, expected: %d", (int)record_offset, AUDIO_SAMPLE_RATE * AUDIO_RECORD_TIME);
            // vTaskDelay(pdMS_TO_TICKS(50)); // Allow some time for audio to be saved
            record_end_time = 0; // Reset end time

            if (record_buffer) {
                heap_caps_free(record_buffer);
                record_buffer = NULL;
            }
            // sr->afe_handle->enable_wakenet(sr->afe_data);
            vTaskDelay(pdMS_TO_TICKS(50)); // Allow some time for Processing ig?
            xTaskCreate(wit_upload_task, "wit_upload_task", 6*1024, NULL, 5, NULL);
        }
    }
    vTaskDelete(NULL);
}

static void sr_feed_audio(void *param) { // Audio Feed Task to read audio data from I2S and feed it to the AFE
    sr_data_t *sr = (sr_data_t *)param;
    size_t bytes = 0;

    int audio_chunk_size = sr->afe_handle->get_feed_chunksize(sr->afe_data);
    
    int16_t *audio_data = heap_caps_malloc(audio_chunk_size * sizeof(int16_t) * FEED_CHANNELS, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    
    // ESP_LOGI(LOG_TAG, "Audio Data chunk size: %d samples", audio_chunk_size);

    if (!audio_data) {
        ESP_LOGE(LOG_TAG, "No Memory for Audio Data Buffer");
        vTaskDelete(NULL);
        return;
    }

    sr->afe_buffer = audio_data;

    while (true) { // Loop for feeding audio data

        if (xEventGroupGetBits(sr->event_group) & EVENT_STOP) {
            ESP_LOGI(LOG_TAG, "Feed task stopping");
            xEventGroupSetBits(sr->event_group, EVENT_FEED_STOP);
            break;
        }

        // if (audio_playing) {
        //     ESP_LOGI(LOG_TAG, "Audio is playing, skipping feed");
        //     vTaskDelay(pdMS_TO_TICKS(10)); // Pause for a short time to allow audio playback
        //     continue; // Skip feeding audio while paused
        // }

        // if (!sr->afe_data) { // Check if I2S is ready
        //     ESP_LOGI(LOG_TAG, "I2S not ready, waiting...");
        //     vTaskDelay(pdMS_TO_TICKS(10)); // Wait for I2S to be ready
        //     continue;
        // }
        // Main Method to read audio data from I2S

        bsp_i2s_read((char *)audio_data, audio_chunk_size * sizeof(int16_t) * I2S_CHANNEL_NUM, &bytes, portMAX_DELAY);

        // Byte Checks to ensure data validity and prep for recording (for Wit.ai)
        if (recording && record_offset < AUDIO_MAX_SAMPLES) {
            size_t sample_copy = audio_chunk_size;

            if (record_offset + sample_copy > AUDIO_MAX_SAMPLES) {
                sample_copy = AUDIO_MAX_SAMPLES - record_offset; // Prevent overflow
            }
            for (size_t i = 0; i < sample_copy; i++) {
                record_buffer[record_offset++] = audio_data[i * I2S_CHANNEL_NUM + 0]; // Store only Ch1
            }

        }

        // Channel Adjustment from ESPBox Factory Demo (For Wakeword)
        for (int  i = audio_chunk_size - 1; i >= 0; i--) {
            audio_data[i * FEED_CHANNELS + 0] = audio_data[i * 2 + 0]; //Ch1
            audio_data[i * FEED_CHANNELS + 1] = audio_data[i * 2 + 1]; //Ch2
            audio_data[i * FEED_CHANNELS + 2] = 0; //Ch3
        }

        sr->afe_handle->feed(sr->afe_data, audio_data);
        if (!recording) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
    heap_caps_free(audio_data); // Frees up audio data buffer
    vTaskDelete(NULL);
}

void sr_init() { // Begins SR
    ESP_LOGI(LOG_TAG, "Initialising Speech Recognition");

    g_sr = heap_caps_calloc(1, sizeof(sr_data_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!g_sr) {
        ESP_LOGE(LOG_TAG, "Failed to load memory for SR data");
        return;
    }

    g_sr->event_group = xEventGroupCreate();
    if (!g_sr->event_group) {
        ESP_LOGE(LOG_TAG, "Failed to create event group for SR");
        heap_caps_free(g_sr);
        g_sr = NULL;
        return;
    }

    srmodel_list_t *models = esp_srmodel_init("model");
    if (!models) {
        ESP_LOGE(LOG_TAG, "Failed to load SR Models");
        return;
    }

    g_sr->afe_handle = &ESP_AFE_SR_HANDLE;

    afe_config_t afe_config = AFE_CONFIG_DEFAULT(); // Default AFE Configuration which is used to set up the AFE

    afe_config.wakenet_model_name = esp_srmodel_filter(models, ESP_WN_PREFIX, NULL);
    afe_config.aec_init = false; // Disabling echo cancellation

    g_sr->afe_data = g_sr->afe_handle->create_from_config(&afe_config);
    if (!g_sr->afe_data) {
        ESP_LOGE(LOG_TAG, "Failed to make AFE data");
        return;
    }

    xTaskCreatePinnedToCore(sr_feed_audio, "sr_feed_task", 4*1024, g_sr, 5, &g_sr->feed_task, 0); // Audio Feed Task to read I2S data and feed it to AFE
    xTaskCreatePinnedToCore(sr_task, "sr_task", 8*1024, g_sr, 5, &g_sr->detect_task, 1); // Speech Recognition Task to detect wakeword and handle recording

    ESP_LOGI(LOG_TAG, "Initialised SR");
}

void sr_stop() { // Function to stop Speech Recognition and free resources
    ESP_LOGI(LOG_TAG, "Stopping Speech Recognition");
    if (!g_sr) {
        ESP_LOGE(LOG_TAG, "SR not initialised");
        return;
    }

    xEventGroupSetBits(g_sr->event_group, EVENT_STOP);

    xEventGroupWaitBits(g_sr->event_group, EVENT_DETECT_STOP | EVENT_FEED_STOP, pdTRUE, pdTRUE, portMAX_DELAY);

    if (g_sr->afe_data) {
        g_sr->afe_handle->destroy(g_sr->afe_data);
        g_sr->afe_data = NULL;
    }
    if (g_sr->afe_buffer) {
        heap_caps_free(g_sr->afe_buffer);
        g_sr->afe_buffer = NULL;
    }
    if (g_sr->event_group) {
        vEventGroupDelete(g_sr->event_group);
        g_sr->event_group = NULL;
    }
    heap_caps_free(g_sr);
    g_sr = NULL;
    ESP_LOGI(LOG_TAG, "Speech Recognition stopped and resources freed");
}
