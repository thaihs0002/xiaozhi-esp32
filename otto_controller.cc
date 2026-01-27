/*
    Otto Robot Controller - Soul FX Extreme (FINAL FIX)
    - Fix Indentation Error
    - Real-time Audio Response
    - Heartbeat with smooth fade
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

// LIÊN KẾT BIẾN TỪ AUDIO_SERVICE
extern float g_real_audio_rms;

#define TAG "OttoController"

// --- CẤU HÌNH ---
#define PIN_HEAD_PAN    39  
#define PIN_HEAD_TILT   38  
#define PIN_HAND_LEFT   8   
#define PIN_HAND_RIGHT  12  
#define PIN_LED_CHEST   17  
#define PIN_LED_BODY    18  
#define LED_COUNT       12  
#define MAX_BRIGHTNESS  150 

static bool g_is_robot_speaking = false;

class OttoController {
private:
    Otto otto_;
    TaskHandle_t action_task_handle_ = nullptr;
    led_strip_handle_t chest_strip_ = nullptr;
    led_strip_handle_t body_strip_ = nullptr;
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

    // --- SỬA LỖI BIÊN DỊCH TẠI ĐÂY ---
    uint8_t Scale(float val) { 
        if (val < 0) {
            val = 0;
        }
        if (val > 255) {
            val = 255;
        }
        return (uint8_t)((val / 255.0f) * MAX_BRIGHTNESS); 
    }

    // Tim đập hiệu ứng Fade mượt
    void UpdateHeartbeat() {
        if (!chest_strip_) return;
        float t = (float)esp_timer_get_time() / 1000000.0f;
        // Nhịp tim Lub-Dub kép
        float pulse = pow(sin(t * 3.5f), 12) * 255.0f; 
        float pulse2 = pow(sin(t * 3.5f + 0.6f), 20) * 120.0f;
        float brightness = pulse + pulse2 + 8.0f; 
        if(brightness > 255) brightness = 255;

        for (int i=0; i<LED_COUNT; i++) {
            led_strip_set_pixel(chest_strip_, i, Scale(0), Scale(200*brightness/255), Scale(255*brightness/255));
        }
        led_strip_refresh(chest_strip_);
    }

    // Nháy theo âm lượng thực tế
    void UpdateAudioViz(int state) {
        if (!body_strip_) return;
        float target = 0.0f;
        
        // Khuếch đại tín hiệu RMS từ audio_service
        if (g_real_audio_rms > 2.0f) {
            target = g_real_audio_rms * 8.0f; // Nháy cực mạnh
        } else {
            // Idle breathing
            target = (state == 0) ? (sin(esp_timer_get_time()/800000.0f)+1.0f)*40.0f : 0.0f;
        }
        if (target > 255) target = 255;

        // Snap and Decay (Làm mượt nhưng phản hồi nhanh)
        if (target > body_current_val_) body_current_val_ = target;
        else body_current_val_ -= (body_current_val_ - target) * 0.2f;

        uint8_t val = (uint8_t)body_current_val_;
        int r=0, g=0, b=0;
        if (state == 1) { r=val; g=val/3; b=0; } // Speaking (Orange)
        else if (state == 2) { r=0; g=val; b=val/6; } // Listening (Green)
        else { r=0; g=0; b=val; } // Idle (Blue)

        for(int i=0; i<LED_COUNT; i++) {
            led_strip_set_pixel(body_strip_, i, Scale(r), Scale(g), Scale(b));
        }
        led_strip_refresh(body_strip_);
    }

    void AutoBehaviorTask() {
        while (true) {
            auto state = Application::GetInstance().GetDeviceState();
            UpdateHeartbeat(); 

            if (state == kDeviceStateSpeaking) {
                if (!g_is_robot_speaking) g_is_robot_speaking = true;
                int act = rand()%100;
                if(act<40) otto_.HeadBob(400,15);
                else if(act<70) otto_.HeadTurn(800,20);
                else {
                    if(rand()%2==0) otto_.HandWave(LEFT);
                    else otto_.HandWave(RIGHT);
                }
                UpdateAudioViz(1);
                vTaskDelay(pdMS_TO_TICKS(20));
            } 
            else if (state == kDeviceStateListening) {
                if (g_is_robot_speaking) { otto_.Home(); g_is_robot_speaking=false; }
                UpdateAudioViz(2);
                vTaskDelay(pdMS_TO_TICKS(20));
            }
            else { 
                if (g_is_robot_speaking) { otto_.Home(); g_is_robot_speaking=false; }
                UpdateAudioViz(0);
                vTaskDelay(pdMS_TO_TICKS(30));
            }
        }
    }

    void RegisterMcpTools() {
        auto& s = McpServer::GetInstance();
        s.AddTool("self.otto.reset", "Reset", PropertyList(), [this](const PropertyList& p){ otto_.Home(); return "OK"; });
    }

    ~OttoController() {
        if(action_task_handle_) vTaskDelete(action_task_handle_);
    }
};

static OttoController* g_otto_controller = nullptr;
void InitializeOttoController(const HardwareConfig& hw_config) {
    if (g_otto_controller == nullptr) g_otto_controller = new OttoController(hw_config);
}