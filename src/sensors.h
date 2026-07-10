#pragma once
#include <Arduino.h>

struct SensorData {
  float temp1C;
  float temp2C;
  double current1A;
  double current2A;
  double current3A;
  double powerW;
  float phaseImbalancePct;
  bool heaterState;
  bool relayState;
  bool relayCommandOn;
  bool phaseTrip;
  uint32_t ts;
};

void SensorsInit();
void SensorsStartTasks();
bool SensorsGetLatest(SensorData& out);
void SensorsSetRemoteRelayDesired(bool on);
bool SensorsGetRemoteRelayDesired();
void SensorsSetSyncedTime(uint32_t ts);
