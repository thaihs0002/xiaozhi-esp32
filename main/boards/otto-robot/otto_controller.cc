/*
    Otto Robot Controller - Soul FX Extreme (Aggressive Viz)
    - Chest: Heartbeat always active (High contrast)
    - Body: Snap & Fade Viz (High dynamic range)
*/

#include <cJSON.h>
#include <esp_log.h>
#include <cstdlib> 
#include <cstring>
#include <cmath>
#include "led_strip.h" 

#include "application.h"
#include "board.h"
#include "config.h"
#include "mcp_server.h"
#include "otto_movements.h"
#include "power_manager.h"
#include "sdkconfig.h"
#include "settings.h"
#include <wifi_manager.h>

// --- BIẾN TOÀN CỤC ĐỂ NHẬN AUDIO RMS (Sẽ dùng ở PHẦN 2) ---
// Mặc định bằng 0, nếu bạn chưa sửa audio_service thì nó sẽ dùng giả lập
float g_real_audio_rms = 0.0f; 

#define TAG "OttoController"

// --- CẤU HÌNH CHÂN ---
#define PIN_HEAD_PAN    39  
#define PIN_HEAD_TILT   38  
#define PIN_HAND_LEFT   8   
#define PIN_HAND_RIGHT  12  

#define PIN_LED_CHEST   17  
#define PIN_LED_BODY    18  

#define LED_COUNT       12  
#define MAX_BRIGHTNESS  120 // Tăng độ sáng tổng thể

static bool g_is_robot_speaking = false;

class OttoController {
private:
    Otto otto_;
    TaskHandle_t action_task_handle_ = nullptr;
    
    led_strip_handle_t chest_strip_ = nullptr;
    led_strip_handle_t body_strip_ = nullptr;

    // Biến trạng thái
    float heartbeat_phase_ = 0.0f;
    float body_current_val_ = 0.0f; 
    
public:
    OttoController(const HardwareConfig& hw_config) {
        otto_.Init(PIN_HEAD_TILT, PIN_HEAD_PAN, -1, -1, PIN_HAND_LEFT, PIN_HAND_RIGHT);
        otto_.SetTrims(0, 0, 0, 0, 0, 0); 
        otto_.Home(); 
        
        InitChestLed();
        InitBodyLed();
        RegisterMcpTools();

        xTaskCreatePinnedToCore(
            [](void* param) { static_cast<OttoController*>(param)->AutoBehaviorTask(); },
            "OttoBehavior", 4096, this, 1, &action_task_handle_, 1
        );
    }

