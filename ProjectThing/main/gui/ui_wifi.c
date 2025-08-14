#include "lvgl.h"
#include "ui_wifi.h" 
#include "ui_main.h" 
#include "app_net.h" 
#include "esp_log.h" 

static lv_obj_t *g_wifi_screen = NULL;
static lv_obj_t *g_qr_obj = NULL;
static lv_obj_t *g_qr_web_obj = NULL;
static lv_obj_t *g_ip_label = NULL;
static lv_obj_t *g_back_btn = NULL;

static const char *TAG = "UI_WIFI";
static const char *WIFI_IP_ADDR = "192.168.4.1";
static const char *QR_LINK = "WIFI:T:WPA;S:SmartESP;P:dumbpassword;;";
static const char *SSID = "SmartESP";
static const char *PASSWORD = "dumbpassword"; // For display

static void back_btn_clicked(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        ui_main_show_screen();
    }
}

esp_err_t ui_wifi_start(void)
{
    if (g_wifi_screen) {
        lv_scr_load(g_wifi_screen);
        ESP_LOGI(TAG, "WiFi screen already created, loading it.");
        return ESP_OK;
    }

    // Create main screen
    g_wifi_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(g_wifi_screen, lv_color_make(240, 245, 250), LV_STATE_DEFAULT);
    lv_obj_set_scroll_dir(g_wifi_screen, LV_DIR_VER);
    lv_obj_set_style_pad_ver(g_wifi_screen, 24, 0);
    lv_obj_set_style_pad_hor(g_wifi_screen, 12, 0);

    // Back button
    g_back_btn = lv_btn_create(g_wifi_screen);
    lv_obj_set_size(g_back_btn, 40, 40);
    lv_obj_align(g_back_btn, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_add_event_cb(g_back_btn, back_btn_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *back_label = lv_label_create(g_back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT);
    lv_obj_center(back_label);

    // title
    lv_obj_t *title_label = lv_label_create(g_wifi_screen);
    lv_label_set_text_fmt(title_label, "Connect to WiFi: %s", SSID);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_22, LV_PART_MAIN);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 8 + 40);

    // Display password
    lv_obj_t *pw_label = lv_label_create(g_wifi_screen);
    lv_label_set_text_fmt(pw_label, "Password: %s", PASSWORD);
    lv_obj_set_style_text_font(pw_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align_to(pw_label, title_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);

    // QR code to connect to wifi
    g_qr_obj = lv_qrcode_create(g_wifi_screen, 180, lv_color_black(), lv_color_white());
    lv_qrcode_update(g_qr_obj, QR_LINK, strlen(QR_LINK));
    lv_obj_align_to(g_qr_obj, title_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 32);
    lv_obj_t *qr_label = lv_label_create(g_wifi_screen);
    lv_label_set_text(qr_label, "Scan to connect to WiFi");
    lv_obj_set_style_text_font(qr_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align_to(qr_label, g_qr_obj, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

    // QR code to access web interface
    char web_link[64];
    snprintf(web_link, sizeof(web_link), "http://%s", WIFI_IP_ADDR);
    g_qr_web_obj = lv_qrcode_create(g_wifi_screen, 180, lv_color_black(), lv_color_white());
    lv_qrcode_update(g_qr_web_obj, web_link, strlen(web_link));
    lv_obj_align_to(g_qr_web_obj, qr_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 18);
    lv_obj_t *qr_web_label = lv_label_create(g_wifi_screen);
    lv_label_set_text(qr_web_label, "Scan to access web interface");
    lv_obj_set_style_text_font(qr_web_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align_to(qr_web_label, g_qr_web_obj, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

    // Display IP address
    g_ip_label = lv_label_create(g_wifi_screen);
    lv_label_set_text_fmt(g_ip_label, "Or go to: http://%s", WIFI_IP_ADDR);
    lv_obj_set_style_text_font(g_ip_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align_to(g_ip_label, qr_web_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 16);

    lv_obj_update_layout(g_wifi_screen);

    lv_scr_load(g_wifi_screen);
    return ESP_OK;
}

void ui_wifi_show_screen(void)
{
    ui_wifi_start();
}
