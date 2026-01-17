/*
    Otto Robot Controller - Iron Man Edition
    - Head: 2 Servos (Pan/Tilt)
    - Chest: 12-LED Ring (Arc Reactor) - Connected to PIN_LEFT_HAND (GPIO 6)
    - Body: LED Strip (Breathing)      - Connected to PIN_RIGHT_HAND (GPIO 7)
*/

#include <cJSON.h>
#include <esp_log.h>
#include <cstdlib> 
#include <cstring>
#include <cmath>
#include "led_strip.h" // Thư viện WS2812B chuẩn của ESP-IDF

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
// Tận dụng chân Servo tay để làm chân LED
#define PIN_LED_CHEST       6   // Chân Left Hand cũ -> Vòng tròn ngực
#define PIN_LED_BODY        7   // Chân Right Hand cũ -> Dây quanh thân

#define LED_COUNT_CHEST     12  // Số bóng vòng ngực
#define LED_COUNT_BODY      16  // Số bóng quanh thân (bạn có thể sửa số này)

#define LED_RMT_RES_HZ      (10 * 1000 * 1000) // 10MHz Resolution

static bool g_is_robot_speaking = false;

class OttoController {
private:
    Otto otto_;
    TaskHandle_t action_task_handle_ = nullptr;
    
    // Hai đối tượng điều khiển 2 dây LED riêng biệt
    led_strip_handle_t chest_strip_ = nullptr;
    led_strip_handle_t body_strip_ = nullptr;

public:
    OttoController(const HardwareConfig& hw_config) {
        // 1. KHỞI TẠO SERVO (CHỈ ĐẦU)
        // Chúng ta truyền -1 vào các chân không dùng để giải phóng cho LED
        otto_.Init(
            hw_config.left_leg_pin,   // Giữ lại làm Đầu (Xoay/Gật)
            hw_config.right_leg_pin,  // Giữ lại làm Đầu (Xoay/Gật)
            -1, // Left Foot -> Bỏ
            -1, // Right Foot -> Bỏ
            -1, // Left Hand -> DÀNH CHO LED NGỰC
            -1  // Right Hand -> DÀNH CHO LED THÂN
        );

        otto_.SetTrims(0, 0, 0, 0, 0, 0); 
        otto_.Home(); 
        
        // 2. KHỞI TẠO 2 HỆ THỐNG LED
        InitChestLed();
        InitBodyLed();

        // 3. Đăng ký MCP & Chạy Task
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

    // --- KHỞI TẠO LED NGỰC (ARC REACTOR) ---
    void InitChestLed() {
        led_strip_config_t strip_config = {
            .strip_gpio_num = PIN_LED_CHEST,
            .max_leds = LED_COUNT_CHEST,
            .led_pixel_format = LED_PIXEL_FORMAT_GRB, 
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

    // --- KHỞI TẠO LED THÂN (BREATHING) ---
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

    // --- HIỆU ỨNG LÒ PHẢN ỨNG (ARC REACTOR) ---
    // Mô phỏng vòng tròn năng lượng xoay hoặc đập
    void UpdateArcReactor(int step, bool is_active) {
        if (!chest_strip_) return;

        // Màu chủ đạo: Cyan (Xanh lơ) đặc trưng của Iron Man
        uint8_t base_r = 0, base_g = 100, base_b = 200; 
        
        if (is_active) { // Khi nói: Lõi nóng lên (chuyển sang trắng/đỏ)
             base_r = 200; base_g = 50; base_b = 50; // Đỏ cam
        }

        for (int i = 0; i < LED_COUNT_CHEST; i++) {
            // Tạo hiệu ứng xoay nhẹ độ sáng
            // offset pha dựa trên vị trí i để tạo cảm giác xoay
            float spin = (sin((step * 0.2) + (i * 0.5)) + 1.0) / 2.0; 
            int brightness = 20 + (int)(spin * 80); // Độ sáng dao động 20-100

            // Giảm độ sáng tổng thể để không chói
            led_strip_set_pixel(chest_strip_, i, 
                (base_r * brightness) / 255, 
                (base_g * brightness) / 255, 
                (base_b * brightness) / 255);
        }
        led_strip_refresh(chest_strip_);
    }

    // --- HIỆU ỨNG NHỊP THỞ (BODY) ---
    void UpdateBodyBreathing(int step, bool is_alert) {
        if (!body_strip_) return;

        // Tính toán nhịp thở (Sine wave)
        float breath = (sin(step * 0.05) + 1.0) / 2.0; // Chậm hơn ngực

        uint8_t r, g, b;
        if (is_alert) {
            // Cảnh báo/Nói: Thở nhanh màu vàng/cam
            breath = (sin(step * 0.15) + 1.0) / 2.0; // Nhanh hơn
            r = 150; g = 100; b = 0;
        } else {
            // Idle: Thở chậm màu xanh dương đậm (Deep Cor)
            r = 0; g = 0; b = 150;
        }

        // Áp dụng độ sáng thở
        int val = 10 + (int)(breath * 100); 

        for (int i = 0; i < LED_COUNT_BODY; i++) {
            led_strip_set_pixel(body_strip_, i, 
                (r * val) / 255, 
                (g * val) / 255, 
                (b * val) / 255);
        }
        led_strip_refresh(body_strip_);
    }

    // --- LOGIC CHÍNH ---
    void AutoBehaviorTask() {
        ESP_LOGI(TAG, "Iron Man Protocol Initiated");
        int tick = 0;
        
        while (true) {
            auto state = Application::GetInstance().GetDeviceState();
            
            // 1. TRẠNG THÁI NÓI (SPEAKING)
            if (state == kDeviceStateSpeaking) {
                if (!g_is_robot_speaking) {
                    g_is_robot_speaking = true;
                }

                // Cử động đầu (chỉ dùng 2 servo đầu)
                int action_rng = rand() % 100;
                if (action_rng < 40) otto_.HeadBob(400, 15);      
                else if (action_rng < 70) otto_.HeadTurn(800, 20);
                
                // LED: Arc Reactor đỏ rực/xoay nhanh, Body thở gấp
                UpdateArcReactor(tick += 2, true); 
                UpdateBodyBreathing(tick, true);

                vTaskDelay(pdMS_TO_TICKS(50)); 
            } 
            // 2. TRẠNG THÁI NGHE (LISTENING)
            else if (state == kDeviceStateListening) {
                if (g_is_robot_speaking) {
                    otto_.Home();
                    g_is_robot_speaking = false;
                }
                
                // LED: Ngực sáng trắng tĩnh (Focus), Body tắt hoặc sáng nhẹ
                if (chest_strip_) {
                    for(int i=0; i<LED_COUNT_CHEST; i++) 
                        led_strip_set_pixel(chest_strip_, i, 100, 100, 100); // Trắng
                    led_strip_refresh(chest_strip_);
                }
                // Body xanh lá nhẹ
                if (body_strip_) {
                    for(int i=0; i<LED_COUNT_BODY; i++) 
                        led_strip_set_pixel(body_strip_, i, 0, 50, 0); 
                    led_strip_refresh(body_strip_);
                }

                vTaskDelay(pdMS_TO_TICKS(50));
            }
            // 3. TRẠNG THÁI NGHỈ (IDLE)
            else {
                if (g_is_robot_speaking) {
                    otto_.Home();
                    g_is_robot_speaking = false;
                }

                // LED: Arc Reactor xoay xanh lơ, Body thở chậm xanh dương
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
