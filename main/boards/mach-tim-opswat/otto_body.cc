#include "otto_body.h"
#include <esp_log.h>
#include <esp_random.h>
#include <cstring>
#include <cmath>

#define TAG "OttoBody"
#define ANIM_INTERVAL_MS 33 // ~30fps

OttoBody::OttoBody(i2c_master_bus_handle_t bus_handle, uint8_t pca_addr, 
                   gpio_num_t body_led_gpio, gpio_num_t chest_led_gpio, int led_count)
    : pca_(bus_handle, pca_addr), led_strip_(nullptr), chest_led_strip_(nullptr), 
      anim_timer_(nullptr), led_count_(led_count), boot_animation_done_(false), 
      current_state_(STATE_IDLE), current_pattern_(MovementPattern::NONE),
      tick_(0), pattern_start_tick_(0), gesture_duration_ms_(0), speech_speed_(0.0f) {
    
    // Initialize trims to zero
    for (int i = 0; i < SERVO_COUNT; i++) {
        trims_[i] = 0;
    }
    
    // Initialize arm gestures to neutral
    left_arm_gesture_ = {90.0f, 0.1f, 0};
    right_arm_gesture_ = {90.0f, 0.1f, 0};
    
    // Body LED Strip Init
    if (body_led_gpio != GPIO_NUM_NC) {
        led_strip_config_t strip_config = {
            .strip_gpio_num = body_led_gpio,
            .max_leds = (uint32_t)led_count,
        };
        led_strip_rmt_config_t rmt_config = {
            .resolution_hz = 10 * 1000 * 1000,
        };
        led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip_);
    }
    
    // Chest LED Init
    if (chest_led_gpio != GPIO_NUM_NC) {
        led_strip_config_t chest_config = {
            .strip_gpio_num = chest_led_gpio,
            .max_leds = 1,
        };
        led_strip_rmt_config_t chest_rmt_config = {
            .resolution_hz = 10 * 1000 * 1000,
        };
        led_strip_new_rmt_device(&chest_config, &chest_rmt_config, &chest_led_strip_);
    }
}

bool OttoBody::Init() {
    if (!pca_.Init()) {
        ESP_LOGE(TAG, "PCA9685 Init failed!");
        return false;
    }
    pca_.SetPWMFreq(50.0f);
    ESP_LOGI(TAG, "PCA9685 initialized");

    // Attach oscillators to PCA9685 channels
    for (int i = 0; i < SERVO_COUNT; i++) {
        oscillators_[i].Attach(&pca_, i, trims_[i]);
        oscillators_[i].SetPosition(90.0f); // Neutral position
    }
    ESP_LOGI(TAG, "Oscillators attached to channels 0-%d", SERVO_COUNT - 1);

    // Start animation timer
    esp_timer_create_args_t args = {};
    args.callback = AnimationTimerCallback;
    args.arg = this;
    args.name = "body_anim";
    esp_timer_create(&args, &anim_timer_);
    esp_timer_start_periodic(anim_timer_, ANIM_INTERVAL_MS * 1000);
    
    ESP_LOGI(TAG, "Animation timer started at %d fps", 1000 / ANIM_INTERVAL_MS);
    return true;
}

void OttoBody::SetStatus(const char* status) {
    State new_state = STATE_IDLE;
    
    if (strstr(status, "HACKED") || strstr(status, "hack")) {
        new_state = STATE_HACKED;
    } else if (strstr(status, "LISTENING")) {
        new_state = STATE_LISTENING;
    } else if (strstr(status, "SPEAKING")) {
        new_state = STATE_SPEAKING;
    }
    
    if (new_state != current_state_) {
        ESP_LOGI(TAG, "State: %d -> %d (%s)", current_state_, new_state, status);
        
        // STRICT SPEC: Only SPEAKING state allows movement
        // IDLE and LISTENING = completely still
        if (current_state_ == STATE_SPEAKING && new_state != STATE_SPEAKING) {
            // END SPEAKING: Return all servos to neutral immediately
            ESP_LOGI(TAG, "END SPEAKING: Returning to neutral position");
            StopAllOscillators();
            for (int i = 0; i < SERVO_COUNT; i++) {
                oscillators_[i].SetPosition(90.0f);
            }
        }
        
        current_state_ = new_state;
        
        // Set movement pattern based on state
        if (new_state == STATE_HACKED) {
            SetMovementPattern(MovementPattern::NONE);  // Use custom hacked pattern
        } else if (new_state == STATE_SPEAKING) {
            SetMovementPattern(MovementPattern::SPEAKING);
        } else {
            // IDLE and LISTENING: NO movement, just LED changes
            SetMovementPattern(MovementPattern::NONE);
        }
    }
}

