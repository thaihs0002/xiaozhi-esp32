/*
    Otto Robot Controller - Soul FX Pro (Heartbeat Fade + Audio Viz)
    - Head: Servo Pan/Tilt (GPIO 39, 38)
    - Hands: Servo Left/Right (GPIO 8, 12)
    - Chest LED: Heartbeat "Lub-Dub" with Fade (GPIO 17)
    - Body LED: Smooth Voice Visualization (GPIO 18)
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

#define TAG "OttoController"

// --- CẤU HÌNH CHÂN (ĐÃ TEST OK) ---
#define PIN_HEAD_PAN    39  
#define PIN_HEAD_TILT   38  
#define PIN_HAND_LEFT   8   
#define PIN_HAND_RIGHT  12  

#define PIN_LED_CHEST   17  
#define PIN_LED_BODY    18  

#define LED_COUNT       12  
#define MAX_BRIGHTNESS  100 

static bool g_is_robot_speaking = false;

class OttoController {
private:
    Otto otto_;
    TaskHandle_t action_task_handle_ = nullptr;
    
    led_strip_handle_t chest_strip_ = nullptr;
    led_strip_handle_t body_strip_ = nullptr;

    // Biến trạng thái cho hiệu ứng
    float heartbeat_phase_ = 0.0f;
    float body_brightness_ = 0.0f; // Dùng để làm mượt (Smooth transition)
    float body_target_ = 0.0f;     // Đích đến của độ sáng
    
public:
    OttoController(const HardwareConfig& hw_config) {
        // 1. KHỞI TẠO 4 SERVO
        otto_.Init(PIN_HEAD_TILT, PIN_HEAD_PAN, -1, -1, PIN_HAND_LEFT, PIN_HAND_RIGHT);
        otto_.SetTrims(0, 0, 0, 0, 0, 0); 
        otto_.Home(); 
        
        // 2. KHỞI TẠO LED
        InitChestLed();
        InitBodyLed();

        // 3. MCP & Task
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

    // Hàm tiện ích: Map giá trị và giới hạn độ sáng
    uint8_t Scale(float val) { 
        if (val < 0) val = 0;
        if (val > 255) val = 255;
        return (uint8_t)((val / 255.0f) * MAX_BRIGHTNESS); 
    }

    // --- HIỆU ỨNG TIM ĐẬP (Heartbeat Fade) ---
    // Tạo nhịp "Lub-Dub" mềm mại
    void UpdateHeartbeat() {
        if (!chest_strip_) return;
        
        // Tăng pha thời gian
        heartbeat_phase_ += 0.08f; 
        if (heartbeat_phase_ > 6.28f) heartbeat_phase_ = 0.0f; // 2*PI

        // Công thức tạo hình sóng tim đập: sin(x)^8 tạo đỉnh nhọn + khoảng nghỉ
        // Kết hợp 2 sóng lệch pha để tạo nhịp kép (Lub-Dub)
        float beat1 = pow(sin(heartbeat_phase_), 8);       // Nhịp chính
        float beat2 = pow(sin(heartbeat_phase_ + 0.8), 16) * 0.6; // Nhịp phụ (nhỏ hơn)
        
        // Tổng hợp độ sáng (có Fade mượt nhờ hàm sin)
        float brightness = (beat1 + beat2) * 255.0f;
        
        // Màu sắc: Lõi lò phản ứng (Xanh Cyan pha trắng)
        int r = 0, g = 200, b = 255;
        
        for (int i = 0; i < LED_COUNT; i++) {
            led_strip_set_pixel(chest_strip_, i, 
                Scale((r/255.0f) * brightness), 
                Scale((g/255.0f) * brightness), 
                Scale((b/255.0f) * brightness));
        }
        led_strip_refresh(chest_strip_);
    }

    // --- HIỆU ỨNG SÓNG ÂM (VOICE VIZ) ---
    // Giả lập phản ứng âm thanh mượt mà (VU Meter style)
    void UpdateVoiceViz(bool is_speaking) {
        if (!body_strip_) return;

        // 1. Xác định mục tiêu độ sáng (Target)
        if (is_speaking) {
            // Khi nói: Đích đến thay đổi ngẫu nhiên mạnh (như sóng âm cao)
            // Thay đổi target mỗi vài chu kỳ để tránh giật quá nhanh
            if (rand() % 3 == 0) {
                body_target_ = 50 + (rand() % 205); // Min 50, Max 255
            }
        } else {
            // Khi nghe (Listening): Đích đến thấp hơn, nhịp nhàng hơn
            if (rand() % 5 == 0) {
                body_target_ = 20 + (rand() % 100); 
            }
        }

        // 2. Làm mượt (Smoothing / Interpolation)
        // Thay vì nhảy bụp sang giá trị mới, ta trượt từ từ đến đó
        // Công thức: Giá trị hiện tại += (Đích - Hiện tại) * Tốc độ trượt
        float smooth_speed = is_speaking ? 0.2f : 0.1f; // Nói thì phản ứng nhanh hơn
        body_brightness_ += (body_target_ - body_brightness_) * smooth_speed;

        // 3. Hiển thị màu
        int r = 0, g = 0, b = 0;
        if (is_speaking) { 
            r = 255; g = 140; b = 0; // Cam Vàng (Năng lượng phát ra)
        } else {
            r = 0; g = 255; b = 50;  // Xanh lá (Thu âm)
        }

        // Tạo hiệu ứng lan tỏa từ giữa hoặc sáng cả dây
        // Ở đây cho sáng cả dây đồng bộ nhưng mượt mà
        for (int i = 0; i < LED_COUNT; i++) {
            led_strip_set_pixel(body_strip_, i, 
                Scale((r/255.0f) * body_brightness_), 
                Scale((g/255.0f) * body_brightness_), 
                Scale((b/255.0f) * body_brightness_));
        }
        led_strip_refresh(body_strip_);
    }

    // --- HIỆU ỨNG HÔ HẤP (IDLE BREATHING) ---
    void UpdateBreathing(int step) {
        if (!body_strip_) return;
        
        // Sóng Sine chuẩn cho nhịp thở
        float breath = (sin(step * 0.05f) + 1.0f) / 2.0f; // 0.0 -> 1.0
        float val = 10 + (breath * 150); // Không tắt hẳn, sáng mờ đến sáng vừa

        // Màu: Xanh dương sâu (Deep Ocean)
        for (int i = 0; i < LED_COUNT; i++) {
            led_strip_set_pixel(body_strip_, i, 0, Scale(val * 0.3), Scale(val));
        }
        led_strip_refresh(body_strip_);
    }

    // --- LOGIC CHÍNH ---
    void AutoBehaviorTask() {
        ESP_LOGI(TAG, "Soul FX Pro System Online");
        int tick = 0;
        
        while (true) {
            auto state = Application::GetInstance().GetDeviceState();
            
            // 1. NGỰC: Luôn đập nhịp tim (Độc lập trạng thái)
            UpdateHeartbeat();

            // 2. THÂN & HÀNH ĐỘNG: Theo trạng thái
            if (state == kDeviceStateSpeaking) {
                // --- TRẠNG THÁI NÓI ---
                if (!g_is_robot_speaking) g_is_robot_speaking = true;

                // Cử động ngẫu nhiên
                int action_rng = rand() % 100;
                if (action_rng < 40) otto_.HeadBob(400, 15);      
                else if (action_rng < 70) otto_.HeadTurn(800, 20);
                else {
                    int hand = rand() % 3;
                    if(hand == 0) otto_.HandWave(LEFT);
                    else if(hand == 1) otto_.HandWave(RIGHT);
                    else otto_.HandsUp(500, 0);
                }
                
                // LED Body: Phản ứng theo âm thanh (Màu Cam)
                UpdateVoiceViz(true);
                
                vTaskDelay(pdMS_TO_TICKS(20)); // Cập nhật nhanh để LED mượt
            } 
            else if (state == kDeviceStateListening) {
                // --- TRẠNG THÁI NGHE ---
                if (g_is_robot_speaking) { otto_.Home(); g_is_robot_speaking = false; }
                
                // LED Body: Phản ứng nhẹ theo tiếng động (Màu Xanh lá)
                UpdateVoiceViz(false);
                
                vTaskDelay(pdMS_TO_TICKS(20));
            }
            else { 
                // --- TRẠNG THÁI NGHỈ ---
                if (g_is_robot_speaking) { otto_.Home(); g_is_robot_speaking = false; }

                // LED Body: Thở nhẹ nhàng (Màu Xanh dương)
                UpdateBreathing(tick++);
                
                vTaskDelay(pdMS_TO_TICKS(30)); // Nhịp thở chậm hơn
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
        ESP_LOGI(TAG, "Otto Soul FX Controller Initialized");
    }
}
