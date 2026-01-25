#include <ESP8266WiFi.h>
#include <espnow.h>
#include <Wire.h>

// Optional DS18B20 support
// Enable by defining: -DTILTED_ENABLE_DS18B20=1 (see sensor/platformio.ini)
#ifndef TILTED_ENABLE_DS18B20
#define TILTED_ENABLE_DS18B20 0
#endif

#if TILTED_ENABLE_DS18B20
#include <OneWire.h>
#include <DallasTemperature.h>
#endif

#include "mpu_sampler.h"

#if TILTED_ENABLE_DS18B20
#include "ds18b20_sampler.h"
#endif
#if defined(TILTED_ENABLE_BMP280)
#include "bmp280_sampler.h"
#endif

#include "tilted_protocol.h"
#include "tilted_sensor_id.h"
#include "tilted_packet_builder.h"
#include "tilted_value_helper.h"
#include "tilted_filters.h"

// Set ADC mode for voltage reading.
ADC_MODE(ADC_VCC);

// Maximum time to be awake, in ms. This is needed in case the MPU sensor
// fails to return any samples.
#define WAKE_TIMEOUT 10000

// I2C pins
#define SDA_PIN 4
#define SCL_PIN 5

// DS18B20 (1-Wire) pin (free GPIO on ESP-12E).
// Wiring reminder: DS18B20 data needs a 4.7k pull-up to 3.3V.
// Optional: only used when TILTED_ENABLE_DS18B20=1
#define ONE_WIRE_PIN 14 // GPIO14 / D5

// number of tilt samples to average
#define MAX_SAMPLES 5

// Normal interval should be long enough to stretch out battery life. Since
// we're using the MPU temp sensor, we're probably going to see slower
// response times so longer intervals aren't a terrible idea.
#define NORMAL_INTERVAL 980

// In calibration mode, we need more frequent updates.
// Here we define the RTC address to use and the number of iterations.
// 60 iterations with an interval of 30 equals 30 minutes.
#define CALIBRATION_INTERVAL 30
#define RTC_ADDRESS 0
#define CALIBRATION_ITERATIONS 60
#define CALIBRATION_TILT_ANGLE_MIN 170
#define CALIBRATION_TILT_ANGLE_MAX 180
#define CALIBRATION_SETUP_TIME 30000
#define WIFI_TIMEOUT 10000

// Version identifier (kept for build info).
const char versionTimestamp[] = "TiltedSensor " __DATE__ " " __TIME__;

// when we booted
static unsigned long bootTime, wifiTime, sent, calibrationSetupStart, calibrationWifiStart = 0;

uint32_t calibrationIterations = 0;

// Sensor state variables
enum SensorState {
    STATE_INIT,
    STATE_SAMPLING,
    STATE_PROCESSING,
    STATE_TRANSMITTING,
    STATE_SLEEPING
};

SensorState currentState = STATE_INIT;

RF_PRE_INIT()
{
	bootTime = millis();
}

//------------------------------------------------------------
static long sleep_interval = NORMAL_INTERVAL;

static MpuSampler mpuSampler(MAX_SAMPLES);

#if TILTED_ENABLE_DS18B20
static OneWire oneWire(ONE_WIRE_PIN);
static Ds18b20Sampler ds18b20Sampler(oneWire);
#endif

#if defined(TILTED_ENABLE_BMP280)
static Bmp280Sampler bmp280Sampler;
#endif

//------------------------------------------------------------
static const int led = LED_BUILTIN;

static inline void ledOn()
{
	digitalWrite(led, LOW);
}
static inline void ledOff()
{
	digitalWrite(led, HIGH);
}


static void actuallySleep()
{
    // Put MPU to sleep if not already done
    mpuSampler.sleep();
    Serial.println("MPU put to sleep");
    // Put BMP280 to sleep if present
#if defined(TILTED_ENABLE_BMP280)
    bmp280Sampler.sleep();
#endif
    
    // Turn off WiFi completely to save power
    WiFi.mode(WIFI_OFF);
    WiFi.forceSleepBegin();
    delay(1); // Give WiFi time to shut down

    double uptime = (millis() - bootTime) / 1000.;

    Serial.printf("bootTime: %ld WifiTime: %ld\n", bootTime, wifiTime);
    Serial.printf("Deep sleeping %ld seconds after %.3g awake\n", sleep_interval, uptime);

    ESP.deepSleepInstant(sleep_interval * 1000000, WAKE_NO_RFCAL);
}

