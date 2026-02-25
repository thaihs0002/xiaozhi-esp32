//--------------------------------------------------------------
//-- Pca9685Oscillator: Sinusoidal oscillation for PCA9685 servos
//-- Adapted from Otto robot oscillator by Juan Gonzalez-Gomez
//--------------------------------------------------------------
#ifndef __PCA9685_OSCILLATOR_H__
#define __PCA9685_OSCILLATOR_H__

#include "pca9685.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef DEG2RAD
#define DEG2RAD(g) ((g) * M_PI / 180.0)
#endif

/**
 * @brief Sinusoidal oscillator for servo connected via PCA9685
 * 
 * Generates smooth, natural-looking movements using sine waves
 * with configurable amplitude, offset, period, and phase.
 */
class Pca9685Oscillator {
public:
    Pca9685Oscillator()
        : pca_(nullptr), channel_(0), amplitude_(30), offset_(0),
          period_(2000), phase0_(0), phase_(0), pos_(90.0f),
          stop_(false), trim_(0), current_velocity_(0), target_angle_(90.0f) {
        UpdateInternals();
    }

    /**
     * @brief Attach to a PCA9685 channel
     */
    void Attach(Pca9685* pca, uint8_t channel, int trim = 0) {
        pca_ = pca;
        channel_ = channel;
        trim_ = trim;
        pos_ = 90.0f;
    }

    // Oscillation parameters
    void SetAmplitude(int amplitude) { amplitude_ = amplitude; }
    void SetOffset(int offset) { offset_ = offset; }
    void SetPeriod(int period_ms) { period_ = period_ms; UpdateInternals(); }
    void SetPhase(double phase_rad) { phase0_ = phase_rad; }
    void SetTrim(int trim) { trim_ = trim; }

    int GetAmplitude() const { return amplitude_; }
    int GetOffset() const { return offset_; }
    int GetPeriod() const { return period_; }
    double GetPhase() const { return phase0_; }
    float GetPosition() const { return pos_; }

    // Control
    void Stop() { stop_ = true; }
    void Play() { stop_ = false; }
    void Reset() { phase_ = 0; }

    /**
     * @brief Set position directly (0-180 degrees)
     */
    void SetPosition(float angle) {
        pos_ = angle;
        WriteAngle(angle);
    }

    /**
     * @brief Call this at regular intervals (e.g., 30fps timer)
     * Calculates and outputs the next position in the oscillation
     */
    void Refresh() {
        if (stop_ || pca_ == nullptr) return;
        
        // Calculate sinusoidal position
        float oscillation = amplitude_ * sin(phase_ + phase0_);
        float target = 90.0f + offset_ + oscillation;
        
        pos_ = target;
        WriteAngle(target);
        
        phase_ += inc_;
        if (phase_ > 2 * M_PI) {
            phase_ -= 2 * M_PI;
        }
    }

    /**
     * @brief Smoothly interpolate position towards target
     * @param target Target angle (0-180)
     * @param speed Interpolation speed (0.0 - 1.0, higher = faster)
     */
    void InterpolateTo(float target, float speed = 0.1f) {
        if (pca_ == nullptr) return;
        
        float diff = target - pos_;
        if (fabs(diff) > 0.5f) {
            pos_ += diff * speed;
            WriteAngle(pos_);
        }
    }

    /**
     * @brief Smooth movement with acceleration/deceleration (digital servo style)
     * @param target Target angle (0-180)
     * @param max_speed Maximum speed factor (0.05 - 0.5, higher = faster)
     * @return true if target reached, false if still moving
     */
    bool SmoothMoveTo(float target, float max_speed = 0.15f) {
        if (pca_ == nullptr) return true;
        
        float diff = target - pos_;
        float distance = fabs(diff);
        
        // Already at target
        if (distance < 0.3f) {
            current_velocity_ = 0;
            return true;
        }
        
        // Calculate desired velocity with acceleration/deceleration
        float desired_velocity = 0;
        const float accel_distance = 15.0f; // degrees to accelerate/decelerate
        
        if (distance < accel_distance) {
            // Deceleration phase - slow down as we approach target
            desired_velocity = max_speed * (distance / accel_distance);
        } else {
            // Full speed
            desired_velocity = max_speed;
        }
        
        // Apply acceleration limit (smooth acceleration)
        const float accel_rate = 0.3f;
        if (current_velocity_ < desired_velocity) {
            current_velocity_ += accel_rate * max_speed;
            if (current_velocity_ > desired_velocity) {
                current_velocity_ = desired_velocity;
            }
        } else {
            current_velocity_ = desired_velocity;
        }
        
        // Move towards target
        float step = distance * current_velocity_;
        if (diff > 0) {
            pos_ += step;
            if (pos_ > target) pos_ = target;
        } else {
            pos_ -= step;
            if (pos_ < target) pos_ = target;
        }
        
        WriteAngle(pos_);
        return false;
    }
    
    /**
     * @brief Set target angle for smooth movement (call SmoothMoveTo in update loop)
     */
    void SetTarget(float target) {
        target_angle_ = target;
    }
    
    float GetTarget() const { return target_angle_; }

private:
    void UpdateInternals() {
        // Calculate phase increment for ~30fps update rate
        const int sample_period_ms = 33; // ~30fps
        num_samples_ = (float)period_ / sample_period_ms;
        inc_ = 2.0 * M_PI / num_samples_;
    }

    void WriteAngle(float angle) {
        if (pca_ == nullptr) return;
        // Clamp to valid range
        angle += trim_;
        if (angle < 0) angle = 0;
        if (angle > 180) angle = 180;
        pca_->SetAngle(channel_, angle);
    }

    Pca9685* pca_;
    uint8_t channel_;
    
    // Oscillation parameters
    int amplitude_;   // Amplitude in degrees
    int offset_;      // Offset from 90 degrees
    int period_;      // Period in milliseconds
    double phase0_;   // Initial phase in radians
    
    // Internal state
    double phase_;    // Current phase
    double inc_;      // Phase increment per sample
    float num_samples_;
    float pos_;       // Current position
    bool stop_;
    int trim_;        // Calibration trim
    
    // Smooth movement state
    float current_velocity_;  // Current movement velocity
    float target_angle_;      // Target angle for smooth movement
};

#endif // __PCA9685_OSCILLATOR_H__
