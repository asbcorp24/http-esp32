#include "sensors.h"
#include "ring_store.h"
#include <math.h>
#include <Preferences.h>
#include <RTClib.h>
#include <Wire.h>
#include "EmonLib.h"
#include <OneWire.h>
#include <DallasTemperature.h>

RTC_DS3231 rtc;

static EnergyMonitor phase1Monitor;
static EnergyMonitor phase2Monitor;
static EnergyMonitor phase3Monitor;
static bool rtcOk = false;

static const uint8_t TEMP_BUS_PIN = 18;
static const uint8_t CURRENT1_PIN = 34;
static const uint8_t CURRENT2_PIN = 32;
static const uint8_t CURRENT3_PIN = 33;
static const uint8_t PUMP_RELAY_PIN = 26;
static const uint8_t HEATER_PIN = 12;

static const float CURRENT_CALIBRATION = 50.0f;
static const float CURRENT_THRESHOLD_A = 0.10f;
static const float PHASE_DETECT_MIN_CURRENT_A = 0.50f;
static const float TEMP_ON_C = -5.0f;
static const float TEMP_OFF_C = 0.0f;

static OneWire oneWire(TEMP_BUS_PIN);
static DallasTemperature ds18b20(&oneWire);
static DeviceAddress tempAddr1{};
static DeviceAddress tempAddr2{};
static bool hasTemp1 = false;
static bool hasTemp2 = false;

static Preferences prefs;
static float supplyVoltage = 220.0f;
static uint16_t sampleIntervalSec = 30;
static uint8_t phaseImbalanceLimitPct = 10;
static bool relayCommandOn = false;
static bool relayState = false;
static bool heaterState = false;
static bool hasSyncedBaseTime = false;
static bool rtcTimeValid = false;
static uint32_t syncedBaseTs = 0;
static uint32_t syncedBaseMillis = 0;

static SensorData latest{};
static bool hasData = false;
static SemaphoreHandle_t dataMtx;

static void logRelayDecision(const char* reason) {
  Serial.printf(
    "[RELAY] %s cmd=%d actual=%d pin=%d\n",
    reason,
    relayCommandOn ? 1 : 0,
    relayState ? 1 : 0,
    digitalRead(PUMP_RELAY_PIN)
  );
}

static uint16_t clampInterval(uint16_t v) {
  if (v < 15) return 15;
  if (v > 60) return 60;
  return v;
}

static uint8_t clampPhasePct(uint8_t v) {
  if (v > 20) return 20;
  return v;
}

static void saveRelayCommand() {
  prefs.begin("cfg", false);
  prefs.putBool("relayCmd", relayCommandOn);
  prefs.end();
}

static void loadConfig() {
  prefs.begin("cfg", true);
  supplyVoltage = prefs.getFloat("voltage", 220.0f);
  sampleIntervalSec = clampInterval(prefs.getUShort("sampleInt", 30));
  phaseImbalanceLimitPct = clampPhasePct((uint8_t)prefs.getUChar("phasePct", 10));
  relayCommandOn = prefs.getBool("relayCmd", false);
  prefs.end();
}

static uint32_t nowTs() {
  if (rtcOk && rtcTimeValid) {
    const uint32_t ts = rtc.now().unixtime();
    Serial.printf("[TIME] RTC ts=%u\n", ts);
    return ts;
  }

  if (hasSyncedBaseTime) {
    const uint32_t ts = syncedBaseTs + ((millis() - syncedBaseMillis) / 1000UL);
    Serial.printf("[TIME] FALLBACK synced-base ts=%u rtcOk=0\n", ts);
    return ts;
  }

  const uint32_t ts = millis() / 1000UL;
  Serial.printf("[TIME] FALLBACK uptime-only ts=%u rtcOk=0 synced=0\n", ts);
  return ts;
}

static void setRelayOutput(bool on) {
  relayState = on;
  digitalWrite(PUMP_RELAY_PIN, on ? HIGH : LOW);
  Serial.printf(
    "[RELAY] OUTPUT set on=%d gpio=%d level=%d\n",
    on ? 1 : 0,
    PUMP_RELAY_PIN,
    digitalRead(PUMP_RELAY_PIN)
  );
}

