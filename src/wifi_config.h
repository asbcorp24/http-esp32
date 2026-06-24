#pragma once
#include <Arduino.h>

// GPIO4 -> GND => WiFi config only
static const gpio_num_t WIFI_CFG_PIN = GPIO_NUM_4;

struct Config {
  String serverHost;
  uint16_t serverPort;
  String location;     // <=500
  String cryptoPass;   // default 12345678
  String adminLogin;   // default admin
  String adminPass;    // default admin
  float voltage;
  uint16_t sampleIntervalSec;
  uint8_t phaseImbalancePct;
};

bool WifiConfigModeActive();
void WifiConfigStart(const Config& initialCfg);
