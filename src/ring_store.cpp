#include "ring_store.h"
#include <Arduino.h>
#include <vector>
#include <FS.h>     
#include <LittleFS.h>
#include <Preferences.h>
 
using namespace fs;
static Preferences prefs;
static String gPath;
static size_t gFileSize = 0;

static const uint32_t MAGIC = 0x52494E47; // 'RING'
static const uint16_t VERSION = 1;

// выравнивание на 24 байта
#pragma pack(push, 1)
struct RecBin {
  uint32_t ts;
  int32_t  current_mA;
  int32_t  power_dW;
  int16_t  temp_cC;
  uint16_t flags;
  uint32_t crc32;
  uint8_t  pad[4]; // до 24
};
#pragma pack(pop)

static const size_t REC_SIZE = sizeof(RecBin);

static uint32_t crc32_simple(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) {
      crc = (crc >> 1) ^ (0xEDB88320u & (-(int)(crc & 1)));
    }
  }
  return ~crc;
}

static uint32_t getU32(const char* key, uint32_t defv) { return prefs.getUInt(key, defv); }
static void putU32(const char* key, uint32_t v) { prefs.putUInt(key, v); }

static uint32_t dataStart() { return 16; } // зарезервируем 16 байт под заголовок
static uint32_t capacityRecs() { return (gFileSize - dataStart()) / REC_SIZE; }
static uint32_t dataOffset(uint32_t idx) { return dataStart() + (idx % capacityRecs()) * REC_SIZE; }

static void writeHeader(File& f) {
  f.seek(0);
  f.write((uint8_t*)&MAGIC, 4);
  f.write((uint8_t*)&VERSION, 2);
  uint16_t rs = (uint16_t)REC_SIZE;
  f.write((uint8_t*)&rs, 2);
  uint32_t cap = capacityRecs();
  f.write((uint8_t*)&cap, 4);
  uint32_t zero = 0;
  f.write((uint8_t*)&zero, 4);
}

static bool ensureFileSized(const char* path, size_t sizeBytes) {
  if (!LittleFS.begin(true)) return false;

  if (!LittleFS.exists(path)) {
    File f = LittleFS.open(path, "w");
    if (!f) return false;
    // быстро выделим размер
    f.seek(sizeBytes - 1);
    f.write((uint8_t)0);
    f.close();
  } else {
    File f = LittleFS.open(path, "r");
    if (!f) return false;
    size_t sz = f.size();
    f.close();
    if (sz != sizeBytes) {
      // пересоздадим под новый размер (лучше так, чем мучить старое)
      LittleFS.remove(path);
      File nf = LittleFS.open(path, "w");
      if (!nf) return false;
      nf.seek(sizeBytes - 1);
      nf.write((uint8_t)0);
      nf.close();
    }
  }
  return true;
}

bool RingStoreBegin(const char* path, size_t fileSizeBytes) {
    if (!LittleFS.begin(true)) {
  Serial.println("❌ LittleFS mount failed even after format");
  return false;
}
  gPath = path;
  gFileSize = fileSizeBytes;

  if (gFileSize < dataStart() + REC_SIZE * 16) return false; // минимум

  if (!ensureFileSized(path, fileSizeBytes)) return false;

  prefs.begin("ring", false);

  File f = LittleFS.open(path, "r+");
  if (!f) return false;

  // проверим header
  uint32_t m = 0;
  f.read((uint8_t*)&m, 4);
  if (m != MAGIC) {
    // init
    f.seek(0);
    writeHeader(f);
    putU32("head", 0);
    putU32("tail", 0);
    putU32("full", 0);
  }

  f.close();
  return true;
}

static bool isFull() { return getU32("full", 0) != 0; }

size_t RingStoreCountApprox() {
  uint32_t head = getU32("head", 0);
  uint32_t tail = getU32("tail", 0);
  if (!isFull()) {
    if (head >= tail) return head - tail;
    // если не full, но head < tail — значит wrap (мы храним head/tail как индексы-каунтеры, поэтому так не бывает)
    return 0;
  } else {
    return capacityRecs();
  }
}

bool RingStoreAppend(const SampleRec& r) {
 // Serial.printf("Ring append: ts=%u\n", r.ts);
  File f = LittleFS.open(gPath, "r+");
  if (!f) return false;

  uint32_t head = getU32("head", 0);
  uint32_t tail = getU32("tail", 0);
  uint32_t cap  = capacityRecs();

  RecBin rb{};
  rb.ts = r.ts;
  rb.current_mA = r.current_mA;
  rb.power_dW = r.power_dW;
  rb.temp_cC = r.temp_cC;
  rb.flags = r.flags;

  rb.crc32 = crc32_simple((uint8_t*)&rb, offsetof(RecBin, crc32));

  uint32_t idx = head % cap;
  uint32_t off = dataOffset(idx);

  f.seek(off);
  f.write((uint8_t*)&rb, REC_SIZE);
  f.flush();
  f.close();

  head++;

  // если было full — tail тоже двигаем (перезапись)
  if (isFull()) {
    tail++;
  }

  // если head догнал tail — стало full (в кольце)
  if (!isFull() && (head - tail) >= cap) {
    putU32("full", 1);
    // tail подтянем так, чтобы head-tail == cap
    tail = head - cap;
  }

  putU32("head", head);
  putU32("tail", tail);
  return true;
}

static bool readOne(File& f, uint32_t idx, RecBin& out) {
  uint32_t off = dataOffset(idx);
  f.seek(off);
  if (f.read((uint8_t*)&out, REC_SIZE) != REC_SIZE) return false;

  uint32_t crc = crc32_simple((uint8_t*)&out, offsetof(RecBin, crc32));
  return (crc == out.crc32);
}

size_t RingStoreReadBatch(std::vector<SampleRec>& out, size_t maxItems) {
  out.clear();
  size_t count = RingStoreCountApprox();
  if (count == 0) return 0;

  uint32_t tail = getU32("tail", 0);
  uint32_t cap  = capacityRecs();

  File f = LittleFS.open(gPath, "r");
  if (!f) return 0;

  size_t n = min(maxItems, count);
  for (size_t i = 0; i < n; i++) {
    RecBin rb{};
    uint32_t idx = (tail + i) % cap;
    if (!readOne(f, idx, rb)) {
      // если запись битая — пропустим её, но лучше сдвинуть tail позже (можно усилить логику)
      continue;
    }
    SampleRec s{};
    s.ts = rb.ts;
    s.current_mA = rb.current_mA;
    s.power_dW = rb.power_dW;
    s.temp_cC = rb.temp_cC;
    s.flags = rb.flags;
    out.push_back(s);
  }

  f.close();
  return out.size();
}

bool RingStoreDrop(size_t count) {
  size_t have = RingStoreCountApprox();
  if (count > have) count = have;

  uint32_t head = getU32("head", 0);
  uint32_t tail = getU32("tail", 0);
  uint32_t cap  = capacityRecs();

  tail += count;

  // если удалили всё — full сбрасываем
  if (tail == head) {
    putU32("full", 0);
  } else if ((head - tail) < cap) {
    // стало не full
    putU32("full", 0);
  }

  putU32("tail", tail);
  return true;
}
