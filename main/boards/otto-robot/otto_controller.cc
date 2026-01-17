/*
    Otto Robot Controller - Iron Man Edition (Safe Power Mode)
    - Head: 2 Servos (Pan/Tilt)
    - Chest: 12-LED Ring (Arc Reactor) - Connected to PIN_LED_CHEST
    - Body: 12-LED Strip (Breathing)   - Connected to PIN_LED_BODY
*/

#include <cJSON.h>
#include <esp_log.h>
#include <cstdlib> 
#include <cstring>
#include <cmath>
#include "led_strip.h" // Thư viện WS2812B

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

// --- CẤU HÌNH LED ---
// Kiểm tra kỹ lại dây cắm của bạn có đúng chân 6 và 7 không
#define PIN_LED_CHEST       6   // Chân Left Hand cũ
#define PIN_LED_BODY        7   // Chân Right Hand cũ

// Cả 2 đều dùng 12 bóng theo yêu cầu
#define LED_COUNT_CHEST     12  
#define LED_COUNT_BODY      12  

// ĐỘ SÁNG AN TOÀN (0-255)
// Giảm xuống thấp để tránh sụt nguồn gây nhiễu loa và lỗi LED
#define MAX_BRIGHTNESS      60  

#define LED_RMT_RES_HZ      (10 * 1000 * 1000) // 10MHz Resolution

static bool g_is_robot_speaking = false;

class OttoController {
private:
    Otto otto_;
    TaskHandle_t action_task_handle_ = nullptr;
    