void OttoBody::SetSpeechSpeed(float speed) {
    // Clamp to 0.0 - 1.0 range
    speech_speed_ = speed;
    if (speech_speed_ < 0.0f) speech_speed_ = 0.0f;
    if (speech_speed_ > 1.0f) speech_speed_ = 1.0f;
    
    ESP_LOGD(TAG, "Speech speed: %.2f", speech_speed_);
}

void OttoBody::SetMovementPattern(MovementPattern pattern) {
    if (pattern == current_pattern_) return;
    
    current_pattern_ = pattern;
    pattern_start_tick_ = tick_;
    
    // Reset oscillators for new pattern
    for (int i = 0; i < SERVO_COUNT; i++) {
        oscillators_[i].Reset();
        oscillators_[i].Play();
    }
    
    ESP_LOGI(TAG, "Movement pattern: %d", (int)pattern);
}

void OttoBody::PlayGesture(MovementPattern gesture, int duration_ms) {
    current_state_ = STATE_GESTURE;
    gesture_duration_ms_ = duration_ms;
    SetMovementPattern(gesture);
}

void OttoBody::AnimationTimerCallback(void* arg) {
    OttoBody* self = (OttoBody*)arg;
    self->UpdateAnimations();
    self->UpdateLEDs();
    self->tick_ += 0.033f; // ~33ms per tick
}

void OttoBody::UpdateAnimations() {
    // Auto-return from gesture when duration expires
    if (current_state_ == STATE_GESTURE && gesture_duration_ms_ > 0) {
        float elapsed_ms = (tick_ - pattern_start_tick_) * 1000.0f;
        if (elapsed_ms >= gesture_duration_ms_) {
            ESP_LOGI(TAG, "Gesture done (%.0fms) - returning to IDLE", elapsed_ms);
            gesture_duration_ms_ = 0;
            current_state_ = STATE_IDLE;
            SetMovementPattern(MovementPattern::NONE);
            StopAllOscillators();
            ReturnToNeutral(0.05f);
            return;
        }
    }

    // Apply current movement pattern
    switch (current_pattern_) {
        case MovementPattern::NONE:
            if (current_state_ == STATE_HACKED) {
                ApplyPattern_Hacked();
            } else {
                ReturnToNeutral();
            }
            break;
        case MovementPattern::IDLE_BREATHING:
            ApplyPattern_IdleBreathing();
            break;
        case MovementPattern::LISTENING:
            ApplyPattern_Listening();
            break;
        case MovementPattern::SPEAKING:
            ApplyPattern_Speaking();
            break;
        case MovementPattern::THINKING:
            ApplyPattern_Thinking();
            break;
        case MovementPattern::AGREEING:
            ApplyPattern_Agreeing();
            break;
        case MovementPattern::GREETING:
            ApplyPattern_Greeting();
            break;
        case MovementPattern::EXCITED:
            ApplyPattern_Excited();
            break;
        // Cử chỉ điều khiển bằng giọng nói
        case MovementPattern::ARM_LEFT_RAISE:
            ApplyPattern_ArmLeftRaise();
            break;
        case MovementPattern::ARM_RIGHT_RAISE:
            ApplyPattern_ArmRightRaise();
            break;
        case MovementPattern::GREETING_WAVE:
            ApplyPattern_GreetingWave();
            break;
        case MovementPattern::NECK_TURN_LEFT:
            ApplyPattern_NeckTurnLeft();
            break;
        case MovementPattern::NECK_TURN_RIGHT:
            ApplyPattern_NeckTurnRight();
            break;
        case MovementPattern::NECK_SHAKE:
            ApplyPattern_NeckShake();
            break;
        case MovementPattern::HEAD_BOW:
            ApplyPattern_HeadBow();
            break;
        case MovementPattern::HEAD_LOOKUP:
            ApplyPattern_HeadLookup();
            break;
        case MovementPattern::HEAD_NOD:
            ApplyPattern_HeadNod();
            break;
    }  // end switch (current_pattern_)
    
    // Refresh all oscillators
    for (int i = 0; i < SERVO_COUNT; i++) {
        oscillators_[i].Refresh();
    }
}

