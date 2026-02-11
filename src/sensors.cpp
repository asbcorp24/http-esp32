#include "sensors.h"
#include <Wire.h>
#include "EmonLib.h"
#include "RTClib.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include "ring_store.h"
#include <Preferences.h>
static EnergyMonitor energyMonitor;
  RTC_DS3231 rtc;

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
analogReadResolution(12);
  // токовый датчик
energyMonitor.current(34, 50);

  // температура
  ds18b20.begin();

  dataMtx = xSemaphoreCreateMutex();
}

static void sensorsTask(void* pv) {
  (void)pv;
  uint32_t lastStoreMs = 0;
  while (true) {
    // Current / Power
    double rawI = energyMonitor.calcIrms(1480); // 1480 samples = ~3 сек при 50 Гц
    double current = rawI - irmsOffset;
    if (current < currentThreshold) current = 0.0;
    double power = current * Voltage;
Serial.print(":");Serial.println(rawI);Serial.println(current);
Serial.println(power);
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
 // ---- запись в кольцо раз в 30 сек ----
    if (millis() - lastStoreMs >= 30000) {
      lastStoreMs = millis();

      // RTC время (реальные секунды)
      uint32_t ts = 0;
      if (rtc.begin()) {
        ts = (uint32_t)rtc.now().unixtime();
      } else {
        // fallback — если RTC недоступны (лучше чем 0)
        ts = (uint32_t)(millis() / 1000);
      }
    // === запись в кольцо ===
    SampleRec rec{};
    rec.ts =ts;  // или RTC unix time
    rec.current_mA = (int32_t)(current * 1000);
    rec.power_dW   = (int32_t)(power);
    rec.temp_cC    = (int16_t)(tempC * 100);
    rec.flags      = heaterState ? 1 : 0;

   bool ok = RingStoreAppend(rec);
        Serial.printf("RingStoreAppend: %s ts=%u I=%ldmA P=%lddW T=%dcC\n",
                    ok ? "OK" : "FAIL", rec.ts, rec.current_mA, rec.power_dW, rec.temp_cC);
    }
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
