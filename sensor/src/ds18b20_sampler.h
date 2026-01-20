#pragma once

#include <Arduino.h>

#if TILTED_ENABLE_DS18B20
#include <OneWire.h>
#include <DallasTemperature.h>

// Simple non-blocking DS18B20 sampler.
// - Call begin() once.
// - Call start() when you want a new reading.
// - While pending(), call sample() periodically.
// - When ready() becomes true, temperatureC() returns the last reading.
class Ds18b20Sampler {
public:
	explicit Ds18b20Sampler(OneWire& oneWire);

	void begin();

	// Begin a new conversion.
	void start();

	// Progress the state machine. Returns true if state changed.
	bool sample();

	bool pending() const { return state_ == State::Converting; }
	bool ready() const { return state_ == State::Ready; }

	float temperatureC() const { return tempC_; }

private:
	enum class State : uint8_t {
		Idle,
		Converting,
		Ready,
	};
	OneWire& oneWire_;
	// Own the DallasTemperature instance so the sampler manages the bus like
	// I2C samplers manage their sensor objects.
	DallasTemperature sensor_;
	State state_ = State::Idle;
	uint32_t startMs_ = 0;
	uint32_t conversionMs_ = 750; // worst-case at 12-bit
	float tempC_ = NAN;
};

#endif // TILTED_ENABLE_DS18B20
