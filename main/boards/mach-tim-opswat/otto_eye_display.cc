#include "otto_eye_display.h"
#include "otto_body.h"
#include <lvgl.h>
#include <esp_log.h>
#include <esp_random.h>
#include <cstring>

#define TAG "OttoEyeDisplay"

// === Configuration: "Soulful" & "Cute" ===
#define EYE_WIDTH_DEFAULT 38  // +25%
#define EYE_HEIGHT_DEFAULT 38 // +25%
#define PUPIL_SIZE 18         // Scaled up
#define EYE_GAP 44          
#define EYE_Y_OFFSET -2     

// Idle Timing
#define IDLE_INTERVAL_MIN_MS 2000
#define IDLE_INTERVAL_MAX_MS 5000

// Animation Durations
#define BLINK_SPEED 180
#define BREATH_PERIOD_MS 3000
#define SACCADE_SPEED 300

OttoEyeDisplay::OttoEyeDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                               int width, int height, bool mirror_x, bool mirror_y,
                               OttoBody* body)
    : OledDisplay(panel_io, panel, width, height, mirror_x, mirror_y),
      left_eye_(nullptr), right_eye_(nullptr),
      left_pupil_(nullptr), right_pupil_(nullptr),
      ip_label_(nullptr),
      is_speaking_(false),
      is_hacked_(false),
      body_(body),
      idle_timer_(nullptr),
      speak_timer_(nullptr),
      matrix_timer_(nullptr) {
    
    SetupUI();
    StartBreathingAnim(); // The "Soul" - always running
    StartIdleLoop();      // The "Brain" - random decisions
}

OttoEyeDisplay::~OttoEyeDisplay() {
    if (idle_timer_) {
        esp_timer_stop(idle_timer_);
        esp_timer_delete(idle_timer_);
    }
    if (speak_timer_) {
        esp_timer_stop(speak_timer_);
        esp_timer_delete(speak_timer_);
    }
    if (matrix_timer_) {
        esp_timer_stop(matrix_timer_);
        esp_timer_delete(matrix_timer_);
    }
}

