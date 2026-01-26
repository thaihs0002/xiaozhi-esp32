/*
    Otto Robot Controller - Soul FX Extreme (Fixed Build & Strong Effects)
    - Chest: Heartbeat always active (High contrast & Fade)
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

// BIẾN TOÀN CỤC ĐỂ NHẬN AUDIO RMS TỪ AUDIO_SERVICE
extern float g_real_audio_rms;

#define TAG "OttoController"

// --- CẤU HÌNH CHÂN (Sửa lại theo thực tế nếu cần) ---
#define PIN_HEAD_PAN    39  
#define PIN_HEAD_TILT   38  
#define PIN_HAND_LEFT   8   
#define PIN_HAND_RIGHT  12  

#define PIN_LED_CHEST   17  
#define PIN_LED_BODY    18  

#define LED_COUNT       12  
// Tăng độ sáng tối đa lên cao để hiệu ứng chớp tắt rõ rệt hơn
#define MAX_BRIGHTNESS  180 

static bool g_is_robot_speaking = false;

class OttoController {
private:
    Otto otto_;
    TaskHandle_t action_task_handle_ = nullptr;
    
    led_strip_handle_t chest_strip_ = nullptr;
    led_strip_handle_t body_strip_ = nullptr;

    // Biến trạng thái hiệu ứng
    float heartbeat_phase_ = 0.0f;
    float body_current_val_ = 0.0f; 
    
public:
    OttoController(const HardwareConfig& hw_config) {
        // 1. KHỞI TẠO SERVO
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

    // --- HÀM SCALE ĐÃ SỬA LỖI BIÊN DỊCH ---
    uint8_t Scale(float val) { 
        if (val < 0) {
            val = 0;
        } 
        if (val > 255) {
            val = 255;
        }
        return (uint8_t)((val / 255.0f) * MAX_BRIGHTNESS); 
    }

    // --- HIỆU ỨNG TIM ĐẬP MẠNH (High Contrast Heartbeat) ---
    void UpdateHeartbeat() {
        if (!chest_strip_) return;
        
        // Dùng thời gian thực để nhịp tim không bị trôi khi robot lag
        float t = (float)esp_timer_get_time() / 1000000.0f;
        
        // Nhịp chính (Lub) - Rất nhọn
        float beat1 = pow(sin(t * 3.0f), 60) * 255.0f; 
        
        // Nhịp phụ (Dub) - Nhẹ hơn, lệch pha
        float beat2 = pow(sin(t * 3.0f + 0.4f), 40) * 150.0f;
        
        float brightness = beat1 + beat2 + 5.0f; // +5 để luôn sáng mờ
        if (brightness > 255) brightness = 255;

        // Màu Cyan (Lò phản ứng)
        // R=0, G=200, B=255
        for (int i=0; i<LED_COUNT; i++) {
            led_strip_set_pixel(chest_strip_, i, 
                Scale(0), 
                Scale((200.0f/255.0f) * brightness), 
                Scale((255.0f/255.0f) * brightness));
        }
        led_strip_refresh(chest_strip_);
    }

    // --- HIỆU ỨNG BODY (VU Meter cực mạnh) ---
    void UpdateBodyViz(int state) {
        if (!body_strip_) return;

        float target = 0.0f;

        // 1. XÁC ĐỊNH MỤC TIÊU ĐỘ SÁNG
        // Nếu có tín hiệu âm thanh thực từ audio_service
        if (g_real_audio_rms > 1.0f) { 
            // Nhân hệ số lớn để LED "bung" hết cỡ khi có tiếng nói
            target = g_real_audio_rms * 15.0f; 
        } 
        else {
            // Giả lập nếu không có tiếng (hoặc chưa sửa audio_service)
            if (state == 1) { // SPEAKING
                // Tạo xung ngẫu nhiên cực mạnh (Tắt hẳn rồi sáng rực)
                if (rand() % 3 == 0) target = 200 + (rand() % 55);
                else target = 0; // Tắt hẳn để tạo độ chớp
            } 
            else if (state == 2) { // LISTENING
                // Nháy nhẹ hơn
                if (rand() % 5 == 0) target = 50 + (rand() % 100);
                else target = 10;
            } 
            else { // IDLE
                // Thở sâu và chậm
                target = (sin(esp_timer_get_time() / 1500000.0f) + 1.0f) * 40.0f;
            }
        }

        // Kẹp giá trị
        if (target > 255) target = 255;

        // 2. LÀM MƯỢT KIỂU "SNAP" (Tăng tức thì, giảm từ từ)
        if (target > body_current_val_) {
            // Tăng tốc cực nhanh (Snap)
            body_current_val_ = target; 
        } else {
            // Giảm tốc độ vừa phải (Fade out) để mắt kịp nhìn thấy
            body_current_val_ -= (body_current_val_ - target) * 0.2f; 
        }

        uint8_t val = (uint8_t)body_current_val_;

        // 3. MÀU SẮC
        int r=0, g=0, b=0;
        if (state == 1) { r = val; g = val/3; b = 0; } // Speaking: Cam Đậm (Rõ rệt)
        else if (state == 2) { r = 0; g = val; b = val/5; } // Listening: Xanh Lá
        else { r = 0; g = 0; b = val; } // Idle: Xanh Dương

        for (int i=0; i<LED_COUNT; i++) {
            led_strip_set_pixel(body_strip_, i, Scale(r), Scale(g), Scale(b));
        }
        led_strip_refresh(body_strip_);
    }

    // --- LOOP CHÍNH ---
    void AutoBehaviorTask() {
        ESP_LOGI(TAG, "Soul FX EXTREME Online");
        
        while (true) {
            auto state = Application::GetInstance().GetDeviceState();
            
            // 1. TIM ĐẬP (Luôn chạy)
            UpdateHeartbeat();

            // 2. XỬ LÝ TRẠNG THÁI
            if (state == kDeviceStateSpeaking) {
                if (!g_is_robot_speaking) g_is_robot_speaking = true;
                
                // Servo múa may
                int action_rng = rand() % 100;
                if (action_rng < 40) otto_.HeadBob(400, 15);      
                else if (action_rng < 70) otto_.HeadTurn(800, 20);
                else {
                    int hand = rand() % 3;
                    if(hand == 0) otto_.HandWave(LEFT);
                    else if(hand == 1) otto_.HandWave(RIGHT);
                    else otto_.HandsUp(500, 0);
                }
                
                // LED Body: Mode 1 (Speaking) - Cập nhật nhanh
                UpdateBodyViz(1);
                vTaskDelay(pdMS_TO_TICKS(20)); 
            } 
            else if (state == kDeviceStateListening) {
                if (g_is_robot_speaking) { otto_.Home(); g_is_robot_speaking = false; }
                
                // LED Body: Mode 2 (Listening)
                UpdateBodyViz(2);
                vTaskDelay(pdMS_TO_TICKS(20));
            }
            else { 
                if (g_is_robot_speaking) { otto_.Home(); g_is_robot_speaking = false; }

                // LED Body: Mode 0 (Idle) - Cập nhật chậm hơn chút
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
        ESP_LOGI(TAG, "Otto Controller Initialized");
    }
}
