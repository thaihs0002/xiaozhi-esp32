#include "otto_movements.h"
#include <algorithm>
#include <cstdlib> 
#include <cmath>   
#include "freertos/idf_additions.h"

static const char* TAG = "OttoMovements";
#define HAND_HOME_POSITION 45
#define HEAD_HOME_POSITION 90 

Otto::Otto() {
    is_otto_resting_ = false;
    has_hands_ = false;
    for (int i = 0; i < SERVO_COUNT; i++) {
        servo_pins_[i] = -1;
        servo_trim_[i] = 0;
    }
}

Otto::~Otto() { DetachServos(); }

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
    is_otto_resting_ = false;
}

void Otto::AttachServos() {
    for (int i = 0; i < SERVO_COUNT; i++) {
        if (servo_pins_[i] != -1) servo_[i].Attach(servo_pins_[i]);
    }
}

void Otto::DetachServos() {
    for (int i = 0; i < SERVO_COUNT; i++) {
        if (servo_pins_[i] != -1) servo_[i].Detach();
    }
}

void Otto::SetTrims(int left_leg, int right_leg, int left_foot, int right_foot, int left_hand, int right_hand) {
    servo_trim_[LEFT_LEG] = left_leg;
    servo_trim_[RIGHT_LEG] = right_leg;
    servo_trim_[LEFT_FOOT] = left_foot;
    servo_trim_[RIGHT_FOOT] = right_foot;
    if (has_hands_) {
        servo_trim_[LEFT_HAND] = left_hand;
        servo_trim_[RIGHT_HAND] = right_hand;
    }
    for (int i = 0; i < SERVO_COUNT; i++) {
        if (servo_pins_[i] != -1) servo_[i].SetTrim(servo_trim_[i]);
    }
}

// --- THUẬT TOÁN LÀM MƯỢT ---
float Otto::EaseInOutCosine(float x) {
    return -(cos(M_PI * x) - 1) / 2;
}