    led_strip_handle_t chest_strip_ = nullptr;
    led_strip_handle_t body_strip_ = nullptr;

public:
    OttoController(const HardwareConfig& hw_config) {
        // 1. KHỞI TẠO SERVO (CHỈ ĐẦU)
        otto_.Init(
            hw_config.left_leg_pin,   
            hw_config.right_leg_pin,  
            -1, // Bỏ chân
            -1, // Bỏ chân
            -1, // DÀNH CHO LED NGỰC (GPIO 6)
            -1  // DÀNH CHO LED THÂN (GPIO 7)
        );

        otto_.SetTrims(0, 0, 0, 0, 0, 0); 
        otto_.Home(); 
        
        // 2. KHỞI TẠO LED
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
        led_strip_config_t strip_config = {
            .strip_gpio_num = PIN_LED_CHEST,
            .max_leds = LED_COUNT_CHEST,
            .led_pixel_format = LED_PIXEL_FORMAT_GRB, // Chuẩn WS2812B
            .led_model = LED_MODEL_WS2812,
            .flags = { .invert_out = false },
        };
        led_strip_rmt_config_t rmt_config = {
            .clk_src = RMT_CLK_SRC_DEFAULT,
            .resolution_hz = LED_RMT_RES_HZ,
            .flags = { .with_dma = false },
        };
        ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &chest_strip_));
        led_strip_clear(chest_strip_);
    }

    void InitBodyLed() {
        led_strip_config_t strip_config = {
            .strip_gpio_num = PIN_LED_BODY,
            .max_leds = LED_COUNT_BODY,
            .led_pixel_format = LED_PIXEL_FORMAT_GRB, 
            .led_model = LED_MODEL_WS2812,
            .flags = { .invert_out = false },
        };
        led_strip_rmt_config_t rmt_config = {
            .clk_src = RMT_CLK_SRC_DEFAULT,
            .resolution_hz = LED_RMT_RES_HZ,
            .flags = { .with_dma = false },
        };
        ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &body_strip_));
        led_strip_clear(body_strip_);
    }

    // Hàm tiện ích: Tự động giảm độ sáng để bảo vệ nguồn
    uint8_t ScaleBrightness(int val) {
        return (val * MAX_BRIGHTNESS) / 255;
    }

    // --- HIỆU ỨNG LÒ PHẢN ỨNG (ARC REACTOR) ---
    void UpdateArcReactor(int step, bool is_active) {
        if (!chest_strip_) return;

        // Màu Iron Man: Xanh lơ (Cyan) hoặc Cam (Alert)
        uint8_t base_r = 0, base_g = 100, base_b = 200; 
        
        if (is_active) { 
             base_r = 255; base_g = 60; base_b = 0; // Cam đỏ rực
        }

        for (int i = 0; i < LED_COUNT_CHEST; i++) {
            // Hiệu ứng xoay
            float spin = (sin((step * 0.25) + (i * 0.5)) + 1.0) / 2.0; 
            int val = 50 + (int)(spin * 200); // 50-255

            led_strip_set_pixel(chest_strip_, i, 
                ScaleBrightness((base_r * val) / 255), 
                ScaleBrightness((base_g * val) / 255), 
                ScaleBrightness((base_b * val) / 255));
        }
        led_strip_refresh(chest_strip_);
    }

    // --- HIỆU ỨNG NHỊP THỞ (BODY) ---
    void UpdateBodyBreathing(int step, bool is_alert) {
        if (!body_strip_) return;

        float breath = (sin(step * 0.08) + 1.0) / 2.0; 

        uint8_t r, g, b;
        if (is_alert) {
            breath = (sin(step * 0.2) + 1.0) / 2.0; // Thở gấp
            r = 255; g = 100; b = 0; // Cam
        } else {
            r = 0; g = 0; b = 255; // Xanh dương đậm
        }

        int val = 20 + (int)(breath * 200); // Không tắt hẳn

        for (int i = 0; i < LED_COUNT_BODY; i++) {
            // Body sáng đều nhau (thở đồng bộ)
            led_strip_set_pixel(body_strip_, i, 
                ScaleBrightness((r * val) / 255), 
                ScaleBrightness((g * val) / 255), 
                ScaleBrightness((b * val) / 255));
        }
        led_strip_refresh(body_strip_);
    }

    // --- LOGIC CHÍNH ---
    void AutoBehaviorTask() {
        ESP_LOGI(TAG, "Iron Man Protocol Initiated (Safe Power)");
        int tick = 0;
        
        while (true) {
            auto state = Application::GetInstance().GetDeviceState();
            
            // 1. SPEAKING
            if (state == kDeviceStateSpeaking) {
                if (!g_is_robot_speaking) {
                    g_is_robot_speaking = true;
                }

                int action_rng = rand() % 100;
                if (action_rng < 40) otto_.HeadBob(400, 15);      
                else if (action_rng < 70) otto_.HeadTurn(800, 20);
                
                // LED chớp nhanh, màu nóng
                UpdateArcReactor(tick += 3, true); 
                UpdateBodyBreathing(tick, true);

                vTaskDelay(pdMS_TO_TICKS(40)); 
            } 
            // 2. LISTENING
            else if (state == kDeviceStateListening) {
                if (g_is_robot_speaking) {
                    otto_.Home();
                    g_is_robot_speaking = false;
                }
                
                // Listening: Sáng tĩnh màu Xanh lá (Green)
                if (chest_strip_) {
                    for(int i=0; i<LED_COUNT_CHEST; i++) 
                        led_strip_set_pixel(chest_strip_, i, 0, ScaleBrightness(200), 0);
                    led_strip_refresh(chest_strip_);
                }
                if (body_strip_) {
                    for(int i=0; i<LED_COUNT_BODY; i++) 
                        led_strip_set_pixel(body_strip_, i, 0, ScaleBrightness(100), 0); 
                    led_strip_refresh(body_strip_);
                }

                vTaskDelay(pdMS_TO_TICKS(50));
            }
            // 3. IDLE
            else {
                if (g_is_robot_speaking) {
                    otto_.Home();
                    g_is_robot_speaking = false;
                }

                // Idle: Xoay chậm màu xanh lơ
                UpdateArcReactor(tick++, false);
                UpdateBodyBreathing(tick, false);

                vTaskDelay(pdMS_TO_TICKS(50));
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
        // Nếu bản led_strip cũ bị lỗi dòng del thì comment lại 2 dòng dưới
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
