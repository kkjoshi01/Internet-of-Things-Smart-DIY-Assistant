#include "lvgl.h"
#include "ui_conversation.h"
#include "ui_main.h"

static lv_obj_t *g_convo_screen = NULL;

void ui_assistant_cancel_timer(void);

static void back_btn_clicked(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        ui_assistant_cancel_timer();
        ui_main_show_screen();
    }
}

void ui_conversation_show_screen(const char *user, const char *assistant) {
    if (g_convo_screen) {
        lv_scr_load(g_convo_screen);
        return;
    }

    g_convo_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(g_convo_screen, lv_color_make(245, 245, 245), LV_STATE_DEFAULT);

    // Back button
    lv_obj_t *back_btn = lv_btn_create(g_convo_screen);
    lv_obj_set_size(back_btn, 40, 40);
    lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 15, 15);
    lv_obj_add_event_cb(back_btn, back_btn_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT);
    lv_obj_center(back_label);

    // User's speech
    lv_obj_t *user_label = lv_label_create(g_convo_screen);
    lv_label_set_text_fmt(user_label, "You: %s", user ? user : "(none)");
    lv_obj_align(user_label, LV_ALIGN_TOP_MID, 0, 60);

    // Assistant's response
    lv_obj_t *assistant_label = lv_label_create(g_convo_screen);
    lv_label_set_text_fmt(assistant_label, "Assistant: %s", assistant ? assistant : "(none)");
    lv_obj_align(assistant_label, LV_ALIGN_TOP_MID, 0, 120);

    lv_scr_load(g_convo_screen);
}
