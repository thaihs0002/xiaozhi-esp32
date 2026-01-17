/*
    Otto Robot Controller - Soul FX Edition
    - Head: Servo Pan/Tilt (GPIO 39, 38)
    - Hands: Servo Left/Right (GPIO 8, 12)
    - Chest LED: Heartbeat Effect (GPIO 17)
    - Body LED: Breathing / Voice Viz (GPIO 18)
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

// --- CẤU HÌNH CHÂN (HARDCODED AN TOÀN) ---
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

    // Biến cho hiệu ứng nhịp tim
    int heartbeat_tick_ = 0;
    
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

    uint8_t Scale(int val) { return (val * MAX_BRIGHTNESS) / 255; }

    // --- HIỆU ỨNG NHỊP TIM (HEARTBEAT) ---
    // Mô phỏng nhịp "Lub-Dub" của tim người
    void UpdateHeartbeat() {
        if (!chest_strip_) return;
        
        // Tăng biến đếm thời gian
        heartbeat_tick_++;
        if (heartbeat_tick_ > 60) heartbeat_tick_ = 0; // Chu kỳ khoảng 1.2 giây (50ms * 60)

        float brightness = 0;
        
        // Nhịp 1 (Lub): Mạnh
        if (heartbeat_tick_ >= 0 && heartbeat_tick_ < 10) {
            brightness = sin(heartbeat_tick_ * 0.314) * 255; 
        }
        // Nhịp 2 (Dub): Nhẹ hơn chút
        else if (heartbeat_tick_ >= 12 && heartbeat_tick_ < 20) {
            brightness = sin((heartbeat_tick_ - 12) * 0.39) * 200;
        }
        // Nghỉ (Diastole)
        else {
            brightness = 10; // Sáng mờ giữ nền
        }

        if (brightness < 0) brightness = 0;

        // Màu sắc: Lõi lò phản ứng (Xanh Cyan pha trắng)
        uint8_t r = 0, g = 200, b = 255;
        
        for (int i = 0; i < LED_COUNT; i++) {
            led_strip_set_pixel(chest_strip_, i, 
                Scale((r * (int)brightness)/255), 
                Scale((g * (int)brightness)/255), 
                Scale((b * (int)brightness)/255));
        }
        led_strip_refresh(chest_strip_);
    }

    // --- HIỆU ỨNG HÔ HẤP (IDLE BREATHING) ---
    void UpdateBreathing(int step) {
        if (!body_strip_) return;
        
        // Sóng Sine chậm
        float breath = (sin(step * 0.05) + 1.0) / 2.0; 
        int val = 20 + (int)(breath * 150); // Min 20, Max 170

        // Màu: Xanh dương sâu (Deep Ocean)
        for (int i = 0; i < LED_COUNT; i++) {
            led_strip_set_pixel(body_strip_, i, 0, Scale(val/2), Scale(val));
        }
        led_strip_refresh(body_strip_);
    }

    // --- HIỆU ỨNG SÓNG ÂM (VOICE VIZ) ---
    // Giả lập sóng âm thanh phản hồi theo giọng nói/nghe
    void UpdateVoiceViz(bool is_speaking) {
        if (!body_strip_) return;

        // Tạo độ lớn ngẫu nhiên để giả lập biên độ âm thanh
        // Speaking: Biên độ lớn, thay đổi nhanh
        // Listening: Biên độ nhỏ hơn, nhấp nháy chờ đợi
        int amplitude = rand() % 255; 
        
        // Màu sắc
        int r, g, b;
        if (is_speaking) { 
            // Nói: Cam/Vàng (Năng lượng phát ra)
            r = 255; g = 100; b = 0; 
            // Giữ biên độ cao hơn
            amplitude = 50 + (rand() % 205);
        } else {
            // Nghe: Xanh lá (Đang thu âm)
            r = 0; g = 255; b = 50;
            // Biên độ nhạy hơn (nhấp nháy theo tiếng ồn môi trường giả lập)
            amplitude = rand() % 150;
        }

        // Hiệu ứng VU Meter (Sáng từ giữa ra hoặc sáng ngẫu nhiên)
        // Ở đây dùng sáng toàn thân đồng bộ theo nhịp (dễ nhìn nhất)
        int val = Scale(amplitude);
        for (int i = 0; i < LED_COUNT; i++) {
            led_strip_set_pixel(body_strip_, i, 
                (r * val)/255, (g * val)/255, (b * val)/255);
        }
        led_strip_refresh(body_strip_);
    }

    // --- LOGIC CHÍNH ---
    void AutoBehaviorTask() {
        ESP_LOGI(TAG, "Soul FX System Online");
        int tick = 0;
        
        while (true) {
            auto state = Application::GetInstance().GetDeviceState();
            
            // Luôn cập nhật nhịp tim (Độc lập với trạng thái)
            UpdateHeartbeat();

            // Xử lý Body LED & Servo theo trạng thái
            if (state == kDeviceStateSpeaking) {
                // 1. SPEAKING
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
                
                // LED Body: Chớp theo giọng nói (Giả lập)
                UpdateVoiceViz(true);
                
                // Delay ngắn để LED chớp nhanh (như tiếng nói)
                vTaskDelay(pdMS_TO_TICKS(50)); 
            } 
            else if (state == kDeviceStateListening) {
                // 2. LISTENING
                if (g_is_robot_speaking) { otto_.Home(); g_is_robot_speaking = false; }
                
                // LED Body: Chớp theo âm thanh môi trường (Giả lập)
                UpdateVoiceViz(false);
                
                vTaskDelay(pdMS_TO_TICKS(50));
            }
            else { 
                // 3. IDLE
                if (g_is_robot_speaking) { otto_.Home(); g_is_robot_speaking = false; }

                // LED Body: Thở nhẹ nhàng
                UpdateBreathing(tick++);
                
                vTaskDelay(pdMS_TO_TICKS(50));
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
