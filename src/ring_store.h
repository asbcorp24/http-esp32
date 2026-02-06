#pragma once
#include <Arduino.h>
#include <vector>

struct SampleRec {
  uint32_t ts;
  int32_t  current_mA;  // Current * 1000
  int32_t  power_dW;    // Power * 10
  int16_t  temp_cC;     // temp * 100
  uint16_t flags;       // bit0=heater
};

bool RingStoreBegin(const char* path, size_t fileSizeBytes); // создаёт файл/структуры
bool RingStoreAppend(const SampleRec& r);                    // пишет, при переполнении затирает старое
size_t RingStoreReadBatch(std::vector<SampleRec>& out, size_t maxItems); // читает от tail, но НЕ удаляет
bool RingStoreDrop(size_t count);                            // удалить (сдвинуть tail) после успешной отправки
size_t RingStoreCountApprox();                               // приблизительно сколько записей в очереди
