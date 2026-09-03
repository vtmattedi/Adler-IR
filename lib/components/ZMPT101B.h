#ifndef ZMPT101B_h
#define ZMPT101B_h

#include <Arduino.h>

#define AC_VOLTAGE_FREQUENCY 60.0f
#define AC_VOLTAGE_SENSITIVITY 540.0f

#if defined(AVR)
	#define ADC_SCALE 1023.0f
	#define VREF 5.0f
#elif defined(ESP8266)
	#define ADC_SCALE 1023.0
	#define VREF 3.3
#elif defined(ESP32)
	#define ADC_SCALE 4095.0
	#define VREF 3.3
#endif

class ZMPT101B
{
public:
	ZMPT101B (uint8_t pin, uint16_t frequency = AC_VOLTAGE_FREQUENCY);
	bool     init();
	void     setSensitivity(float value);
	float 	 getRmsVoltage(uint8_t loopCount = 1);

private:
	uint8_t  pin;
	uint32_t period;
	float 	 sensitivity = AC_VOLTAGE_SENSITIVITY;
	int 	 getZeroPoint();
	uint32_t readSamplesForPeriod(uint32_t *sum, uint32_t *absSum, int32_t zeroPoint = 0, bool subtractZero = false);
	bool initialized = false;
	bool i2sEnabled = false;
};

#endif