//=================================================================
// Movement Pattern Implementations
//=================================================================

void OttoBody::ApplyPattern_IdleBreathing() {
    // STRICT SPEC: IDLE = completely still, no movement at all
    // Robot stands still, waiting for activation
    // Only LED changes, no servo movement
    StopAllOscillators();
    ReturnToNeutral(0.1f);
}

void OttoBody::ApplyPattern_Listening() {
    // STRICT SPEC: LISTENING = completely still, no movement
    // Robot is activated but waiting for AI response
    // Only LED changes (brighter), no servo movement
    StopAllOscillators();
    ReturnToNeutral(0.1f);
}

void OttoBody::ApplyPattern_Speaking() {
    // SPEAKING: Robot gestures naturally while AI is talking
    // Arms move randomly within defined range with speech-responsive speed
    // Head movement: RANDOM selection for natural variation
    
    // HEAD: Random movement pattern (changes every time this is called)
    static uint32_t last_head_change = 0;
    uint32_t now = esp_timer_get_time() / 1000000;  // seconds
    
    // Change head pattern every 3-5 seconds
    if (now - last_head_change > 3) {
        last_head_change = now;
        int head_pattern = esp_random() % 3;
        
        switch (head_pattern) {
            case 0:
                // Only yaw (xoay cổ qua lại)
                SetOscillatorParams(SERVO_HEAD_YAW, 17, 0, 2500, 0);
                SetOscillatorParams(SERVO_HEAD_PITCH, 0, 0, 2000, 0);  // No pitch
                ESP_LOGD("OttoBody", "Head pattern: YAW only (neck rotation)");
                break;
            case 1:
                // Only pitch (gật đầu)
                SetOscillatorParams(SERVO_HEAD_YAW, 0, 0, 2500, 0);  // No yaw
                SetOscillatorParams(SERVO_HEAD_PITCH, 15, 0, 2000, DEG2RAD(45));
                ESP_LOGD("OttoBody", "Head pattern: PITCH only (nodding)");
                break;
            case 2:
                // Both yaw and pitch (cả hai)
                SetOscillatorParams(SERVO_HEAD_YAW, 17, 0, 2500, 0);
                SetOscillatorParams(SERVO_HEAD_PITCH, 15, 0, 2000, DEG2RAD(45));
                ESP_LOGD("OttoBody", "Head pattern: BOTH (yaw + pitch)");
                break;
        }
    }
    
    // ARMS: Random gesture system
    // Stop oscillators and use smooth movement instead
    oscillators_[SERVO_ARM_LEFT].Stop();
    oscillators_[SERVO_ARM_RIGHT].Stop();
    
    // Update arm gestures
    UpdateArmGestures();
}

void OttoBody::ApplyPattern_Thinking() {
    // Thinking pose: head tilted, one arm raised (hand to chin gesture)
    
    // Head: tilted to one side, looking up slightly
    SetOscillatorParams(SERVO_HEAD_YAW, 5, -15, 4000, 0);
    SetOscillatorParams(SERVO_HEAD_PITCH, 4, -8, 3500, 0);
    
    // Left arm: raised (thinking gesture)
    SetOscillatorParams(SERVO_ARM_LEFT, 8, 20, 3000, 0);
    // Right arm: relaxed
    SetOscillatorParams(SERVO_ARM_RIGHT, 5, 45, 5000, 0);
}

