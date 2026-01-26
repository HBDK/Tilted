#include "mpu_sampler.h"
#include <RunningMedian.h>

// The upstream MPU6050 library can be constructed with a specific TwoWire instance.
// We use dynamic allocation here so the sampler can switch buses at runtime and the
// object can be constructed after the bus is chosen.

MpuSampler::MpuSampler(uint8_t sampleCount) { 
	runningMedian_ = new RunningMedian(sampleCount);
	reset(); 
}

void MpuSampler::reset()
{
	runningMedian_->clear();
	tempC_ = NAN;
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
	if (runningMedian_ == nullptr)
		return false;
	if (!mpu_->getIntDataReadyStatus())
		return false;

	int16_t ax, ay, az;
	mpu_->getAcceleration(&ax, &az, &ay);

	float tilt = calculateTiltDeg_(ax, az, ay);

	// Ignore zero readings as well as readings of precisely 90.
	// Both of these indicate failures to read correct data from the MPU.
	if (tilt > 0.0f && tilt != 90.0f)
	{
		runningMedian_->add(tilt);

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
	if (!initialized_ || isComplete() || mpu_ == nullptr || runningMedian_ == nullptr)
		return false;
	return mpu_->getIntDataReadyStatus();
}

float MpuSampler::filteredTiltDeg() const
{
	if (runningMedian_ == nullptr || runningMedian_->getCount() == 0)
		return NAN;

	return runningMedian_->getMedian();
}

uint8_t MpuSampler::samplesLeft() const
{
	if (runningMedian_ == nullptr)
		return 0;
	const uint8_t cnt = static_cast<uint8_t>(runningMedian_->getCount());
	const uint8_t size = static_cast<uint8_t>(runningMedian_->getSize());
	return (size > cnt) ? (size - cnt) : 0;
}

bool MpuSampler::isComplete() const
{
	if (runningMedian_ == nullptr)
		return false;
	return runningMedian_->getCount() >= runningMedian_->getSize();
}

bool MpuSampler::pending() const
{
	if (runningMedian_ == nullptr)
		return false;
	return initialized_ && (runningMedian_->getSize() > 0) && !isComplete();
}

bool MpuSampler::ready() const
{
	return !pending();
}

float MpuSampler::calculateTiltDeg_(float ax, float az, float ay)
{
	if (ax == 0 && ay == 0 && az == 0)
		return 0.0f;

	return acos(az / (sqrt(ax * ax + ay * ay + az * az))) * 180.0f / PI;
}
