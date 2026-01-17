#include "otto_emoji_display.h"
#include <esp_log.h>
#include <cstring>
#include "assets/lang_config.h"
#include "display/lvgl_display/lvgl_theme.h"

#define TAG "OttoDisplay"

// Màu sắc chủ đạo (OPSWAT Blue)
#define COLOR_OPSWAT_BLUE lv_color_hex(0x00AEEF) 
#define COLOR_CORE_GLOW   lv_color_hex(0x00E5FF)
#define COLOR_BG_BLACK    lv_color_hex(0x000000)

OttoEmojiDisplay::OttoEmojiDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width, int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y, bool swap_xy)
    : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y, mirror_x, mirror_y, swap_xy) {
    
    // Khởi tạo Timer cho hiệu ứng nhịp thở (Pulse)
    esp_timer_create_args_t timer_args = {
        .callback = &OttoEmojiDisplay::PulseTimerCallback,
        .arg = this,
        .name = "ui_pulse"
    };
    esp_timer_create(&timer_args, &pulse_timer_);
    
    // Vẽ giao diện
    SetupOpswatUI();
}

void OttoEmojiDisplay::SetupOpswatUI() {
    DisplayLockGuard lock(this);

    // 1. Màn hình nền đen
    main_screen_ = lv_obj_create(lv_scr_act());
    lv_obj_set_size(main_screen_, width_, height_);
    lv_obj_set_style_bg_color(main_screen_, COLOR_BG_BLACK, 0);
    lv_obj_set_style_border_width(main_screen_, 0, 0);
    lv_obj_center(main_screen_);

    // 2. Vòng tròn năng lượng (Arc) - Giống lò phản ứng của Iron Man
    arc_core_ = lv_arc_create(main_screen_);
    lv_obj_set_size(arc_core_, width_ * 0.8, height_ * 0.8);
    lv_arc_set_rotation(arc_core_, 270);
    lv_arc_set_bg_angles(arc_core_, 0, 360);
    lv_arc_set_value(arc_core_, 100);
    lv_obj_remove_style(arc_core_, NULL, LV_PART_KNOB); // Bỏ núm xoay
    lv_obj_center(arc_core_);
    
    // Style cho vòng tròn
    lv_obj_set_style_arc_color(arc_core_, COLOR_OPSWAT_BLUE, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc_core_, 15, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc_core_, lv_color_hex(0x222222), LV_PART_MAIN); // Màu nền mờ
    lv_obj_set_style_arc_width(arc_core_, 15, LV_PART_MAIN);

    // 3. Logo thương hiệu nhỏ ở trên (DEEP COR / OPSWAT)
    label_brand_ = lv_label_create(main_screen_);
    lv_label_set_text(label_brand_, "OPSWAT");
    lv_obj_set_style_text_color(label_brand_, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(label_brand_, &lv_font_montserrat_14, 0); // Font nhỏ
    lv_obj_align(label_brand_, LV_ALIGN_TOP_MID, 0, 25);

    // 4. Trạng thái chính (IDLE, LISTENING...) - Font TO
    label_status_ = lv_label_create(main_screen_);
    lv_label_set_text(label_status_, "SYSTEM\nONLINE");
    lv_obj_set_style_text_color(label_status_, COLOR_OPSWAT_BLUE, 0);
    // Dùng font to có sẵn của XiaoZhi hoặc default
    lv_obj_set_style_text_align(label_status_, LV_TEXT_ALIGN_CENTER, 0);
    // Nếu bạn muốn font to hơn nữa, cần add thêm font, tạm thời dùng font lớn nhất có sẵn
    auto theme = LvglThemeManager::GetInstance().GetTheme("dark");
    if (theme) {
        lv_obj_set_style_text_font(label_status_, static_cast<LvglTheme*>(theme)->text_font()->font(), 0);
    }
    lv_obj_center(label_status_);

    // Bắt đầu hiệu ứng
    esp_timer_start_periodic(pulse_timer_, 50 * 1000); // 50ms cập nhật 1 lần
}

// Hàm cập nhật hiệu ứng (Pulse)
void OttoEmojiDisplay::PulseTimerCallback(void* arg) {
    auto display = static_cast<OttoEmojiDisplay*>(arg);
    DisplayLockGuard lock(display);

    if (display->is_speaking_) {
        // Hiệu ứng đập nhanh khi nói
        display->pulse_val_ += 5 * display->pulse_dir_;
        if (display->pulse_val_ >= 100) display->pulse_dir_ = -1;
        if (display->pulse_val_ <= 20) display->pulse_dir_ = 1;
        
        // Thay đổi độ dày vòng tròn theo nhịp nói
        lv_obj_set_style_arc_width(display->arc_core_, 10 + (display->pulse_val_ / 5), LV_PART_INDICATOR);
        lv_obj_set_style_shadow_width(display->arc_core_, display->pulse_val_ / 2, LV_PART_INDICATOR);
        lv_obj_set_style_shadow_color(display->arc_core_, COLOR_CORE_GLOW, LV_PART_INDICATOR);
    } else {
        // Hiệu ứng thở nhẹ khi IDLE
        display->pulse_val_ += 2 * display->pulse_dir_;
        if (display->pulse_val_ >= 80) display->pulse_dir_ = -1;
        if (display->pulse_val_ <= 40) display->pulse_dir_ = 1;
        
        lv_obj_set_style_shadow_width(display->arc_core_, 0, LV_PART_INDICATOR); // Tắt shadow
        lv_obj_set_style_arc_width(display->arc_core_, 15, LV_PART_INDICATOR); // Độ dày cố định
    }
}

void OttoEmojiDisplay::SetStatus(const char* status) {
    // Gọi class cha để xử lý các logic nền
    SpiLcdDisplay::SetStatus(status);
    if (!status) return;

    DisplayLockGuard lock(this);
    
    // --- LOGIC HIỂN THỊ THEO PDF ---
    
    // 1. LISTENING
    if (strcmp(status, Lang::Strings::LISTENING) == 0) {
        is_speaking_ = false;
        lv_label_set_text(label_status_, "LISTENING");
        lv_obj_set_style_text_color(label_status_, lv_color_hex(0x00FF00), 0); // Chữ Xanh lá
        lv_obj_set_style_arc_color(arc_core_, lv_color_hex(0x00FF00), LV_PART_INDICATOR);
        ESP_LOGI(TAG, "UI: LISTENING MODE");
    } 
    // 2. SPEAKING
    else if (strcmp(status, Lang::Strings::SPEAKING) == 0) {
        is_speaking_ = true; // Kích hoạt hiệu ứng Pulse mạnh
        lv_label_set_text(label_status_, "SPEAKING");
        lv_obj_set_style_text_color(label_status_, lv_color_hex(0xFF0000), 0); // Chữ Đỏ (hoặc Cam)
        lv_obj_set_style_arc_color(arc_core_, lv_color_hex(0xFF0000), LV_PART_INDICATOR);
        ESP_LOGI(TAG, "UI: SPEAKING MODE");
    } 
    // 3. IDLE / STANDBY
    else if (strcmp(status, Lang::Strings::STANDBY) == 0 || strcmp(status, "") == 0) {
        is_speaking_ = false;
        lv_label_set_text(label_status_, "IDLE");
        lv_obj_set_style_text_color(label_status_, COLOR_OPSWAT_BLUE, 0); // Chữ Xanh dương
        lv_obj_set_style_arc_color(arc_core_, COLOR_OPSWAT_BLUE, LV_PART_INDICATOR);
        ESP_LOGI(TAG, "UI: IDLE MODE");
    }
    // 4. Các trạng thái khác (Connecting...)
    else {
        is_speaking_ = false;
        lv_label_set_text(label_status_, status); // Hiện nguyên văn status hệ thống
        lv_obj_set_style_text_color(label_status_, lv_color_hex(0xFFFF00), 0); // Màu vàng
    }
}