void OttoBody::ApplyPattern_Agreeing() {
    // Nodding agreement: clear up-down head motion
    
    // Head: strong nodding motion
    SetOscillatorParams(SERVO_HEAD_YAW, 3, 0, 3000, 0); // Minimal side-to-side
    SetOscillatorParams(SERVO_HEAD_PITCH, 18, 5, 800, 0); // Strong nod
    
    // Arms: slight sympathetic movement
    SetOscillatorParams(SERVO_ARM_LEFT, 8, -40, 1600, DEG2RAD(90));
    SetOscillatorParams(SERVO_ARM_RIGHT, 8, 40, 1600, DEG2RAD(-90));
}

void OttoBody::ApplyPattern_Greeting() {
    // Wave hello: tầm vẫy tối đa — tay phải giơ cao vẫy rất rộng
    
    // Head: turns toward waving arm
    SetOscillatorParams(SERVO_HEAD_YAW,  12, 18, 900, 0);
    SetOscillatorParams(SERVO_HEAD_PITCH, 6,  5, 1800, 0);
    
    // Right arm: maximum wave — center giơ cao, biên độ rất lớn
    SetOscillatorParams(SERVO_ARM_RIGHT, 55, 40, 300, 0);  // center=130°±55°, rất nhanh
    // Left arm: đối xứng nhẹ
    SetOscillatorParams(SERVO_ARM_LEFT,  20, -20, 600, 0);
}

void OttoBody::ApplyPattern_Excited() {
    // Excited/happy movement: bouncy, energetic
    
    // Head: quick, bouncy movement
    SetOscillatorParams(SERVO_HEAD_YAW, 15, 0, 800, 0);
    SetOscillatorParams(SERVO_HEAD_PITCH, 10, 0, 600, DEG2RAD(90));
    
    // Arms: energetic flapping/celebratory movement
    SetOscillatorParams(SERVO_ARM_LEFT, 40, -20, 700, 0);
    SetOscillatorParams(SERVO_ARM_RIGHT, 40, 20, 700, DEG2RAD(180));
}

void OttoBody::ApplyPattern_Hacked() {
    // HACKED: Rapid, jerky, Parkinson-like movements
    StopAllOscillators();
    float random_yaw = (esp_random() % 60) - 30;
    float random_pitch = (esp_random() % 40) - 20;
    oscillators_[SERVO_HEAD_YAW].SetPosition(90 + random_yaw);
    oscillators_[SERVO_HEAD_PITCH].SetPosition(90 + random_pitch);
    float left_pos = 70 + (esp_random() % 60);
    float right_pos = 70 + (esp_random() % 60);
    oscillators_[SERVO_ARM_LEFT].SetPosition(left_pos);
    oscillators_[SERVO_ARM_RIGHT].SetPosition(right_pos);
}

//=================================================================
// Cử chỉ điều khiển bằng giọng nói
//=================================================================

void OttoBody::ApplyPattern_ArmLeftRaise() {
    // Giơ tay trái lên thẳng (offset âm = lên cao với servo trái)
    SetOscillatorParams(SERVO_ARM_LEFT,  0, -55, 1000, 0);  // 90-55=35° (hướng giơ lên)
    SetOscillatorParams(SERVO_ARM_RIGHT, 0,   0, 1000, 0);  // 90° trung lập
    SetOscillatorParams(SERVO_HEAD_YAW,  0,   0, 1000, 0);
    SetOscillatorParams(SERVO_HEAD_PITCH,0,   0, 1000, 0);
}

void OttoBody::ApplyPattern_ArmRightRaise() {
    // Giơ tay phải lên thẳng (offset dương = lên cao với servo phải)
    SetOscillatorParams(SERVO_ARM_LEFT,  0,   0, 1000, 0);  // 90° trung lập
    SetOscillatorParams(SERVO_ARM_RIGHT, 0,  55, 1000, 0);  // 90+55=145° (hướng giơ lên)
    SetOscillatorParams(SERVO_HEAD_YAW,  0,   0, 1000, 0);
    SetOscillatorParams(SERVO_HEAD_PITCH,0,   0, 1000, 0);
}

