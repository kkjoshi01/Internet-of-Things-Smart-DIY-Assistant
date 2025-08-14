#pragma once
#include <stdint.h>
#include "esp_err.h"

esp_err_t record_audio(int16_t **out_buffer, size_t *out_len);
esp_err_t save_audio(const char *file_path, const int16_t *pcm, size_t pcm_length);
esp_err_t save_raw_audio(const char *file_path, const int16_t *pcm, size_t sample_count);
esp_err_t play_audio(const char *file_path);
esp_err_t audio_init(void);
esp_err_t audio_toggle(void);
bool audio_is_on(void);
esp_err_t audio_set_volume(int volume);

