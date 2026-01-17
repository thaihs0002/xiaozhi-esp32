/*
    Otto Robot Controller - State Machine Version
    Logic: Power On -> Idle -> Listening -> Speaking (Move + Pulse LED) -> Idle
*/

#include <cJSON.h>
#include <esp_log.h>
#include <cstdlib> 
#include <cstring>
#include <cmath>
#include <driver/ledc.h> // Thêm thư viện điều khiển LED PWM
#include "led_strip.h" // Thư viện điều khiển WS2812B
#include "application.h"
#include "board.h"
#include "config.h"
#include "mcp_server.h"
#include "otto_movements.h"
#include "power_manager.h"
#include "sdkconfig.h"
#include "settings.h"
#include <wifi_manager.h>

#define TAG "OttoController"

// Cấu hình LED PWM
#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_OUTPUT_IO          (BUILTIN_LED_GPIO) // Lấy từ config.h
#define LEDC_CHANNEL            LEDC_CHANNEL_0
#define LEDC_DUTY_RES           LEDC_TIMER_13_BIT // 0-8191
#define LEDC_FREQUENCY          5000 // 5 kHz

// Độ sáng LED
#define LED_BRIGHTNESS_IDLE     500  // Sáng nhẹ
#define LED_BRIGHTNESS_LISTEN   4000 // Sáng rõ
#define LED_BRIGHTNESS_MAX      8000 

static bool g_is_robot_speaking = false;

class OttoController {
private:
    Otto otto_;
    TaskHandle_t action_task_handle_ = nullptr;
    bool has_hands_ = false;
    bool led_initialized_ = false;

public:
    OttoController(const HardwareConfig& hw_config) {
        // 1. Khởi tạo Servo
        otto_.Init(
            hw_config.left_leg_pin, 
            hw_config.right_leg_pin, 
            hw_config.left_foot_pin, 
            hw_config.right_foot_pin, 
            hw_config.left_hand_pin, 
            hw_config.right_hand_pin
        );

        otto_.SetTrims(0, 0, 0, 0, 0, 0); 
        otto_.Home(); 
        
        has_hands_ = (hw_config.left_hand_pin != GPIO_NUM_NC && hw_config.right_hand_pin != GPIO_NUM_NC);

        // 2. Khởi tạo LED PWM
        InitLed();

        // 3. Đăng ký MCP
        RegisterMcpTools();

        // 4. Chạy Task điều khiển hành vi
        xTaskCreatePinnedToCore(
            [](void* param) { static_cast<OttoController*>(param)->AutoBehaviorTask(); },
            "OttoBehavior", 
            4096, 
            this, 
            1, 
            &action_task_handle_, 
            1
        );
    }

    void InitLed() {
        if (BUILTIN_LED_GPIO != GPIO_NUM_NC) {
            ledc_timer_config_t ledc_timer = {
                .speed_mode       = LEDC_MODE,
                .duty_resolution  = LEDC_DUTY_RES,
                .timer_num        = LEDC_TIMER,
                .freq_hz          = LEDC_FREQUENCY,  
                .clk_cfg          = LEDC_AUTO_CLK
            };
            ledc_timer_config(&ledc_timer);

            ledc_channel_config_t ledc_channel = {
                .gpio_num       = BUILTIN_LED_GPIO,
                .speed_mode     = LEDC_MODE,
                .channel        = LEDC_CHANNEL,
                .intr_type      = LEDC_INTR_DISABLE,
                .timer_sel      = LEDC_TIMER,
                .duty           = 0, 
                .hpoint         = 0
            };
            ledc_channel_config(&ledc_channel);
            led_initialized_ = true;
        }
    }

    void SetLedBrightness(int duty) {
        if (!led_initialized_) return;
        ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty);
        ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
    }

    // Hiệu ứng Pulse (Nhịp thở) cho LED khi nói
    void PulseLed(int step) {
        if (!led_initialized_) return;
        // Tạo hiệu ứng sin: Sáng -> Tối -> Sáng
        float val = (sin(step * 0.1) + 1.0) / 2.0; // 0.0 -> 1.0
        int duty = LED_BRIGHTNESS_IDLE + (int)(val * (LED_BRIGHTNESS_MAX - LED_BRIGHTNESS_IDLE));
        SetLedBrightness(duty);
    }

    // --- LOGIC TRÌNH TỰ HOẠT ĐỘNG ---
    void AutoBehaviorTask() {
        ESP_LOGI(TAG, "Otto Behavior Task Started");
        int pulse_step = 0;
        
        while (true) {
            auto state = Application::GetInstance().GetDeviceState();
            
            // --- 1. TRẠNG THÁI: SPEAKING (AI TRẢ LỜI) ---
            if (state == kDeviceStateSpeaking) {
                if (!g_is_robot_speaking) {
                    ESP_LOGI(TAG, "State: SPEAKING -> Start Moving");
                    g_is_robot_speaking = true;
                }

                // A. LED: Pulse (Nhịp thở)
                PulseLed(pulse_step++);

                // B. Cử động: Ngẫu nhiên (Chỉ cử động khi nói)
                int action_rng = rand() % 100;
                if (action_rng < 40) otto_.HeadBob(400, 15);      // Gật đầu
                else if (action_rng < 70) otto_.HeadTurn(800, 20); // Nghiêng đầu
                else {
                     if (has_hands_) otto_.HandWave(rand() % 2 == 0 ? LEFT : RIGHT); // Múa tay
                     else otto_.HeadBob(500, 20);
                }
                
                // Nghỉ ngắn giữa các nhịp
                vTaskDelay(pdMS_TO_TICKS(100 + (rand() % 200))); 

            } 
            // --- 2. TRẠNG THÁI: LISTENING (KÍCH HOẠT) ---
            else if (state == kDeviceStateListening) {
                if (g_is_robot_speaking) {
                    otto_.Home(); // Dừng ngay nếu đang cử động
                    g_is_robot_speaking = false;
                }
                
                // A. LED: Sáng hơn (Focus)
                SetLedBrightness(LED_BRIGHTNESS_LISTEN);

                // B. Robot: Đứng yên
                // (Không gọi hàm move nào)
                
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            // --- 3. TRẠNG THÁI: IDLE (CHỜ / POWER ON) ---
            else { // kDeviceStateIdle hoặc trạng thái khác
                if (g_is_robot_speaking) {
                    ESP_LOGI(TAG, "State: IDLE -> Stop Moving");
                    otto_.Home(); // Về vị trí trung lập
                    g_is_robot_speaking = false;
                }

                // A. LED: Sáng nhẹ (Idle)
                SetLedBrightness(LED_BRIGHTNESS_IDLE);

                // B. Robot: Đứng yên
                // (Không gọi hàm move nào)

                vTaskDelay(pdMS_TO_TICKS(200));
            }
        }
    }

    void RegisterMcpTools() {
        auto& mcp_server = McpServer::GetInstance();
        mcp_server.AddTool("self.otto.reset", "Reset servos", PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                otto_.Home();
                return "Reset OK";
            });
    }

    ~OttoController() {
        if (action_task_handle_) vTaskDelete(action_task_handle_);
    }
};

static OttoController* g_otto_controller = nullptr;

void InitializeOttoController(const HardwareConfig& hw_config) {
    if (g_otto_controller == nullptr) {
        g_otto_controller = new OttoController(hw_config);
        ESP_LOGI(TAG, "Otto Controller Initialized");
    }
}