void OttoBody::ApplyPattern_GreetingWave() {
    // Vẫy tay chào: tầm vẫy tối đa, tay phải giơ lên cao vẫy rất nhanh
    SetOscillatorParams(SERVO_ARM_RIGHT, 55, 40, 280, 0);  // center=130°±55° (75°→185°) rất nhanh
    SetOscillatorParams(SERVO_ARM_LEFT,  20, -20, 560, 0); // tay trái lay động đối xứng
    SetOscillatorParams(SERVO_HEAD_YAW,  12,  18, 600, 0); // đầu nhìn theo hướng tay
    SetOscillatorParams(SERVO_HEAD_PITCH, 6,   5, 350, 0); // gật nhẹ
}

void OttoBody::ApplyPattern_NeckTurnLeft() {
    // Quay đầu sang trái (yaw offset dương = sang trái sau khi đảo hướng)
    SetOscillatorParams(SERVO_HEAD_YAW,   0, 40, 1000, 0);   // 90+40=130° (quay trái)
    SetOscillatorParams(SERVO_HEAD_PITCH, 0,  0, 1000, 0);
    SetOscillatorParams(SERVO_ARM_LEFT,   0,  0, 1000, 0);
    SetOscillatorParams(SERVO_ARM_RIGHT,  0,  0, 1000, 0);
}

void OttoBody::ApplyPattern_NeckTurnRight() {
    // Quay đầu sang phải (yaw offset âm = sang phải sau khi đảo hướng)
    SetOscillatorParams(SERVO_HEAD_YAW,   0, -40, 1000, 0);  // 90-40=50° (quay phải)
    SetOscillatorParams(SERVO_HEAD_PITCH, 0,   0, 1000, 0);
    SetOscillatorParams(SERVO_ARM_LEFT,   0,   0, 1000, 0);
    SetOscillatorParams(SERVO_ARM_RIGHT,  0,   0, 1000, 0);
}

void OttoBody::ApplyPattern_NeckShake() {
    // Lắc đầu trái-phải nhanh (bác bỏ / không)
    SetOscillatorParams(SERVO_HEAD_YAW,  35, 0, 450, 0);  // lắc nhanh ±35°
    SetOscillatorParams(SERVO_HEAD_PITCH, 0, 0, 1000, 0);
    SetOscillatorParams(SERVO_ARM_LEFT,   0, 0, 1000, 0);
    SetOscillatorParams(SERVO_ARM_RIGHT,  0, 0, 1000, 0);
}

void OttoBody::ApplyPattern_HeadBow() {
    // Cúi đầu xuống (pitch offset âm = cúi xuống)
    SetOscillatorParams(SERVO_HEAD_PITCH, 0, -25, 1000, 0);  // 90-25=65° (cúi xuống)
    SetOscillatorParams(SERVO_HEAD_YAW,   0,   0, 1000, 0);
    SetOscillatorParams(SERVO_ARM_LEFT,   0,   0, 1000, 0);
    SetOscillatorParams(SERVO_ARM_RIGHT,  0,   0, 1000, 0);
}

void OttoBody::ApplyPattern_HeadLookup() {
    // Ngẩng đầu lên (pitch offset dương = ngẩng lên)
    SetOscillatorParams(SERVO_HEAD_PITCH, 0, 20, 1000, 0);   // 90+20=110° (ngẩng lên)
    SetOscillatorParams(SERVO_HEAD_YAW,   0,  0, 1000, 0);
    SetOscillatorParams(SERVO_ARM_LEFT,   0,  0, 1000, 0);
    SetOscillatorParams(SERVO_ARM_RIGHT,  0,  0, 1000, 0);
}

