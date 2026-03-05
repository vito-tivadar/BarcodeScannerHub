#include <Arduino.h>
#include <NimBLEDevice.h>
#include <BLEScanner.h>

#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>

// -------------------- Storage --------------------
Preferences prefs;

static String cfg_targetName;
static String cfg_postUrl;
static String cfg_wifiSsid;
static String cfg_wifiPass;

// -------------------- Web --------------------
WebServer server(80);

static WiFiClientSecure tlsClient;

static String htmlEscape(const String& s) {
  String out;
  out.reserve(s.length());
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    switch (c) {
      case '&': out += F("&amp;"); break;
      case '<': out += F("&lt;"); break;
      case '>': out += F("&gt;"); break;
      case '"': out += F("&quot;"); break;
      case '\'': out += F("&#39;"); break;
      default: out += c; break;
    }
  }
  return out;
}

static void saveConfig() {
  prefs.putString("target", cfg_targetName);
  prefs.putString("postUrl", cfg_postUrl);
  prefs.putString("ssid", cfg_wifiSsid);
  prefs.putString("pass", cfg_wifiPass);
}

static void loadConfig() {
  cfg_targetName = prefs.getString("target", "NT scanner LE");
  cfg_postUrl    = prefs.getString("postUrl", "");
  cfg_wifiSsid   = prefs.getString("ssid", "");
  cfg_wifiPass   = prefs.getString("pass", "");
}

static String pageRoot() {
  String ip = WiFi.isConnected() ? WiFi.localIP().toString() : String("(not connected)");
  String mode = WiFi.isConnected() ? String("STA") : String("AP (config)");

  String html;
  html.reserve(2000);
  html += F("<!doctype html><html><head><meta charset='utf-8'/>");
  html += F("<meta name='viewport' content='width=device-width, initial-scale=1'/>");
  html += F("<title>ESP32 Scanner Config</title></head><body>");
  html += F("<h2>ESP32 Scanner Config</h2>");
  html += F("<p><b>WiFi mode:</b> "); html += mode; html += F("<br/>");
  html += F("<b>IP:</b> "); html += ip; html += F("</p>");

  html += F("<form method='POST' action='/save'>");

  html += F("<h3>BLE</h3>");
  html += F("Target name: <input name='target' style='width:100%' value='");
  html += htmlEscape(cfg_targetName);
  html += F("'/><br/>");

  html += F("<h3>Webhook</h3>");
  html += F("POST URL: <input name='postUrl' style='width:100%' value='");
  html += htmlEscape(cfg_postUrl);
  html += F("'/><br/>");

  html += F("<h3>WiFi</h3>");
  html += F("SSID: <input name='ssid' style='width:100%' value='");
  html += htmlEscape(cfg_wifiSsid);
  html += F("'/><br/>");
  html += F("Password: <input name='pass' type='password' style='width:100%' value='");
  html += htmlEscape(cfg_wifiPass);
  html += F("'/><br/>");

  html += F("<br/><button type='submit'>Save & Reboot</button>");
  html += F("</form>");

  html += F("<p><a href='/status'>/status</a></p>");
  html += F("</body></html>");
  return html;
}

static void handleRoot() { server.send(200, "text/html", pageRoot()); }

static void handleStatus() {
  String json = "{";
  json += "\"wifiConnected\":" + String(WiFi.isConnected() ? "true" : "false") + ",";
  json += "\"ip\":\"" + (WiFi.isConnected() ? WiFi.localIP().toString() : String("")) + "\",";
  json += "\"target\":\"" + htmlEscape(cfg_targetName) + "\",";
  json += "\"postUrl\":\"" + htmlEscape(cfg_postUrl) + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

static void handleSave() {
  cfg_targetName = server.arg("target"); cfg_targetName.trim();
  cfg_postUrl    = server.arg("postUrl"); cfg_postUrl.trim();
  cfg_wifiSsid   = server.arg("ssid"); cfg_wifiSsid.trim();
  cfg_wifiPass   = server.arg("pass");

  saveConfig();

  server.send(200, "text/html",
              "<html><body><h3>Saved.</h3><p>Rebooting...</p></body></html>");
  delay(300);
  ESP.restart();
}

static void startWebServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/status", HTTP_GET, handleStatus);
  server.begin();
  Serial.println("🌐 Web server started on port 80");
}

