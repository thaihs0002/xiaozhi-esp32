/*
    Otto Robot Controller - Iron Man Edition (FINAL HARDWARE FIX)
    
    *** WIRING GUIDE (Sơ đồ cắm dây bắt buộc) ***
    
    [CỤM BÊN TRÁI - 4 CỔNG] (Từ trên xuống dưới):
    1. Servo Đầu Xoay (Pan):   GPIO 39
    2. Servo Đầu Gật (Tilt):   GPIO 38
    3. LED Ngực (Chest):       GPIO 17
    4. LED Thân (Body):        GPIO 18
    
    [CỤM BÊN PHẢI - 2 CỔNG]:
    1. Servo Tay Trái:         GPIO 8
    2. Servo Tay Phải:         GPIO 12
    
    TUYỆT ĐỐI KHÔNG CẮM VÀO GPIO 6, 7 (Đó là đường Âm thanh!)
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

// --- ĐỊNH NGHĨA CHÂN CỨNG (HARDCODED) ---
// Servo
#define PIN_HEAD_PAN    39  // Xoay trái phải
#define PIN_HEAD_TILT   38  // Gật gù
#define PIN_HAND_LEFT   8   // Tay trái
#define PIN_HAND_RIGHT  12  // Tay phải

// LED (Cắm vào cụm chân bên trái còn dư)
#define PIN_LED_CHEST   17  // Lò phản ứng
#define PIN_LED_BODY    18  // Dây quanh thân

// Cấu hình LED
#define LED_COUNT       12  // Mỗi dây 12 bóng
#define MAX_BRIGHTNESS  100 // Độ sáng (0-255). Tăng lên 100 nếu nguồn khỏe.

static bool g_is_robot_speaking = false;

class OttoController {
private:
    Otto otto_;
    TaskHandle_t action_task_handle_ = nullptr;
    
    led_strip_handle_t chest_strip_ = nullptr;
    led_strip_handle_t body_strip_ = nullptr;
    bool has_hands_ = true;

public:
    OttoController(const HardwareConfig& hw_config) {
        // 1. KHỞI TẠO 4 SERVO (Đầu + Tay)
        // Chúng ta bỏ qua file config.h và dùng chân cứng đã xác định an toàn
        otto_.Init(
            PIN_HEAD_TILT,   // Left Leg -> Dùng làm Đầu Gật
            PIN_HEAD_PAN,    // Right Leg -> Dùng làm Đầu Xoay
            -1,              // Left Foot -> Bỏ
            -1,              // Right Foot -> Bỏ
            PIN_HAND_LEFT,   // Left Hand
            PIN_HAND_RIGHT   // Right Hand
        );

        otto_.SetTrims(0, 0, 0, 0, 0, 0); 
        otto_.Home(); 
        
        // 2. KHỞI TẠO 2 LED
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

    // Khởi tạo LED Ngực
    void InitChestLed() {
        led_strip_config_t strip_config = {
            .strip_gpio_num = PIN_LED_CHEST, 
            .max_leds = LED_COUNT,
        };
        led_strip_rmt_config_t rmt_config = {
            .resolution_hz = 10 * 1000 * 1000, 
            .flags = { .with_dma = false },
        };
        ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &chest_strip_));
        led_strip_clear(chest_strip_);
    }

    // Khởi tạo LED Thân
    void InitBodyLed() {
        led_strip_config_t strip_config = {
            .strip_gpio_num = PIN_LED_BODY, 
            .max_leds = LED_COUNT,
        };
        led_strip_rmt_config_t rmt_config = {
            .resolution_hz = 10 * 1000 * 1000,
            .flags = { .with_dma = false },
        };
        ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &body_strip_));
        led_strip_clear(body_strip_);
    }

    uint8_t Scale(int val) { return (val * MAX_BRIGHTNESS) / 255; }

    // --- HIỆU ỨNG ARC REACTOR (Xoay vòng) ---
    void UpdateArcReactor(int step, bool is_active) {
        if (!chest_strip_) return;
        
        // Màu mặc định: Xanh Cyan
        int r = 0, g = 150, b = 255; 
        if (is_active) { r = 255; g = 50; b = 0; } // Nói: Cam Đỏ

        for (int i = 0; i < LED_COUNT; i++) {
            // Tạo sóng xoay
            float wave = (sin((step * 0.3) + (i * 0.6)) + 1.0) / 2.0; 
            int br = 20 + (int)(wave * 235); 

            led_strip_set_pixel(chest_strip_, i, 
                Scale((r * br)/255), Scale((g * br)/255), Scale((b * br)/255));
        }
        led_strip_refresh(chest_strip_);
    }

    // --- HIỆU ỨNG BODY (Nhịp thở đồng bộ) ---
    void UpdateBodyBreathing(int step, bool is_active) {
        if (!body_strip_) return;

        // Nhịp thở
        float breath = (sin(step * 0.05) + 1.0) / 2.0; 
        if (is_active) breath = (sin(step * 0.2) + 1.0) / 2.0; // Thở gấp khi nói

        int r = 0, g = 0, b = 255; // Xanh dương
        if (is_active) { r = 255; g = 100; b = 0; } // Vàng cam

        int br = 10 + (int)(breath * 245);

        for (int i = 0; i < LED_COUNT; i++) {
            led_strip_set_pixel(body_strip_, i, 
                Scale((r * br)/255), Scale((g * br)/255), Scale((b * br)/255));
        }
        led_strip_refresh(body_strip_);
    }

    // --- LOGIC HÀNH VI ---
    void AutoBehaviorTask() {
        ESP_LOGI(TAG, "Iron Man System Online");
        int tick = 0;
        
        while (true) {
            auto state = Application::GetInstance().GetDeviceState();
            
            // 1. TRẠNG THÁI NÓI (SPEAKING)
            if (state == kDeviceStateSpeaking) {
                if (!g_is_robot_speaking) g_is_robot_speaking = true;

                // Cử động ngẫu nhiên: Đầu + Tay
                int action_rng = rand() % 100;
                if (action_rng < 40) otto_.HeadBob(400, 15);      
                else if (action_rng < 70) otto_.HeadTurn(800, 20);
                else {
                    // Múa tay
                    int hand = rand() % 3;
                    if(hand == 0) otto_.HandWave(LEFT);
                    else if(hand == 1) otto_.HandWave(RIGHT);
                    else otto_.HandsUp(500, 0);
                }
                
                // LED: Chớp nhanh, màu nóng
                UpdateArcReactor(tick += 4, true); 
                UpdateBodyBreathing(tick, true);
                vTaskDelay(pdMS_TO_TICKS(30)); 
            } 
            // 2. TRẠNG THÁI NGHE (LISTENING)
            else if (state == kDeviceStateListening) {
                if (g_is_robot_speaking) { otto_.Home(); g_is_robot_speaking = false; }
                
                // LED: Sáng tĩnh (Focus Mode)
                if (chest_strip_) {
                    for(int i=0; i<LED_COUNT; i++) led_strip_set_pixel(chest_strip_, i, 100,100,100); // Trắng
                    led_strip_refresh(chest_strip_);
                }
                if (body_strip_) {
                    for(int i=0; i<LED_COUNT; i++) led_strip_set_pixel(body_strip_, i, 0,150,0); // Xanh lá
                    led_strip_refresh(body_strip_);
                }
                vTaskDelay(pdMS_TO_TICKS(50));
            }
            // 3. TRẠNG THÁI NGHỈ (IDLE)
            else {
                if (g_is_robot_speaking) { otto_.Home(); g_is_robot_speaking = false; }

                // LED: Xoay chậm, Thở chậm (Cool Mode)
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
        // Comment nếu bị lỗi biên dịch
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