//-----------------------------------------------------------------
static int voltage = 0;

static inline int readVoltage()
{
    // Read voltage multiple times and average for better accuracy
    const int readings = 3;
    int sum = 0;
    for (int i = 0; i < readings; i++) {
        sum += ESP.getVcc();
        delay(5);
    }
    return (voltage = sum / readings);
}

// Perform a non-invasive I2C bus scan and print addresses. This is run only
// on a full power-on (not deep-sleep wake) to help with diagnostics.
static void doI2CScan(TwoWire& wire)
{
    Serial.println("I2C scan starting");
    for (uint8_t addr = 1; addr < 127; addr++) {
        wire.beginTransmission(addr);
        uint8_t err = wire.endTransmission();
        if (err == 0) {
            Serial.print("Found I2C device at 0x");
            if (addr < 16) Serial.print('0');
            Serial.println(addr, HEX);
        }
        delay(1);
    }
    Serial.println("Scan complete");
}

// TLV item capacity depends on optional sensors.
// Base fields: tilt, temp, battery, interval
#if TILTED_ENABLE_DS18B20
    #if defined(TILTED_ENABLE_BMP280)
        static constexpr uint8_t TILTED_ITEM_CAPACITY = 6; // DS18B20 + BMP (two aux temps)
    #else
        static constexpr uint8_t TILTED_ITEM_CAPACITY = 5; // DS18B20 only
    #endif
#else
    #if defined(TILTED_ENABLE_BMP280)
        static constexpr uint8_t TILTED_ITEM_CAPACITY = 5; // BMP aux temp
    #else
        static constexpr uint8_t TILTED_ITEM_CAPACITY = 4; // base
    #endif
#endif

static void sendSensorData()
{
    Serial.println("Processing and sending data...");

    // Median-filtered tilt over our sample window.
    float filteredValue = mpuSampler.filteredTiltDeg();

    // --- Build TLV readings packet (dynamic fields) ---
    // Items we currently include:
    //  - tilt (0.1 deg)
    //  - temperature (0.1 C)
    //  - battery (mV)
    //  - interval (seconds)
    TiltedValueItem items[TILTED_ITEM_CAPACITY];
    uint8_t itemCount = 0;

    items[itemCount++] = TiltedValueHelper::tiltDeg(filteredValue);
    items[itemCount++] = TiltedValueHelper::tempC(mpuSampler.tempC());

    // Optional DS18B20 aux temperature.
#if TILTED_ENABLE_DS18B20
    float auxTemperature = ds18b20Sampler.temperatureC();
    if (isfinite(auxTemperature))
    {
        items[itemCount++] = TiltedValueHelper::auxTempC(auxTemperature);
    }
#endif
    // Optional BMP280 auxiliary temperature only (no environmental pressure)
#if defined(TILTED_ENABLE_BMP280)
    float temperature = bmp280Sampler.temperatureC();
    if (isfinite(temperature))
    {
        items[itemCount++] = TiltedValueHelper::auxTempC(temperature);
    }
#endif
    items[itemCount++] = TiltedValueHelper::batteryMv(voltage);
    items[itemCount++] = TiltedValueHelper::intervalS(sleep_interval);

    char name[TILTED_MAX_NAME_LEN + 1];
    uint8_t nameLen = tilted_build_name_from_type(name, sizeof(name), "tilt");
    if (nameLen > TILTED_MAX_NAME_LEN)
        nameLen = TILTED_MAX_NAME_LEN;

    uint16_t pktLen = tilted_readings_packet_size(nameLen, itemCount);
    if (pktLen == 0)
    {
        Serial.println("TLV packet sizing failed; not sending");
        actuallySleep();
        return;
    }

    // Initialize WiFi in STA mode
    WiFi.forceSleepWake();
    delay(1);
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    unsigned long espnow_start = millis();
    unsigned long timeout = WAKE_TIMEOUT / 2;  // Shorter timeout for ESP-NOW
    
    bool init_success = false;
    while ((millis() - espnow_start) < timeout) {
        if (esp_now_init() == 0) {
            init_success = true;
            break;
        }
        delay(10);
    }
    
    if (!init_success) {
        Serial.println("ESP-NOW init failed, sleeping without sending data");
        actuallySleep();
        return;
    }

    esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
    esp_now_add_peer((uint8_t*)TILTED_GATEWAY_MAC, ESP_NOW_ROLE_SLAVE, TILTED_ESPNOW_CHANNEL, NULL, 0);

    wifiTime = millis();

    // Build packet in a stack buffer.
    // Keep this small; ESP-NOW max payload is limited.
    uint8_t buf[sizeof(TiltedReadingsHeader) + TILTED_MAX_NAME_LEN + sizeof(items)];
    if (pktLen > sizeof(buf))
    {
        Serial.println("TLV packet too large; not sending");
        actuallySleep();
        return;
    }

    const uint32_t chipId = tilted_get_chip_id32();
    const uint16_t wrote = tilted_encode_readings_packet(
        buf,
        sizeof(buf),
        chipId,
        (uint16_t)sleep_interval,
        name,
        nameLen,
        items,
        itemCount);

    if (wrote != pktLen)
    {
        Serial.println("Failed to encode TLV packet; not sending");
        actuallySleep();
        return;
    }

    esp_now_send(NULL, buf, pktLen); // NULL means send to all peers
    sent = millis();
    
    Serial.printf("TLV sent (name=%.*s, items=%u, len=%u)\n", nameLen, name, itemCount, pktLen);
    Serial.println("Data sent, preparing to sleep");
    
    // Clean up ESP-NOW to save power
    esp_now_deinit();
}

