#ifndef __OTTO_MOVEMENTS_H__
#define __OTTO_MOVEMENTS_H__

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "oscillator.h"
#include <cmath>

// --- ĐỊNH NGHĨA M_PI NẾU THIẾU ---
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

//-- Constants
#define FORWARD 1
#define BACKWARD -1
#define LEFT 1
#define RIGHT -1
#define BOTH 0
#define SMALL 5
#define MEDIUM 15
#define BIG 30

// -- Servo indexes
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

    //-- Core Motion Functions (Upgraded for Smoothness)
    void MoveServos(int time, int servo_target[]); 
    void MoveSingle(int position, int servo_number);
    void OscillateServos(int amplitude[SERVO_COUNT], int offset[SERVO_COUNT], int period, double phase_diff[SERVO_COUNT], float cycle);
    void Execute2(int amplitude[SERVO_COUNT], int center_angle[SERVO_COUNT], int period, double phase_diff[SERVO_COUNT], float steps);

    //-- High Level Actions
    void Home(bool hands_down = true);
    
    //-- Head Movements (Mapping Legs to Head)
    void HeadBob(int speed = 500, int intensity = 15); // Gật đầu nhẹ
    void HeadTurn(int speed = 600, int intensity = 20); // Nghiêng/Xoay đầu nhẹ

    //-- Hand Actions
    void HandWave(int dir = LEFT);
    void HandsUp(int period = 1000, int dir = 0);
    
    bool GetRestState();
    void SetRestState(bool state);

private:
    Oscillator servo_[SERVO_COUNT];
    int servo_pins_[SERVO_COUNT];
    int servo_trim_[SERVO_COUNT];
    bool is_otto_resting_;
    bool has_hands_;

    // Helper for smoothing
    float EaseInOutCosine(float time_percent);
};

#endif // __OTTO_MOVEMENTS_H__
