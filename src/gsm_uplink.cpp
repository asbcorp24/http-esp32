#include "gsm_uplink.h"

#include <TinyGsmClient.h>
#include <ArduinoHttpClient.h>
#include <Preferences.h>

#include "sensors.h"
#include "ring_store.h"
#include "crypto_aes.h"

// ===================== Serial =====================
#define SerialMon Serial
#define SerialAT  Serial1

// ===================== APN =====================
static const char apn[]  = "internet.tele2.ru";
static const char guser[] = "";
static const char gpass[] = "";

// ===================== GSM =====================
static TinyGsm modem(SerialAT);
static TinyGsmClient gsmClient(modem);

// ===================== CONFIG =====================
static Preferences prefs;
static String cfgHost;
static uint16_t cfgPort;
static String cryptoPass;

// ===================== DEVICE =====================
static String deviceId;

// ===================== HELPERS =====================
static void loadUplinkCfg() {
  prefs.begin("cfg", true);
  cfgHost    = prefs.getString("serverHost", "78.138.169.178");
  cfgPort    = prefs.getUShort("serverPort", 33775);
  cryptoPass = prefs.getString("cryptoPass", "12345678");
  prefs.end();
}

static uint32_t loadSeq() {
  prefs.begin("uplink", false);
  uint32_t seq = prefs.getUInt("seq", 1);
  prefs.end();
  return seq;
}

static void saveSeq(uint32_t seq) {
  prefs.begin("uplink", false);
  prefs.putUInt("seq", seq);
  prefs.end();
}

static String makeDeviceId() {
  uint64_t mac = ESP.getEfuseMac();
  char buf[24];
  sprintf(buf, "esp32-%04X%08X", (uint16_t)(mac >> 32), (uint32_t)mac);
  return String(buf);
}

// ===================== HTTP POST =====================
/*static bool postBlob(const char* path,
                     const uint8_t* blob,
                     size_t blobLen,
                     int& outStatus,
                     String& outBody) {

  HttpClient http(gsmClient, cfgHost.c_str(), cfgPort);

  http.beginRequest();
  http.post(path);
  http.sendHeader("Content-Type", "application/octet-stream");
  http.sendHeader("Content-Length", blobLen);
  http.beginBody();
  http.write(blob, blobLen);
  http.endRequest();

  outStatus = http.responseStatusCode();
  outBody   = http.responseBody();
  http.stop();

  SerialMon.printf("POST %s -> %d '%s'\n",
                   path, outStatus, outBody.c_str());

  return (outStatus == 200);
}
*/
/**/

static bool postBlob(const char* path,
                     const uint8_t* blob,
                     size_t blobLen,
                     int& outStatus,
                     String& outBody) {

  SerialMon.println("---- HTTP POST BEGIN ----");
    SerialMon.println(path);

  if (!modem.isGprsConnected()) {
    SerialMon.println("GPRS NOT CONNECTED!");
    outStatus = -100;
    return false;
  }

  gsmClient.stop();
  delay(100);

  SerialMon.println("Opening TCP...");

  if (!gsmClient.connect(cfgHost.c_str(), cfgPort)) {
    SerialMon.println("TCP connect FAILED");
    outStatus = -101;
    return false;
  }

  SerialMon.println("TCP connected");

  // ===== HTTP HEADER =====
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

  // ===== BODY =====
  gsmClient.write(blob, blobLen);

  SerialMon.println("Request sent");

  // ===== READ RESPONSE =====
  unsigned long t0 = millis();
  while (!gsmClient.available()) {
    if (millis() - t0 > 15000) {
      SerialMon.println("Response timeout");
      gsmClient.stop();
      outStatus = -3;
      return false;
    }
  }

  String statusLine = gsmClient.readStringUntil('\n');
  SerialMon.print("STATUS LINE: ");
  SerialMon.println(statusLine);

  int code = -1;
  if (statusLine.startsWith("HTTP/1.1")) {
    code = statusLine.substring(9, 12).toInt();
  }

  outStatus = code;

  outBody = "";
  while (gsmClient.available()) {
    outBody += gsmClient.readString();
  }

  SerialMon.printf("HTTP STATUS: %d\n", outStatus);
  SerialMon.println("---- HTTP POST END ----");

  gsmClient.stop();

  return (outStatus == 200);
}





// ===================== REGISTER =====================
static bool doRegister(uint32_t& seq) {

  SerialMon.println("Registering device...");

  // ---- nonce ----
  uint32_t rnd = esp_random();
  String nonce = String(rnd, HEX);

  // ---- JSON ----
  String plain = "{";
  plain += "\"device_id\":\"" + deviceId + "\",";
  plain += "\"nonce\":\"" + nonce + "\",";
  plain += "\"seq\":" + String(seq);
  plain += "}";

  SerialMon.println("Register JSON:");
  SerialMon.println(plain);

  // ---- encrypt ----
  std::vector<uint8_t> blob;
  if (!aesEncryptBlob(
        cryptoPass,
        (uint8_t*)plain.c_str(),
        plain.length(),
        blob)) {

    SerialMon.println("AES encrypt failed");
    return false;
  }

  SerialMon.print("Encrypted size: ");
  SerialMon.println(blob.size());

  int status;
  String body;

  bool ok = postBlob("/register", blob.data(), blob.size(), status, body);

  SerialMon.print("Register status=");
  SerialMon.println(status);

  SerialMon.print("Register body=");
  SerialMon.println(body);

  if (ok && body.indexOf("OK") >= 0) {
    SerialMon.println("Register success");

    seq++;
    saveSeq(seq);
    return true;
  }

  SerialMon.println("Register failed");
  return false;
}

