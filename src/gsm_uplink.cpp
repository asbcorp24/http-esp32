#include "gsm_uplink.h"

#include <ArduinoJson.h>
#include <Preferences.h>
#include <RTClib.h>
#include <StreamDebugger.h>
#include <TinyGsmClient.h>

#include "crypto_aes.h"
#include "ring_store.h"
#include "sensors.h"

#define SerialMon Serial
#define SerialAT Serial1

static const bool GSM_AT_DEBUG = true;

extern RTC_DS3231 rtc;

static const char guser[] = "";
static const char gpass[] = "";

static StreamDebugger modemDebugger(SerialAT, SerialMon);
static TinyGsm modem(GSM_AT_DEBUG ? (Stream&)modemDebugger : (Stream&)SerialAT);
static TinyGsmClient gsmClient(modem);

static Preferences prefs;
static String cfgApn;
static String cfgHost;
static uint16_t cfgPort;
static String cryptoPass;
static uint16_t sampleIntervalSec;
static String deviceId;
static String bootId;

static const char* FW_VERSION = "2026-07-18-debug2";

static uint16_t clampInterval(uint16_t v) {
  if (v < 15) return 15;
  if (v > 60) return 60;
  return v;
}

static String sanitizeCfgString(String value) {
  value.replace("\r", "");
  value.replace("\n", "");
  value.replace("\t", " ");
  value.trim();
  return value;
}

static void loadUplinkCfg() {
  prefs.begin("cfg", true);
  cfgApn = sanitizeCfgString(prefs.getString("apn", "internet.tele2.ru"));
  cfgHost = sanitizeCfgString(prefs.getString("serverHost", "specdpo.ru"));
  cfgPort = prefs.getUShort("serverPort", 80);
  cryptoPass = sanitizeCfgString(prefs.getString("cryptoPass", "12345678"));
  sampleIntervalSec = clampInterval(prefs.getUShort("sampleInt", 30));
  prefs.end();

  if (!cfgApn.length()) cfgApn = "internet.tele2.ru";
  if (!cfgHost.length()) cfgHost = "specdpo.ru";
  if (cryptoPass.length() < 8) cryptoPass = "12345678";
}

static uint32_t loadSeq() {
  prefs.begin("uplink", false);
  const uint32_t seq = prefs.getUInt("seq", 1);
  prefs.end();
  return seq;
}

static void saveSeq(uint32_t seq) {
  prefs.begin("uplink", false);
  prefs.putUInt("seq", seq);
  prefs.end();
}

static String makeDeviceId() {
  const uint64_t mac = ESP.getEfuseMac();
  char buf[24];
  sprintf(buf, "esp32-%04X%08X", (uint16_t)(mac >> 32), (uint32_t)mac);
  return String(buf);
}

static String makeBootId() {
  char buf[24];
  sprintf(buf, "%08lX-%08lX", (unsigned long)millis(), (unsigned long)esp_random());
  return String(buf);
}

static bool readHttpResponse(int& outStatus, String& outBody) {
  unsigned long startedAt = millis();
  SerialMon.printf("[HTTP] wait headers connected=%d available=%d\n", gsmClient.connected() ? 1 : 0, gsmClient.available());
  while (!gsmClient.available()) {
    if (millis() - startedAt > 15000) {
      outStatus = -3;
      outBody = "";
      SerialMon.println("[HTTP] response timeout before headers");
      return false;
    }
    delay(20);
  }

  String statusLine = gsmClient.readStringUntil('\n');
  statusLine.trim();
  SerialMon.print("[HTTP] status line: ");
  SerialMon.println(statusLine);
  outStatus = -1;
  if (statusLine.startsWith("HTTP/1.1") || statusLine.startsWith("HTTP/1.0")) {
    outStatus = statusLine.substring(9, 12).toInt();
  }
  SerialMon.printf("[HTTP] headers start connected=%d available=%d\n", gsmClient.connected() ? 1 : 0, gsmClient.available());

  while (gsmClient.connected() || gsmClient.available()) {
    String headerLine = gsmClient.readStringUntil('\n');
    headerLine.trim();
    if (headerLine.length()) {
      SerialMon.print("[HTTP] header: ");
      SerialMon.println(headerLine);
    }
    if (headerLine.length() == 0) break;
  }

  outBody = "";
  unsigned long lastDataAt = millis();
  while (millis() - lastDataAt < 2000) {
    while (gsmClient.available()) {
      outBody += gsmClient.readString();
      lastDataAt = millis();
    }
    if (!gsmClient.connected()) {
      delay(100);
      while (gsmClient.available()) {
        outBody += gsmClient.readString();
        lastDataAt = millis();
      }
      break;
    }
    delay(20);
  }

  SerialMon.printf("[HTTP] parsed status=%d bodyLen=%u body=%s\n", outStatus, (unsigned)outBody.length(), outBody.c_str());

  return outStatus == 200;
}

