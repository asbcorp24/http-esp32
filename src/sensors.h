#pragma once
#include <Arduino.h>

struct SensorData {
  float tempC;
  double currentA;
  double powerW;
  bool heaterState;
  uint32_t tsMs;
};

void SensorsInit();
void SensorsStartTasks();
bool SensorsGetLatest(SensorData& out); // non-blocking (returns false if no data yet)