// ===================== SYNC TIME =====================
static void doSyncTime(uint32_t& seq) {
  String plain = "{";
  plain += "\"device_id\":\"" + deviceId + "\",";
  plain += "\"seq\":" + String(seq);
  plain += "}";

  std::vector<uint8_t> blob;
  if (!aesEncryptBlob(cryptoPass,
        (uint8_t*)plain.c_str(),
        plain.length(),
        blob)) return;

  int status;
  String body;
  postBlob("/sync_time", blob.data(), blob.size(), status, body);
SerialMon.print(body);
  seq++;
  saveSeq(seq);
}

// ===================== SEND DATA =====================
static void sendData(uint32_t& seq) {
  SerialMon.print("Sending data, seq=");
  SerialMon.println(seq);

  std::vector<SampleRec> batch;
  size_t n = RingStoreReadBatch(batch, 1); // маленький пакет для SIM900
  if (n == 0) {
    SerialMon.println("No data in ring buffer");
    return;
  }

  // ---- nonce ----
  uint32_t rnd = esp_random();
  String nonce = String(rnd, HEX);

  // ---- JSON ----
  String plain = "{";
  plain += "\"device_id\":\"" + deviceId + "\",";
  plain += "\"nonce\":\"" + nonce + "\",";
  plain += "\"seq\":" + String(seq) + ",";
  plain += "\"records\":[";

  for (size_t i = 0; i < batch.size(); i++) {
    if (i) plain += ",";

    plain += "{";
    plain += "\"ts\":" + String(batch[i].ts) + ",";
    plain += "\"current_mA\":" + String(batch[i].current_mA) + ",";
    plain += "\"power_dW\":" + String(batch[i].power_dW) + ",";
    plain += "\"temp_cC\":" + String(batch[i].temp_cC);
    plain += "}";
  }

  plain += "]}";

  SerialMon.println("JSON payload:");
  SerialMon.println(plain);

  // ---- encrypt ----
  std::vector<uint8_t> blob;
  if (!aesEncryptBlob(
        cryptoPass,
        (uint8_t*)plain.c_str(),
        plain.length(),
        blob)) {
    SerialMon.println("AES encrypt failed");
    return;
  }

  SerialMon.print("Encrypted size: ");
  SerialMon.println(blob.size());

  int status;
  String body;

  bool ok = postBlob("/data", blob.data(), blob.size(), status, body);

  SerialMon.print("Server status=");
  SerialMon.println(status);
  SerialMon.print("Server body=");
  SerialMon.println(body);

  // ---- success ----
  if (ok && body.indexOf("OK") >= 0) {
    SerialMon.println("Data accepted, dropping from ring");
    RingStoreDrop(batch.size());

    seq++;
    saveSeq(seq);
  }
  // ---- not registered ----
  else if (body.indexOf("notreg") >= 0) {
    SerialMon.println("Device not registered -> registering");

    if (doRegister(seq)) {
      SerialMon.println("Register OK, retry send");
      sendData(seq);
    }
  }
}


// ===================== TASK =====================
static void gsmTask(void* pv) {
  (void)pv;
uint32_t lastTimeSync = 0;
const uint32_t TIME_SYNC_INTERVAL = 40000; // 1 час
  const uint32_t SEND_INTERVAL = 30000;
  uint32_t lastSend = 0;

  uint32_t seq = loadSeq();

  while (true) {
    if (!modem.isGprsConnected()) {
      SerialMon.println("GPRS disconnected, reconnect...");
      modem.gprsConnect(apn, guser, gpass);
      vTaskDelay(pdMS_TO_TICKS(5000));
      continue;
    }

    if (millis() - lastSend >= SEND_INTERVAL) {
      lastSend = millis();

      loadUplinkCfg();

      // 1) синк времени (редко, но пусть тут)
     if (millis() - lastTimeSync > TIME_SYNC_INTERVAL) {
        doSyncTime(seq);
        lastTimeSync = millis();
    }
      // 2) отправка данных
      sendData(seq);
    }

    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

// ===================== API =====================
void GsmInit() {
  loadUplinkCfg();
  deviceId = makeDeviceId();

  SerialAT.begin(9600, SERIAL_8N1, 16, 17);
  delay(300);

  SerialMon.println("Restart modem");
  modem.restart();

  SerialMon.print("Modem: ");
  SerialMon.println(modem.getModemInfo());

  SerialMon.print("Waiting for network...");
  modem.waitForNetwork(60000);

  SerialMon.print("Connecting GPRS...");
  modem.gprsConnect(apn, guser, gpass);
}

void GsmStartTask() {
  xTaskCreatePinnedToCore(
    gsmTask,
    "gsmTask",
    8192,
    nullptr,
    2,
    nullptr,
    1
  );
}
