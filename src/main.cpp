#include <Arduino.h>
#include "wifi_config.h"
#include "sensors.h"
#include "gsm_uplink.h"
#include "ring_store.h"
#include "esp_sleep.h"
#include "esp_system.h"
#define STATUS_LED_PIN 2   
// Дефолты для конфига (если в Preferences пусто)
static Config defaultCfg() {
  Config c;
  //c.serverHost = "78.138.169.178";
  c.serverHost = "specdpo.ru";
  c.serverPort = 80;//33775;
  c.location   = "";
  c.cryptoPass = "12345678";
  c.adminLogin = "admin";
  c.adminPass  = "admin";
  return c;
}
bool isWifiConfigModeNow() {
  uint8_t lowCount = 0;
  for (int i = 0; i < 5; i++) {
    if (digitalRead(WIFI_CFG_PIN) == LOW) lowCount++;
    delay(2);
  }
  return (lowCount >= 4); // 80% подтверждения
}
void coldResetESP() {
  Serial.println("❄️ COLD RESET via deep sleep");

  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  esp_sleep_enable_timer_wakeup(1000); // 1 мс
  esp_deep_sleep_start();
}
void systemTask(void* pv) {
  (void)pv;

  bool lastWifiMode = isWifiConfigModeNow();
 uint8_t changeCount = 0;

  // начальная индикация
  digitalWrite(STATUS_LED_PIN, lastWifiMode ? HIGH : LOW);

  while (true) {
    vTaskDelay(pdMS_TO_TICKS(10000)); // 10 секунд

    bool currentWifiMode = isWifiConfigModeNow();

    // === LED отражает режим WiFi ===
    digitalWrite(STATUS_LED_PIN, currentWifiMode ? HIGH : LOW);

    // === если режим сменился → перезагрузка ===

if (currentWifiMode != lastWifiMode) {
  changeCount++;
  if (changeCount >= 2) {   // 2 × 10 сек = 20 сек стабильно
    Serial.println("🔁 GPIO4 stable change → RESTART");
    delay(100);
coldResetESP();
  }
} else {
  changeCount = 0;
}

    lastWifiMode = currentWifiMode;
  }
}


void setup() {
  Serial.begin(115200);
  delay(300);
  pinMode(WIFI_CFG_PIN, INPUT_PULLUP);
  // === определяем режим СРАЗУ ===
  bool bootWifiMode = isWifiConfigModeNow();

  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, bootWifiMode ? HIGH : LOW);
Serial.printf("BOOT MODE: %s\n", bootWifiMode ? "WIFI" : "NORMAL");
Serial.printf("GPIO4 at boot = %d\n", digitalRead(WIFI_CFG_PIN));

  xTaskCreatePinnedToCore(
    systemTask,
    "systemTask",
    2048,
    nullptr,
    3,
    nullptr,
    1
  );

  // === Режим WiFi-конфига по GPIO4 ===
  if (bootWifiMode) {
    Serial.println("🟢 WIFI CONFIG MODE (GPIO4=GND)");
    WifiConfigStart(defaultCfg());

    while (true) {
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  }

  Serial.println("⚪ NORMAL MODE (WiFi disabled)");
if (!RingStoreBegin("/queue.bin", 256 * 1024)) {
  Serial.println("❌ RingStore init failed");
}
  // === обычный режим ===
  SensorsInit();
  SensorsStartTasks();

  GsmInit();
  GsmStartTask();
}


void loop() {
  // Здесь ничего не делаем — всё в FreeRTOS задачах
  vTaskDelay(pdMS_TO_TICKS(1000));
}
