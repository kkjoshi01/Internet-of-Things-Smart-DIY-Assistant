// Audio_Manager 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "bsp_board.h"
#include "bsp/esp-bsp.h"
#include "audio_manager.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

static const char *TAG = "audio_manager";

#define SAMPLE_RATE 16000 // Sample rate for audio recording and playback
#define AUDIO_CHANNELS 1 // Wit.Ai uses Mono Channel
#define AUDIO_BITS 16

static bool g_audio_muted = false;
static int g_audio_volume = 90;

typedef struct { // See http://soundfile.sapp.org/doc/WaveFormat/
    char riff[4];
    uint32_t size;
    char wave[4];
    char fmt[4];
    uint32_t fmt_size;
    uint16_t audio_fmt;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    char data[4];
    uint32_t data_size;
} wav_header_t;

static void fill_header(wav_header_t *hdr, uint32_t sample_num) { // Fills WAV header with values
    memcpy(hdr->riff, "RIFF", 4);
    hdr->size = 36 + sample_num * 2;
    memcpy(hdr->wave, "WAVE", 4);
    memcpy(hdr->fmt, "fmt ", 4);
    hdr->fmt_size = 16;
    hdr->audio_fmt = 1; // PCM
    hdr->num_channels = AUDIO_CHANNELS;
    hdr->sample_rate = SAMPLE_RATE;
    hdr->byte_rate = SAMPLE_RATE * AUDIO_CHANNELS * 2;
    hdr->block_align = AUDIO_CHANNELS * 2; // 2 bytes per sample
    hdr->bits_per_sample = AUDIO_BITS;
    memcpy(hdr->data, "data", 4);
    hdr->data_size = sample_num * 2; // 2 bytes per sample
}

// toggle mute state
esp_err_t audio_toggle(void) {
    g_audio_muted = !g_audio_muted;
    
    if (g_audio_muted) {
        // Turn audio off (mute)
        bsp_codec_mute_set(true);
        bsp_codec_volume_set(0, NULL);
        ESP_LOGI(TAG, "Audio turned OFF (muted)");
    } else {
        // Turn audio on (unmute and set volume)
        bsp_codec_mute_set(false);
        bsp_codec_volume_set(g_audio_volume, NULL);
        ESP_LOGI(TAG, "Audio turned ON (volume: %d%%)", g_audio_volume);
    }
    
    return ESP_OK;
}

// Get current audio state
bool audio_is_on(void) {
    return !g_audio_muted;
}

// Set audio volume (when audio is on)
esp_err_t audio_set_volume(int volume) {
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    
    g_audio_volume = volume;
    
    if (!g_audio_muted) {
        bsp_codec_volume_set(g_audio_volume, NULL);
        ESP_LOGI(TAG, "Audio volume set to %d%%", g_audio_volume);
    }
    
    return ESP_OK;
}

// Initialize audio system
esp_err_t audio_init(void) {
    g_audio_muted = false;
    g_audio_volume = 90;
    
    bsp_codec_mute_set(false);
    bsp_codec_volume_set(g_audio_volume, NULL);
    
    ESP_LOGI(TAG, "Audio system initialized - ON, Volume: %d%%", g_audio_volume);
    return ESP_OK;
}

esp_err_t save_raw_audio(const char *file_path, const int16_t *pcm, size_t sample_count) { // Writes raw PCM data to disk
    FILE *f = fopen(file_path, "wb"); // Write binary
    if (!f) {
        ESP_LOGW(TAG, "Couldn't open file %s for writing", file_path);
        return ESP_FAIL;
    }

    fwrite(pcm, sizeof(int16_t), sample_count, f);
    fclose(f);

    ESP_LOGI(TAG, "Saved raw audio to %s with samples %d", file_path, (int)sample_count);
    return ESP_OK;
}


esp_err_t save_audio(const char *file_path, const int16_t *pcm, size_t sample_count) { // Writes WAV file to disk
    wav_header_t header;
    fill_header(&header, sample_count);

    FILE *f = fopen(file_path, "wb"); // Write binary
    if (!f) {
        ESP_LOGW(TAG, "Couldn't open file %s for writing", file_path);
        return ESP_FAIL;
    }

    fwrite(&header, sizeof(wav_header_t), 1, f);
    fwrite(pcm, sizeof(int16_t), sample_count, f);
    // Logging Header to check for errors
    ESP_LOGI("WAVE HEADER INFO: ", "RIFF: %.4s, Size: %d, WAVE: %.4s, fmt: %.4s, fmt_size: %d, audio_fmt: %d, num_channels: %d, sample_rate: %d, byte_rate: %d, block_align: %d, bits_per_sample: %d, data: %.4s, data_size: %d",
             header.riff, header.size, header.wave, header.fmt, header.fmt_size,
             header.audio_fmt, header.num_channels, header.sample_rate,
             header.byte_rate, header.block_align, header.bits_per_sample,
             header.data, header.data_size);
    fclose(f);

    ESP_LOGI(TAG, "Saved audio to %s with samples %d", file_path,(int)sample_count);
    return ESP_OK;
}

esp_err_t play_audio(const char *file_path) { // Plays audio from a WAV file in SPIFFS
    FILE *f = fopen(file_path, "rb");
    if (!f) {
        ESP_LOGW(TAG, "Couldn't open file %s for reading", file_path);
        return ESP_FAIL;
    }
    wav_header_t header;
    fread(&header, sizeof(wav_header_t), 1, f);
    if (g_audio_muted) {
        ESP_LOGW(TAG, "Audio is muted, skipping playback of %s", file_path);
        fclose(f);
        return ESP_OK;
    }
    bsp_codec_set_fs(header.sample_rate, header.bits_per_sample, I2S_SLOT_MODE_MONO); // Set codec to match WAV file MONO
    bsp_codec_mute_set(false); // Disable mute
    bsp_codec_volume_set(g_audio_volume, NULL); // Set Volume to 90%
    
    size_t written = 0;
    size_t read = 0;
    uint8_t chunk[1024];
    while ((read = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        bsp_i2s_write((char *)chunk, read, &written, portMAX_DELAY);
    } // Write audio to I2S
    fclose(f);
    ESP_LOGI(TAG, "Finished playing audio from %s", file_path);
    bsp_codec_set_fs(16000, 16, 2);
    return ESP_OK;
}

