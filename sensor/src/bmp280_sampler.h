#pragma once

#include <Arduino.h>

#if defined(TILTED_ENABLE_BMP280)

#include <Wire.h>
#include <Adafruit_BMP280.h>

// BMP280-only sampler: we only use BMP280 for auxiliary temperature here.
// We intentionally do NOT expose pressure to avoid confusing environmental
// pressure with keg/fermentor gauge pressure.

// Simple BMP280 sampler. Usage pattern mirrors other samplers in the project:
// - Call begin(TwoWire&)
// - Call start() when you want a new reading
// - While pending() call sample() periodically
// - When ready() returns true, use temperatureC()
class Bmp280Sampler {
public:
    // Optionally pass the I2C address (0x76 or 0x77 typical)
    explicit Bmp280Sampler(uint8_t i2cAddr = 0x76);

    void begin(TwoWire& wire);

    // Start a new read cycle
    void start();

    // Progress the state machine. Returns true if a sample was taken.
    bool sample();

    bool pending() const { return state_ == State::Reading; }
    bool ready() const { return state_ == State::Ready; }

    float temperatureC() const { return tempC_; }

    void sleep();

private:
    enum class State : uint8_t {
        Idle,
        Reading,
        Ready,
    };

    TwoWire* wire_ = nullptr;
    Adafruit_BMP280* sensor_ = nullptr;
    uint8_t addr_ = 0x76;
    State state_ = State::Idle;

    float tempC_ = NAN;
    // pressure is intentionally not stored/exposed
};

#endif // TILTED_ENABLE_BMP280