    void InitChestLed() {
        led_strip_config_t strip_config = { .strip_gpio_num = PIN_LED_CHEST, .max_leds = LED_COUNT };
        led_strip_rmt_config_t rmt_config = { .resolution_hz = 10 * 1000 * 1000, .flags = { .with_dma = false } };
        ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &chest_strip_));
        led_strip_clear(chest_strip_);
    }

    void InitBodyLed() {
        led_strip_config_t strip_config = { .strip_gpio_num = PIN_LED_BODY, .max_leds = LED_COUNT };
        led_strip_rmt_config_t rmt_config = { .resolution_hz = 10 * 1000 * 1000, .flags = { .with_dma = false } };
        ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &body_strip_));
        led_strip_clear(body_strip_);
    }

    // --- XỬ LÝ HEARTBEAT (Đập mạnh, dứt khoát) ---
    void UpdateHeartbeat() {
        if (!chest_strip_) return;
        
        // Tốc độ đập
        heartbeat_phase_ += 0.15f; 
        if (heartbeat_phase_ > 6.28f) heartbeat_phase_ = 0.0f;

        // Hàm mũ cao (pow 12) để tạo nhịp đập rất nhọn và khoảng nghỉ sâu
        // Beat 1 (Mạnh) + Beat 2 (Nhẹ)
        float beat = (pow(sin(heartbeat_phase_), 12) * 255.0f) + 
                     (pow(sin(heartbeat_phase_ + 2.5f), 20) * 100.0f);
        
        // Giới hạn
        if (beat > 255) beat = 255;
        if (beat < 5) beat = 5; // Luôn sáng mờ nền

        // Màu Cyan (Lò phản ứng)
        uint8_t val = (uint8_t)beat;
        // Giảm bớt Green để ra màu xanh Iron Man đẹp hơn
        for (int i=0; i<LED_COUNT; i++) {
            led_strip_set_pixel(chest_strip_, i, 0, (val*100)/255, val); 
        }
        led_strip_refresh(chest_strip_);
    }

    // --- XỬ LÝ BODY (VU Meter Style: Tăng nhanh, Giảm chậm) ---
    void UpdateBodyViz(int state) {
        if (!body_strip_) return;

        float target = 0.0f;

        // 1. XÁC ĐỊNH MỤC TIÊU ĐỘ SÁNG (TARGET)
        if (g_real_audio_rms > 1.0f) { 
            // A. Dùng Audio Thực (Nếu có RMS từ hệ thống)
            // RMS thường nhỏ, nhân lên để map ra 0-255
            target = g_real_audio_rms * 10.0f; 
            if (target > 255) target = 255;
        } 
        else {
            // B. Dùng Giả Lập (Simulation) - Khi chưa sửa audio_service
            if (state == 1) { // SPEAKING
                // Tạo xung ngẫu nhiên cực mạnh (50 -> 255)
                if (rand() % 2 == 0) target = 150 + (rand() % 105);
                else target = 20; // Rớt xuống thấp để tạo độ chớp
            } 
            else if (state == 2) { // LISTENING
                // Nháy theo âm thanh môi trường (nhẹ hơn)
                if (rand() % 3 == 0) target = 50 + (rand() % 100);
                else target = 10;
            } 
            else { // IDLE
                // Thở
                target = (sin(esp_timer_get_time() / 1000000.0f) + 1.0f) * 60.0f;
            }
        }

        // 2. THUẬT TOÁN VU METER (Snap & Decay)
        if (target > body_current_val_) {
            // Tăng tốc: Nhảy ngay lập tức hoặc rất nhanh (Snap)
            body_current_val_ += (target - body_current_val_) * 0.6f; 
        } else {
            // Giảm tốc: Tắt dần từ từ (Fade Out / Decay)
            body_current_val_ -= (body_current_val_ - target) * 0.15f; 
        }

        // Kẹp giá trị
        if (body_current_val_ > 255) body_current_val_ = 255;
        if (body_current_val_ < 0) body_current_val_ = 0;

        uint8_t val = (uint8_t)body_current_val_;

        // 3. MÀU SẮC
        uint8_t r=0, g=0, b=0;
        if (state == 1) { r = val; g = val/2; b = 0; } // Speaking: Cam Đậm (Rõ rệt)
        else if (state == 2) { r = 0; g = val; b = val/4; } // Listening: Xanh Lá
        else { r = 0; g = 0; b = val; } // Idle: Xanh Dương

        for (int i=0; i<LED_COUNT; i++) {
            led_strip_set_pixel(body_strip_, i, r, g, b);
        }
        led_strip_refresh(body_strip_);
    }

    void AutoBehaviorTask() {
        ESP_LOGI(TAG, "Soul FX EXTREME Online");
        int tick = 0;
        
        while (true) {
            auto state = Application::GetInstance().GetDeviceState();
            
            // 1. CẬP NHẬT TIM (Luôn chạy, độc lập loop)
            UpdateHeartbeat();

            // 2. XỬ LÝ TRẠNG THÁI
            if (state == kDeviceStateSpeaking) {
                if (!g_is_robot_speaking) g_is_robot_speaking = true;
                
                // Servo
                int action_rng = rand() % 100;
                if (action_rng < 40) otto_.HeadBob(400, 15);      
                else if (action_rng < 70) otto_.HeadTurn(800, 20);
                else {
                    int hand = rand() % 3;
                    if(hand == 0) otto_.HandWave(LEFT);
                    else if(hand == 1) otto_.HandWave(RIGHT);
                    else otto_.HandsUp(500, 0);
                }
                
                // LED Body: Mode 1 (Speaking)
                UpdateBodyViz(1);
                
                vTaskDelay(pdMS_TO_TICKS(25)); // Delay thấp để LED mượt
            } 
            else if (state == kDeviceStateListening) {
                if (g_is_robot_speaking) { otto_.Home(); g_is_robot_speaking = false; }
                
                // LED Body: Mode 2 (Listening)
                UpdateBodyViz(2);
                vTaskDelay(pdMS_TO_TICKS(25));
            }
            else { 
                if (g_is_robot_speaking) { otto_.Home(); g_is_robot_speaking = false; }

                // LED Body: Mode 0 (Idle)
                UpdateBodyViz(0);
                vTaskDelay(pdMS_TO_TICKS(30));
            }
        }
    }

    void RegisterMcpTools() {
        auto& mcp_server = McpServer::GetInstance();
        mcp_server.AddTool("self.otto.reset", "Reset", PropertyList(),
            [this](const PropertyList& p) -> ReturnValue { otto_.Home(); return "OK"; });
    }

    ~OttoController() {
        if (action_task_handle_) vTaskDelete(action_task_handle_);
        if (chest_strip_) led_strip_del(chest_strip_);
        if (body_strip_) led_strip_del(body_strip_);
    }
};

static OttoController* g_otto_controller = nullptr;

void InitializeOttoController(const HardwareConfig& hw_config) {
    if (g_otto_controller == nullptr) {
        g_otto_controller = new OttoController(hw_config);
    }
}
