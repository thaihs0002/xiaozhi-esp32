/*
    Otto Robot Controller - Chatbot Mode (Enhanced Smooth Version)
    Logic: Kết hợp cử động Tay và Đầu khi nói
*/

#include <cJSON.h>
#include <esp_log.h>
#include <cstdlib> 
#include <cstring>
#include <cmath>

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

static bool g_is_robot_speaking = false;

class OttoController {
private:
    Otto otto_;
    TaskHandle_t action_task_handle_ = nullptr;
    bool has_hands_ = false;

public:
    OttoController(const HardwareConfig& hw_config) {
        otto_.Init(
            hw_config.left_leg_pin, 
            hw_config.right_leg_pin, 
            hw_config.left_foot_pin, 
            hw_config.right_foot_pin, 
            hw_config.left_hand_pin, 
            hw_config.right_hand_pin
        );

        // NVS Loading Trim... (Giản lược để code gọn, bạn có thể thêm lại nếu cần)
        otto_.SetTrims(0, 0, 0, 0, 0, 0); 
        otto_.Home(); 
        
        has_hands_ = (hw_config.left_hand_pin != GPIO_NUM_NC && hw_config.right_hand_pin != GPIO_NUM_NC);

        RegisterMcpTools();

        // --- CHATBOT BEHAVIOR TASK ---
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

    // --- LOGIC TỰ ĐỘNG CỬ ĐỘNG ---
    void AutoBehaviorTask() {
        ESP_LOGI(TAG, "Chatbot Mode Active: Enhanced Smooth Motions");
        
        while (true) {
            // 1. Kiểm tra trạng thái AI
            bool audio_busy = false; 
            auto state = Application::GetInstance().GetDeviceState();
            if (state == kDeviceStateSpeaking || state == kDeviceStatePlaying) {
                audio_busy = true;
            }

            if (audio_busy) {
                if (!g_is_robot_speaking) {
                    ESP_LOGI(TAG, "Speaking Start -> Motion Active");
                    g_is_robot_speaking = true;
                }

                // --- LOGIC RANDOM CỬ ĐỘNG (Mix giữa Đầu và Tay) ---
                int action_rng = rand() % 100;

                // 40% Gật đầu nhẹ (Head Bob) - Rất tự nhiên khi nói
                if (action_rng < 40) {
                    otto_.HeadBob(400, 10 + (rand() % 10)); // Speed 400, Intensity 10-20
                }
                // 30% Nghiêng đầu (Head Turn)
                else if (action_rng < 70) {
                    otto_.HeadTurn(800, 15 + (rand() % 10)); // Speed 800, Intensity 15-25
                }
                // 30% Múa tay (nếu có tay)
                else {
                    if (has_hands_) {
                        int hand_type = rand() % 3;
                        if (hand_type == 0) otto_.HandWave(LEFT);
                        else if (hand_type == 1) otto_.HandWave(RIGHT);
                        else {
                            // Giơ nhẹ 2 tay kiểu diễn thuyết (không giơ cao hết cỡ)
                            int pos[6] = {90, 90, 90, 90, 120, 60}; // Tay hơi nâng lên
                            otto_.MoveServos(800, pos);
                            vTaskDelay(pdMS_TO_TICKS(500));
                        }
                    } else {
                        // Nếu không có tay thì gật đầu tiếp
                        otto_.HeadBob(500, 15);
                    }
                }
                
                // Nghỉ ngắn giữa các cụm từ để không bị rối
                vTaskDelay(pdMS_TO_TICKS(100 + (rand() % 300))); 

            } else {
                // --- KHI NGỪNG NÓI ---
                if (g_is_robot_speaking) {
                    ESP_LOGI(TAG, "Speaking End -> Return Home Smoothly");
                    otto_.Home(); // Hàm Home giờ đã dùng MoveServos mượt mà
                    g_is_robot_speaking = false;
                }
                
                vTaskDelay(pdMS_TO_TICKS(100));
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
        mcp_server.AddTool("self.otto.wave", "Wave hand", PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                if(has_hands_) otto_.HandWave(LEFT);
                else otto_.HeadTurn(500, 30);
                return "Waving/Moving";
            });
    }

    ~OttoController() {
        if (action_task_handle_) vTaskDelete(action_task_handle_);
    }
};

static OttoController* g_otto_controller = nullptr;

void InitializeOttoController(const HardwareConfig& hw_config) {
    if (g_otto_controller == nullptr) {
        g_otto_controller = new OttoController(hw_config);
        ESP_LOGI(TAG, "Otto Controller Initialized");
    }
}
