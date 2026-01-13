#include <esp_log.h>
#include "application.h"
#include "board.h"
#include "config.h"
#include "otto_movements.h"
#include "driver/ledc.h"

#define TAG "OttoController"

// Cấu hình LED trên chân Servo dư (ví dụ chân LEFT_FOOT)
#define LED_PWM_CHANNEL LEDC_CHANNEL_5
#define LED_PWM_PIN     8  // <<-- THAY BẰNG SỐ CHÂN THỰC TẾ CỦA BẠN

enum RobotState { STATE_IDLE, STATE_LISTENING, STATE_SPEAKING };

class OttoController {
private:
    Otto otto_;
    RobotState current_state_ = STATE_IDLE;

    void InitLed() {
        ledc_timer_config_t timer_conf = {
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .duty_resolution = LEDC_TIMER_13_BIT,
            .timer_num = LEDC_TIMER_1,
            .freq_hz = 5000,
            .clk_cfg = LEDC_AUTO_CLK
        };
        ledc_timer_config(&timer_conf);

        ledc_channel_config_t lcd_conf = {
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = LED_PWM_CHANNEL,
            .timer_sel = LEDC_TIMER_1,
            .intr_type = LEDC_INTR_DISABLE,
            .gpio_num = LED_PWM_PIN,
            .duty = 0,
            .hpoint = 0
        };
        ledc_channel_config(&lcd_conf);
    }

    void UpdateDisplay(const char* text) {
        auto display = Board::GetInstance().GetDisplay();
        if (display) display->SetStatus(text);
    }

public:
    OttoController(const HardwareConfig& hw_config) {
        // Init Servo (Legs dùng làm Đầu)
        otto_.Init(hw_config.left_leg_pin, hw_config.right_leg_pin, -1, -1, 
                   hw_config.left_hand_pin, hw_config.right_hand_pin);
        InitLed();
        
        xTaskCreatePinnedToCore([](void* p){ ((OttoController*)p)->Loop(); }, 
                                "OttoLoop", 4096, this, 1, NULL, 1);
    }

    void Loop() {
        static int breath = 0, dir = 1;
        while (true) {
            auto app_state = Application::GetInstance().GetDeviceState();
            RobotState target = STATE_IDLE;

            if (app_state == kDeviceStateListening) target = STATE_LISTENING;
            else if (app_state == kDeviceStateSpeaking) target = STATE_SPEAKING;

            if (target != current_state_) {
                current_state_ = target;
                if (current_state_ == STATE_SPEAKING) {
                    otto_.StartSpeakingMode();
                    UpdateDisplay("SPEAKING");
                } else {
                    otto_.Home();
                    UpdateDisplay(current_state_ == STATE_LISTENING ? "LISTENING" : "IDLE");
                }
            }

            // Hiệu ứng LED & Cử động
            if (current_state_ == STATE_SPEAKING) {
                otto_.UpdateSpeakingMotion();
                ledc_set_duty(LEDC_LOW_SPEED_MODE, LED_PWM_CHANNEL, 8000); // Sáng mạnh
            } else {
                breath += (50 * dir);
                if (breath >= 4000 || breath <= 0) dir *= -1;
                ledc_set_duty(LEDC_LOW_SPEED_MODE, LED_PWM_CHANNEL, breath); // Thở nhẹ
            }
            ledc_update_duty(LEDC_LOW_SPEED_MODE, LED_PWM_CHANNEL);
            
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
};

static OttoController* g_otto = nullptr;
void InitializeOttoController(const HardwareConfig& hw_config) {
    if (!g_otto) g_otto = new OttoController(hw_config);
}
