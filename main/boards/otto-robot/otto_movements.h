#ifndef __OTTO_MOVEMENTS_H__
#define __OTTO_MOVEMENTS_H__

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "oscillator.h"

#define FORWARD 1
#define BACKWARD -1
#define LEFT 1
#define RIGHT -1
#define BOTH 0

#define LEFT_LEG 0
#define RIGHT_LEG 1
#define LEFT_FOOT 2
#define RIGHT_FOOT 3
#define LEFT_HAND 4
#define RIGHT_HAND 5
#define SERVO_COUNT 6

class Otto {
public:
    Otto();
    ~Otto();

    void Init(int left_leg, int right_leg, int left_foot, int right_foot, int left_hand = -1, int right_hand = -1);
    void AttachServos();
    void DetachServos();
    void SetTrims(int left_leg, int right_leg, int left_foot, int right_foot, int left_hand = 0, int right_hand = 0);
    
    // Các hàm quan trọng cho Chatbot
    void Home();                 
    void StartSpeakingMode();    
    void UpdateSpeakingMotion(); 
    
    bool GetRestState() { return is_otto_resting_; }
    void SetRestState(bool state) { is_otto_resting_ = state; }

private:
    void _moveServos(int time, int servo_target[]);
    
    Oscillator oscillators_[SERVO_COUNT];
    int servo_pins_[SERVO_COUNT];
    int servo_trim_[SERVO_COUNT];
    bool is_otto_resting_;
    bool has_hands_;
};

#endif