// The only difference between "normal" and "calibration"
// is the update frequency. We still deep sleep between samples.
void calibrationMode(bool firstIteration)
{
	readVoltage();
	sleep_interval = CALIBRATION_INTERVAL;
	if (firstIteration)
	{
		calibrationIterations = 1;
	}
	else
	{
		calibrationIterations += 1;
	}
	ESP.rtcUserMemoryWrite(RTC_ADDRESS, &calibrationIterations, sizeof(calibrationIterations));
}

static bool isCalibrationMode()
{
	return (calibrationIterations != 0) ? true : false;
}

void normalMode()
{
	readVoltage();
}

// OTA update logic temporarily removed. To re-enable OTA, restore
// the previous implementation which used ESPhttpUpdate.

void setup()
{
	pinMode(led, OUTPUT);
	ledOff();

	Serial.begin(74880);
	rst_info *resetInfo;
	resetInfo = ESP.getResetInfoPtr();
	Serial.println("Reboot");

	Serial.print("Booting because ");
	Serial.println(ESP.getResetReason());

	Serial.println("Build: " + String(versionTimestamp));

	// Turn off WiFi by default to save power
	WiFi.mode(WIFI_OFF);
	WiFi.forceSleepBegin();

	// INITIALIZE MPU
	Serial.println("Starting MPU-6050");
    Wire.begin(SDA_PIN, SCL_PIN);

    // Run a non-invasive I2C scan only on power-on (not deep-sleep wake).
    if (resetInfo->reason != REASON_DEEP_SLEEP_AWAKE) {
        doI2CScan(Wire);
    }

    Wire.setClock(400000);
    mpuSampler.begin(Wire);

    // Initialize DS18B20 (optional)
#if TILTED_ENABLE_DS18B20
    ds18b20Sampler.begin();
#endif

#if defined(TILTED_ENABLE_BMP280)
    bmp280Sampler.begin(Wire);
#endif
	// Read RTC memory to get current number of calibration iterations.
	ESP.rtcUserMemoryRead(RTC_ADDRESS, &calibrationIterations, sizeof(calibrationIterations));


    if (resetInfo->reason != REASON_DEEP_SLEEP_AWAKE)
	{
		float tilt;

		calibrationSetupStart = millis();
		while ((millis() - calibrationSetupStart) < CALIBRATION_SETUP_TIME)
		{
            // Sample until we have at least one valid reading to detect calibration posture.
            mpuSampler.sample();
            tilt = mpuSampler.filteredTiltDeg();
			if (tilt > 0.0 && tilt > CALIBRATION_TILT_ANGLE_MIN && tilt < CALIBRATION_TILT_ANGLE_MAX)
			{
                Serial.println("Initiate calibration mode");
				calibrationMode(true);

				break;
			}
			delay(2000);
		}
	}
	else if (isCalibrationMode() && calibrationIterations < CALIBRATION_ITERATIONS)
	{
		Serial.printf("Calibration mode, %d iterations...", calibrationIterations);
		calibrationMode(false);
	}
	else
	{
		Serial.println("Normal mode");
		normalMode();
	}





	currentState = STATE_SAMPLING;
    // Ensure we always start a cycle with a fresh sample window.
    mpuSampler.reset(MAX_SAMPLES);
    Serial.printf("[SAMPLE_INIT] target=%u left=%u\n", (unsigned)MAX_SAMPLES, (unsigned)mpuSampler.samplesLeft());

#if TILTED_ENABLE_DS18B20
    // Start DS18B20 conversion alongside MPU sampling.
    ds18b20Sampler.start();
#endif
#if defined(TILTED_ENABLE_BMP280)
    // BMP280 read is immediate; request a read cycle to be taken during sampling.
    bmp280Sampler.start();
#endif
	Serial.println("Finished setup");
}

