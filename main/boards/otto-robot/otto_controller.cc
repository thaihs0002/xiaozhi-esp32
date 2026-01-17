/*
    Otto Robot Controller - Iron Man Edition (SAFE GPIO FIX)
    Board: XiaoZhi ESP32-S3 (Non-Camera/Standard)
    
    WIRING (Left Side Header - Top to Bottom):
    1. Servo Pan  (Head): GPIO 39 (Was Right Leg)
    2. Servo Tilt (Head): GPIO 17 (Was Left Leg)
    3. LED Chest  (Ring): GPIO 18 (Was Left Foot) -> SAFE FOR LED
    4. LED Body   (Strip): GPIO 38 (Was Right Foot) -> SAFE FOR LED
    
    WARNING: DO NOT USE PINS 4, 5, 6, 7 (Reserved for Audio!)
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

// --- CẤU HÌNH CHÂN AN TOÀN (HARDCODED) ---
// Dựa trên sơ đồ chân chuẩn của board XiaoZhi S3
#define PIN_SERVO_PAN       39  // Cổng Servo 1 (Trên cùng bên trái)
#define PIN_SERVO_TILT      17  // Cổng Servo 2
#define PIN_LED_CHEST       18  // Cổng Servo 3 -> Dùng cho LED Ngực
#define PIN_LED_BODY        38  // Cổng Servo 4 (Dưới cùng bên trái) -> Dùng cho LED Thân

#define LED_COUNT_CHEST     12  
#define LED_COUNT_BODY      12  
#define MAX_BRIGHTNESS      80  // Giữ mức này để bảo vệ nguồn

static bool g_is_robot_speaking = false;

class OttoController {
private:
    Otto otto_;
    TaskHandle_t action_task_handle_ = nullptr;
    
    led_strip_handle_t chest_strip_ = nullptr;
    led_strip_handle_t body_strip_ = nullptr;

public:
    OttoController(const HardwareConfig& hw_config) {
        // 1. KHỞI TẠO SERVO (CHỈ 2 CÁI ĐẦU)
        // Chúng ta ghi đè chân từ config bằng chân cứng an toàn
        otto_.Init(
            PIN_SERVO_TILT,   // Left Leg -> Dùng làm Gật (Tilt)
            PIN_SERVO_PAN,    // Right Leg -> Dùng làm Xoay (Pan)
            -1, -1, -1, -1    // Tắt hết các chân khác
        );

        otto_.SetTrims(0, 0, 0, 0, 0, 0); 
        otto_.Home(); 
        
        // 2. KHỞI TẠO LED (Tại chân 18 và 38)
        InitChestLed();
        InitBodyLed();

        // 3. MCP & Task
        RegisterMcpTools();

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

    void InitChestLed() {
        // LED Ngực cắm vào chân 18 (Cổng thứ 3 từ trên xuống bên trái)
        led_strip_config_t strip_config = {
            .strip_gpio_num = PIN_LED_CHEST, 
            .max_leds = LED_COUNT_CHEST,
        };
        led_strip_rmt_config_t rmt_config = {
            .resolution_hz = 10 * 1000 * 1000, 
            .flags = { .with_dma = false },
        };
        // Thử khởi tạo, nếu lỗi RMT thì thử SPI (nhưng S3 thường chạy RMT ngon)
        ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &chest_strip_));
        led_strip_clear(chest_strip_);
    }

    void InitBodyLed() {
        // LED Thân cắm vào chân 38 (Cổng dưới cùng bên trái)
        led_strip_config_t strip_config = {
            .strip_gpio_num = PIN_LED_BODY, 
            .max_leds = LED_COUNT_BODY,
        };
        led_strip_rmt_config_t rmt_config = {
            .resolution_hz = 10 * 1000 * 1000,
            .flags = { .with_dma = false },
        };
        ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &body_strip_));
        led_strip_clear(body_strip_);
    }

    uint8_t Scale(int val) { return (val * MAX_BRIGHTNESS) / 255; }

    // --- HIỆU ỨNG ARC REACTOR (Lõi - Xoay) ---
    void UpdateArcReactor(int step, bool is_active) {
        if (!chest_strip_) return;
        
        int r = 0, g = 100, b = 200; // Cyan
        if (is_active) { r = 255; g = 60; b = 0; } // Cam Đỏ

        for (int i = 0; i < LED_COUNT_CHEST; i++) {
            float wave = (sin((step * 0.3) + (i * 0.5)) + 1.0) / 2.0; 
            int br = 30 + (int)(wave * 200); 

            led_strip_set_pixel(chest_strip_, i, 
                Scale((r * br)/255), Scale((g * br)/255), Scale((b * br)/255));
        }
        led_strip_refresh(chest_strip_);
    }

    // --- HIỆU ỨNG BODY (Thở) ---
    void UpdateBodyBreathing(int step, bool is_active) {
        if (!body_strip_) return;

        float breath = (sin(step * 0.05) + 1.0) / 2.0; 
        if (is_active) breath = (sin(step * 0.2) + 1.0) / 2.0; 

        int r = 0, g = 0, b = 200; // Xanh
        if (is_active) { r = 200; g = 100; b = 0; } // Vàng

        int br = 20 + (int)(breath * 200);

        for (int i = 0; i < LED_COUNT_BODY; i++) {
            led_strip_set_pixel(body_strip_, i, 
                Scale((r * br)/255), Scale((g * br)/255), Scale((b * br)/255));
        }
        led_strip_refresh(body_strip_);
    }

    void AutoBehaviorTask() {
        ESP_LOGI(TAG, "Iron Man System Online (Pins 17/39/18/38)");
        int tick = 0;
        
        while (true) {
            auto state = Application::GetInstance().GetDeviceState();
            
            if (state == kDeviceStateSpeaking) {
                if (!g_is_robot_speaking) g_is_robot_speaking = true;

                int action_rng = rand() % 100;
                if (action_rng < 40) otto_.HeadBob(400, 15);      
                else if (action_rng < 70) otto_.HeadTurn(800, 20);
                
                UpdateArcReactor(tick += 3, true); 
                UpdateBodyBreathing(tick, true);
                vTaskDelay(pdMS_TO_TICKS(30)); 
            } 
            else if (state == kDeviceStateListening) {
                if (g_is_robot_speaking) { otto_.Home(); g_is_robot_speaking = false; }
                
                // Listening: Ngực Trắng, Thân Xanh Lá
                if (chest_strip_) {
                    for(int i=0; i<LED_COUNT_CHEST; i++) led_strip_set_pixel(chest_strip_, i, 80,80,80);
                    led_strip_refresh(chest_strip_);
                }
                if (body_strip_) {
                    for(int i=0; i<LED_COUNT_BODY; i++) led_strip_set_pixel(body_strip_, i, 0,100,0);
                    led_strip_refresh(body_strip_);
                }
                vTaskDelay(pdMS_TO_TICKS(50));
            }
            else { // IDLE
                if (g_is_robot_speaking) { otto_.Home(); g_is_robot_speaking = false; }

                UpdateArcReactor(tick++, false);
                UpdateBodyBreathing(tick, false);
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
        ESP_LOGI(TAG, "Iron Man Controller Initialized");
    }
}
