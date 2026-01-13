#include "otto_movements.h"
#include <algorithm>
#include "freertos/idf_additions.h"
#include "oscillator.h"

static const char* TAG = "OttoMovements";

// Cấu hình biên độ dao động (Độ rộng góc quay)
#define AMP_HEAD_PAN   20  // Đầu quay trái phải 20 độ
#define AMP_HEAD_TILT  15  // Đầu gật 15 độ
#define AMP_HANDS      30  // Tay vung 30 độ

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
    servo_pins_[LEFT_LEG] = left_leg;     // Dùng cho ĐẦU QUAY (Yaw)
    servo_pins_[RIGHT_LEG] = right_leg;   // Dùng cho ĐẦU GẬT (Pitch)
    servo_pins_[LEFT_FOOT] = left_foot;   // (Không dùng)
    servo_pins_[RIGHT_FOOT] = right_foot; // (Không dùng)
    servo_pins_[LEFT_HAND] = left_hand;   // TAY TRÁI
    servo_pins_[RIGHT_HAND] = right_hand; // TAY PHẢI

    has_hands_ = (left_hand != -1 && right_hand != -1);
    AttachServos();
    is_otto_resting_ = false;
}

void Otto::AttachServos() {
    for (int i = 0; i < SERVO_COUNT; i++) {
        if (servo_pins_[i] != -1) {
            oscillators_[i].Attach(servo_pins_[i]);
            // Cài đặt tốc độ giới hạn để không bị giật
            oscillators_[i].EnableLimiter(50); 
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
        oscillators_[i].SetTrim(servo_trim_[i]);
    }
}

void Otto::Home() {
    if (is_otto_resting_) return;

    for (int i = 0; i < SERVO_COUNT; i++) {
        oscillators_[i].SetPosition(90); // Về giữa (90 độ)
        oscillators_[i].Stop();          // Dừng dao động
    }
    is_otto_resting_ = true;
}

// --- CHỨC NĂNG MỚI: CHUYỂN ĐỘNG TRÌNH BÀY MƯỢT MÀ ---
void Otto::StartSpeakingMode() {
    is_otto_resting_ = false;
    
    // 1. Đầu quay trái phải (Yaw) - Chậm rãi
    oscillators_[LEFT_LEG].SetO(0);         // Offset
    oscillators_[LEFT_LEG].SetA(AMP_HEAD_PAN); 
    oscillators_[LEFT_LEG].SetT(3000);      // Chu kỳ 3 giây (Rất mượt)
    oscillators_[LEFT_LEG].SetPh(0);
    oscillators_[LEFT_LEG].Play();

    // 2. Đầu gật nhẹ (Pitch)
    oscillators_[RIGHT_LEG].SetO(0);
    oscillators_[RIGHT_LEG].SetA(AMP_HEAD_TILT);
    oscillators_[RIGHT_LEG].SetT(2000);     // Chu kỳ 2 giây
    oscillators_[RIGHT_LEG].SetPh(M_PI/2);  // Lệch pha để không trùng nhịp quay
    oscillators_[RIGHT_LEG].Play();

    // 3. Tay trái vung nhẹ
    if (has_hands_) {
        oscillators_[LEFT_HAND].SetO(30);   // Nâng tay lên một chút
        oscillators_[LEFT_HAND].SetA(AMP_HANDS);
        oscillators_[LEFT_HAND].SetT(2500);
        oscillators_[LEFT_HAND].SetPh(0);
        oscillators_[LEFT_HAND].Play();

        // 4. Tay phải vung nhẹ (ngược chiều tay trái)
        oscillators_[RIGHT_HAND].SetO(30);
        oscillators_[RIGHT_HAND].SetA(AMP_HANDS);
        oscillators_[RIGHT_HAND].SetT(2500);
        oscillators_[RIGHT_HAND].SetPh(M_PI); // Ngược pha
        oscillators_[RIGHT_HAND].Play();
    }
}

void Otto::UpdateSpeakingMotion() {
    // Hàm này phải được gọi liên tục trong vòng lặp để cập nhật vị trí Servo theo hình sin
    for (int i = 0; i < SERVO_COUNT; i++) {
        if (servo_pins_[i] != -1) {
            oscillators_[i].Refresh();
        }
    }
}

// Các hàm cũ giữ nguyên (Move, Walk...) nếu cần, nhưng ta tập trung vào SpeakingMode
void Otto::_moveServos(int time, int servo_target[]) {
    // Giữ nguyên code cũ để tương thích
    AttachServos();
    if (GetRestState()) SetRestState(false);
    
    // Logic di chuyển thẳng servo (ít dùng trong mode Speaking này)
    // ... (Code cũ)
}

// ... Giữ lại các hàm Walk/Jump cũ nếu muốn ...
