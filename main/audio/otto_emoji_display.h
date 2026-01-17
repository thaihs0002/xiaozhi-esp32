#pragma once

#include "display/lcd_display.h"
#include "lvgl.h"

/**
 * @brief Giao diện OPSWAT / DEEP COR cho Robot Otto
 * Thay thế Emoji bằng giao diện HUD (Head Up Display) công nghệ cao
 */
class OttoEmojiDisplay : public SpiLcdDisplay {
   public:
    OttoEmojiDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width, int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y, bool swap_xy);

    virtual ~OttoEmojiDisplay() = default;
    virtual void SetStatus(const char* status) override;
    // Hàm này giữ lại để tránh lỗi compile nhưng sẽ không dùng tới GIF
    virtual void SetPreviewImage(std::unique_ptr<LvglImage> image) override {}; 

   private:
    void SetupOpswatUI();
    void UpdateUIState(const char* status);

    // Các thành phần giao diện LVGL
    lv_obj_t* main_screen_ = nullptr;
    lv_obj_t* arc_core_ = nullptr;      // Vòng tròn năng lượng ở giữa
    lv_obj_t* label_status_ = nullptr;  // Chữ trạng thái lớn (IDLE, SPEAKING...)
    lv_obj_t* label_brand_ = nullptr;   // Chữ nhỏ (DEEP COR / OPSWAT)
    lv_obj_t* led_indicator_ = nullptr; // Điểm sáng trang trí
    
    // Timer để tạo hiệu ứng nhịp thở cho vòng tròn
    esp_timer_handle_t pulse_timer_ = nullptr;
    static void PulseTimerCallback(void* arg);
    bool is_speaking_ = false;
    int pulse_val_ = 0;
    int pulse_dir_ = 1;
};