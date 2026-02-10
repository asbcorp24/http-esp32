#include "sensors.h"
#include <Wire.h>
#include "EmonLib.h"
#include "RTClib.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include "ring_store.h"
#include <Preferences.h>
static EnergyMonitor energyMonitor;
static RTC_DS3231 rtc;

// DS18B20 moved off GPIO4 to avoid conflict with WIFI_CFG_PIN
static const uint8_t ONE_WIRE_BUS = 27;
static OneWire oneWire(ONE_WIRE_BUS);
static DallasTemperature ds18b20(&oneWire);

static Preferences prefs;
static float Voltage = 220.0;

static void loadVoltage() {
  prefs.begin("cfg", true);
  Voltage = prefs.getFloat("voltage", 220.0);
  prefs.end();
}
static const float irmsOffset = 1.0;
static const float currentThreshold = 0.10;

static const int HEATER_PIN = 25;
static const float TEMP_ON  = -5.0;
static const float TEMP_OFF = 0.0;

static bool heaterState = false;

static SensorData latest{};
static bool hasData = false;
static SemaphoreHandle_t dataMtx;

static void heaterControl(float tempC) {
  if (!heaterState && tempC <= TEMP_ON) {
    digitalWrite(HEATER_PIN, HIGH);
    heaterState = true;
  } else if (heaterState && tempC >= TEMP_OFF) {
    digitalWrite(HEATER_PIN, LOW);
    heaterState = false;
  }
}

void SensorsInit() {
  pinMode(HEATER_PIN, OUTPUT);
  digitalWrite(HEATER_PIN, LOW);
  heaterState = false;
loadVoltage();
  Wire.begin();
  rtc.begin();

  // токовый датчик
  energyMonitor.current(35, 82);

  // температура
  ds18b20.begin();

  dataMtx = xSemaphoreCreateMutex();
}

static void sensorsTask(void* pv) {
  (void)pv;

  while (true) {
    // Current / Power
    double rawI = energyMonitor.calcIrms(2048);
    double current = rawI - irmsOffset;
    if (current < currentThreshold) current = 0.0;
    double power = current * Voltage;

    // Temp
    ds18b20.requestTemperatures();
    float tempC = ds18b20.getTempCByIndex(0);
    if (tempC == DEVICE_DISCONNECTED_C) tempC = -127.0;

    heaterControl(tempC);

    // publish
    if (xSemaphoreTake(dataMtx, pdMS_TO_TICKS(30)) == pdTRUE) {
      latest.tempC = tempC;
      latest.currentA = current;
      latest.powerW = power;
      latest.heaterState = heaterState;
      latest.tsMs = millis();
      hasData = true;
      xSemaphoreGive(dataMtx);
    }

    // === запись в кольцо ===
    SampleRec rec{};
    rec.ts = millis() / 1000;  // или RTC unix time
    rec.current_mA = (int32_t)(current * 1000);
    rec.power_dW   = (int32_t)(power * 10);
    rec.temp_cC    = (int16_t)(tempC * 100);
    rec.flags      = heaterState ? 1 : 0;

    RingStoreAppend(rec);

    vTaskDelay(pdMS_TO_TICKS(10000)); // 1 минута
  }
}


void SensorsStartTasks() {
  xTaskCreatePinnedToCore(sensorsTask, "sensorsTask", 4096, nullptr, 2, nullptr, 1);
}

bool SensorsGetLatest(SensorData& out) {
  if (!hasData) return false;
  if (xSemaphoreTake(dataMtx, 0) != pdTRUE) return false;
  out = latest;
  xSemaphoreGive(dataMtx);
  return true;
}
