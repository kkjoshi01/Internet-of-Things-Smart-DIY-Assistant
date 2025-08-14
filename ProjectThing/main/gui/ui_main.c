// Main UI Code, handles all the connections to other screens and shows the main UI elements visible
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_check.h"
#include "bsp_board.h"
#include "bsp/esp-bsp.h"
#include "lvgl.h"
#include "lv_symbol_extra_def.h"
#include "ui_aboutus.h"
#include "ui_wifi.h"
#include "ui_main.h"
#include "ui_assistant.h"
#include "audio_manager.h"

// Images
#include "gui/image/info.h"
#include "gui/image/volume_on.h"
#include "gui/image/volume_off.h"
#include "gui/image/mic_logo.h"
#include "gui/image/cloud_connected.h"
#include "gui/image/cloud_not_connected.h"
#include "gui/image/wifi_icon.h"
#include "gui/image/no_wifi_icon.h"



static const char *TAG = "ui_main";

SemaphoreHandle_t g_guisemaphore;

// Global UI elements for easy access and updates
static lv_obj_t *g_main_screen = NULL;
static lv_obj_t *g_time_label = NULL;
static lv_obj_t *g_day_label = NULL;
static lv_obj_t *g_date_label = NULL;
static lv_obj_t *g_weather_label = NULL;
static lv_obj_t *g_city_label = NULL;
static lv_obj_t *g_wifi_btn = NULL;
static lv_obj_t *g_audio_btn = NULL;
static lv_obj_t *g_cloud_label = NULL;
static lv_obj_t *g_info_btn = NULL;
static lv_obj_t *g_voice_btn = NULL;

// Track current states
static lv_obj_t *g_wifi_icon = NULL;
static lv_obj_t *g_audio_icon = NULL;
static bool g_wifi_connected = false;
static bool g_cloud_connected = false;
static bool g_time_synced = false;

void ui_acquire(void)
{
    bsp_display_lock(0);
}

void ui_release(void)
{
    bsp_display_unlock();
}

bool ui_main_is_wifi_connected(void) {
    return g_wifi_connected;
}

// Handle wifi button
static void wifi_btn_clicked(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "WiFi button clicked - navigating to WiFi screen");
        ui_wifi_show_screen();
    }
}

void ui_main_update_audio_status(bool audio_on)
{
    if (g_audio_icon) {
        lv_img_set_src(g_audio_icon, audio_on ? &volume_on : &volume_off);
    }
}

// Handle audio button
static void audio_btn_clicked(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Audio button clicked - toggling audio");
        
        audio_toggle();
        
        bool audio_on = audio_is_on();
        ui_main_update_audio_status(audio_on);
        
        ESP_LOGI(TAG, "Audio is now %s", audio_on ? "ON" : "OFF");
    }
}

// Handle info button for about us page
static void info_btn_clicked(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Info button clicked - navigating to about us screen");
        ui_aboutus_show_screen();
    }
}

// Handle voice button
static void voice_btn_clicked(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Voice button clicked");
        ui_assistant_show_screen();
    }
}

// Load main screen
void ui_main_show_screen(void)
{
    if (!g_main_screen) {
        ui_main_start();
    } else {
        lv_scr_load(g_main_screen);
        ESP_LOGI(TAG, "Main screen loaded");
    }
}

void ui_main_set_time_synced(bool synced) {
    g_time_synced = synced;
}

