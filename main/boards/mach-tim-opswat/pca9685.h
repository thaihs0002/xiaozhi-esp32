#ifndef _PCA9685_H_
#define _PCA9685_H_

#include <driver/i2c_master.h>
#include <math.h>

#define PCA9685_ADDR 0x40

// Registers
#define PCA9685_MODE1 0x00
#define PCA9685_MODE2 0x01
#define PCA9685_PRESCALE 0xFE
#define PCA9685_LED0_ON_L 0x06

class Pca9685 {
public:
    Pca9685(i2c_master_bus_handle_t bus_handle, uint8_t address = PCA9685_ADDR);
    bool Init();
    void SetPWMFreq(float freq_hz);
    void SetPWM(uint8_t channel, uint16_t on, uint16_t off);
    void SetAngle(uint8_t channel, float angle); // 0-180 mappings for typical servos
    
private:
    void WriteByte(uint8_t reg, uint8_t data);
    uint8_t ReadByte(uint8_t reg);

    i2c_master_bus_handle_t bus_handle_;
    i2c_master_dev_handle_t dev_handle_;
    uint8_t address_;
};

#endif // _PCA9685_H_