void loop()
{
    switch (currentState) {
        case STATE_SAMPLING:
            if (sent) {
                currentState = STATE_SLEEPING;
            }
            else if ((millis() - bootTime) > WAKE_TIMEOUT && !isCalibrationMode()) {
                currentState = STATE_SLEEPING;
            }
            else {
                // In fallback mode (no INT pin), sample() will still make progress.
                // If dataReady is required/enabled, sample() will return false until ready.
                if (mpuSampler.pending())
                    mpuSampler.sample();

#if TILTED_ENABLE_DS18B20
                if (ds18b20Sampler.pending())
                    ds18b20Sampler.sample();
                #if defined(TILTED_ENABLE_BMP280)
                    if (bmp280Sampler.pending())
                        bmp280Sampler.sample();
                    const bool nonePending = !mpuSampler.pending() && !ds18b20Sampler.pending() && !bmp280Sampler.pending();
                #else
                    const bool nonePending = !mpuSampler.pending() && !ds18b20Sampler.pending();
                #endif
#else
                #if defined(TILTED_ENABLE_BMP280)
                        if (bmp280Sampler.pending())
                            bmp280Sampler.sample();
                        const bool nonePending = !mpuSampler.pending() && !bmp280Sampler.pending();
                #else
                    const bool nonePending = !mpuSampler.pending();
                #endif
#endif

                // Move on once everything has finished its work for this cycle.
                // We keep ready() for future changes, but we don't require it here.
                if (nonePending) {
                    // Put the MPU back to sleep immediately after data collection
                    mpuSampler.sleep();
                    Serial.println("MPU put to sleep");

                    currentState = STATE_PROCESSING;
                }
            }
            
            // mpu.getIntDataReadyStatus() hits the I2C bus. We don't need
			// to poll every ms while we're gathering samples. Once we have
			// the samples we're just waiting for the transmit to clear, so
			// loop a bit quicker.
            // Poll slower while we're gathering samples.
			delay((mpuSampler.samplesLeft() > 0) ? 10 : 1);
            break;
            
        case STATE_PROCESSING:
            // Process data and prepare for transmission
            currentState = STATE_TRANSMITTING;
            break;
            
        case STATE_TRANSMITTING:
            // Send sensor data through ESP-NOW
            sendSensorData();
            currentState = STATE_SLEEPING;
            break;
            
        case STATE_SLEEPING:
            // Go to deep sleep
            actuallySleep();
            break;
            
        default:
            // Should never reach here, but just in case reset to sampling state
            currentState = STATE_SAMPLING;
            break;
    }
}