#pragma once

#include "display/oled_display.h"
#include <esp_timer.h>
#include <lvgl.h>

class OttoBody;

/**
 * @brief Enhanced Dynamic Otto-style eye display
 * Uses LVGL objects and animations for smooth, sharp expressions.
 * Features "Soulful" micro-movements and diverse idle behaviors.
 */
class OttoEyeDisplay : public OledDisplay {
public:
    OttoEyeDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, 
                   int width, int height, bool mirror_x, bool mirror_y,
                   OttoBody* body = nullptr);
    virtual ~OttoEyeDisplay();

    // Override display methods
    virtual void SetStatus(const char* status) override;
    virtual void SetChatMessage(const char* role, const char* content) override;
    virtual void SetEmotion(const char* emotion) override;
    virtual void ClearChatMessages() override;
    virtual void UpdateStatusBar(bool update_all = false) override;
    virtual void ShowNotification(const char* content, int duration_ms = 2000) override;

private:
    // UI Objects
    lv_obj_t* left_eye_;
    lv_obj_t* right_eye_;
    lv_obj_t* left_pupil_;
    lv_obj_t* right_pupil_;
    lv_obj_t* ip_label_;

    // State
    bool is_speaking_;
    bool is_hacked_;  // NEW: Hacked mode state
    OttoBody* body_;
    
    // Animation timers
    esp_timer_handle_t idle_timer_; // Main brain for random behavior
    esp_timer_handle_t speak_timer_; // Rhythm for speaking
    esp_timer_handle_t matrix_timer_; // Matrix rain effect
    
    // Setup
    void SetupUI();
    
    // Animation Loops
    void StartIdleLoop();
    void StopIdleLoop();
    void StartBreathingAnim(); // Continuous "soul" movement
    
    // Specific Animations
    void Blink(int count = 1, int speed_ms = 150);
    void MovePupils(int x, int y, int duration_ms = 200);
    void EyeRoll();
    void ShiftyEyes();
    void Squint();
    void WidenEyes();
    void ResetToNeutral();
    
    // Matrix Effect (Hacked Mode)
    void ShowMatrixEffect();
    void StopMatrixEffect();
    
    // Context Helpers
    void AnalyzeContext(const char* text);
    
    // Callbacks
    static void IdleTimerCallback(void* arg);
    static void SpeakTimerCallback(void* arg);
    static void MatrixTimerCallback(void* arg);  // NEW
    
    // Property Setters for LVGL Animations
    static void AnimSetHeight(void* var, int32_t v);
    static void AnimSetWidth(void* var, int32_t v);
    static void AnimSetY(void* var, int32_t v);
    static void AnimSetPupilX(void* var, int32_t v);
    static void AnimSetPupilY(void* var, int32_t v);
    
    // Emotion presets
    void SetHappiestEyes();
    void SetSadEyes();
    void SetAngryEyes();
    void SetConfusedEyes();
    void SetLoveEyes();
};
