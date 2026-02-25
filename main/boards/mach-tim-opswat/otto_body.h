#ifndef _OTTO_BODY_H_
#define _OTTO_BODY_H_

#include "pca9685.h"
#include "pca9685_oscillator.h"
#include <driver/i2c_master.h>
#include <esp_timer.h>

// LED Configuration
#include "led_strip.h"

// Servo Channel Definitions
#define SERVO_HEAD_YAW   0  // Head turn left/right
#define SERVO_HEAD_PITCH 1  // Head nod up/down
#define SERVO_ARM_LEFT   2  // Left arm
#define SERVO_ARM_RIGHT  3  // Right arm
#define SERVO_COUNT      4

/**
 * @brief Movement patterns for choreographed animations
 */
enum class MovementPattern {
    NONE,               // No oscillation, static position
    IDLE_BREATHING,     // Subtle idle movement
    LISTENING,          // Attentive pose
    SPEAKING,           // Natural speaking gestures
    THINKING,           // Hand to chin, head tilt
    AGREEING,           // Nodding agreement
    GREETING,           // Wave hello
    EXCITED,            // Energetic movement
    // --- Cử chỉ điều khiển bằng giọng nói ---
    ARM_LEFT_RAISE,     // Giơ tay trái lên cao
    ARM_RIGHT_RAISE,    // Giơ tay phải lên cao
    GREETING_WAVE,      // Vẫy tay chào (tay phải)
    NECK_TURN_LEFT,     // Quay cổ sang trái
    NECK_TURN_RIGHT,    // Quay cổ sang phải
    NECK_SHAKE,         // Lắc đầu (không đồng ý)
    HEAD_BOW,           // Cúi đầu xuống
    HEAD_LOOKUP,        // Ngẩng đầu lên
    HEAD_NOD,           // Gật đầu (đồng ý)
};

/**
 * @brief OttoBody: Control body servos and LEDs with lifelike animations
 */
class OttoBody {
public:
    OttoBody(i2c_master_bus_handle_t bus_handle, uint8_t pca_addr, 
             gpio_num_t body_led_gpio, gpio_num_t chest_led_gpio, int led_count);
    
    bool Init();
    void SetStatus(const char* status); // "IDLE", "LISTENING", "SPEAKING"
    
    // Speech speed control (affects gesture speed)
    void SetSpeechSpeed(float speed); // 0.0 = slow/normal, 1.0 = fast/excited
    
    // Movement control
    void SetMovementPattern(MovementPattern pattern);
    void PlayGesture(MovementPattern gesture, int duration_ms = 2000);
    
    // Fine-grained control
    void SetHeadYaw(float angle);   // -45 to +45 from center
    void SetHeadPitch(float angle); // -30 to +30 from center
    void SetArmLeft(float angle);   // 0-180
    void SetArmRight(float angle);  // 0-180

private:
    // Animation states
    enum State {
        STATE_IDLE,
        STATE_LISTENING,
        STATE_SPEAKING,
        STATE_GESTURE,
        STATE_HACKED  // NEW: Hacked mode with jerky movements
    };

    // Core components
    Pca9685 pca_;
    Pca9685Oscillator oscillators_[SERVO_COUNT];
    
    // LED strips
    led_strip_handle_t led_strip_;        // Body LEDs
    led_strip_handle_t chest_led_strip_;  // Chest LED (heartbeat)
    
    // Animation timer
    esp_timer_handle_t anim_timer_;
    static void AnimationTimerCallback(void* arg);
    
    // State
    int led_count_;
    bool boot_animation_done_;
    State current_state_;
    MovementPattern current_pattern_;
    float tick_;
    float pattern_start_tick_;
    int gesture_duration_ms_;
    
    // Random gesture system for arms
    struct ArmGesture {
        float target_angle;      // Target angle to move to
        float speed;             // Movement speed
        uint32_t next_change_ms; // When to generate next gesture
    };
    ArmGesture left_arm_gesture_;
    ArmGesture right_arm_gesture_;
    float speech_speed_;  // 0.0 = slow/normal, 1.0 = fast/excited
    
    // Servo trims (calibration)
    int trims_[SERVO_COUNT];
    
    // Animation methods
    void UpdateAnimations();
    void UpdateLEDs();
    
    // Movement pattern implementations
    void ApplyPattern_IdleBreathing();
    void ApplyPattern_Listening();
    void ApplyPattern_Speaking();
    void ApplyPattern_Thinking();
    void ApplyPattern_Agreeing();
    void ApplyPattern_Greeting();
    void ApplyPattern_Excited();
    void ApplyPattern_Hacked();          // Jerky Parkinson-like movements
    // Cử chỉ điều khiển bằng giọng nói
    void ApplyPattern_ArmLeftRaise();
    void ApplyPattern_ArmRightRaise();
    void ApplyPattern_GreetingWave();
    void ApplyPattern_NeckTurnLeft();
    void ApplyPattern_NeckTurnRight();
    void ApplyPattern_NeckShake();
    void ApplyPattern_HeadBow();
    void ApplyPattern_HeadLookup();
    void ApplyPattern_HeadNod();
    
    // Helper
    void SetOscillatorParams(int channel, int amplitude, int offset, 
                             int period_ms, double phase_rad);
    void StopAllOscillators();
    void ReturnToNeutral(float speed = 0.05f);
    
    // Random gesture generation
    void GenerateRandomArmGesture(ArmGesture& gesture, bool is_left_arm);
    void UpdateArmGestures();
};

#endif // _OTTO_BODY_H_
