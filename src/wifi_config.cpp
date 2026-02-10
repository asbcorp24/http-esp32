#include "wifi_config.h"
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

static const char* AP_SSID = "ESP32-CONFIG";
static const char* AP_PASS = "12345678";

static Preferences prefs;
static WebServer web(80);

static Config cfg; // local copy

static String prefGetString(const char* key, const char* defVal) {
  return prefs.getString(key, defVal);
}
static uint16_t prefGetUShort(const char* key, uint16_t defVal) {
  return prefs.getUShort(key, defVal);
}
static void loadConfig() {
  cfg.serverHost = prefGetString("serverHost", "78.138.169.178");
  cfg.serverPort = prefGetUShort("serverPort", 33775);
  cfg.location   = prefGetString("location", "");
  if (cfg.location.length() > 500) cfg.location = cfg.location.substring(0, 500);

  cfg.cryptoPass = prefGetString("cryptoPass", "12345678");
  cfg.adminLogin = prefGetString("adminLogin", "admin");
  cfg.adminPass  = prefGetString("adminPass",  "admin");
  cfg.voltage = prefs.getFloat("voltage", 220.0);
}
static void saveConfig() {
  prefs.putString("serverHost", cfg.serverHost);
  prefs.putUShort("serverPort", cfg.serverPort);
  prefs.putString("location", cfg.location);
  prefs.putString("cryptoPass", cfg.cryptoPass);
  prefs.putString("adminLogin", cfg.adminLogin);
  prefs.putString("adminPass",  cfg.adminPass);
  prefs.putFloat("voltage", cfg.voltage);
}

static bool requireAuth() {
  const String u = cfg.adminLogin.length() ? cfg.adminLogin : "admin";
  const String p = cfg.adminPass.length()  ? cfg.adminPass  : "admin";
  if (!web.authenticate(u.c_str(), p.c_str())) {
    web.requestAuthentication();
    return false;
  }
  return true;
}

static String htmlPage() {
  String h; h.reserve(5200);
  h += "<!doctype html><html lang='ru'><head>";
  h += "<meta charset='utf-8'/>";
  h += "<meta name='viewport' content='width=device-width, initial-scale=1'/>";
  h += "<title>ESP32 Config</title>";
  h += "<style>";
  h += "body{font-family:system-ui,-apple-system,Segoe UI,Roboto,Arial;margin:0;background:#0b1220;color:#e8eefc;}";
  h += ".wrap{max-width:720px;margin:0 auto;padding:16px;}";
  h += ".card{background:#121b2e;border:1px solid rgba(255,255,255,.08);border-radius:16px;padding:16px;box-shadow:0 8px 24px rgba(0,0,0,.25);}";
  h += "h1{font-size:20px;margin:0 0 12px;}";
  h += "label{display:block;margin:10px 0 6px;font-size:14px;opacity:.95;}";
  h += "input,textarea{width:100%;padding:12px;border-radius:12px;border:1px solid rgba(255,255,255,.12);background:#0b1220;color:#e8eefc;outline:none;}";
  h += "textarea{min-height:110px;resize:vertical;}";
  h += ".row{display:grid;grid-template-columns:1fr 1fr;gap:12px;}";
  h += "@media(max-width:560px){.row{grid-template-columns:1fr;}}";
  h += ".btn{margin-top:14px;width:100%;padding:12px 14px;border:0;border-radius:12px;background:#2a66ff;color:white;font-size:16px;font-weight:600;}";
  h += ".muted{opacity:.75;font-size:12px;line-height:1.35;margin-top:10px;}";
  h += ".ok{padding:10px 12px;border-radius:12px;background:rgba(40,180,99,.15);border:1px solid rgba(40,180,99,.25);margin-bottom:12px;display:none;}";
  h += "</style></head><body><div class='wrap'><div class='card'>";
  h += "<h1>Настройки устройства</h1>";
  h += "<div id='ok' class='ok'>✅ Сохранено!</div>";
  h += "<form id='f'>";

  h += "<label>Адрес сервера (IP/домен)</label>";
  h += "<input name='serverHost' required value='" + cfg.serverHost + "'/>";

  h += "<div class='row'>";
  h += "<div><label>Порт</label>";
  h += "<input name='serverPort' type='number' min='1' max='65535' required value='" + String(cfg.serverPort) + "'/></div>";
  h += "<div><label>Пароль шифрования</label>";
  h += "<input name='cryptoPass' minlength='8' value='" + cfg.cryptoPass + "'/></div>";
  h += "</div>";

  h += "<label>Местоположение (до 500 символов)</label>";
  h += "<textarea name='location' maxlength='500'>";
  h += cfg.location;
  h += "</textarea>";
  h += "<label>Напряжение сети (В)</label>";
  h += "<input name='voltage' type='number' step='0.1' value='" + String(cfg.voltage) + "'/>";
  h += "<h1 style='margin-top:18px'>Доступ</h1>";
  h += "<div class='row'>";
  h += "<div><label>Логин</label><input name='adminLogin' value='" + cfg.adminLogin + "'/></div>";
  h += "<div><label>Пароль</label><input name='adminPass' value='" + cfg.adminPass + "'/></div>";
  h += "</div>";

  h += "<button class='btn' type='submit'>Сохранить</button>";
  h += "<div class='muted'>Wi-Fi включается только при замыкании GPIO4 на GND при старте.<br>";
  h += "Сеть: <b>ESP32-CONFIG</b> пароль: <b>12345678</b> адрес: <b>http://192.168.4.1/</b></div>";
  h += "</form>";

  h += "<script>";
  h += "const f=document.getElementById('f');";
  h += "f.addEventListener('submit', async (e)=>{e.preventDefault();";
  h += "const fd=new FormData(f); const p=new URLSearchParams();";
  h += "for(const [k,v] of fd.entries()) p.append(k,v);";
  h += "const r=await fetch('/save',{method:'POST',body:p});";
  h += "if(r.ok){const ok=document.getElementById('ok');ok.style.display='block';setTimeout(()=>ok.style.display='none',1800);}else alert('Ошибка');";
  h += "});";
  h += "</script>";

  h += "</div></div></body></html>";
  return h;
}