void OttoBody::ApplyPattern_HeadNod() {
    // Gật đầu lên xuống (oscillate pitch, đồng ý)
    SetOscillatorParams(SERVO_HEAD_PITCH, 20, 0, 550, 0);    // gật nhanh ±20°
    SetOscillatorParams(SERVO_HEAD_YAW,    3, 0, 800, 0);    // cường nhẹ yếu
    SetOscillatorParams(SERVO_ARM_LEFT,    0, 0, 1000, 0);
    SetOscillatorParams(SERVO_ARM_RIGHT,   0, 0, 1000, 0);
}

//=================================================================
// Helper Methods
//=================================================================

void OttoBody::SetOscillatorParams(int channel, int amplitude, int offset, 
                                   int period_ms, double phase_rad) {
    if (channel >= SERVO_COUNT) return;
    
    oscillators_[channel].SetAmplitude(amplitude);
    oscillators_[channel].SetOffset(offset);
    oscillators_[channel].SetPeriod(period_ms);
    oscillators_[channel].SetPhase(phase_rad);
}

void OttoBody::StopAllOscillators() {
    for (int i = 0; i < SERVO_COUNT; i++) {
        oscillators_[i].Stop();
    }
}

void OttoBody::ReturnToNeutral(float speed) {
    for (int i = 0; i < SERVO_COUNT; i++) {
        oscillators_[i].Stop();
        oscillators_[i].InterpolateTo(90.0f, speed);
    }
}

void OttoBody::SetHeadYaw(float angle) {
    oscillators_[SERVO_HEAD_YAW].Stop();
    oscillators_[SERVO_HEAD_YAW].SetPosition(90.0f + angle);
}

void OttoBody::SetHeadPitch(float angle) {
    oscillators_[SERVO_HEAD_PITCH].Stop();
    oscillators_[SERVO_HEAD_PITCH].SetPosition(90.0f + angle);
}

void OttoBody::SetArmLeft(float angle) {
    oscillators_[SERVO_ARM_LEFT].Stop();
    oscillators_[SERVO_ARM_LEFT].SetPosition(angle);
}

void OttoBody::SetArmRight(float angle) {
    oscillators_[SERVO_ARM_RIGHT].Stop();
    oscillators_[SERVO_ARM_RIGHT].SetPosition(angle);
}

//=================================================================
// LED Animations
//=================================================================