static void updateRelayOutput() {
  const bool shouldOn = relayCommandOn;
  setRelayOutput(shouldOn);
  if (!relayCommandOn) {
    logRelayDecision("blocked: command OFF");
  } else {
    logRelayDecision("allowed: direct remote control");
  }
}

static void heaterControl(float controlTempC) {
  if (!heaterState && controlTempC <= TEMP_ON_C) {
    digitalWrite(HEATER_PIN, HIGH);
    heaterState = true;
  } else if (heaterState && controlTempC >= TEMP_OFF_C) {
    digitalWrite(HEATER_PIN, LOW);
    heaterState = false;
  }
}

static float sanitizeCurrent(double rawI) {
  if (rawI < CURRENT_THRESHOLD_A) return 0.0f;
  return (float)rawI;
}

static float readTemperatureByAddress(const DeviceAddress addr, bool valid) {
  if (!valid) return -127.0f;
  const float tempC = ds18b20.getTempC(addr);
  return tempC == DEVICE_DISCONNECTED_C ? -127.0f : tempC;
}

static float computePhaseImbalancePct(float i1, float i2, float i3) {
  const float avg = (i1 + i2 + i3) / 3.0f;
  if (avg < PHASE_DETECT_MIN_CURRENT_A) return 0.0f;

  const float d1 = fabsf(i1 - avg);
  const float d2 = fabsf(i2 - avg);
  const float d3 = fabsf(i3 - avg);
  const float maxDev = max(d1, max(d2, d3));
  return (maxDev / avg) * 100.0f;
}

static void updateLatest(const SensorData& snapshot) {
  if (xSemaphoreTake(dataMtx, pdMS_TO_TICKS(50)) != pdTRUE) return;
  latest = snapshot;
  hasData = true;
  xSemaphoreGive(dataMtx);
}

void SensorsSetRemoteRelayDesired(bool on) {
  Serial.printf(
    "[RELAY] REMOTE CMD received=%d prev_cmd=%d prev_actual=%d\n",
    on ? 1 : 0,
    relayCommandOn ? 1 : 0,
    relayState ? 1 : 0
  );
  relayCommandOn = on;
  saveRelayCommand();
  updateRelayOutput();
}

bool SensorsGetRemoteRelayDesired() {
  return relayCommandOn;
}

void SensorsSetSyncedTime(uint32_t ts) {
  syncedBaseTs = ts;
  syncedBaseMillis = millis();
  hasSyncedBaseTime = true;
  rtcTimeValid = true;
  Serial.printf("[TIME] synced base stored ts=%u millis=%u\n", syncedBaseTs, syncedBaseMillis);
}

bool SensorsHasValidTime() {
  return (rtcOk && rtcTimeValid) || hasSyncedBaseTime;
}

void SensorsInit() {
  pinMode(PUMP_RELAY_PIN, OUTPUT);
  pinMode(HEATER_PIN, OUTPUT);
  digitalWrite(PUMP_RELAY_PIN, LOW);
  digitalWrite(HEATER_PIN, LOW);

  loadConfig();
  updateRelayOutput();

  Wire.begin();
  rtcOk = rtc.begin();
  rtcTimeValid = false;
  if (rtcOk) {
    const uint32_t rtcTs = rtc.now().unixtime();
    // Trust RTC only if it already contains a plausible Unix time.
    rtcTimeValid = rtcTs >= 1704067200UL; // 2024-01-01 00:00:00 UTC
    Serial.printf("[TIME] RTC begin OK ts=%u valid=%d\n", rtcTs, rtcTimeValid ? 1 : 0);
  } else {
    Serial.println("[TIME] RTC begin FAIL");
  }

  analogReadResolution(12);
  phase1Monitor.current(CURRENT1_PIN, CURRENT_CALIBRATION);
  phase2Monitor.current(CURRENT2_PIN, CURRENT_CALIBRATION);
  phase3Monitor.current(CURRENT3_PIN, CURRENT_CALIBRATION);

  ds18b20.begin();
  hasTemp1 = ds18b20.getAddress(tempAddr1, 0);
  hasTemp2 = ds18b20.getAddress(tempAddr2, 1);

  dataMtx = xSemaphoreCreateMutex();
}

