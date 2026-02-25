#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "otto_eye_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"
#include "lamp_controller.h"
#include "led/single_led.h"

#include <esp_log.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_sh1106.h>
#include "otto_body.h"
#include <esp_wifi.h>

#define TAG "MachTimOpswat"

class MachTimOpswat : public WifiBoard {
private:
    Button boot_button_;
    OttoEyeDisplay* display_;
    bool is_hacked_ = false;  // Track hacked mode state
    esp_timer_handle_t hacked_timer_ = nullptr;  // Timer auto-exit hacked mode

    void InitializeI2c(i2c_master_bus_handle_t* bus_handle) {
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = DISPLAY_I2C_SDA_PIN,
            .scl_io_num = DISPLAY_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, bus_handle));
        ESP_LOGI(TAG, "I2C bus initialized");
    }

    i2c_master_bus_handle_t i2c_bus_ = nullptr;
    OttoBody* body_ = nullptr;

    void InitializeOledDisplay() {
        InitializeI2c(&i2c_bus_); // Store globally

        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        // Initialize panel IO for SH1106
        ESP_LOGD(TAG, "Install panel IO for SH1106");
        esp_lcd_panel_io_i2c_config_t io_config = {
            .dev_addr = DISPLAY_I2C_ADDR,
            .control_phase_bytes = 1,
            .dc_bit_offset = 6,
            .lcd_cmd_bits = 8,
            .lcd_param_bits = 8,
            .flags = {
                .dc_low_on_data = 0,
                .disable_control_phase = 0,
            },
            .scl_speed_hz = 400000,
        };
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus_, &io_config, &panel_io));

        // Initialize SH1106 panel
        ESP_LOGD(TAG, "Install SH1106 panel driver");
        esp_lcd_panel_dev_config_t panel_config = {
            .reset_gpio_num = GPIO_NUM_NC,
            .bits_per_pixel = 1,
            .flags = {
                .reset_active_high = 0,
            },
            .vendor_config = nullptr,
        };
        ESP_ERROR_CHECK(esp_lcd_new_panel_sh1106(panel_io, &panel_config, &panel));

        esp_lcd_panel_reset(panel);
        esp_lcd_panel_init(panel);
        esp_lcd_panel_disp_on_off(panel, true);

        // Initialize OttoBody on the same bus with dual LED system
        body_ = new OttoBody(i2c_bus_, PCA9685_I2C_ADDR, OTTO_BODY_LED_GPIO, OTTO_CHEST_LED_GPIO, OTTO_BODY_LED_COUNT);
        if (body_->Init()) {
            ESP_LOGI(TAG, "OttoBody initialized");
        } else {
            ESP_LOGE(TAG, "OttoBody init failed");
        }

        // Create Otto Eye Display with Body reference
        display_ = new OttoEyeDisplay(panel_io, panel, DISPLAY_WIDTH, DISPLAY_HEIGHT, 
                                      DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, body_);
        
        ESP_LOGI(TAG, "SH1106 OLED display initialized with Otto eyes");
    }

    void InitializeButtons() {
        // Single click: Toggle chat state
        boot_button_.OnClick([this]() {
            // Khi đang hacked mode, bỏ qua single-click để không cài thiện được trung đạo
            if (is_hacked_) return;
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });

        // Double-click: Enter WiFi config mode
        boot_button_.OnMultipleClick([this]() {
            ESP_LOGI(TAG, "Double-click detected - entering WiFi config mode");
            EnterWifiConfigMode();
        }, 2);

        // Triple-click: Enter Hacked mode (auto-exit after 10 seconds)
        boot_button_.OnMultipleClick([this]() {
            ESP_LOGI(TAG, "Triple-click detected - ENTERING HACKED MODE (10s)");
            is_hacked_ = true;
            display_->SetStatus("HACKED");

            // Cancel previous timer if any
            if (hacked_timer_ != nullptr) {
                esp_timer_stop(hacked_timer_);
                esp_timer_delete(hacked_timer_);
                hacked_timer_ = nullptr;
            }

            // Start 10-second auto-exit timer
            // IMPORTANT: timer callback chạy từ ISR context, phải dùng
            // app.Schedule() để chạy trên main thread an toàn
            esp_timer_create_args_t timer_args = {};
            timer_args.callback = [](void* arg) {
                MachTimOpswat* self = (MachTimOpswat*)arg;
                self->hacked_timer_ = nullptr;
                // Schedule exit về main thread để tránh deadlock với LVGL lock
                auto& app = Application::GetInstance();
                app.Schedule([self]() {
                    self->is_hacked_ = false;
                    ESP_LOGI(TAG, "Hacked mode expired (10s) - sending HACKED_EXIT");
                    // Dùng "HACKED_EXIT" để OttoEyeDisplay biết đây là lệnh thoát có chủ đích
                    self->display_->SetStatus("HACKED_EXIT");
                });
            };
            timer_args.arg = this;
            timer_args.name = "hacked_exit";
            esp_timer_create(&timer_args, &hacked_timer_);
            esp_timer_start_once(hacked_timer_, 10 * 1000 * 1000);  // 10 seconds
        }, 3);

        ESP_LOGI(TAG, "Buttons initialized (1-click: chat, 2-click: WiFi, 3-click: hacked 10s)");
    }

    // IoT initialization - add AI-visible devices
    void InitializeTools() {
        static LampController lamp(LAMP_GPIO);

        // Robot gesture control via MCP tool
        auto& mcp_server = McpServer::GetInstance();
        OttoBody* body = body_;  // capture raw pointer for lambda

        mcp_server.AddTool("robot.gesture",
            "Điều khiển cử chỉ cơ thể robot Otto. Gọi tool này khi người dùng yêu cầu robot thực hiện cử động (bằng tiếng Việt hoặc tiếng Anh).\n"
            "Tham số 'gesture' là một trong các giá trị sau:\n"
            "- arm_left_raise  : giơ tay trái / raise left arm / left arm up\n"
            "- arm_right_raise : giơ tay phải / raise right arm / right arm up\n"
            "- greeting_wave   : vẫy tay chào / wave hello / wave hand\n"
            "- neck_turn_left  : quay cổ sang trái / turn left / look left\n"
            "- neck_turn_right : quay cổ sang phải / turn right / look right\n"
            "- neck_shake      : lắc đầu / shake head / no\n"
            "- head_bow        : cúi đầu / bow / look down\n"
            "- head_lookup     : ngẩng đầu / look up\n"
            "- head_nod        : gật đầu / nod / yes",
            PropertyList({
                Property("gesture", kPropertyTypeString)
            }),
            [body](const PropertyList& properties) -> ReturnValue {
                if (body == nullptr) return false;
                auto gesture_str = properties["gesture"].value<std::string>();

                struct GestureEntry {
                    const char* name;
                    MovementPattern pattern;
                    int duration_ms;
                };
                static const GestureEntry gesture_table[] = {
                    { "arm_left_raise",  MovementPattern::ARM_LEFT_RAISE,  2500 },
                    { "arm_right_raise", MovementPattern::ARM_RIGHT_RAISE, 2500 },
                    { "greeting_wave",   MovementPattern::GREETING_WAVE,   2500 },
                    { "neck_turn_left",  MovementPattern::NECK_TURN_LEFT,  2000 },
                    { "neck_turn_right", MovementPattern::NECK_TURN_RIGHT, 2000 },
                    { "neck_shake",      MovementPattern::NECK_SHAKE,      1500 },
                    { "head_bow",        MovementPattern::HEAD_BOW,        2000 },
                    { "head_lookup",     MovementPattern::HEAD_LOOKUP,     2000 },
                    { "head_nod",        MovementPattern::HEAD_NOD,        1500 },
                };
                for (const auto& entry : gesture_table) {
                    if (gesture_str == entry.name) {
                        ESP_LOGI(TAG, "Robot gesture: %s (%d ms)", entry.name, entry.duration_ms);
                        body->PlayGesture(entry.pattern, entry.duration_ms);
                        return true;
                    }
                }
                ESP_LOGW(TAG, "Unknown gesture: %s", gesture_str.c_str());
                return false;
            });
    }

    virtual void StartNetwork() override {
        WifiBoard::StartNetwork();
        // Lower WiFi power to avoid Brownout (Level 0 BOD is 2.5V, but current spikes can still be high)
        // Range 8-84, unit 0.25dBm. 52 * 0.25 = 13dBm. Default is often 20dBm.
        esp_wifi_set_max_tx_power(52); 
    }

public:
    MachTimOpswat() :
        boot_button_(BOOT_BUTTON_GPIO) {
        InitializeOledDisplay();
        InitializeButtons();
        InitializeTools();
    }

    virtual Led* GetLed() override {
        if (BUILTIN_LED_GPIO == GPIO_NUM_NC) {
            static NoLed led;
            return &led;
        }
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
#ifdef AUDIO_I2S_METHOD_SIMPLEX
        static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT, 
            AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
#else
        static NoAudioCodecDuplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN);
#endif
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Backlight* GetBacklight() override {
        // SH1106 OLED doesn't have backlight
        return nullptr;
    }
};

DECLARE_BOARD(MachTimOpswat);
