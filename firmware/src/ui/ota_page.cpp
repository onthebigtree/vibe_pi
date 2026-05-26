#include "ota_page.h"
#include "theme.h"
#include "../system/i18n.h"
#include "../system/ota_manager.h"
#include "config.h"

static lv_obj_t *page = nullptr;
static lv_obj_t *lbl_status = nullptr;
static lv_obj_t *lbl_version = nullptr;
static lv_obj_t *lbl_changelog = nullptr;
static lv_obj_t *bar_progress = nullptr;
static lv_obj_t *lbl_progress = nullptr;
static lv_obj_t *btn_action = nullptr;

static void on_accept_update(lv_event_t *e) {
    ota_start_download();
}

lv_obj_t *ota_page_create(lv_obj_t *parent) {
    page = lv_obj_create(parent);
    lv_obj_set_size(page, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(page, 0, 0);

    lv_obj_t *title = lv_label_create(page);
    lv_label_set_text(title, i18n(S_UPDATE_AVAILABLE));
    lv_obj_set_style_text_color(title, CLR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(title, FONT_LARGE, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 75);

    lbl_version = lv_label_create(page);
    lv_label_set_text(lbl_version, "");
    lv_obj_set_style_text_color(lbl_version, CLR_ACCENT, 0);
    lv_obj_set_style_text_font(lbl_version, FONT_BODY, 0);
    lv_obj_align(lbl_version, LV_ALIGN_CENTER, 0, -50);

    lbl_changelog = lv_label_create(page);
    lv_label_set_text(lbl_changelog, "");
    lv_obj_set_style_text_color(lbl_changelog, CLR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(lbl_changelog, FONT_SMALL, 0);
    lv_obj_set_width(lbl_changelog, 320);
    lv_obj_set_style_text_align(lbl_changelog, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(lbl_changelog, LV_LABEL_LONG_WRAP);
    lv_obj_align(lbl_changelog, LV_ALIGN_CENTER, 0, -15);

    bar_progress = lv_bar_create(page);
    lv_obj_set_size(bar_progress, 300, 8);
    lv_obj_set_style_bg_color(bar_progress, CLR_BORDER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_progress, CLR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar_progress, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(bar_progress, 4, LV_PART_INDICATOR);
    lv_bar_set_range(bar_progress, 0, 100);
    lv_bar_set_value(bar_progress, 0, LV_ANIM_ON);
    lv_obj_align(bar_progress, LV_ALIGN_CENTER, 0, 30);
    lv_obj_add_flag(bar_progress, LV_OBJ_FLAG_HIDDEN);

    lbl_progress = lv_label_create(page);
    lv_label_set_text(lbl_progress, "");
    lv_obj_set_style_text_color(lbl_progress, CLR_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(lbl_progress, FONT_SMALL, 0);
    lv_obj_align(lbl_progress, LV_ALIGN_CENTER, 0, 50);

    lbl_status = lv_label_create(page);
    lv_label_set_text(lbl_status, "");
    lv_obj_set_style_text_color(lbl_status, CLR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(lbl_status, FONT_SMALL, 0);
    lv_obj_align(lbl_status, LV_ALIGN_CENTER, 0, 80);

    btn_action = lv_btn_create(page);
    lv_obj_set_size(btn_action, 200, 45);
    lv_obj_set_style_bg_color(btn_action, CLR_ACCENT, 0);
    lv_obj_set_style_radius(btn_action, 22, 0);
    lv_obj_align(btn_action, LV_ALIGN_CENTER, 0, 110);
    lv_obj_add_event_cb(btn_action, on_accept_update, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_flag(btn_action, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *btn_lbl = lv_label_create(btn_action);
    lv_label_set_text(btn_lbl, "Update Now");
    lv_obj_set_style_text_color(btn_lbl, lv_color_black(), 0);
    lv_obj_set_style_text_font(btn_lbl, FONT_BODY, 0);
    lv_obj_center(btn_lbl);

    return page;
}

void ota_page_refresh() {
    if (!page) return;

    OtaState st = ota_get_state();
    OtaInfo &inf = ota_get_info();

    switch (st) {
        case OtaState::IDLE:
            lv_label_set_text(lbl_version, FW_VERSION);
            lv_label_set_text(lbl_status, i18n(S_UP_TO_DATE));
            lv_obj_add_flag(bar_progress, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(btn_action, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(lbl_changelog, "");
            lv_label_set_text(lbl_progress, "");
            break;

        case OtaState::AVAILABLE:
            lv_label_set_text_fmt(lbl_version, "%s → %s", FW_VERSION, inf.version);
            lv_label_set_text(lbl_changelog,
                (i18n_get_lang() == Lang::ZH && strlen(inf.changelog_zh) > 0)
                    ? inf.changelog_zh : inf.changelog);
            lv_label_set_text(lbl_status, "");
            lv_obj_add_flag(bar_progress, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(btn_action, LV_OBJ_FLAG_HIDDEN);
            break;

        case OtaState::DOWNLOADING:
            lv_label_set_text(lbl_status, i18n(S_DOWNLOADING));
            lv_obj_remove_flag(bar_progress, LV_OBJ_FLAG_HIDDEN);
            lv_bar_set_value(bar_progress, ota_get_progress(), LV_ANIM_ON);
            lv_label_set_text_fmt(lbl_progress, "%d%%", ota_get_progress());
            lv_obj_add_flag(btn_action, LV_OBJ_FLAG_HIDDEN);
            break;

        case OtaState::VERIFYING:
            lv_label_set_text(lbl_status, "Verifying...");
            lv_bar_set_value(bar_progress, 100, LV_ANIM_ON);
            break;

        case OtaState::INSTALLING:
            lv_label_set_text(lbl_status, i18n(S_INSTALLING));
            break;

        case OtaState::SUCCESS:
            lv_label_set_text(lbl_status, i18n(S_UPDATE_SUCCESS));
            lv_obj_set_style_text_color(lbl_status, CLR_SUCCESS, 0);
            lv_obj_add_flag(bar_progress, LV_OBJ_FLAG_HIDDEN);
            break;

        case OtaState::FAILED:
            lv_label_set_text_fmt(lbl_status, "%s: %s", i18n(S_UPDATE_FAILED), ota_get_error());
            lv_obj_set_style_text_color(lbl_status, CLR_ERROR, 0);
            lv_obj_add_flag(bar_progress, LV_OBJ_FLAG_HIDDEN);
            break;
    }
}