static void sensorsTask(void* pv) {
  (void)pv;

  while (true) {
    loadConfig();

    const float current1 = sanitizeCurrent(phase1Monitor.calcIrms(1480));
    const float current2 = sanitizeCurrent(phase2Monitor.calcIrms(1480));
    const float current3 = sanitizeCurrent(phase3Monitor.calcIrms(1480));
    const float currentSum = current1 + current2 + current3;
    const double powerW = currentSum * supplyVoltage;

    ds18b20.requestTemperatures();
    const float temp1C = readTemperatureByAddress(tempAddr1, hasTemp1);
    const float temp2C = readTemperatureByAddress(tempAddr2, hasTemp2);
    const float controlTempC = temp1C > -100.0f ? temp1C : temp2C;
    heaterControl(controlTempC > -100.0f ? controlTempC : 25.0f);

    const float phaseImbalancePct = computePhaseImbalancePct(current1, current2, current3);
    const bool phaseTripActive = phaseImbalancePct > phaseImbalanceLimitPct;
    if (phaseTripActive) {
      Serial.printf(
        "[RELAY] PHASE TRIP telemetry imbalance=%.2f limit=%u currents=[%.3f,%.3f,%.3f]\n",
        phaseImbalancePct,
        phaseImbalanceLimitPct,
        current1,
        current2,
        current3
      );
    }
    updateRelayOutput();

    SensorData snapshot{};
    snapshot.temp1C = temp1C;
    snapshot.temp2C = temp2C;
    snapshot.current1A = current1;
    snapshot.current2A = current2;
    snapshot.current3A = current3;
    snapshot.powerW = powerW;
    snapshot.phaseImbalancePct = phaseImbalancePct;
    snapshot.heaterState = heaterState;
    snapshot.relayState = relayState;
    snapshot.relayCommandOn = relayCommandOn;
    snapshot.phaseTrip = phaseTripActive;
    snapshot.ts = nowTs();
    updateLatest(snapshot);

    SampleRec rec{};
    rec.ts = snapshot.ts;
    rec.current1_mA = (int32_t)(snapshot.current1A * 1000.0);
    rec.current2_mA = (int32_t)(snapshot.current2A * 1000.0);
    rec.current3_mA = (int32_t)(snapshot.current3A * 1000.0);
    rec.power_dW = (int32_t)(snapshot.powerW * 10.0);
    rec.temp1_cC = (int16_t)(snapshot.temp1C * 100.0f);
    rec.temp2_cC = (int16_t)(snapshot.temp2C * 100.0f);
    rec.phaseImbalance_dPct = (uint16_t)(snapshot.phaseImbalancePct * 10.0f);
    rec.flags = 0;
    if (snapshot.relayState) rec.flags |= SAMPLE_FLAG_RELAY_ON;
    if (snapshot.heaterState) rec.flags |= SAMPLE_FLAG_HEATER_ON;
    if (snapshot.phaseTrip) rec.flags |= SAMPLE_FLAG_PHASE_TRIP;
    if (snapshot.relayCommandOn) rec.flags |= SAMPLE_FLAG_RELAY_CMD_ON;

    const bool ok = RingStoreAppend(rec);
    Serial.printf(
      "Sample %s ts=%u I=[%ld,%ld,%ld]mA P=%lddW T=[%d,%d]cC phase=%u.%u%% relay=%d cmd=%d trip=%d\n",
      ok ? "OK" : "FAIL",
      rec.ts,
      rec.current1_mA,
      rec.current2_mA,
      rec.current3_mA,
      rec.power_dW,
      rec.temp1_cC,
      rec.temp2_cC,
      rec.phaseImbalance_dPct / 10,
      rec.phaseImbalance_dPct % 10,
      snapshot.relayState,
      snapshot.relayCommandOn,
      snapshot.phaseTrip
    );

    vTaskDelay(pdMS_TO_TICKS(sampleIntervalSec * 1000UL));
  }
}

void SensorsStartTasks() {
  xTaskCreatePinnedToCore(sensorsTask, "sensorsTask", 6144, nullptr, 2, nullptr, 1);
}

bool SensorsGetLatest(SensorData& out) {
  if (!hasData) return false;
  if (xSemaphoreTake(dataMtx, 0) != pdTRUE) return false;
  out = latest;
  xSemaphoreGive(dataMtx);
  return true;
}
