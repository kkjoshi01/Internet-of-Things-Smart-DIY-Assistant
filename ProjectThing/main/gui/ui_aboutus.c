// ui_aboutus.c - Info Config
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_app_desc.h"
#include "lvgl.h"
#include "ui_main.h"
#include "ui_aboutus.h"
#include "gui/image/info.h"


static const char *TAG = "ui_aboutus";

// Global UI elements
static lv_obj_t *g_aboutus_screen = NULL;
static lv_obj_t *g_wifi_status_label = NULL;
static lv_obj_t *g_cloud_status_label = NULL;

// Back button event handler
static void back_btn_clicked(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Back button clicked - returning to main screen");
        ui_main_show_screen();
    }
}

// Function to get MAC address as string
static void get_mac_address_string(char *mac_str, size_t size)
{
    uint8_t mac[6];
    esp_err_t ret = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (ret == ESP_OK) {
        snprintf(mac_str, size, "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    } else {
        strncpy(mac_str, "Unknown", size);
    }
}

// Function to get software version
static const char* get_software_version(void)
{
    const esp_app_desc_t *app_desc = esp_app_get_description();
    return app_desc->version;
}

// Function to create info row with label and value
static lv_obj_t* create_info_row(lv_obj_t *parent, const char *label_text, const char *value_text, lv_obj_t *prev_obj)
{
    // Create container for the row
    lv_obj_t *row_cont = lv_obj_create(parent);
    lv_obj_set_size(row_cont, LV_PCT(90), 35);
    lv_obj_set_style_bg_opa(row_cont, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_opa(row_cont, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row_cont, 0, LV_PART_MAIN);
    
    if (prev_obj) {
        lv_obj_align_to(row_cont, prev_obj, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);
    } else {
        lv_obj_align(row_cont, LV_ALIGN_TOP_MID, 0, 0);
    }
    
    // Create label
    lv_obj_t *label = lv_label_create(row_cont);
    lv_label_set_text(label, label_text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_make(60, 60, 60), LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
    
    // Create value
    lv_obj_t *value = lv_label_create(row_cont);
    lv_label_set_text(value, value_text);
    lv_obj_set_style_text_font(value, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(value, lv_color_make(100, 100, 100), LV_PART_MAIN);
    lv_obj_align(value, LV_ALIGN_RIGHT_MID, 0, 0);
    
    return row_cont;
}

esp_err_t ui_aboutus_start(void)
{
    if (g_aboutus_screen) {
        lv_scr_load(g_aboutus_screen);
        ESP_LOGI(TAG, "About Us screen already created, loading it.");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Creating about us screen UI");

    // Create about us screen
    g_aboutus_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(g_aboutus_screen, lv_color_make(245, 245, 245), LV_STATE_DEFAULT);

    lv_obj_t *back_btn = lv_btn_create(g_aboutus_screen);
    lv_obj_set_size(back_btn, 40, 40);
    lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 15, 15); // Set x/y to 0,0 for exact match
    lv_obj_add_event_cb(back_btn, back_btn_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT);
    lv_obj_center(back_label);

    // Title
    lv_obj_t *title_label = lv_label_create(g_aboutus_screen);
    lv_label_set_text(title_label, "About Device");
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(title_label, lv_color_make(40, 40, 40), LV_PART_MAIN);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 25);

    // ESP Logo (using external reference)
    lv_obj_t *logo = lv_img_create(g_aboutus_screen);
    lv_img_set_src(logo, &info);
    lv_obj_align_to(logo, title_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 15);

    // Info section
    lv_obj_t *info_container = lv_obj_create(g_aboutus_screen);
    lv_obj_set_size(info_container, LV_PCT(85), 200);
    lv_obj_align_to(info_container, logo, LV_ALIGN_OUT_BOTTOM_MID, 0, 20);
    lv_obj_set_style_bg_color(info_container, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_radius(info_container, 12, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(info_container, 8, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(info_container, LV_OPA_10, LV_PART_MAIN);
    lv_obj_set_style_pad_all(info_container, 15, LV_PART_MAIN);

    // Get system information
    char mac_str[18];
    get_mac_address_string(mac_str, sizeof(mac_str));
    const char *version = get_software_version();

    // Create info rows
    lv_obj_t *version_row = create_info_row(info_container, "Software Version:", version, NULL);
    lv_obj_t *mac_row = create_info_row(info_container, "MAC Address:", mac_str, version_row);
    
    // WiFi status
    lv_obj_t *wifi_row_cont = lv_obj_create(info_container);
    lv_obj_set_size(wifi_row_cont, LV_PCT(90), 35);
    lv_obj_set_style_bg_opa(wifi_row_cont, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_opa(wifi_row_cont, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(wifi_row_cont, 0, LV_PART_MAIN);
    lv_obj_align_to(wifi_row_cont, mac_row, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);
    
    lv_obj_t *wifi_label = lv_label_create(wifi_row_cont);
    lv_label_set_text(wifi_label, "WiFi Connected:");
    lv_obj_set_style_text_font(wifi_label, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(wifi_label, lv_color_make(60, 60, 60), LV_PART_MAIN);
    lv_obj_align(wifi_label, LV_ALIGN_LEFT_MID, 0, 0);
    
    g_wifi_status_label = lv_label_create(wifi_row_cont);
    lv_label_set_text(g_wifi_status_label, "False");
    lv_obj_set_style_text_font(g_wifi_status_label, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(g_wifi_status_label, lv_color_make(200, 50, 50), LV_PART_MAIN); // Red for disconnected
    lv_obj_align(g_wifi_status_label, LV_ALIGN_RIGHT_MID, 0, 0);

    ui_aboutus_update_wifi_status(ui_main_is_wifi_connected());

    
    // Cloud status
    lv_obj_t *cloud_row_cont = lv_obj_create(info_container);
    lv_obj_set_size(cloud_row_cont, LV_PCT(90), 35);
    lv_obj_set_style_bg_opa(cloud_row_cont, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_opa(cloud_row_cont, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(cloud_row_cont, 0, LV_PART_MAIN);
    lv_obj_align_to(cloud_row_cont, wifi_row_cont, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);
    
    lv_obj_t *cloud_label = lv_label_create(cloud_row_cont);
    lv_label_set_text(cloud_label, "Cloud Connected:");
    lv_obj_set_style_text_font(cloud_label, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(cloud_label, lv_color_make(60, 60, 60), LV_PART_MAIN);
    lv_obj_align(cloud_label, LV_ALIGN_LEFT_MID, 0, 0);
    
    g_cloud_status_label = lv_label_create(cloud_row_cont);
    lv_label_set_text(g_cloud_status_label, "False");
    lv_obj_set_style_text_font(g_cloud_status_label, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(g_cloud_status_label, lv_color_make(200, 50, 50), LV_PART_MAIN);
    lv_obj_align(g_cloud_status_label, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_scr_load(g_aboutus_screen);
    ESP_LOGI(TAG, "About us screen UI created successfully");

    return ESP_OK;
}

void ui_aboutus_show_screen(void)
{
    ui_aboutus_start();
}

void ui_aboutus_update_wifi_status(bool connected)
{
    if (g_wifi_status_label) {
        lv_label_set_text(g_wifi_status_label, connected ? "True" : "False");
        lv_obj_set_style_text_color(g_wifi_status_label, 
                                   connected ? lv_color_make(50, 150, 50) : lv_color_make(200, 50, 50), 
                                   LV_PART_MAIN);
    }
}

void ui_aboutus_update_cloud_status(bool connected)
{
    if (g_cloud_status_label) {
        lv_label_set_text(g_cloud_status_label, connected ? "True" : "False");
        lv_obj_set_style_text_color(g_cloud_status_label, 
                                   connected ? lv_color_make(50, 150, 50) : lv_color_make(200, 50, 50), 
                                   LV_PART_MAIN);
    }
}