#pragma once
#include <Arduino.h>
#include <vector>

enum SampleFlags : uint16_t {
  SAMPLE_FLAG_RELAY_ON = 1 << 0,
  SAMPLE_FLAG_HEATER_ON = 1 << 1,
  SAMPLE_FLAG_PHASE_TRIP = 1 << 2,
  SAMPLE_FLAG_RELAY_CMD_ON = 1 << 3,
};

struct SampleRec {
  uint32_t ts;
  int32_t current1_mA;
  int32_t current2_mA;
  int32_t current3_mA;
  int32_t power_dW;
  int16_t temp1_cC;
  int16_t temp2_cC;
  uint16_t phaseImbalance_dPct; // percent * 10
  uint16_t flags;
};

bool RingStoreBegin(const char* path, size_t fileSizeBytes);
bool RingStoreAppend(const SampleRec& r);
size_t RingStoreReadBatch(std::vector<SampleRec>& out, size_t maxItems);
bool RingStoreDrop(size_t count);
size_t RingStoreCountApprox();