void OttoBody::UpdateLEDs() {
    // Boot animation - 4 seconds total
    if (!boot_animation_done_ && tick_ < 4.0f) {
        // Body LEDs: slower fade over 4 seconds
        int lit_count = (int)(tick_ * (led_count_ / 4.0f));  // led_count_ LEDs / 4 seconds
        if (lit_count > led_count_) lit_count = led_count_;
        uint8_t brightness = (uint8_t)(fmin(tick_ * 63.75f, 255.0f));  // 255 / 4 = 63.75
        
        for (int i = 0; i < led_count_; i++) {
            if (i < lit_count) {
                led_strip_set_pixel(led_strip_, i, 0, 0, brightness);
            } else {
                led_strip_set_pixel(led_strip_, i, 0, 0, 0);
            }
        }
        led_strip_refresh(led_strip_);
        
        // Chest LED: Enhanced startup sequence
        if (chest_led_strip_) {
            uint8_t white = 0;
            
            if (tick_ < 2.0f) {
                // Phase 1 (0-2s): Gradual fade in from 0 to 255
                white = (uint8_t)(tick_ / 2.0f * 255.0f);
            } else if (tick_ < 2.5f) {
                // Phase 2 (2-2.5s): Hold at full brightness
                white = 255;
            } else if (tick_ < 3.5f) {
                // Phase 3 (2.5-3.5s): Unstable flicker (180-255)
                white = 180 + (esp_random() % 76);
            } else {
                // Phase 4 (3.5-4s): Transition to heartbeat
                // First heartbeat pulse
                float phase = (tick_ - 3.5f) / 0.5f;  // 0.0 to 1.0 over 0.5s
                if (phase < 0.3f) {
                    white = (uint8_t)(255.0f * sin(phase / 0.3f * M_PI));
                } else {
                    white = 20;
                }
            }
            
            led_strip_set_pixel(chest_led_strip_, 0, white, white, white);
            led_strip_refresh(chest_led_strip_);
        }
        return;
    }
    boot_animation_done_ = true;
    
    // State-based LED colors
    if (led_strip_) {
        static uint8_t prev_r = 0, prev_g = 0, prev_b = 0;
        uint8_t target_r = 0, target_g = 0, target_b = 0;
        
        switch (current_state_) {
            case STATE_IDLE:
                // Blue breathing
                {
                    float val = (sin(tick_ * 0.8f) + 1.0f) / 2.0f;
                    target_b = (uint8_t)(30 + val * 70);
                }
                break;
            case STATE_LISTENING:
                // Static green
                target_g = 120;
                break;
            case STATE_SPEAKING:
                // Pulsing red/orange — 50% độ sáng so với gốc
                {
                    float val = (sin(tick_ * 2.5f) + 1.0f) / 2.0f;
                    target_r = (uint8_t)(50 + val * 77);  // (100+val*155)*0.5, max=127
                    target_g = (uint8_t)(val * 15);        // (val*30)*0.5, max=15
                }
                break;
            case STATE_GESTURE:
                // Purple for gestures
                target_r = 100;
                target_b = 150;
                break;
            case STATE_HACKED:
                // Chaotic random flashing (red dominant)
                target_r = 150 + (esp_random() % 106);  // 150-255
                target_g = esp_random() % 50;            // 0-50
                target_b = esp_random() % 50;            // 0-50
                // Skip smooth fade for instant chaos
                prev_r = target_r;
                prev_g = target_g;
                prev_b = target_b;
                break;
        }
        
        // Smooth fade
        prev_r += (target_r - prev_r) * 0.1f;
        prev_g += (target_g - prev_g) * 0.1f;
        prev_b += (target_b - prev_b) * 0.1f;
        
        for (int i = 0; i < led_count_; i++) {
            led_strip_set_pixel(led_strip_, i, prev_r, prev_g, prev_b);
        }
        led_strip_refresh(led_strip_);
    }
    
    // Chest LED heartbeat
    if (chest_led_strip_) {
        float heartbeat_speed = (current_state_ == STATE_SPEAKING) ? 2.5f : 1.2f;
        float phase = fmod(tick_ * heartbeat_speed, 1.0f);
        
        uint8_t white = 0;
        if (phase < 0.15f) {
            white = (uint8_t)(255.0f * sin(phase / 0.15f * M_PI));
        } else if (phase < 0.3f) {
            white = (uint8_t)(200.0f * sin((phase - 0.15f) / 0.15f * M_PI));
        } else {
            white = 20;
        }
        
        led_strip_set_pixel(chest_led_strip_, 0, white, white, white);
        led_strip_refresh(chest_led_strip_);
    }
}

//=================================================================
// Random Gesture Generation
//=================================================================