static bool postBlob(const char* path,
                     const uint8_t* blob,
                     size_t blobLen,
                     int& outStatus,
                     String& outBody) {
  if (!modem.isGprsConnected()) {
    outStatus = -100;
    outBody = "";
    SerialMon.println("[HTTP] GPRS is not connected");
    return false;
  }

  gsmClient.stop();
  delay(100);

  if (!gsmClient.connect(cfgHost.c_str(), cfgPort)) {
    outStatus = -101;
    outBody = "";
    SerialMon.printf("[HTTP] connect failed host=%s port=%u\n", cfgHost.c_str(), cfgPort);
    return false;
  }

  SerialMon.printf("[HTTP] POST %s host=%s port=%u blobLen=%u\n", path, cfgHost.c_str(), cfgPort, (unsigned)blobLen);

  gsmClient.print("POST ");
  gsmClient.print(path);
  gsmClient.println(" HTTP/1.1");
  gsmClient.print("Host: ");
  gsmClient.println(cfgHost);
  gsmClient.println("Connection: close");
  gsmClient.println("Content-Type: application/octet-stream");
  gsmClient.print("Content-Length: ");
  gsmClient.println(blobLen);
  gsmClient.println();
  gsmClient.write(blob, blobLen);

  const bool ok = readHttpResponse(outStatus, outBody);
  gsmClient.stop();
  return ok;
}

static bool postEncrypted(const char* path, const String& plain, int& outStatus, String& outBody) {
  std::vector<uint8_t> blob;
  SerialMon.printf("[HTTP] plain %s payload=%s\n", path, plain.c_str());
  if (!aesEncryptBlob(cryptoPass, (const uint8_t*)plain.c_str(), plain.length(), blob)) {
    outStatus = -200;
    outBody = "";
    SerialMon.printf("[HTTP] encrypt failed for %s\n", path);
    return false;
  }
  return postBlob(path, blob.data(), blob.size(), outStatus, outBody);
}

static bool parseJson(const String& body, DynamicJsonDocument& doc) {
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    SerialMon.print("JSON parse failed: ");
    SerialMon.println(err.c_str());
    SerialMon.println(body);
    return false;
  }
  return true;
}

static String makeNonce() {
  return String((uint32_t)esp_random(), HEX);
}

static bool doRegister(uint32_t& seq) {
  String plain = "{";
  plain += "\"device_id\":\"" + deviceId + "\",";
  plain += "\"fw\":\"" + String(FW_VERSION) + "\",";
  plain += "\"boot_id\":\"" + bootId + "\",";
  plain += "\"nonce\":\"" + makeNonce() + "\",";
  plain += "\"seq\":" + String(seq);
  plain += "}";

  int status = 0;
  String body;
  const bool ok = postEncrypted("/register", plain, status, body);
  if (!ok) return false;

  if (body.indexOf("\"OK\"") >= 0) {
    seq++;
    saveSeq(seq);
    return true;
  }
  return false;
}

