#include "lvgl.h"
#include "ui_main.h"
#include "ui_assistant.h"
#include "ui_conversation.h"

// Import the mic image asset (same as ui_main.c)
#include "gui/image/mic_logo.h"

static lv_obj_t *g_assistant_screen = NULL;
static lv_obj_t *g_anim_mic = NULL;
static lv_timer_t *g_timer = NULL;

void ui_assistant_cancel_timer(void) {
    if (g_timer) {
        lv_timer_del(g_timer);
        g_timer = NULL;
    }
}

static void back_btn_clicked(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        ui_main_show_screen();
        ui_assistant_cancel_timer();
    }
}

// Animation callback: scale the mic icon up/down
static void mic_anim_cb(void *obj, int32_t v) {
    lv_img_set_zoom((lv_obj_t*)obj, 256 + v); // 256 = 1x, v is the pulse
}

esp_err_t ui_assistant_start(void) {
    if (g_assistant_screen) {
        lv_scr_load(g_assistant_screen);
        return ESP_OK;
    }

    g_assistant_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(g_assistant_screen, lv_color_make(242, 243, 245), LV_PART_MAIN);

    // Back button
    lv_obj_t *back_btn = lv_btn_create(g_assistant_screen);
    lv_obj_set_size(back_btn, 40, 40);
    lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 15, 15);
    lv_obj_add_event_cb(back_btn, back_btn_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT);
    lv_obj_center(back_label);

    // Mic icon
    g_anim_mic = lv_img_create(g_assistant_screen);
    lv_img_set_src(g_anim_mic, &mic_logo);
    lv_obj_align(g_anim_mic, LV_ALIGN_CENTER, 0, -20);

    // Listening text
    lv_obj_t *listen_lbl = lv_label_create(g_assistant_screen);
    lv_label_set_text(listen_lbl, "Listening ...");
    lv_obj_set_style_text_font(listen_lbl, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align_to(listen_lbl, g_anim_mic, LV_ALIGN_OUT_BOTTOM_MID, 0, 20);

    // Animation
    static lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, g_anim_mic);
    lv_anim_set_values(&a, 0, 64);        // Pulse from 1x to 1.25x
    lv_anim_set_time(&a, 600);
    lv_anim_set_playback_time(&a, 600);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&a, mic_anim_cb);
    lv_anim_start(&a);

    if (g_timer) {
        lv_timer_del(g_timer);
    }

    lv_scr_load(g_assistant_screen);
    return ESP_OK;
}

void ui_assistant_show_screen(void) {
    ui_assistant_start();
}
