#include "otto_emoji_display.h"

#include <esp_log.h>
#include <cstring>

#include "assets/lang_config.h"
#include "display/lvgl_display/emoji_collection.h"
#include "display/lvgl_display/lvgl_image.h"
#include "display/lvgl_display/lvgl_theme.h"
#include "otto_emoji_gif.h"

#define TAG "OttoEmojiDisplay"

OttoEmojiDisplay::OttoEmojiDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width, int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y, bool swap_xy)
    : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y, mirror_x, mirror_y, swap_xy) {
    InitializeOttoEmojis();
    SetupPreviewImage();
    SetTheme(LvglThemeManager::GetInstance().GetTheme("dark"));
}

void OttoEmojiDisplay::SetupPreviewImage() {
    DisplayLockGuard lock(this);
    lv_obj_set_size(preview_image_, width_ , height_ );
}

void OttoEmojiDisplay::InitializeOttoEmojis() {
    ESP_LOGI(TAG, "Initialize Otto GIFs");

    auto otto_emoji_collection = std::make_shared<EmojiCollection>();

    // 1. IDLE - Dùng ảnh tĩnh hoặc biểu cảm nhẹ
    otto_emoji_collection->AddEmoji("idle", new LvglRawImage((void*)staticstate.data, staticstate.data_size));
    otto_emoji_collection->AddEmoji("neutral", new LvglRawImage((void*)staticstate.data, staticstate.data_size));

    // 2. LISTENING - Biểu cảm suy nghĩ hoặc chờ đợi
    // (Dùng 'buxue' hoặc 'thinking' từ file asset bạn đã có)
    otto_emoji_collection->AddEmoji("listening", new LvglRawImage((void*)buxue.data, buxue.data_size));
    otto_emoji_collection->AddEmoji("thinking", new LvglRawImage((void*)buxue.data, buxue.data_size));

    // 3. SPEAKING - Biểu cảm vui/nói
    otto_emoji_collection->AddEmoji("speaking", new LvglRawImage((void*)happy.data, happy.data_size));
    otto_emoji_collection->AddEmoji("happy", new LvglRawImage((void*)happy.data, happy.data_size));

    // Các biểu cảm khác
    otto_emoji_collection->AddEmoji("sad", new LvglRawImage((void*)sad.data, sad.data_size));
    otto_emoji_collection->AddEmoji("angry", new LvglRawImage((void*)anger.data, anger.data_size));
    otto_emoji_collection->AddEmoji("surprised", new LvglRawImage((void*)scare.data, scare.data_size));

    auto& theme_manager = LvglThemeManager::GetInstance();
    auto light_theme = theme_manager.GetTheme("light");
    auto dark_theme = theme_manager.GetTheme("dark");

    if (light_theme) light_theme->set_emoji_collection(otto_emoji_collection);
    if (dark_theme) dark_theme->set_emoji_collection(otto_emoji_collection);

    // Mặc định ban đầu
    SetEmotion("idle");
}

LV_FONT_DECLARE(OTTO_ICON_FONT);

// HÀM NÀY QUAN TRỌNG: ĐỒNG BỘ TRẠNG THÁI HỆ THỐNG VỚI MÀN HÌNH
void OttoEmojiDisplay::SetStatus(const char* status) {
    // Gọi hàm cha để xử lý text hiển thị (nếu có)
    SpiLcdDisplay::SetStatus(status);
    
    if (!status) return;

    // --- LOGIC HIỂN THỊ MÀN HÌNH THEO TRẠNG THÁI ---
    
    // 1. LISTENING (Đang nghe/Kích hoạt)
    if (strcmp(status, Lang::Strings::LISTENING) == 0) {
        // Set Emoji: Suy nghĩ/Nghe
        SetEmotion("listening"); 
        ESP_LOGI(TAG, "Display: LISTENING");
    } 
    // 2. SPEAKING (Đang nói)
    else if (strcmp(status, Lang::Strings::SPEAKING) == 0) {
        // Set Emoji: Vui vẻ/Nói
        SetEmotion("speaking");
        ESP_LOGI(TAG, "Display: SPEAKING");
    } 
    // 3. CONNECTING / STANDBY / IDLE
    else if (strcmp(status, Lang::Strings::STANDBY) == 0 || strcmp(status, "") == 0) {
        // Set Emoji: Tĩnh/Nghỉ
        SetEmotion("idle");
        ESP_LOGI(TAG, "Display: IDLE");
    }
}

void OttoEmojiDisplay::SetPreviewImage(std::unique_ptr<LvglImage> image) {
    DisplayLockGuard lock(this);
    if (preview_image_ == nullptr) return;

    if (image == nullptr) {
        esp_timer_stop(preview_timer_);
        lv_obj_remove_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
        preview_image_cached_.reset();
        if (gif_controller_) {
            gif_controller_->Start();
        }
        return;
    }

    preview_image_cached_ = std::move(image);
    auto img_dsc = preview_image_cached_->image_dsc();
    lv_image_set_src(preview_image_, img_dsc);
    lv_image_set_rotation(preview_image_, -900);
    if (img_dsc->header.w > 0 && img_dsc->header.h > 0) {
        lv_image_set_scale(preview_image_, 256 * width_ / img_dsc->header.w);
    }

    if (gif_controller_) {
        gif_controller_->Stop();
    }
    lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
    esp_timer_stop(preview_timer_);
    ESP_ERROR_CHECK(esp_timer_start_once(preview_timer_, PREVIEW_IMAGE_DURATION_MS * 1000));
}