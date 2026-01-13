#include <esp_log.h>
#include "application.h"
#include "board.h"
#include "config.h"
#include "otto_movements.h"
#include "driver/ledc.h"
#include "display/display.h" // <<-- ĐÃ THÊM: Quan trọng để gọi SetStatus

#define TAG "OttoController"

// CẤU HÌNH LED (Sửa lại số GPIO chân dư của bạn vào đây, ví dụ GPIO 8)
#define LED_PWM_CHANNEL LEDC_CHANNEL_5
#define LED_PWM_PIN     8 

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
        if (display) {
            display->SetStatus(text);
        }
    }

public:
    OttoController(const HardwareConfig& hw_config) {
        // Khởi tạo Servo: Leg dùng làm Đầu, Hand dùng làm Tay
        // Tắt chân Foot (dư) để dùng cho LED
        otto_.Init(hw_config.left_leg_pin, hw_config.right_leg_pin, -1, -1, 
                   hw_config.left_hand_pin, hw_config.right_hand_pin);
        
        InitLed();
        
        // Tạo task chạy loop
        xTaskCreatePinnedToCore([](void* p){ ((OttoController*)p)->Loop(); }, 
                                "OttoLoop", 4096, this, 1, NULL, 1);
    }

    void Loop() {
        static int breath = 0, dir = 1;
        while (true) {
            auto app_state = Application::GetInstance().GetDeviceState();
            RobotState target = STATE_IDLE;

            // ĐÃ SỬA: Chỉ dùng các trạng thái cơ bản có sẵn
            if (app_state == kDeviceStateListening) {
                target = STATE_LISTENING;
            }
            else if (app_state == kDeviceStateSpeaking) {
                target = STATE_SPEAKING;
            }
            // Các trạng thái khác (Idle, Connecting...) đều coi là Idle

            // Xử lý chuyển đổi trạng thái
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

            // Hiệu ứng LED & Cử động liên tục
            if (current_state_ == STATE_SPEAKING) {
                otto_.UpdateSpeakingMotion();
                // LED sáng mạnh khi nói
                ledc_set_duty(LEDC_LOW_SPEED_MODE, LED_PWM_CHANNEL, 8000); 
            } else {
                // LED thở nhẹ khi nghỉ
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
