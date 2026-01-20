#if defined(TILTED_ENABLE_BMP280)

#include "bmp280_sampler.h"

Bmp280Sampler::Bmp280Sampler(uint8_t i2cAddr) : addr_(i2cAddr) {}

void Bmp280Sampler::begin(TwoWire& wire)
{
    wire_ = &wire;

    if (sensor_ != nullptr) {
        delete sensor_;
        sensor_ = nullptr;
    }

    // Initialize BMP280 (no probing here). begin() may fail if the device is
    // not present at `addr_`.
    Adafruit_BMP280* bmp = new Adafruit_BMP280();
    if (bmp->begin(addr_)) {
        Serial.println("BMP280 init succeeded");
        sensor_ = bmp;
    } else {
        delete bmp;
        sensor_ = nullptr;
        Serial.println("BMP280 init failed");
    }
    state_ = State::Idle;
}

void Bmp280Sampler::start()
{
    // BMP280 read is immediate; set state to Reading and let sample() do the read.
    state_ = State::Reading;
}

bool Bmp280Sampler::sample()
{
    if (state_ != State::Reading || sensor_ == nullptr)
        return false;

    auto bmp = sensor_;
    if (bmp == nullptr)
        return false;
    float t = reinterpret_cast<Adafruit_BMP280*>(bmp)->readTemperature();
    // Intentionally do not read or expose pressure to avoid mixing
    // environmental pressure with fermentor gauge pressure.
    tempC_ = isnan(t) ? NAN : t;

    state_ = State::Ready;
    return true;
}

void Bmp280Sampler::sleep()
{
    // BMP has no standard sleep API in Adafruit library. Free the object.
    if (sensor_ != nullptr) {
        delete sensor_;
        sensor_ = nullptr;
    }
}

#endif // TILTED_ENABLE_BMP280
