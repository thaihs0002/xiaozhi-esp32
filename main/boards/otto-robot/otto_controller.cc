#include <cJSON.h>
#include <esp_log.h>
#include "application.h"
#include "board.h"
#include "config.h"
#include "otto_movements.h"
#include <cstring>

#define TAG "OttoLogic"

// Biến trạng thái
enum RobotState {
    STATE_IDLE,
    STATE_LISTENING,
    STATE_SPEAKING
};

class OttoController {
private:
    Otto otto_;
    TaskHandle_t logic_task_handle_ = nullptr;
    RobotState current_state_ = STATE_IDLE;
    RobotState last_state_ = STATE_SPEAKING; // Để force update lần đầu

public:
    OttoController(const HardwareConfig& hw_config) {
        // Init Servo: LEG trái/phải đóng vai trò ĐẦU
        otto_.Init(
            hw_config.left_leg_pin, 
            hw_config.right_leg_pin, 
            -1, -1, // Không dùng chân đi bộ
            hw_config.left_hand_pin, 
            hw_config.right_hand_pin
        );
        
        otto_.SetTrims(0, 0, 0, 0, 0, 0); // Chỉnh lệch tâm nếu cần
        otto_.Home();

        // Tạo Task xử lý logic liên tục
        xTaskCreatePinnedToCore(
            [](void* param) { static_cast<OttoController*>(param)->Loop(); },
            "OttoLoop", 4096, this, 1, &logic_task_handle_, 1
        );
    }

    void UpdateDisplay(const char* text) {
        // Gọi xuống tầng board để hiển thị chữ lên màn hình
        auto display = Board::GetInstance().GetDisplay();
        if (display) {
            display->SetStatus(text);
        }
    }

    void Loop() {
        while (true) {
            // 1. Lấy trạng thái từ XiaoZhi System
            auto app_state = Application::GetInstance().GetDeviceState();
            
            // 2. Chuyển đổi sang trạng thái Robot
            RobotState target_state = STATE_IDLE;
            
            if (app_state == kDeviceStateIdle) {
                target_state = STATE_IDLE;
            } 
            else if (app_state == kDeviceStateListening || app_state == kDeviceStateRecording) {
                target_state = STATE_LISTENING;
            } 
            else if (app_state == kDeviceStateSpeaking || app_state == kDeviceStatePlaying) {
                target_state = STATE_SPEAKING;
            }

            // 3. Xử lý logic chuyển đổi trạng thái (State Machine)
            if (target_state != current_state_) {
                // Chỉ chạy 1 lần khi trạng thái thay đổi
                current_state_ = target_state;
                
                switch (current_state_) {
                    case STATE_IDLE:
                        ESP_LOGI(TAG, ">>> IDLE: DỪNG TOÀN BỘ");
                        otto_.Home();               // Servo về giữa ngay lập tức
                        UpdateDisplay("IDLE");      // Màn hình hiển thị IDLE
                        break;

                    case STATE_LISTENING:
                        ESP_LOGI(TAG, ">>> LISTENING: ĐỨNG YÊN NGHE");
                        otto_.Home();               // Vẫn đứng yên
                        UpdateDisplay("LISTENING"); // Màn hình hiển thị LISTENING
                        break;

                    case STATE_SPEAKING:
                        ESP_LOGI(TAG, ">>> SPEAKING: BẮT ĐẦU DIỄN THUYẾT");
                        otto_.StartSpeakingMode();  // Kích hoạt dao động mượt
                        UpdateDisplay("SPEAKING");  // Màn hình hiển thị SPEAKING
                        break;
                }
            }

            // 4. Xử lý hành động liên tục (cho chuyển động mượt)
            if (current_state_ == STATE_SPEAKING) {
                // Cập nhật vị trí servo theo hình sin mỗi 20ms
                otto_.UpdateSpeakingMotion();
                vTaskDelay(pdMS_TO_TICKS(20));
            } else {
                // Nếu không nói, kiểm tra ít thường xuyên hơn để tiết kiệm CPU
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        }
    }

    ~OttoController() {
        if (logic_task_handle_) vTaskDelete(logic_task_handle_);
    }
};

static OttoController* g_controller = nullptr;

void InitializeOttoController(const HardwareConfig& hw_config) {
    if (!g_controller) {
        g_controller = new OttoController(hw_config);
    }
}
