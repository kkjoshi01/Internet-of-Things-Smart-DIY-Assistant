/*
 * SPDX-FileCopyrightText: 2015-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
// Modified from the original to remove all unnecessary components and includes from factory demo
#include <stdio.h>
#include <math.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "bsp_storage.h"
#include "file_iterator.h"
#include "bsp_board.h"
#include "bsp/esp-bsp.h"
#include "sr_main.h"
#include "ui_main.h"
#include "app_net.h" // Our WiFI Handling
#include "esp_event.h" 


static const char *TAG = "main";

file_iterator_instance_t *file_iterator;


void app_main(void)
{
    ESP_LOGI(TAG, "Compile time: %s %s", __DATE__, __TIME__);
    // Initialize NVS 
    ESP_LOGI(TAG, "Initializing NVS");
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_LOGI(TAG, "NVS initialized");
    ESP_LOGI(TAG, "Initializing WiFi Netif");
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_LOGI(TAG, "Initializing WiFi");
    wifi_init();
    start_web_server();
    // DNS Task which was omitted cause of exceeding repeats
    // xTaskCreatePinnedToCore(dns_task, "dns_server", 4096, NULL, 5, NULL, 1);

    // Initialize SPIFFS
    bsp_spiffs_mount();

    // Initialise I2C
    bsp_i2c_init();
    // Initialize the display and BSP
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_H_RES * CONFIG_BSP_LCD_DRAW_BUF_HEIGHT,
        .double_buffer = 0,
        .flags = {
            .buff_dma = true,
        }
    };
    cfg.lvgl_port_cfg.task_affinity = 1;
    bsp_display_start_with_config(&cfg);
    bsp_board_init();
    
    vTaskDelay(pdMS_TO_TICKS(500));
    bsp_display_backlight_on();

    vTaskDelay(pdMS_TO_TICKS(4 * 500));
    ESP_LOGI(TAG, "Display LVGL demo");
    ESP_ERROR_CHECK(ui_main_start());

    
    file_iterator = file_iterator_new("/spiffs/mp3");
    assert(file_iterator != NULL);

    const board_res_desc_t *brd = bsp_board_get_description();

    ESP_LOGI(TAG, "speech recognition start");
    
    sr_init();

}