// Update time
static void time_update_cb(lv_timer_t *timer)
{
    if (!g_time_label) return;
    if (!g_time_synced) {
        lv_label_set_text(g_time_label, "--:--");
        lv_label_set_text(g_day_label, "Updating...");
        lv_label_set_text(g_date_label, "");
        return;
    }
    
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    // Update time (HH:MM format)
    lv_label_set_text_fmt(g_time_label, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    
    // Update day and date
    if (g_day_label && g_date_label) {
        const char* days[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
        const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", 
                               "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
        
        lv_label_set_text(g_day_label, days[timeinfo.tm_wday]);
        lv_label_set_text_fmt(g_date_label, "%d %s", timeinfo.tm_mday, months[timeinfo.tm_mon]);
    }
}

// Function to update weather info
void ui_main_update_weather(const char* temperature, const char* city)
{
    if (g_weather_label && temperature) {
        lv_label_set_text(g_weather_label, temperature);
    }
    if (g_city_label && city) {
        lv_label_set_text(g_city_label, city);
    }
}

// Function to update WiFi status
void ui_main_update_wifi_status(bool connected)
{
    g_wifi_connected = connected;
    
    if (g_wifi_icon) {
        lv_img_set_src(g_wifi_icon, connected ? &wifi_icon : &no_wifi_icon);
    }
    ui_aboutus_update_wifi_status(connected);
}

// Function to update cloud status
void ui_main_update_cloud_status(bool connected)
{
    g_cloud_connected = connected; // Store the status
    
    if (g_cloud_label) {
        lv_img_set_src(g_cloud_label, connected ? &cloud_connected : &cloud_not_connected);
    }
    
    // Update about us screen if it exists
    ui_aboutus_update_cloud_status(connected);
}

esp_err_t ui_main_start(void)
{
    if (g_main_screen) {
        lv_scr_load(g_main_screen);
        ESP_LOGI(TAG, "Main screen already exists, loading.");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Creating main screen UI");

    audio_init();

    // Create main screen
    g_main_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(g_main_screen, lv_color_make(240, 245, 250), LV_STATE_DEFAULT);
    lv_scr_load(g_main_screen);

    // Info button
    g_info_btn = lv_btn_create(g_main_screen);
    lv_obj_set_size(g_info_btn, 40, 40);
    lv_obj_align(g_info_btn, LV_ALIGN_TOP_LEFT, 15, 15);
    lv_obj_set_style_bg_color(g_info_btn, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_radius(g_info_btn, 20, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(g_info_btn, 5, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(g_info_btn, LV_OPA_20, LV_PART_MAIN);
    lv_obj_add_event_cb(g_info_btn, info_btn_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *info_icon = lv_img_create(g_info_btn);
    lv_img_set_src(info_icon, &info);
    lv_obj_center(info_icon);

    // WiFi Button
    g_wifi_btn = lv_btn_create(g_main_screen);
    lv_obj_set_size(g_wifi_btn, 40, 40);
    lv_obj_align(g_wifi_btn, LV_ALIGN_TOP_RIGHT, -15, 15);
    lv_obj_set_style_bg_color(g_wifi_btn, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_radius(g_wifi_btn, 20, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(g_wifi_btn, 5, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(g_wifi_btn, LV_OPA_20, LV_PART_MAIN);
    lv_obj_add_event_cb(g_wifi_btn, wifi_btn_clicked, LV_EVENT_CLICKED, NULL);

    g_wifi_icon = lv_img_create(g_wifi_btn);
    lv_img_set_src(g_wifi_icon, &no_wifi_icon);
    lv_obj_center(g_wifi_icon);

    // Audio Button
    g_audio_btn = lv_btn_create(g_main_screen);
    lv_obj_set_size(g_audio_btn, 40, 40);
    lv_obj_align_to(g_audio_btn, g_wifi_btn, LV_ALIGN_OUT_LEFT_MID, -10, 0);
    lv_obj_set_style_bg_color(g_audio_btn, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_radius(g_audio_btn, 20, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(g_audio_btn, 5, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(g_audio_btn, LV_OPA_20, LV_PART_MAIN);
    lv_obj_add_event_cb(g_audio_btn, audio_btn_clicked, LV_EVENT_CLICKED, NULL);

    g_audio_icon = lv_img_create(g_audio_btn);
    lv_img_set_src(g_audio_icon, &volume_on);
    lv_obj_center(g_audio_icon);

    // Cloud Status
    g_cloud_label = lv_img_create(g_main_screen);
    lv_img_set_src(g_cloud_label, &cloud_not_connected);
    lv_obj_align_to(g_cloud_label, g_audio_btn, LV_ALIGN_OUT_LEFT_MID, -15, 0);

    // Display time
    g_time_label = lv_label_create(g_main_screen);
    lv_label_set_text_static(g_time_label, "00:00");
    lv_obj_set_style_text_font(g_time_label, &lv_font_montserrat_32, LV_PART_MAIN);
    lv_obj_set_style_text_color(g_time_label, lv_color_make(50, 50, 50), LV_PART_MAIN);
    lv_obj_align(g_time_label, LV_ALIGN_CENTER, 0, -30);

    // Voice button
    g_voice_btn = lv_btn_create(g_main_screen);
    lv_obj_set_size(g_voice_btn, 60, 60);
    lv_obj_align_to(g_voice_btn, g_time_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 30);
    lv_obj_set_style_bg_color(g_voice_btn, lv_color_make(100, 150, 255), LV_PART_MAIN);
    lv_obj_set_style_radius(g_voice_btn, 30, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(g_voice_btn, 8, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(g_voice_btn, LV_OPA_30, LV_PART_MAIN);
    // lv_obj_add_event_cb(g_voice_btn, voice_btn_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *voice_icon = lv_img_create(g_voice_btn);
    lv_img_set_src(voice_icon, &mic_logo);
    lv_obj_center(voice_icon);

    // Display day and time
    g_day_label = lv_label_create(g_main_screen);
    lv_label_set_text_static(g_day_label, "Monday");
    lv_obj_set_style_text_font(g_day_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(g_day_label, lv_color_make(70, 70, 70), LV_PART_MAIN);
    lv_obj_align(g_day_label, LV_ALIGN_BOTTOM_LEFT, 15, -50);

    g_date_label = lv_label_create(g_main_screen);
    lv_label_set_text_static(g_date_label, "12 Mar");
    lv_obj_set_style_text_font(g_date_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(g_date_label, lv_color_make(120, 120, 120), LV_PART_MAIN);
    lv_obj_align_to(g_date_label, g_day_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 2);

    // Display weather and city
    g_weather_label = lv_label_create(g_main_screen);
    lv_label_set_text_static(g_weather_label, "13°C");
    lv_obj_set_style_text_font(g_weather_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(g_weather_label, lv_color_make(70, 70, 70), LV_PART_MAIN);
    lv_obj_align(g_weather_label, LV_ALIGN_BOTTOM_RIGHT, -15, -50);

    g_city_label = lv_label_create(g_main_screen);
    lv_label_set_text_static(g_city_label, "Sheffield");
    lv_obj_set_style_text_font(g_city_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(g_city_label, lv_color_make(120, 120, 120), LV_PART_MAIN);
    lv_obj_align_to(g_city_label, g_weather_label, LV_ALIGN_OUT_BOTTOM_RIGHT, 0, 2);

    lv_timer_t *timer = lv_timer_create(time_update_cb, 1000, NULL);
    time_update_cb(timer);

    lv_scr_load(g_main_screen);

    // Set initial status
    ui_main_update_wifi_status(false);
    ui_main_update_audio_status(audio_is_on());
    ui_main_update_cloud_status(false);

    ESP_LOGI(TAG, "Main screen UI created successfully");

    return ESP_OK;
}