void OttoEyeDisplay::SetupUI() {
    DisplayLockGuard lock(this);
    
    // 1. Aggressively clean everything on ALL layers
    lv_obj_t* screen = lv_screen_active();
    lv_obj_clean(screen);
    
    lv_obj_t* top_layer = lv_layer_top();
    lv_obj_clean(top_layer);
    
    lv_obj_t* sys_layer = lv_layer_sys();
    lv_obj_clean(sys_layer);
    
    // 2. Clear background style of screen
    // Background: White (Hardware Inverted -> Black)
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    // Left Eye: Black (Hardware Inverted -> White)
    left_eye_ = lv_obj_create(screen);
    lv_obj_set_size(left_eye_, EYE_WIDTH_DEFAULT, EYE_HEIGHT_DEFAULT);
    lv_obj_set_style_radius(left_eye_, LV_RADIUS_CIRCLE, 0); 
    lv_obj_set_style_bg_color(left_eye_, lv_color_black(), 0);
    lv_obj_set_style_border_width(left_eye_, 0, 0);
    lv_obj_set_scrollbar_mode(left_eye_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_align(left_eye_, LV_ALIGN_CENTER, -(EYE_WIDTH_DEFAULT/2 + EYE_GAP/2), EYE_Y_OFFSET);

    // Right Eye: Black (Hardware Inverted -> White)
    right_eye_ = lv_obj_create(screen);
    lv_obj_set_size(right_eye_, EYE_WIDTH_DEFAULT, EYE_HEIGHT_DEFAULT);
    lv_obj_set_style_radius(right_eye_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(right_eye_, lv_color_black(), 0);
    lv_obj_set_style_border_width(right_eye_, 0, 0);
    lv_obj_set_scrollbar_mode(right_eye_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_align(right_eye_, LV_ALIGN_CENTER, (EYE_WIDTH_DEFAULT/2 + EYE_GAP/2), EYE_Y_OFFSET);

    // Left Pupil: White (Hardware Inverted -> Black)
    left_pupil_ = lv_obj_create(left_eye_);
    lv_obj_set_size(left_pupil_, PUPIL_SIZE, PUPIL_SIZE);
    lv_obj_set_style_radius(left_pupil_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(left_pupil_, lv_color_white(), 0);
    lv_obj_set_style_border_width(left_pupil_, 0, 0);
    lv_obj_center(left_pupil_);

    // Right Pupil: White (Hardware Inverted -> Black)
    right_pupil_ = lv_obj_create(right_eye_);
    lv_obj_set_size(right_pupil_, PUPIL_SIZE, PUPIL_SIZE);
    lv_obj_set_style_radius(right_pupil_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(right_pupil_, lv_color_white(), 0);
    lv_obj_set_style_border_width(right_pupil_, 0, 0);
    lv_obj_center(right_pupil_);
    
    // IP Label: Black (Hardware Inverted -> White)
    ip_label_ = lv_label_create(screen);
    lv_obj_set_style_text_color(ip_label_, lv_color_black(), 0);
    lv_obj_set_style_text_align(ip_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(ip_label_, 128);
    lv_obj_align(ip_label_, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_obj_add_flag(ip_label_, LV_OBJ_FLAG_HIDDEN);
}

// === Animation Primitive Setters ===
void OttoEyeDisplay::AnimSetHeight(void* var, int32_t v) { lv_obj_set_height((lv_obj_t*)var, v); }
void OttoEyeDisplay::AnimSetWidth(void* var, int32_t v) { lv_obj_set_width((lv_obj_t*)var, v); }
// Use Direct Y for bouncing to ensure it moves physically
void OttoEyeDisplay::AnimSetY(void* var, int32_t v) { lv_obj_set_y((lv_obj_t*)var, v); }
void OttoEyeDisplay::AnimSetPupilX(void* var, int32_t v) { lv_obj_set_x((lv_obj_t*)var, v); }
void OttoEyeDisplay::AnimSetPupilY(void* var, int32_t v) { lv_obj_set_y((lv_obj_t*)var, v); }


// === The "Soul": Continuous Breathing & Bobbing ===
void OttoEyeDisplay::StartBreathingAnim() {
    DisplayLockGuard lock(this);
    
    // Clean up old
    lv_anim_del(left_eye_, AnimSetHeight);
    lv_anim_del(right_eye_, AnimSetHeight);
    lv_anim_del(left_eye_, AnimSetY);
    lv_anim_del(right_eye_, AnimSetY);

    // Speaking? Increase amplitude
    int h_amp = is_speaking_ ? 8 : 4; 
    int y_amp = is_speaking_ ? 8 : 6; // Bob up/down 6px standard
    int period = is_speaking_ ? BREATH_PERIOD_MS / 2 : BREATH_PERIOD_MS;
    
    // 1. Height Breathing (Squeeze)
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, left_eye_);
    lv_anim_set_exec_cb(&a, AnimSetHeight);
    lv_anim_set_values(&a, EYE_HEIGHT_DEFAULT, EYE_HEIGHT_DEFAULT - h_amp);
    lv_anim_set_time(&a, period);
    lv_anim_set_playback_time(&a, period);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);

    a.var = right_eye_;
    lv_anim_start(&a);

    // 2. Vertical Bobbing (Direct Y around EYE_Y_OFFSET)
    lv_anim_set_exec_cb(&a, AnimSetY); 
    lv_anim_set_values(&a, EYE_Y_OFFSET - y_amp, EYE_Y_OFFSET + y_amp); 
    lv_anim_set_var(&a, left_eye_);
    lv_anim_start(&a);

    lv_anim_set_values(&a, EYE_Y_OFFSET - y_amp, EYE_Y_OFFSET + y_amp);
    lv_anim_set_var(&a, right_eye_);
    lv_anim_start(&a);
}

// === The "Brain": Idle Loop ===
void OttoEyeDisplay::StartIdleLoop() {
    esp_timer_create_args_t args = {};
    args.callback = IdleTimerCallback;
    args.arg = this;
    args.name = "idle_brain";
    esp_timer_create(&args, &idle_timer_);
    esp_timer_start_once(idle_timer_, 1000000);
}

void OttoEyeDisplay::IdleTimerCallback(void* arg) {
    OttoEyeDisplay* self = (OttoEyeDisplay*)arg;
    
    int r = esp_random() % 100;
    
    // When speaking, we double the chance of rapid movements
    if (self->is_speaking_) {
         self->MovePupils((esp_random() % 20) - 10, (esp_random() % 20) - 10, 150);
         if (r < 30) self->Blink(1, 100);
    } else {
        if (r < 40) { self->Blink(1, BLINK_SPEED); } 
        else if (r < 70) {
            int x = (esp_random() % 10) - 5;
            int y = (esp_random() % 10) - 5;
            self->MovePupils(x, y);
        }
        else if (r < 80) { self->Blink(2, 100); }
        else if (r < 85) { self->Squint(); }
        else if (r < 90) { self->EyeRoll(); }
        else if (r < 95) { self->ShiftyEyes(); }
        else { self->WidenEyes(); }
    }

    uint64_t next_delay = self->is_speaking_ ? 500 : (IDLE_INTERVAL_MIN_MS + (esp_random() % (IDLE_INTERVAL_MAX_MS - IDLE_INTERVAL_MIN_MS)));
    esp_timer_start_once(self->idle_timer_, next_delay * 1000);
}

// === Animations ===

void OttoEyeDisplay::Blink(int count, int speed) {
    DisplayLockGuard lock(this);
    if(!left_eye_ || !right_eye_) return;

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, left_eye_);
    lv_anim_set_exec_cb(&a, AnimSetHeight);
    
    lv_coord_t h = lv_obj_get_height(left_eye_);
    lv_anim_set_values(&a, h, 2);
    lv_anim_set_time(&a, speed / 2);
    lv_anim_set_playback_time(&a, speed / 2);
    lv_anim_set_repeat_count(&a, count);
    lv_anim_start(&a);

    a.var = right_eye_;
    lv_anim_start(&a);
}

void OttoEyeDisplay::MovePupils(int x, int y, int duration) {
    DisplayLockGuard lock(this);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_exec_cb(&a, AnimSetPupilX);
    lv_anim_set_time(&a, duration);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    
    // Increased limit when speaking
    int limit = is_speaking_ ? 12 : 6;
    if (x > limit) { x = limit; } if (x < -limit) { x = -limit; }
    if (y > limit) { y = limit; } if (y < -limit) { y = -limit; }

    lv_anim_set_values(&a, lv_obj_get_x(left_pupil_), x);
    lv_anim_set_var(&a, left_pupil_); lv_anim_start(&a);
    lv_anim_set_var(&a, right_pupil_); lv_anim_start(&a);

    lv_anim_set_exec_cb(&a, AnimSetPupilY);
    lv_anim_set_values(&a, lv_obj_get_y(left_pupil_), y);
    lv_anim_set_var(&a, left_pupil_); lv_anim_start(&a);
    lv_anim_set_var(&a, right_pupil_); lv_anim_start(&a);
}

void OttoEyeDisplay::Squint() {
    DisplayLockGuard lock(this);
    lv_obj_set_height(left_eye_, 10);
    lv_obj_set_height(right_eye_, 10);
}

void OttoEyeDisplay::WidenEyes() {
    DisplayLockGuard lock(this);
    lv_obj_set_size(left_eye_, 38, 38);
    lv_obj_set_size(right_eye_, 38, 38);
}

void OttoEyeDisplay::EyeRoll() {
    MovePupils(0, -8, 200);
}

void OttoEyeDisplay::ShiftyEyes() {
    MovePupils(10, 0, 100);
}

void OttoEyeDisplay::ResetToNeutral() {
    DisplayLockGuard lock(this);
    lv_obj_set_size(left_eye_, EYE_WIDTH_DEFAULT, EYE_HEIGHT_DEFAULT);
    lv_obj_set_size(right_eye_, EYE_WIDTH_DEFAULT, EYE_HEIGHT_DEFAULT);
    MovePupils(0, 0);
}

// === Context & Emotions ===

void OttoEyeDisplay::AnalyzeContext(const char* text) {
    if(!text) return;
    if(strstr(text, "love") || strstr(text, "thích")) SetLoveEyes();
    else if(strstr(text, "fun") || strstr(text, "happy") || strstr(text, "vui")) SetHappiestEyes();
    else if(strstr(text, "sad") || strstr(text, "buồn")) SetSadEyes();
    else if(strstr(text, "?")) SetConfusedEyes();
    else if(strstr(text, "angry")) SetAngryEyes();
}

void OttoEyeDisplay::SetChatMessage(const char* role, const char* content) {
    if (role && strcmp(role, "assistant") == 0 && content) {
        AnalyzeContext(content); 
    }
}

void OttoEyeDisplay::SetStatus(const char* status) {
    if(!status) return;
    
    ESP_LOGI("OttoEye", "SetStatus called: '%s'", status);

    // HACKED MODE PROTECTION:
    // Khi đang ở hacked mode, bỏ qua MỌI status từ Application.
    // Chỉ thoát khi nhận "HACKED_EXIT" (từ timer 10s của chúng ta).
    if (is_hacked_) {
        if (strstr(status, "HACKED_EXIT")) {
            ESP_LOGI("OttoEye", "HACKED_EXIT - exiting hacked mode");
            StopMatrixEffect();
            is_hacked_ = false;
            if (body_ != nullptr) body_->SetStatus("IDLE");
            StartBreathingAnim();
        } else {
            ESP_LOGD("OttoEye", "Hacked mode: ignoring status '%s'", status);
        }
        return;
    }
    
    bool state_changed = false;
    const char* body_state = "IDLE";
    {
        DisplayLockGuard lock(this);
        bool was_speaking = is_speaking_;
        bool was_hacked = is_hacked_;
        
        // Check for HACKED mode first
        if (strstr(status, "HACKED") || strstr(status, "hack") || strstr(status, "bị hack")) {
            is_hacked_ = true;
            is_speaking_ = false;
            body_state = "HACKED";
            if (!was_hacked) {
                ShowMatrixEffect();
            }
        } else {
            // Normal modes (is_hacked_ was false here, already guarded above)

            // Check for both English and Vietnamese keywords
            if (strstr(status, "SPEAKING") || strstr(status, "Speaking") || strstr(status, "nói")) {
                is_speaking_ = true;
                body_state = "SPEAKING";
                SetHappiestEyes();
            } else if (strstr(status, "LISTENING") || strstr(status, "Listening") || strstr(status, "nghe")) {
                is_speaking_ = false;
                body_state = "LISTENING";
            } else {
                is_speaking_ = false;
                body_state = "IDLE";
            }
        }
        
        if (is_speaking_ != was_speaking || is_hacked_ != was_hacked) {
            state_changed = true;
        }

        if (strstr(status, "192.168") || strstr(status, "Access point")) {
            // WiFi Config Mode
            if (ip_label_) {
                lv_label_set_text(ip_label_, "WIFI: Xiaozhi\\nIP: 192.168.4.1");
                lv_obj_remove_flag(ip_label_, LV_OBJ_FLAG_HIDDEN);
            }
            // Hide eyes in config mode
            if (left_eye_) lv_obj_add_flag(left_eye_, LV_OBJ_FLAG_HIDDEN);
            if (right_eye_) lv_obj_add_flag(right_eye_, LV_OBJ_FLAG_HIDDEN);
        } else if (!is_hacked_) {
            // Normal Mode (not hacked, not config)
            if (ip_label_) lv_obj_add_flag(ip_label_, LV_OBJ_FLAG_HIDDEN);
            if (left_eye_) lv_obj_remove_flag(left_eye_, LV_OBJ_FLAG_HIDDEN);
            if (right_eye_) lv_obj_remove_flag(right_eye_, LV_OBJ_FLAG_HIDDEN);
        }
    } // lock releases here automatically

    // ALWAYS update OttoBody state, not just on state change
    if (body_ != nullptr) {
        ESP_LOGI("OttoEye", "Updating OttoBody to: %s", body_state);
        body_->SetStatus(body_state);
    }
    
    if (state_changed && !is_hacked_) {
        StartBreathingAnim();
    }
}

void OttoEyeDisplay::UpdateStatusBar(bool update_all) {
    // DO NOTHING: Hide all standard status bar updates to keep eye UI clean
}

void OttoEyeDisplay::ShowNotification(const char* content, int duration_ms) {
    // DO NOTHING: Hide all notifications to keep eye UI clean and prevent crash (label deleted)
}

void OttoEyeDisplay::SetEmotion(const char* emotion) {
    if(!emotion) { ResetToNeutral(); return; }
    if(strstr(emotion,"happy")) SetHappiestEyes();
    else if(strstr(emotion,"sad")) SetSadEyes();
    else if(strstr(emotion,"angry")) SetAngryEyes();
    else if(strstr(emotion,"love")) SetLoveEyes();
    else ResetToNeutral();
}

void OttoEyeDisplay::SetHappiestEyes() {
    DisplayLockGuard lock(this);
    lv_obj_set_size(left_eye_, 32, 16);
    lv_obj_set_size(right_eye_, 32, 16);
}

void OttoEyeDisplay::SetSadEyes() {
    DisplayLockGuard lock(this);
    lv_obj_set_size(left_eye_, 28, 30);
    lv_obj_set_size(right_eye_, 28, 30);
}

void OttoEyeDisplay::SetAngryEyes() {
    DisplayLockGuard lock(this);
    lv_obj_set_size(left_eye_, 28, 18);
    lv_obj_set_size(right_eye_, 28, 18);
}

void OttoEyeDisplay::SetConfusedEyes() {
    DisplayLockGuard lock(this);
    lv_obj_set_size(left_eye_, 25, 25);
    lv_obj_set_size(right_eye_, 35, 35);
}

void OttoEyeDisplay::SetLoveEyes() {
    DisplayLockGuard lock(this);
    lv_obj_set_size(left_eye_, 35, 35);
    lv_obj_set_size(right_eye_, 35, 35);
    lv_obj_set_size(left_pupil_, 22, 22);
    lv_obj_set_size(right_pupil_, 22, 22);
}

//=================================================================
// Matrix Effect (Hacked Mode)
//=================================================================

void OttoEyeDisplay::ShowMatrixEffect() {
    DisplayLockGuard lock(this);
    
    // Hide eyes
    if (left_eye_) lv_obj_add_flag(left_eye_, LV_OBJ_FLAG_HIDDEN);
    if (right_eye_) lv_obj_add_flag(right_eye_, LV_OBJ_FLAG_HIDDEN);
    if (ip_label_) lv_obj_add_flag(ip_label_, LV_OBJ_FLAG_HIDDEN);
    
    // Create Matrix rain effect - full screen text
    lv_obj_t* screen = lv_screen_active();
    
    // Create a single label for Matrix effect
    lv_obj_t* matrix_label = lv_label_create(screen);
    lv_obj_set_size(matrix_label, 128, 64);
    lv_obj_set_style_text_color(matrix_label, lv_color_white(), 0);  // White (inverted to green on OLED)
    lv_obj_set_style_text_font(matrix_label, &lv_font_montserrat_14, 0);
    lv_obj_align(matrix_label, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_label_set_text(matrix_label, "");
    lv_obj_set_user_data(matrix_label, this);  // Store reference
    
    // Start Matrix animation timer
    esp_timer_create_args_t args = {};
    args.callback = MatrixTimerCallback;
    args.arg = this;
    args.name = "matrix_rain";
    esp_timer_create(&args, &matrix_timer_);
    esp_timer_start_periodic(matrix_timer_, 100000);  // 100ms updates
    
    ESP_LOGI("OttoEye", "Matrix effect started");
}

void OttoEyeDisplay::StopMatrixEffect() {
    if (matrix_timer_) {
        esp_timer_stop(matrix_timer_);
        esp_timer_delete(matrix_timer_);
        matrix_timer_ = nullptr;
    }
    
    DisplayLockGuard lock(this);
    
    // Clean up Matrix labels
    lv_obj_t* screen = lv_screen_active();
    lv_obj_clean(screen);
    
    // Recreate normal UI
    SetupUI();
    StartBreathingAnim();
    StartIdleLoop();
    
    ESP_LOGI("OttoEye", "Matrix effect stopped");
}

void OttoEyeDisplay::MatrixTimerCallback(void* arg) {
    OttoEyeDisplay* self = (OttoEyeDisplay*)arg;
    DisplayLockGuard lock(self);
    
    // Find the matrix label
    lv_obj_t* screen = lv_screen_active();
    lv_obj_t* matrix_label = lv_obj_get_child(screen, 0);
    
    if (!matrix_label) return;
    
    // Generate random Matrix-style characters
    static const char matrix_chars[] = "01アイウエオカキクケコサシスセソタチツテト!@#$%^&*";
    static char matrix_text[256];
    static int scroll_offset = 0;
    
    // Create 8 rows of random characters
    int idx = 0;
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 16; col++) {
            int char_idx = esp_random() % (sizeof(matrix_chars) - 1);
            matrix_text[idx++] = matrix_chars[char_idx];
        }
        matrix_text[idx++] = '\n';
    }
    matrix_text[idx] = '\0';
    
    lv_label_set_text(matrix_label, matrix_text);
    
    // Scroll effect
    scroll_offset = (scroll_offset + 1) % 64;
    lv_obj_set_y(matrix_label, -scroll_offset);
}

void OttoEyeDisplay::ClearChatMessages() {
    ResetToNeutral();
}
