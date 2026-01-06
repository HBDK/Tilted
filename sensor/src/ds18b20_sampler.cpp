#include "ds18b20_sampler.h"

#if TILTED_ENABLE_DS18B20

Ds18b20Sampler::Ds18b20Sampler(DallasTemperature& sensor) : sensor_(sensor) {}

void Ds18b20Sampler::begin()
{
	sensor_.begin();
	// Make requestTemperatures() return immediately.
	sensor_.setWaitForConversion(false);

	// Lower resolution to speed up conversions (and reduce blocking risk).
	// DS18B20 supports 9-12 bits; 10-bit gives 0.25°C steps and ~188ms typical conversion.
	constexpr uint8_t kResolutionBits = 10;
	sensor_.setResolution(kResolutionBits);

	// Determine conversion time based on configured resolution.
	// DallasTemperature returns 9-12; clamp for safety.
	uint8_t res = sensor_.getResolution();
	if (res < 9)
		res = 9;
	if (res > 12)
		res = 12;

	// Typical DS18B20 conversion times:
	// 9b: 94ms, 10b: 188ms, 11b: 375ms, 12b: 750ms
	switch (res)
	{
	case 9:
		conversionMs_ = 94;
		break;
	case 10:
		conversionMs_ = 188;
		break;
	case 11:
		conversionMs_ = 375;
		break;
	default:
		conversionMs_ = 750;
		break;
	}

	state_ = State::Idle;
	tempC_ = NAN;
}

void Ds18b20Sampler::start()
{
	// Kick off a new conversion.
	tempC_ = NAN;
	startMs_ = millis();
	state_ = State::Converting;
	(void)sensor_.requestTemperatures();
}

bool Ds18b20Sampler::sample()
{
	if (state_ != State::Converting)
		return false;

	const uint32_t now = millis();
	if ((now - startMs_) < conversionMs_)
		return false;

	const float t = sensor_.getTempCByIndex(0);
	if (t > -100.0f && t < 150.0f)
	{
		tempC_ = t;
	}
	state_ = State::Ready;
	return true;
}

#endif // TILTED_ENABLE_DS18B20