static void doSyncTime(uint32_t& seq) {
  String plain = "{";
  plain += "\"device_id\":\"" + deviceId + "\",";
  plain += "\"fw\":\"" + String(FW_VERSION) + "\",";
  plain += "\"boot_id\":\"" + bootId + "\",";
  plain += "\"nonce\":\"" + makeNonce() + "\",";
  plain += "\"seq\":" + String(seq);
  plain += "}";

  int status = 0;
  String body;
  if (!postEncrypted("/sync_time", plain, status, body)) {
    SerialMon.printf("[TIME] sync failed status=%d body=%s\n", status, body.c_str());
    return;
  }
  SerialMon.printf("[TIME] sync raw body=%s\n", body.c_str());

  DynamicJsonDocument doc(256);
  if (!parseJson(body, doc)) return;
  const uint32_t ts = doc["ts"] | 0;
  if (ts == 0) {
    SerialMon.println("[TIME] sync response has zero ts");
    return;
  }

  SensorsSetSyncedTime(ts);
  rtc.adjust(DateTime(ts));
  SerialMon.printf("[TIME] rtc adjusted ts=%u rtcOk_assumed=1\n", ts);
  seq++;
  saveSeq(seq);
}

static void applyRelayCommandFromBody(const String& body) {
  DynamicJsonDocument doc(256);
  if (!parseJson(body, doc)) return;
  const bool relayOn = doc["relay_on"] | false;
  SerialMon.printf("[RELAY] /control response relay_on=%d body=%s\n", relayOn ? 1 : 0, body.c_str());
  SensorsSetRemoteRelayDesired(relayOn);
}

static void pollControl(uint32_t& seq) {
  String plain = "{";
  plain += "\"device_id\":\"" + deviceId + "\",";
  plain += "\"fw\":\"" + String(FW_VERSION) + "\",";
  plain += "\"boot_id\":\"" + bootId + "\",";
  plain += "\"nonce\":\"" + makeNonce() + "\",";
  plain += "\"seq\":" + String(seq);
  plain += "}";

  int status = 0;
  String body;
  const bool ok = postEncrypted("/control", plain, status, body);
  if (!ok) {
    SerialMon.printf("[RELAY] /control failed status=%d body=%s\n", status, body.c_str());
    return;
  }

  applyRelayCommandFromBody(body);
  seq++;
  saveSeq(seq);
}

static String buildRecordsJson(const std::vector<SampleRec>& batch) {
  String plain = "{";
  plain += "\"device_id\":\"" + deviceId + "\",";
  plain += "\"fw\":\"" + String(FW_VERSION) + "\",";
  plain += "\"boot_id\":\"" + bootId + "\",";
  plain += "\"nonce\":\"" + makeNonce() + "\",";
  plain += "\"seq\":" + String(loadSeq()) + ",";
  plain += "\"records\":[";

  for (size_t i = 0; i < batch.size(); i++) {
    if (i) plain += ",";

    plain += "{";
    plain += "\"ts\":" + String(batch[i].ts) + ",";
    plain += "\"current1_mA\":" + String(batch[i].current1_mA) + ",";
    plain += "\"current2_mA\":" + String(batch[i].current2_mA) + ",";
    plain += "\"current3_mA\":" + String(batch[i].current3_mA) + ",";
    plain += "\"power_dW\":" + String(batch[i].power_dW) + ",";
    plain += "\"temp1_cC\":" + String(batch[i].temp1_cC) + ",";
    plain += "\"temp2_cC\":" + String(batch[i].temp2_cC) + ",";
    plain += "\"phase_imbalance_dPct\":" + String(batch[i].phaseImbalance_dPct) + ",";
    plain += "\"relay_on\":" + String((batch[i].flags & SAMPLE_FLAG_RELAY_ON) ? 1 : 0) + ",";
    plain += "\"heater_on\":" + String((batch[i].flags & SAMPLE_FLAG_HEATER_ON) ? 1 : 0) + ",";
    plain += "\"phase_trip\":" + String((batch[i].flags & SAMPLE_FLAG_PHASE_TRIP) ? 1 : 0) + ",";
    plain += "\"relay_cmd_on\":" + String((batch[i].flags & SAMPLE_FLAG_RELAY_CMD_ON) ? 1 : 0);
    plain += "}";
  }

  plain += "]}";
  return plain;
}

