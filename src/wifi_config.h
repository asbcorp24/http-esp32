#pragma once
#include <Arduino.h>

// GPIO4 (D4) -> GND => WiFi config only
static const gpio_num_t WIFI_CFG_PIN = GPIO_NUM_15;

struct Config {
  String serverHost;
  uint16_t serverPort;
  String location;     // <=500
  String cryptoPass;   // default 12345678
  String adminLogin;   // default admin
  String adminPass;    // default admin
};

bool WifiConfigModeActive();   // true if GPIO4 grounded at boot
void WifiConfigStart(const Config& initialCfg); // starts AP + web task
