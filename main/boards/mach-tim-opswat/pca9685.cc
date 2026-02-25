#include "pca9685.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_rom_sys.h>

#define TAG "PCA9685"

Pca9685::Pca9685(i2c_master_bus_handle_t bus_handle, uint8_t address) 
    : bus_handle_(bus_handle), dev_handle_(nullptr), address_(address) {}

bool Pca9685::Init() {
    // Add Device
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address_,
        .scl_speed_hz = 100000, // standard speed for better reliability
    };
    if (i2c_master_bus_add_device(bus_handle_, &dev_cfg, &dev_handle_) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add PCA9685 device");
        return false;
    }

    // Reset chip
    WriteByte(PCA9685_MODE1, 0x00);
    vTaskDelay(pdMS_TO_TICKS(10));
    
    // Verify communication: Read MODE1 - should be 0x01 (ALLCALL) or similar, NOT 0xFF
    uint8_t mode1 = ReadByte(PCA9685_MODE1);
    if (mode1 == 0xFF) { 
        // 0xFF means no device responding (pull-ups returning high)
        ESP_LOGE(TAG, "No PCA9685 found at 0x%02X. Check wiring!", address_);
        return false;
    }
    
    // Set Mode 2: Totem Pole output (0x04) for strong servo signals
    WriteByte(PCA9685_MODE2, 0x04);
    
    ESP_LOGI(TAG, "PCA9685 OK at 0x%02X. MODE1:0x%02X, MODE2:0x%02X", 
             address_, ReadByte(PCA9685_MODE1), ReadByte(PCA9685_MODE2));
    return true;
}

void Pca9685::SetPWMFreq(float freq_hz) {
    float prescaleval = 25000000;
    prescaleval /= 4096;
    prescaleval /= freq_hz;
    prescaleval -= 1;
    uint8_t prescale = floor(prescaleval + 0.5);

    uint8_t oldmode = ReadByte(PCA9685_MODE1);
    uint8_t newmode = (oldmode & 0x7F) | 0x10; // sleep
    WriteByte(PCA9685_MODE1, newmode); // go to sleep
    WriteByte(PCA9685_PRESCALE, prescale); // set prescale
    WriteByte(PCA9685_MODE1, oldmode);
    esp_rom_delay_us(5000);
    WriteByte(PCA9685_MODE1, oldmode | 0xA0);  
}

void Pca9685::SetPWM(uint8_t channel, uint16_t on, uint16_t off) {
    if (!dev_handle_) return;

    uint8_t buffer[5];
    buffer[0] = PCA9685_LED0_ON_L + 4 * channel;
    buffer[1] = on & 0xFF;
    buffer[2] = on >> 8;
    buffer[3] = off & 0xFF;
    buffer[4] = off >> 8;
    
    i2c_master_transmit(dev_handle_, buffer, 5, 50);
}

// Map 0-180 degrees to Pulse Width 
// Tuned for SG90: 0.5ms (102) - 2.4ms (491).
#define SERVO_MIN 102
#define SERVO_MAX 491

void Pca9685::SetAngle(uint8_t channel, float angle) {
    if (angle < 0) angle = 0;
    if (angle > 180) angle = 180;
    
    uint16_t off = (uint16_t)(SERVO_MIN + (angle / 180.0f) * (SERVO_MAX - SERVO_MIN));
    SetPWM(channel, 0, off);
}

void Pca9685::WriteByte(uint8_t reg, uint8_t data) {
    if (!dev_handle_) return;
    uint8_t buffer[2] = {reg, data};
    i2c_master_transmit(dev_handle_, buffer, 2, 50);
}

uint8_t Pca9685::ReadByte(uint8_t reg) {
    if (!dev_handle_) return 0;
    uint8_t data = 0;
    i2c_master_transmit_receive(dev_handle_, &reg, 1, &data, 1, 50);
    return data;
}