static void sendData(uint32_t& seq) {
  RingStoreDebugState("before-read");
  std::vector<SampleRec> batch;
  const size_t n = RingStoreReadBatch(batch, 2);
  if (n == 0) {
    SerialMon.println("[SEND] no records in queue");
    return;
  }

  SerialMon.printf("[SEND] batch size=%u seq=%u firstTs=%u lastTs=%u\n",
    (unsigned)n,
    seq,
    batch.front().ts,
    batch.back().ts);

  int status = 0;
  String body;
  const String plain = buildRecordsJson(batch);
  const bool ok = postEncrypted("/data", plain, status, body);
  SerialMon.printf("[SEND] result ok=%d status=%d body=%s\n", ok ? 1 : 0, status, body.c_str());
  if (ok && body.indexOf("\"OK\"") >= 0) {
    RingStoreDebugState("before-drop");
    RingStoreDrop(batch.size());
    RingStoreDebugState("after-drop");
    seq++;
    saveSeq(seq);
    SerialMon.printf("[SEND] drop success newSeq=%u\n", seq);
    return;
  }

  SerialMon.println("[SEND] batch NOT dropped");

  if (body.indexOf("notreg") >= 0 && doRegister(seq)) {
    SerialMon.println("[SEND] retry after registration");
    sendData(seq);
  }
}

static void gsmTask(void* pv) {
  (void)pv;

  uint32_t lastTimeSync = 0;
  uint32_t lastSend = 0;
  uint32_t seq = loadSeq();
  const uint32_t timeSyncIntervalMs = 1000UL * 60UL * 60UL;
  bool firstTimeSyncPending = true;
  SerialMon.printf("[BOOT] fw=%s boot_id=%s device=%s\n", FW_VERSION, bootId.c_str(), deviceId.c_str());

  while (true) {
    loadUplinkCfg();
    SerialMon.printf("[GSM] loop fw=%s boot_id=%s host=%s port=%u apn=%s sampleInt=%u seq=%u validTime=%d gprs=%d\n",
      FW_VERSION,
      bootId.c_str(),
      cfgHost.c_str(),
      cfgPort,
      cfgApn.c_str(),
      sampleIntervalSec,
      seq,
      SensorsHasValidTime() ? 1 : 0,
      modem.isGprsConnected() ? 1 : 0);

    if (!modem.isGprsConnected()) {
      SerialMon.println("GPRS disconnected, reconnect...");
      modem.gprsConnect(cfgApn.c_str(), guser, gpass);
      vTaskDelay(pdMS_TO_TICKS(5000));
      continue;
    }

    const uint32_t nowMs = millis();
    if (firstTimeSyncPending || !SensorsHasValidTime()) {
      doSyncTime(seq);
      lastTimeSync = nowMs;
      firstTimeSyncPending = false;
    } else if (nowMs - lastTimeSync >= timeSyncIntervalMs) {
      doSyncTime(seq);
      lastTimeSync = nowMs;
    }

    if (nowMs - lastSend >= sampleIntervalSec * 1000UL) {
      lastSend = nowMs;
      pollControl(seq);
      sendData(seq);
    }

    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

void GsmInit() {
  loadUplinkCfg();
  deviceId = makeDeviceId();
  bootId = makeBootId();

  SerialAT.begin(9600, SERIAL_8N1, 16, 17);
  delay(300);

  if (GSM_AT_DEBUG) {
    SerialMon.println("[GSM] AT debug enabled");
  }

  SerialMon.println("Restart modem");
  modem.restart();

  SerialMon.print("Modem: ");
  SerialMon.println(modem.getModemInfo());

  SerialMon.print("Waiting for network...");
  modem.waitForNetwork(60000);

  SerialMon.print("Connecting GPRS...");
  modem.gprsConnect(cfgApn.c_str(), guser, gpass);
}

void GsmStartTask() {
  xTaskCreatePinnedToCore(gsmTask, "gsmTask", 8192, nullptr, 2, nullptr, 1);
}
