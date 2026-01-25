#include "mpu_sampler.h"
#include <RunningMedian.h>

// The upstream MPU6050 library can be constructed with a specific TwoWire instance.
// We use dynamic allocation here so the sampler can switch buses at runtime and the
// object can be constructed after the bus is chosen.

MpuSampler::MpuSampler(uint8_t sampleCount) { reset(sampleCount); }

void MpuSampler::reset(uint8_t sampleCount)
{
	targetSamples_ = sampleCount;
	if (targetSamples_ > kMaxSamples)
		targetSamples_ = kMaxSamples;
	samplesTaken_ = 0;
	tempC_ = NAN;

	for (uint8_t i = 0; i < kMaxSamples; i++)	{
		samples_[i] = 0.0f;
	}
}

void MpuSampler::begin(TwoWire& wire)
{
	wire_ = &wire;

	if (mpu_ != nullptr)
	{
		delete mpu_;
		mpu_ = nullptr;
	}

	// This MPU6050 library supports selecting the I2C bus via the wireObj arg.
	mpu_ = new MPU6050(MPU6050_DEFAULT_ADDRESS, static_cast<void*>(wire_));

	mpu_->initialize();
	mpu_->setFullScaleAccelRange(MPU6050_ACCEL_FS_2);
	mpu_->setFullScaleGyroRange(MPU6050_GYRO_FS_250);
	mpu_->setDLPFMode(MPU6050_DLPF_BW_5);
	mpu_->setTempSensorEnabled(true);
	mpu_->setInterruptLatch(0); // pulse
	mpu_->setInterruptMode(1);  // Active Low
	mpu_->setInterruptDrive(1); // Open drain
	mpu_->setRate(17);
	// Data-ready interrupt is optional depending on wiring. We still gate sampling
	// on the status bit which the library reads over I2C.
	mpu_->setIntDataReadyEnabled(true);

	initialized_ = true;

	// Ensure we're awake.
	mpu_->setSleepEnabled(false);
}

void MpuSampler::sleep()
{
	if (mpu_ != nullptr)
		mpu_->setSleepEnabled(true);
}

bool MpuSampler::sample()
{
	if (!initialized_ || isComplete())
		return false;
	if (mpu_ == nullptr)
		return false;

	if (requireDataReady_)
	{
		if (!mpu_->getIntDataReadyStatus())
			return false;
	}
	else
	{
		// Throttle raw reads a bit so we don't hammer I2C in a tight loop.
		const uint32_t now = millis();
		if ((now - lastReadMs_) < 8)
			return false;
		lastReadMs_ = now;
	}

	int16_t ax, ay, az;
	mpu_->getAcceleration(&ax, &az, &ay);

	float tilt = calculateTiltDeg_(ax, az, ay);

	// Ignore zero readings as well as readings of precisely 90.
	// Both of these indicate failures to read correct data from the MPU.
	if (tilt > 0.0f && tilt != 90.0f)
	{
		samples_[samplesTaken_++] = tilt;

		if (isComplete())
		{
			// This offset is from the MPU documentation. Displays temperature in degrees C.
			tempC_ = mpu_->getTemperature() / 340.0f + 36.53f;
		}

		return true;
	}

	return false;
}

bool MpuSampler::dataReady() const
{
	if (!initialized_ || isComplete() || mpu_ == nullptr)
		return false;
	return mpu_->getIntDataReadyStatus();
}

float MpuSampler::filteredTiltDeg() const
{
	if (samplesTaken_ == 0)
		return NAN;

	// If we sampled fewer than requested (e.g., timeouts), median still works using the
	// first samplesTaken_ values. To keep this simple and detenministic, when we don't
	// have all samples we just return the latest value.
	if (samplesTaken_ < targetSamples_)
		return samples_[samplesTaken_ - 1];

	// Use RunningMedian library for a clear, tested median implementation.
	RunningMedian rm(targetSamples_);
	for (uint8_t i = 0; i < targetSamples_; i++) {
		rm.add(samples_[i]);
	}
	return rm.getMedian();
}

float MpuSampler::calculateTiltDeg_(float ax, float az, float ay)
{
	if (ax == 0 && ay == 0 && az == 0)
		return 0.0f;

	return acos(az / (sqrt(ax * ax + ay * ay + az * az))) * 180.0f / PI;
}