// -------------------- WiFi --------------------
// AP_PASS is only used when no WiFi credentials are configured. Change it
// before deploying in a production environment.
static const char* AP_SSID = "ESP32-SCANNER-SETUP";
static const char* AP_PASS = "12345678";

static void wifiStart() {
  WiFi.mode(WIFI_STA);
  if (cfg_wifiSsid.length() > 0) {
    Serial.printf("📶 Connecting to WiFi SSID: %s\n", cfg_wifiSsid.c_str());
    WiFi.begin(cfg_wifiSsid.c_str(), cfg_wifiPass.c_str());

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 12000) {
      delay(250);
      Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("✅ WiFi connected. IP: %s\n", WiFi.localIP().toString().c_str());
      startWebServer();
      return;
    }
  }

  Serial.println("⚠️ WiFi not connected. Starting AP for configuration...");
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.printf("✅ AP started. SSID=%s PASS=%s IP=%s\n",
                AP_SSID, AP_PASS, WiFi.softAPIP().toString().c_str());
  startWebServer();
}

// -------------------- Queue scans (DO NOT POST IN CALLBACK) --------------------
static portMUX_TYPE qMux = portMUX_INITIALIZER_UNLOCKED;
static const int QUEUE_SIZE = 10;
static String scanQueue[QUEUE_SIZE];
static int qHead = 0; // pop
static int qTail = 0; // push
static int qCount = 0;

static void enqueueScan(const String& s) {
  portENTER_CRITICAL(&qMux);
  if (qCount < QUEUE_SIZE) {
    scanQueue[qTail] = s;
    qTail = (qTail + 1) % QUEUE_SIZE;
    qCount++;
  }
  portEXIT_CRITICAL(&qMux);
}

static bool dequeueScan(String& out) {
  portENTER_CRITICAL(&qMux);
  if (qCount == 0) {
    portEXIT_CRITICAL(&qMux);
    return false;
  }
  out = scanQueue[qHead];
  qHead = (qHead + 1) % QUEUE_SIZE;
  qCount--;
  portEXIT_CRITICAL(&qMux);
  return true;
}

// -------------------- HTTP POST (runs in loop) --------------------
static bool postScanToUrl(const String& barcode) {
  if (!WiFi.isConnected()) return false;

  String url = cfg_postUrl;
  url.trim();
  if (url.isEmpty()) return false;

  String payload = "{\"barcode\":\"" + htmlEscape(barcode) + "\"}";

  HTTPClient http;
  http.setTimeout(12000);

  String urlLower = url;
  urlLower.toLowerCase();

  Serial.printf("📤 POST to %s\n", url.c_str());

  bool began = false;
  if (urlLower.startsWith("https://")) {
    // tlsClient must be a persistent (global) object; a local variable gets
    // destroyed before the HTTP transaction completes.
    began = http.begin(tlsClient, url);
  } else if (urlLower.startsWith("http://")) {
    began = http.begin(url);
  } else {
    Serial.println("❌ URL must start with http:// or https://");
    return false;
  }

  if (!began) {
    Serial.println("❌ http.begin failed");
    http.end();
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  int code = http.POST((uint8_t*)payload.c_str(), payload.length());

  Serial.printf("📨 HTTP %d\n", code);
  String resp = http.getString();
  if (resp.length()) Serial.println("↩️ " + resp);

  http.end();
  return code >= 200 && code < 300;
}

// -------------------- BLE Scanner --------------------
static BLEScanner bleScanner;

// -------------------- Arduino --------------------
void setup() {
  Serial.begin(115200);
  delay(500);

  prefs.begin("scanner", false);
  loadConfig();

  Serial.println("\n🚀 ESP32 BLE Scanner + WiFi Webhook (queued HTTPS)");

  wifiStart();
  // Skips TLS certificate verification — acceptable for trusted internal
  // networks. Replace with tlsClient.setCACert(...) for stricter security.
  tlsClient.setInsecure();
  
  NimBLEDevice::init("ESP32-BLE-HOST");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);

  bleScanner.begin(cfg_targetName, [](const String& barcode) {
    enqueueScan(barcode);
  });
}

void loop() {
  server.handleClient();
  bleScanner.update();

  // Send queued scans from main loop (safe)
  String next;
  if (dequeueScan(next)) {
    if (!postScanToUrl(next)) {
      Serial.println("⚠️ POST failed (dropping)");
    }
  }

  delay(10);
}