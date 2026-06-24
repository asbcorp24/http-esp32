#include "ring_store.h"
#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <stddef.h>
#include <vector>

using namespace fs;

static Preferences prefs;
static String gPath;
static size_t gFileSize = 0;

static const uint32_t MAGIC = 0x52494E47; // RING
static const uint16_t VERSION = 2;

#pragma pack(push, 1)
struct RecBin {
  uint32_t ts;
  int32_t current1_mA;
  int32_t current2_mA;
  int32_t current3_mA;
  int32_t power_dW;
  int16_t temp1_cC;
  int16_t temp2_cC;
  uint16_t phaseImbalance_dPct;
  uint16_t flags;
  uint32_t crc32;
};
#pragma pack(pop)

static const size_t REC_SIZE = sizeof(RecBin);

static uint32_t crc32_simple(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) {
      crc = (crc >> 1) ^ (0xEDB88320u & (-(int)(crc & 1)));
    }
  }
  return ~crc;
}

static uint32_t getU32(const char* key, uint32_t defv) {
  return prefs.getUInt(key, defv);
}

static void putU32(const char* key, uint32_t v) {
  prefs.putUInt(key, v);
}

static uint32_t dataStart() {
  return 16;
}

static uint32_t capacityRecs() {
  return (uint32_t)((gFileSize - dataStart()) / REC_SIZE);
}

static uint32_t dataOffset(uint32_t idx) {
  const uint32_t cap = capacityRecs();
  return dataStart() + (idx % cap) * REC_SIZE;
}

static void writeHeader(File& f) {
  f.seek(0);
  f.write((const uint8_t*)&MAGIC, 4);
  f.write((const uint8_t*)&VERSION, 2);
  const uint16_t rs = (uint16_t)REC_SIZE;
  f.write((const uint8_t*)&rs, 2);
  const uint32_t cap = capacityRecs();
  f.write((const uint8_t*)&cap, 4);
  const uint32_t zero = 0;
  f.write((const uint8_t*)&zero, 4);
}

static bool ensureFileSized(const char* path, size_t sizeBytes) {
  if (!LittleFS.begin(true)) return false;

  if (!LittleFS.exists(path)) {
    File f = LittleFS.open(path, "w");
    if (!f) return false;
    f.seek(sizeBytes - 1);
    f.write((uint8_t)0);
    f.close();
    return true;
  }

  File f = LittleFS.open(path, "r");
  if (!f) return false;
  const size_t sz = f.size();
  f.close();
  if (sz == sizeBytes) return true;

  LittleFS.remove(path);
  File nf = LittleFS.open(path, "w");
  if (!nf) return false;
  nf.seek(sizeBytes - 1);
  nf.write((uint8_t)0);
  nf.close();
  return true;
}

static void resetMeta(File& f) {
  f.seek(0);
  writeHeader(f);
  putU32("head", 0);
  putU32("tail", 0);
}

bool RingStoreBegin(const char* path, size_t fileSizeBytes) {
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed");
    return false;
  }

  gPath = path;
  gFileSize = fileSizeBytes;
  if (gFileSize < dataStart() + REC_SIZE * 16) return false;
  if (!ensureFileSized(path, fileSizeBytes)) return false;

  prefs.begin("ring", false);

  File f = LittleFS.open(path, "r+");
  if (!f) return false;

  uint32_t magic = 0;
  uint16_t version = 0;
  uint16_t recSize = 0;
  uint32_t cap = 0;

  f.read((uint8_t*)&magic, 4);
  f.read((uint8_t*)&version, 2);
  f.read((uint8_t*)&recSize, 2);
  f.read((uint8_t*)&cap, 4);

  if (magic != MAGIC || version != VERSION || recSize != REC_SIZE || cap != capacityRecs()) {
    resetMeta(f);
  }

  f.close();
  return true;
}

size_t RingStoreCountApprox() {
  const uint32_t head = getU32("head", 0);
  const uint32_t tail = getU32("tail", 0);
  const uint32_t cap = capacityRecs();
  if (head <= tail) return 0;

  const uint32_t diff = head - tail;
  return diff > cap ? cap : diff;
}

bool RingStoreAppend(const SampleRec& r) {
  File f = LittleFS.open(gPath, "r+");
  if (!f) return false;

  uint32_t head = getU32("head", 0);
  uint32_t tail = getU32("tail", 0);
  const uint32_t cap = capacityRecs();

  RecBin rb{};
  rb.ts = r.ts;
  rb.current1_mA = r.current1_mA;
  rb.current2_mA = r.current2_mA;
  rb.current3_mA = r.current3_mA;
  rb.power_dW = r.power_dW;
  rb.temp1_cC = r.temp1_cC;
  rb.temp2_cC = r.temp2_cC;
  rb.phaseImbalance_dPct = r.phaseImbalance_dPct;
  rb.flags = r.flags;
  rb.crc32 = crc32_simple((const uint8_t*)&rb, offsetof(RecBin, crc32));

  f.seek(dataOffset(head));
  f.write((const uint8_t*)&rb, REC_SIZE);
  f.flush();
  f.close();

  head++;
  if (head - tail > cap) {
    tail = head - cap;
  }

  putU32("head", head);
  putU32("tail", tail);
  return true;
}

static bool readOne(File& f, uint32_t idx, RecBin& out) {
  f.seek(dataOffset(idx));
  if (f.read((uint8_t*)&out, REC_SIZE) != REC_SIZE) return false;
  const uint32_t crc = crc32_simple((const uint8_t*)&out, offsetof(RecBin, crc32));
  return crc == out.crc32;
}

size_t RingStoreReadBatch(std::vector<SampleRec>& out, size_t maxItems) {
  out.clear();
  const size_t count = RingStoreCountApprox();
  if (count == 0) return 0;

  const uint32_t tail = getU32("tail", 0);
  File f = LittleFS.open(gPath, "r");
  if (!f) return 0;

  const size_t n = min(maxItems, count);
  for (size_t i = 0; i < n; i++) {
    RecBin rb{};
    if (!readOne(f, tail + (uint32_t)i, rb)) continue;

    SampleRec s{};
    s.ts = rb.ts;
    s.current1_mA = rb.current1_mA;
    s.current2_mA = rb.current2_mA;
    s.current3_mA = rb.current3_mA;
    s.power_dW = rb.power_dW;
    s.temp1_cC = rb.temp1_cC;
    s.temp2_cC = rb.temp2_cC;
    s.phaseImbalance_dPct = rb.phaseImbalance_dPct;
    s.flags = rb.flags;
    out.push_back(s);
  }

  f.close();
  return out.size();
}

bool RingStoreDrop(size_t count) {
  const size_t have = RingStoreCountApprox();
  if (count > have) count = have;

  uint32_t tail = getU32("tail", 0);
  tail += (uint32_t)count;
  putU32("tail", tail);
  return true;
}