void OttoBody::GenerateRandomArmGesture(ArmGesture& gesture, bool is_left_arm) {
    // Arm constraints (tăng thêm 10° mỗi phía):
    // - Rest position: 90°
    // - Backward limit: 60° (90° - 30°)
    // - Forward limit: 140° (90° + 50°)
    // - 2 chế độ:
    //   Mode A (ngược nhau): tay trái và phải đối xứng (tự nhiên khi đi/nói)
    //   Mode B (cùng lên): cả 2 tay cùng giơ lên phía trước
    
    const float MIN_ANGLE = 60.0f;   // +10° so với trước (70→60)
    const float MAX_ANGLE = 140.0f;  // +10° so với trước (130→140)
    const float RAISE_ANGLE = 135.0f; // góc khi cùng giơ tay lên cao
    
    // Chọn chế độ cử động mỗi khi sinh gesture mới cho tay trái
    static int arm_mode = 0;  // 0=ngược nhau, 1=cùng lên
    static float shared_target = 90.0f;
    
    if (is_left_arm) {
        // Chọn mode ngẫu nhiên khi cập nhật tay trái
        // 60% ngược nhau, 40% cùng lên
        arm_mode = ((esp_random() % 10) < 6) ? 0 : 1;
        
        if (arm_mode == 1) {
            // Mode B: cả 2 tay cùng giơ lên (góc lớn, đưa ra phía trước)
            float raise_variation = (esp_random() % 20) - 5.0f;  // ±10° biến đổi
            shared_target = RAISE_ANGLE + raise_variation;
            if (shared_target > MAX_ANGLE) shared_target = MAX_ANGLE;
            gesture.target_angle = shared_target;
            ESP_LOGD(TAG, "Arm mode: BOTH RAISE, left=%.1f°", gesture.target_angle);
        } else {
            // Mode A: ngược nhau (tự nhiên như đi bộ)
            float range = MAX_ANGLE - MIN_ANGLE;
            shared_target = MIN_ANGLE + (esp_random() % 1000) / 1000.0f * range;
            gesture.target_angle = shared_target;
            ESP_LOGD(TAG, "Arm mode: OPPOSITE, left=%.1f°", gesture.target_angle);
        }
    } else {
        if (arm_mode == 1) {
            // Mode B: tay phải cũng giơ lên giống tay trái
            float raise_variation = (esp_random() % 20) - 5.0f;
            gesture.target_angle = RAISE_ANGLE + raise_variation;
            if (gesture.target_angle > MAX_ANGLE) gesture.target_angle = MAX_ANGLE;
            ESP_LOGD(TAG, "Arm mode: BOTH RAISE, right=%.1f°", gesture.target_angle);
        } else {
            // Mode A: tay phải ngược với tay trái
            // Công thức đối xứng: right = 180 - left
            gesture.target_angle = 180.0f - shared_target;
            if (gesture.target_angle < MIN_ANGLE) gesture.target_angle = MIN_ANGLE;
            if (gesture.target_angle > MAX_ANGLE) gesture.target_angle = MAX_ANGLE;
            ESP_LOGD(TAG, "Arm mode: OPPOSITE, right=%.1f°", gesture.target_angle);
        }
    }
    
    // Speed based on speech_speed_
    // Normal speech (0.0): slow movements (0.05 - 0.1)
    // Fast/excited speech (1.0): quick movements (0.2 - 0.4)
    float base_speed = 0.05f + speech_speed_ * 0.15f;  // 0.05 - 0.20
    float speed_variation = (esp_random() % 100) / 1000.0f;  // 0.0 - 0.1
    gesture.speed = base_speed + speed_variation;
    
    // Time until next gesture change
    // Normal speech: 800ms - 2500ms
    // Fast/excited speech: 300ms - 1000ms
    uint32_t min_interval = (uint32_t)(800 - speech_speed_ * 500);  // 800 - 300
    uint32_t max_interval = (uint32_t)(2500 - speech_speed_ * 1500); // 2500 - 1000
    uint32_t interval_range = max_interval - min_interval;
    uint32_t interval = min_interval + (esp_random() % interval_range);
    
    gesture.next_change_ms = (uint32_t)(tick_ * 1000) + interval;
}

void OttoBody::UpdateArmGestures() {
    uint32_t current_ms = (uint32_t)(tick_ * 1000);
    
    // Check if it's time to generate new left arm gesture
    if (current_ms >= left_arm_gesture_.next_change_ms) {
        GenerateRandomArmGesture(left_arm_gesture_, true);
    }
    
    // Check if it's time to generate new right arm gesture
    if (current_ms >= right_arm_gesture_.next_change_ms) {
        GenerateRandomArmGesture(right_arm_gesture_, false);
    }
    
    // Smoothly move arms to their target positions
    oscillators_[SERVO_ARM_LEFT].SmoothMoveTo(left_arm_gesture_.target_angle, 
                                               left_arm_gesture_.speed);
    oscillators_[SERVO_ARM_RIGHT].SmoothMoveTo(right_arm_gesture_.target_angle, 
                                                right_arm_gesture_.speed);
}
