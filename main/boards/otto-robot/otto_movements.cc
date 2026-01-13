#include "otto_movements.h"
#include <cmath>
#include <algorithm>

static const char* TAG = "OttoMovements";

Otto::Otto() {
    is_otto_resting_ = false;
    has_hands_ = false;
    for (int i = 0; i < SERVO_COUNT; i++) {
        servo_pins_[i] = -1;
        servo_trim_[i] = 0;
    }
}

Otto::~Otto() {
    DetachServos();
}

unsigned long IRAM_ATTR millis() {
    return (unsigned long)(esp_timer_get_time() / 1000ULL);
}

void Otto::Init(int left_leg, int right_leg, int left_foot, int right_foot, int left_hand, int right_hand) {
    servo_pins_[LEFT_LEG] = left_leg;
    servo_pins_[RIGHT_LEG] = right_leg;
    servo_pins_[LEFT_FOOT] = left_foot;
    servo_pins_[RIGHT_FOOT] = right_foot;
    servo_pins_[LEFT_HAND] = left_hand;
    servo_pins_[RIGHT_HAND] = right_hand;

    has_hands_ = (left_hand != -1 && right_hand != -1);
    AttachServos();
    Home();
}

void Otto::AttachServos() {
    for (int i = 0; i < SERVO_COUNT; i++) {
        if (servo_pins_[i] != -1) {
            oscillators_[i].Attach(servo_pins_[i]);
            oscillators_[i].SetTrim(servo_trim_[i]);
            // SỬA LẠI: Dùng đúng tên hàm trong thư viện của bạn
            oscillators_[i].EnableLimiter(60); 
        }
    }
}

void Otto::DetachServos() {
    for (int i = 0; i < SERVO_COUNT; i++) {
        if (servo_pins_[i] != -1) {
            oscillators_[i].Detach();
        }
    }
}

void Otto::SetTrims(int left_leg, int right_leg, int left_foot, int right_foot, int left_hand, int right_hand) {
    servo_trim_[LEFT_LEG] = left_leg;
    servo_trim_[RIGHT_LEG] = right_leg;
    servo_trim_[LEFT_FOOT] = left_foot;
    servo_trim_[RIGHT_FOOT] = right_foot;
    servo_trim_[LEFT_HAND] = left_hand;
    servo_trim_[RIGHT_HAND] = right_hand;
    
    for (int i = 0; i < SERVO_COUNT; i++) {
        if (servo_pins_[i] != -1) {
            oscillators_[i].SetTrim(servo_trim_[i]);
        }
    }
}

void Otto::Home() {
    for (int i = 0; i < SERVO_COUNT; i++) {
        if (servo_pins_[i] != -1) {
            oscillators_[i].Stop();
            oscillators_[i].SetPosition(90);
        }
    }
    is_otto_resting_ = true;
}

void Otto::StartSpeakingMode() {
    is_otto_resting_ = false;
    
    // 1. Đầu quay trái phải (Yaw)
    oscillators_[LEFT_LEG].SetO(0);
    oscillators_[LEFT_LEG].SetA(20); 
    oscillators_[LEFT_LEG].SetT(3000); 
    oscillators_[LEFT_LEG].Play();

    // 2. Đầu gật nhẹ (Pitch)
    oscillators_[RIGHT_LEG].SetO(0);
    oscillators_[RIGHT_LEG].SetA(15);
    oscillators_[RIGHT_LEG].SetT(2000);
    oscillators_[RIGHT_LEG].SetPh(M_PI/2); 
    oscillators_[RIGHT_LEG].Play();

    // 3. Hai tay
    if (has_hands_) {
        oscillators_[LEFT_HAND].SetO(30); 
        oscillators_[LEFT_HAND].SetA(30);
        oscillators_[LEFT_HAND].SetT(2500);
        oscillators_[LEFT_HAND].Play();

        oscillators_[RIGHT_HAND].SetO(30);
        oscillators_[RIGHT_HAND].SetA(30);
        oscillators_[RIGHT_HAND].SetT(2500);
        oscillators_[RIGHT_HAND].SetPh(M_PI); 
        oscillators_[RIGHT_HAND].Play();
    }
}

void Otto::UpdateSpeakingMotion() {
    for (int i = 0; i < SERVO_COUNT; i++) {
        if (servo_pins_[i] != -1) {
            oscillators_[i].Refresh();
        }
    }
}

void Otto::_moveServos(int time, int servo_target[]) {
    for (int i = 0; i < SERVO_COUNT; i++) {
        if (servo_pins_[i] != -1) {
            oscillators_[i].SetPosition(servo_target[i]);
        }
    }
}