static void webSetupRoutes() {
  web.on("/", HTTP_GET, []() {
    if (!requireAuth()) return;
    web.send(200, "text/html; charset=utf-8", htmlPage());
  });

  web.on("/save", HTTP_POST, []() {
    if (!requireAuth()) return;

    if (web.hasArg("serverHost")) cfg.serverHost = web.arg("serverHost");
    if (web.hasArg("serverPort")) cfg.serverPort = (uint16_t)web.arg("serverPort").toInt();
    if (web.hasArg("location"))   cfg.location   = web.arg("location");
    if (web.hasArg("cryptoPass")) cfg.cryptoPass = web.arg("cryptoPass");
    if (web.hasArg("adminLogin")) cfg.adminLogin = web.arg("adminLogin");
    if (web.hasArg("adminPass"))  cfg.adminPass  = web.arg("adminPass");
    if (web.hasArg("voltage"))     cfg.voltage = web.arg("voltage").toFloat();
    if (cfg.location.length() > 500) cfg.location = cfg.location.substring(0, 500);
    if (cfg.cryptoPass.length() < 8) cfg.cryptoPass = "12345678";
    if (!cfg.serverPort) cfg.serverPort = 33775;
    if (!cfg.serverHost.length()) cfg.serverHost = "78.138.169.178";

    saveConfig();
    web.send(200, "text/plain; charset=utf-8", "OK");
  });

  web.onNotFound([]() {
    web.sendHeader("Location", "/");
    web.send(302, "text/plain", "");
  });
}

static void wifiTask(void* pv) {
  (void)pv;

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  delay(200);

  IPAddress ip = WiFi.softAPIP();
  Serial.println();
  Serial.print("AP IP: "); Serial.println(ip);

  webSetupRoutes();
  web.begin();
  Serial.println("WiFi config web server started");

  // Асинхронность: отдельная задача, которая постоянно обслуживает клиентов
  while (true) {
    web.handleClient();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

bool WifiConfigModeActive() {
  pinMode(WIFI_CFG_PIN, INPUT_PULLUP);
  delay(10);
  return (digitalRead(WIFI_CFG_PIN) == LOW);
}

void WifiConfigStart(const Config& initialCfg) {
  prefs.begin("cfg", false);
  loadConfig();

  // если Preferences пустые — заполним дефолтами из initialCfg
  // (не затираем существующие)
  if (!prefs.isKey("serverHost")) cfg.serverHost = initialCfg.serverHost;
  if (!prefs.isKey("serverPort")) cfg.serverPort = initialCfg.serverPort;
  if (!prefs.isKey("location"))   cfg.location = initialCfg.location;
  if (!prefs.isKey("cryptoPass")) cfg.cryptoPass = initialCfg.cryptoPass;
  if (!prefs.isKey("adminLogin")) cfg.adminLogin = initialCfg.adminLogin;
  if (!prefs.isKey("adminPass"))  cfg.adminPass  = initialCfg.adminPass;

  saveConfig();

  // Запускаем WiFi таску на core 0 (обычно WiFi/Netstack удобнее там)
  xTaskCreatePinnedToCore(wifiTask, "wifiTask", 8192, nullptr, 2, nullptr, 0);
}
