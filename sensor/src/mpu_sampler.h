#pragma once

#include <Arduino.h>
#include <Wire.h>

#include "MPU6050.h"

class RunningMedian;

// Simple MPU6050 sampler that collects N tilt samples and one temperature sample.
//
// Usage:
//  MpuSampler mpu(MAX_SAMPLES);
//  Wire.begin(SDA_PIN, SCL_PIN);
//  mpu.begin(Wire);
//  while (mpu.samplesLeft() > 0) {
//    mpu.sample();
//    delay(10);
//  }
//  float tilt = mpu.filteredTiltDeg();
//  float temp = mpu.tempC();
class MpuSampler {
public:
	explicit MpuSampler(uint8_t sampleCount);

	// Assumes the bus has already been started by the caller (e.g. Wire.begin()).
	void begin(TwoWire& wire);
	void reset();

	// Returns true if a sample was consumed (i.e., we recorded a tilt reading).
	bool sample();

	// Exposes the MPU "data ready" status bit so callers can replicate the old
	// sampling loop pattern (check ready + sample count).
	bool dataReady() const;

	uint8_t samplesLeft() const;
	bool isComplete() const;

	// New state-style API:
	// - pending(): we're still trying to collect samples
	// - ready():   we've collected the full window and have derived values available
	bool pending() const;
	bool ready() const;

	// Median-filtered tilt in degrees for the collected sample window.
	float filteredTiltDeg() const;

	// Sampled temperature in degrees C (taken when sampling completes).
	float tempC() const { return tempC_; }

	// Put the sensor into low-power sleep mode.
	void sleep();

private:
	static float calculateTiltDeg_(float ax, float az, float ay);

	RunningMedian* runningMedian_ = nullptr;
	TwoWire* wire_ = nullptr;
	MPU6050* mpu_ = nullptr;

	float tempC_ = NAN;

	bool initialized_ = false;
};
