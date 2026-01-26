/*
    Otto Robot Controller - Soul FX V2 + AUTO WIFI CONFIG
    - Head: GPIO 39, 38
    - Hands: GPIO 8, 12
    - LED Chest: Heartbeat Fade
    - LED Body: Audio Viz + WIFI STATUS
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

// BIẾN ÂM LƯỢNG TỪ AUDIO SERVICE
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
#define MAX_BRIGHTNESS  120 

// THỜI GIAN CHỜ WIFI TRƯỚC KHI BÁO LỖI (Giây)
#define WIFI_CONNECT_TIMEOUT_SEC  20 

#define AUDIO_THRESHOLD 3.0f

static bool g_is_robot_speaking = false;

class OttoController {
private:
    Otto otto_;
    TaskHandle_t action_task_handle_ = nullptr;
    led_strip_handle_t chest_strip_ = nullptr;
    led_strip_handle_t body_strip_ = nullptr;
    
    float body_current_val_ = 0.0f; 
    
    // Biến đếm thời gian WiFi
    int wifi_wait_tick_ = 0;
    bool wifi_connected_once_ = false;

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

    uint8_t Scale(float val) { 
        if (val < 0) val = 0; if (val > 255) val = 255;
        return (uint8_t)((val / 255.0f) * MAX_BRIGHTNESS); 
    }

    // --- HIỆU ỨNG WIFI DISCONNECTED (Nháy Đỏ Cảnh Báo) ---
    void UpdateWifiErrorAnim(int step) {
        if (!body_strip_) return;
        // Nháy đỏ nhanh báo hiệu cần cấu hình
        float flash = (sin(step * 0.5) + 1.0f) / 2.0f; // Nhịp nhanh
        int r = (int)(flash * 255);
        for(int i=0; i<LED_COUNT; i++) {
            led_strip_set_pixel(body_strip_, i, Scale(r), 0, 0); // Đỏ toàn thân
        }
        led_strip_refresh(body_strip_);
        
        // Ngực màu Vàng cảnh báo
        if (chest_strip_) {
            for(int i=0; i<LED_COUNT; i++) led_strip_set_pixel(chest_strip_, i, 50, 50, 0);
            led_strip_refresh(chest_strip_);
        }
    }

    // --- TIM ĐẬP ---
    void UpdateHeartbeat() {
        if (!chest_strip_) return;
        float t = (float)esp_timer_get_time() / 1000000.0f;
        float pulse = pow(sin(t * 3.5f), 12) * 255.0f; 
        float pulse2 = pow(sin(t * 3.5f + 0.6f), 20) * 120.0f;
        float brightness = pulse + pulse2 + 5.0f; 
        if(brightness > 255) brightness = 255;

        int r = 0, g = 200, b = 255;
        for (int i=0; i<LED_COUNT; i++) {
            led_strip_set_pixel(chest_strip_, i, Scale(r*brightness/255), Scale(g*brightness/255), Scale(b*brightness/255));
        }
        led_strip_refresh(chest_strip_);
    }

    // --- AUDIO VIZ ---
    void UpdateAudioViz(int state) {
        if (!body_strip_) return;
        float target = 0.0f;
        
        if (g_real_audio_rms > AUDIO_THRESHOLD) {
            target = g_real_audio_rms * 4.0f; 
        } else {
            target = (state == 0) ? (sin(esp_timer_get_time()/800000.0f)+1.0f)*30.0f : 0.0f;
        }
        if (target > 255) target = 255;

        if (target > body_current_val_) body_current_val_ = target; 
        else body_current_val_ -= (body_current_val_ - target) * 0.15f; 

        uint8_t val = (uint8_t)body_current_val_;
        int r=0, g=0, b=0;
        if (state == 1) { r=255; g=120; b=0; } 
        else if (state == 2) { r=0; g=255; b=50; } 
        else { r=0; g=0; b=val; } 

        float display_val = (state == 0) ? val : (float)val; 
        for(int i=0; i<LED_COUNT; i++) {
            led_strip_set_pixel(body_strip_, i, Scale(r*display_val/255), Scale(g*display_val/255), Scale(b*display_val/255));
        }
        led_strip_refresh(body_strip_);
    }

    void AutoBehaviorTask() {
        ESP_LOGI(TAG, "Soul FX V2 + WiFi Monitor Started");
        int tick = 0;
        
        while (true) {
            auto& wifi = WifiManager::GetInstance();
            std::string ip = wifi.GetIpAddress();
            bool has_ip = !ip.empty() && ip != "0.0.0.0";

            // --- LOGIC KIỂM TRA WIFI ---
            if (has_ip) {
                wifi_connected_once_ = true; // Đã từng kết nối thành công
                wifi_wait_tick_ = 0; // Reset bộ đếm
            } else {
                // Chưa có IP
                if (!wifi_connected_once_ && wifi_wait_tick_ < (WIFI_CONNECT_TIMEOUT_SEC * 10)) {
                    wifi_wait_tick_++; // Đếm thời gian (mỗi tick 100ms)
                }
            }

            // Nếu chưa kết nối và quá thời gian chờ -> Chế độ báo lỗi
            if (!wifi_connected_once_ && wifi_wait_tick_ >= (WIFI_CONNECT_TIMEOUT_SEC * 10)) {
                // Hết thời gian chờ mà chưa có IP:
                // Nháy đèn ĐỎ cảnh báo để người dùng biết cần nhấn nút BOOT
                UpdateWifiErrorAnim(tick++);
                
                // Mẹo: Nếu muốn tự động vào chế độ Config, cần gọi Application::ToggleConfigMode()
                // Nhưng do không truy cập được API đó từ đây, ta dùng đèn báo hiệu.
                
                vTaskDelay(pdMS_TO_TICKS(100));
                continue; // Bỏ qua các hiệu ứng khác
            }

            // --- LOGIC BÌNH THƯỜNG (KHI ĐÃ CÓ WIFI) ---
            auto state = Application::GetInstance().GetDeviceState();
            
            UpdateHeartbeat(); 

            if (state == kDeviceStateSpeaking) {
                if (!g_is_robot_speaking) g_is_robot_speaking = true;
                
                int act = rand()%100;
                if(act<40) otto_.HeadBob(400,15);
                else if(act<70) otto_.HeadTurn(800,20);
                else {
                    int h = rand()%3;
                    if(h==0) otto_.HandWave(LEFT);
                    else if(h==1) otto_.HandWave(RIGHT);
                    else otto_.HandsUp(500,0);
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
        s.AddTool("self.otto.reset", "Reset", PropertyList(), 
            [this](const PropertyList& p){ otto_.Home(); return "OK"; });
    }

    ~OttoController() {
        if(action_task_handle_) vTaskDelete(action_task_handle_);
        if(chest_strip_) led_strip_del(chest_strip_);
        if(body_strip_) led_strip_del(body_strip_);
    }
};

static OttoController* g_otto_controller = nullptr;

void InitializeOttoController(const HardwareConfig& hw_config) {
    if (g_otto_controller == nullptr) {
        g_otto_controller = new OttoController(hw_config);
    }
}
