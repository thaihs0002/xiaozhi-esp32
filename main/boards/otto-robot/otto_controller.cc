#include <esp_log.h>
#include "application.h"
#include "board.h"
#include "config.h"
#include "otto_movements.h"
#include "driver/ledc.h"
// QUAN TRỌNG: Thêm dòng này để sửa lỗi 'incomplete type class Display'
#include "display/display.h" 

#define TAG "OttoController"

// CẤU HÌNH LED (Sửa lại GPIO của bạn nếu khác 8)
#define LED_PWM_CHANNEL LEDC_CHANNEL_5
#define LED_PWM_PIN     8 

enum RobotState { STATE_IDLE, STATE_LISTENING, STATE_SPEAKING };

class OttoController {
private:
    Otto otto_;
    RobotState current_state_ = STATE_IDLE;

    void InitLed() {
        // Sửa lỗi "designator order": Gán trực tiếp không dùng struct initializer list
        ledc_timer_config_t timer_conf = {};
        timer_conf.speed_mode = LEDC_LOW_SPEED_MODE;
        timer_conf.duty_resolution = LEDC_TIMER_13_BIT;
        timer_conf.timer_num = LEDC_TIMER_1;
        timer_conf.freq_hz = 5000;
        timer_conf.clk_cfg = LEDC_AUTO_CLK;
        
        ledc_timer_config(&timer_conf);

        ledc_channel_config_t lcd_conf = {};
        lcd_conf.speed_mode = LEDC_LOW_SPEED_MODE;
        lcd_conf.channel = LED_PWM_CHANNEL;
        lcd_conf.timer_sel = LEDC_TIMER_1;
        lcd_conf.intr_type = LEDC_INTR_DISABLE;
        lcd_conf.gpio_num = LED_PWM_PIN;
        lcd_conf.duty = 0;
        lcd_conf.hpoint = 0;

        ledc_channel_config(&lcd_conf);
    }

    void UpdateDisplay(const char* text) {
        auto display = Board::GetInstance().GetDisplay();
        if (display) {
            display->SetStatus(text);
        }
    }

public:
    OttoController(const HardwareConfig& hw_config) {
        // Khởi tạo Servo
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

            if (app_state == kDeviceStateListening) {
                target = STATE_LISTENING;
            }
            else if (app_state == kDeviceStateSpeaking) {
                target = STATE_SPEAKING;
            }

            if (target != current_state_) {
                current_state_ = target;
                if (current_state_ == STATE_SPEAKING) {
                    otto_.StartSpeakingMode();
                    UpdateDisplay("SPEAKING");
                } else if (current_state_ == STATE_LISTENING) {
                    otto_.Home();
                    UpdateDisplay("LISTENING");
                } else {
                    otto_.Home();
                    UpdateDisplay("IDLE");
                }
            }

            // Điều khiển LED và Cử động
            if (current_state_ == STATE_SPEAKING) {
                otto_.UpdateSpeakingMotion();
                ledc_set_duty(LEDC_LOW_SPEED_MODE, LED_PWM_CHANNEL, 8000); 
            } else {
                breath += (50 * dir);
                if (breath >= 4000 || breath <= 0) dir *= -1;
                ledc_set_duty(LEDC_LOW_SPEED_MODE, LED_PWM_CHANNEL, breath); 
            }
            ledc_update_duty(LEDC_LOW_SPEED_MODE, LED_PWM_CHANNEL);
            
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
};

static OttoController* g_otto = nullptr;

void InitializeOttoController(const HardwareConfig& hw_config) {
    if (!g_otto) {
        g_otto = new OttoController(hw_config);
        ESP_LOGI(TAG, "Otto Controller Initialized (Chatbot Mode)");
    }
}
