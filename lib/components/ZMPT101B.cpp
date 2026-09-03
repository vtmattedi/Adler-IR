#include "ZMPT101B.h"

/// @brief ZMPT101B constructor
/// @param pin analog pin that ZMPT101B connected to.
/// @param frequency AC system frequency
ZMPT101B::ZMPT101B(uint8_t pin, uint16_t frequency)
{
	this->pin = pin;
	period = 1000000 / frequency;
	initialized = true;
	i2sEnabled = false;
	pinMode(pin, INPUT);
}

bool ZMPT101B::init()
{
	initialized = true;
	i2sEnabled = false;
	return false;
}

/// @brief Set sensitivity
/// @param value Sensitivity value
void ZMPT101B::setSensitivity(float value)
{
	sensitivity = value;
}

uint32_t ZMPT101B::readSamplesForPeriod(uint32_t *sum, uint32_t *absSum, int32_t zeroPoint, bool subtractZero)
{
	if (sum == nullptr)
	{
		return 0;
	}

	*sum = 0;
	if (absSum != nullptr)
	{
		*absSum = 0;
	}

	uint32_t measurements_count = 0;
	const uint32_t t_start = micros();

	while (micros() - t_start < period)
	{
		const int32_t raw = analogRead(pin);
		*sum += static_cast<uint32_t>(raw);
		if (absSum != nullptr)
		{
			int32_t delta = raw;
			if (subtractZero)
			{
				delta -= zeroPoint;
			}
			*absSum += static_cast<uint32_t>(_abs(delta));
		}
		measurements_count++;
	}

	return measurements_count;
}

/// @brief Calculate zero point
/// @return zero / center value
int ZMPT101B::getZeroPoint()
{
	uint32_t Vsum = 0;
	const uint32_t measurements_count = readSamplesForPeriod(&Vsum, nullptr);
	if (measurements_count == 0)
	{
		return 0;
	}

	return static_cast<int>(Vsum / measurements_count);
}

/// @brief Calculate root mean square (RMS) of AC valtage
/// @param loopCount Loop count to calculate
/// @return root mean square (RMS) of AC valtage
float ZMPT101B::getRmsVoltage(uint8_t loopCount)
{
	double readingVoltage = 0.0f;

	for (uint8_t i = 0; i < loopCount; i++)
	{
		int zeroPoint = this->getZeroPoint();
		uint32_t Vsum = 0;
		uint32_t absSum = 0;
		const uint32_t measurements_count = readSamplesForPeriod(&Vsum, &absSum, zeroPoint, true);
		if (measurements_count == 0)
		{
			continue;
		}

		readingVoltage += (absSum / static_cast<double>(measurements_count)) / ADC_SCALE * VREF * sensitivity;
	}

	if (loopCount == 0)
	{
		return 0.0f;
	}

	return static_cast<float>(readingVoltage / loopCount);
}