// --- HÀM DI CHUYỂN SMOOTH ---
void Otto::MoveServos(int time_ms, int servo_target[]) {
    if (GetRestState()) SetRestState(false);
    
    if (time_ms <= 10) {
        for (int i = 0; i < SERVO_COUNT; i++) {
            if (servo_pins_[i] != -1) servo_[i].SetPosition(servo_target[i]);
        }
        vTaskDelay(pdMS_TO_TICKS(time_ms));
        return;
    }

    int start_pos[SERVO_COUNT];
    for (int i = 0; i < SERVO_COUNT; i++) {
        if (servo_pins_[i] != -1) start_pos[i] = servo_[i].GetPosition();
    }

    unsigned long start_time = millis();
    unsigned long current_time = start_time;
    
    while ((current_time - start_time) < time_ms) {
        float progress = (float)(current_time - start_time) / time_ms;
        float ease_factor = EaseInOutCosine(progress);

        for (int i = 0; i < SERVO_COUNT; i++) {
            if (servo_pins_[i] != -1) {
                int new_pos = start_pos[i] + (servo_target[i] - start_pos[i]) * ease_factor;
                servo_[i].SetPosition(new_pos);
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
        current_time = millis();
    }

    for (int i = 0; i < SERVO_COUNT; i++) {
        if (servo_pins_[i] != -1) servo_[i].SetPosition(servo_target[i]);
    }
}

void Otto::MoveSingle(int position, int servo_number) {
    if (position > 180) position = 180;
    if (position < 0) position = 0;
    if (GetRestState()) SetRestState(false);
    if (servo_number >= 0 && servo_number < SERVO_COUNT && servo_pins_[servo_number] != -1) {
        servo_[servo_number].SetPosition(position);
    }
}

void Otto::OscillateServos(int amplitude[SERVO_COUNT], int offset[SERVO_COUNT], int period, double phase_diff[SERVO_COUNT], float cycle) {
    for (int i = 0; i < SERVO_COUNT; i++) {
        if (servo_pins_[i] != -1) {
            servo_[i].SetO(offset[i]);
            servo_[i].SetA(amplitude[i]);
            servo_[i].SetT(period);
            servo_[i].SetPh(phase_diff[i]);
        }
    }
    double ref = millis();
    double end_time = period * cycle + ref;
    while (millis() < end_time) {
        for (int i = 0; i < SERVO_COUNT; i++) {
            if (servo_pins_[i] != -1) servo_[i].Refresh();
        }
        vTaskDelay(5);
    }
}

void Otto::Execute2(int amplitude[SERVO_COUNT], int center_angle[SERVO_COUNT], int period, double phase_diff[SERVO_COUNT], float steps) {
    if (GetRestState()) SetRestState(false);
    int offset[SERVO_COUNT];
    for (int i = 0; i < SERVO_COUNT; i++) offset[i] = center_angle[i] - 90;
    
    int cycles = (int)steps;
    if (cycles >= 1) {
        for (int i = 0; i < cycles; i++) OscillateServos(amplitude, offset, period, phase_diff, 1.0);
    }
    OscillateServos(amplitude, offset, period, phase_diff, (float)steps - cycles);
}

void Otto::Home(bool hands_down) {
    if (is_otto_resting_) return;
    int homes[SERVO_COUNT];
    for (int i = 0; i < SERVO_COUNT; i++) {
        if (i == LEFT_HAND) homes[i] = hands_down ? HAND_HOME_POSITION : servo_[i].GetPosition();
        else if (i == RIGHT_HAND) homes[i] = hands_down ? (180 - HAND_HOME_POSITION) : servo_[i].GetPosition();
        else homes[i] = HEAD_HOME_POSITION;
    }
    MoveServos(1000, homes);
    is_otto_resting_ = true;
}

// --- CÁC HÀM CỬ ĐỘNG ĐẦU MỚI ---
void Otto::HeadBob(int speed, int intensity) {
    int target[SERVO_COUNT];
    target[LEFT_HAND] = servo_[LEFT_HAND].GetPosition();
    target[RIGHT_HAND] = servo_[RIGHT_HAND].GetPosition();
    target[LEFT_LEG] = HEAD_HOME_POSITION;
    target[LEFT_FOOT] = 90; target[RIGHT_FOOT] = 90;

    target[RIGHT_LEG] = HEAD_HOME_POSITION + intensity;
    MoveServos(speed/2, target);
    
    target[RIGHT_LEG] = HEAD_HOME_POSITION - intensity;
    MoveServos(speed/2, target);
}

void Otto::HeadTurn(int speed, int intensity) {
    int target[SERVO_COUNT];
    target[LEFT_HAND] = servo_[LEFT_HAND].GetPosition();
    target[RIGHT_HAND] = servo_[RIGHT_HAND].GetPosition();
    target[RIGHT_LEG] = HEAD_HOME_POSITION;
    target[LEFT_FOOT] = 90; target[RIGHT_FOOT] = 90;

    target[LEFT_LEG] = HEAD_HOME_POSITION + (rand() % 2 == 0 ? intensity : -intensity);
    MoveServos(speed, target);
}

void Otto::HandWave(int dir) {
    if (!has_hands_) return;
    int center[SERVO_COUNT] = {90, 90, 90, 90, 160, 20};
    int A[SERVO_COUNT] = {0, 0, 0, 0, 0, 0};
    double phase[SERVO_COUNT] = {0, 0, 0, 0, 0, 0};
    
    if (dir == LEFT) { A[4] = 25; phase[4] = M_PI/2; center[4] = 150; }
    else { A[5] = 25; phase[5] = M_PI/2; center[5] = 30; }
    
    Execute2(A, center, 400, phase, 3);
}

void Otto::HandsUp(int period, int dir) {
    if (!has_hands_) return;
    int target[SERVO_COUNT] = {90, 90, 90, 90, 170, 10};
    MoveServos(period, target);
}

bool Otto::GetRestState() { return is_otto_resting_; }
void Otto::SetRestState(bool state) { is_otto_resting_ = state; }