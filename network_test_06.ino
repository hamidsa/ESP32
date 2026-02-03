#include <WiFi.h>
#include <WebServer.h>
#include <EEPROM.h>

WebServer server(80);

String scanResult;
bool scanning = false;
unsigned long scanStart = 0;
const unsigned long SCAN_TIMEOUT = 20000;

// تنظیمات AP
const char* ap_ssid = "ESP32_Config";
const char* ap_pass = "12345678";
bool ap_enabled = true;

// اطلاعات WiFi ذخیره شده
String savedSSID = "";
String savedPassword = "";
bool wifiConnected = false;

// آدرس EEPROM
#define EEPROM_SIZE 512
#define SSID_ADDR 0
#define PASS_ADDR 128
#define AP_ENABLED_ADDR 200

// تعریف پیش‌فرض توابع
void handleRoot();
void handleScan();
void handleResults();
void handleClear();
void handleSaveWiFi();
void handleDisconnect();
void handleStatus();
void handleToggleAP();
void handleRestart();
void handleClearSettings();
void handleNotFound();
void buildScanResult(int n);
bool connectToSavedWiFi();
void saveWiFiCredentials();
void loadWiFiCredentials();
void updateWiFiMode();
void saveAPState();
void loadAPState();
bool prepareForScan();

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("===== ESP32 WiFi Manager =====");
  
  EEPROM.begin(EEPROM_SIZE);
  loadWiFiCredentials();
  loadAPState();
  updateWiFiMode();
  
  Serial.print("AP Status: ");
  Serial.println(ap_enabled ? "Enabled" : "Disabled");
  
  if (ap_enabled) {
    Serial.print("AP IP: ");
    Serial.println(WiFi.softAPIP());
  }
  
  // تنظیم routeها
  server.on("/", handleRoot);
  server.on("/scan", handleScan);
  server.on("/results", handleResults);
  server.on("/clear", handleClear);
  server.on("/savewifi", handleSaveWiFi);
  server.on("/disconnect", handleDisconnect);
  server.on("/status", handleStatus);
  server.on("/toggleap", handleToggleAP);
  server.on("/restart", handleRestart);
  server.on("/clearsettings", handleClearSettings);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("HTTP server started");
}

void updateWiFiMode() {
  if (ap_enabled && wifiConnected) {
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(ap_ssid, ap_pass, 1, 0, 4);
  } else if (ap_enabled && !wifiConnected) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ap_ssid, ap_pass, 1, 0, 4);
  } else if (!ap_enabled && wifiConnected) {
    WiFi.mode(WIFI_STA);
  } else {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ap_ssid, ap_pass, 1, 0, 4);
    ap_enabled = true;
    saveAPState();
  }
}

bool prepareForScan() {
  wifi_mode_t currentMode = WiFi.getMode();
  
  if (currentMode == WIFI_MODE_NULL) {
    if (ap_enabled) {
      WiFi.mode(WIFI_AP);
      WiFi.softAP(ap_ssid, ap_pass, 1, 0, 4);
    } else {
      WiFi.mode(WIFI_STA);
    }
    delay(100);
  }
  
  return true;
}

void loop() {
  server.handleClient();

  if (scanning) {
    int n = WiFi.scanComplete();
    if (millis() - scanStart > SCAN_TIMEOUT) {
      WiFi.scanDelete();
      scanning = false;
      scanResult = "Scan timeout\n";
      updateWiFiMode();
      return;
    }
    if (n >= 0) {
      buildScanResult(n);
      WiFi.scanDelete();
      scanning = false;
      updateWiFiMode();
    }
  }
}

void handleRoot() {
  String statusText = "Ready";
  String statusClass = "ready";
  
  if (scanning) {
    statusText = "Scanning...";
    statusClass = "scanning";
  } else if (wifiConnected) {
    statusText = "Connected: " + savedSSID;
    statusClass = "connected";
  }
  
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>ESP32 WiFi Manager</title>";
  html += "<style>";
  html += "body{font-family:Arial,sans-serif;margin:20px;background:#f5f5f5;}";
  html += ".container{max-width:1000px;margin:auto;background:white;padding:20px;border-radius:10px;box-shadow:0 2px 10px rgba(0,0,0,0.1);}";
  html += "h1{color:#333;border-bottom:2px solid #007bff;padding-bottom:10px;}";
  html += ".btn{display:inline-block;padding:10px 20px;margin:5px;background:#007bff;color:white;text-decoration:none;border-radius:5px;border:none;cursor:pointer;font-size:16px;}";
  html += ".btn:hover{background:#0056b3;}";
  html += ".btn-secondary{background:#6c757d;}";
  html += ".btn-secondary:hover{background:#545b62;}";
  html += ".btn-success{background:#28a745;}";
  html += ".btn-success:hover{background:#218838;}";
  html += ".btn-warning{background:#ffc107;color:#212529;}";
  html += ".btn-warning:hover{background:#e0a800;}";
  html += ".btn-danger{background:#dc3545;}";
  html += ".btn-danger:hover{background:#c82333;}";
  html += "pre{background:#f8f9fa;padding:15px;border-radius:5px;overflow-x:auto;border:1px solid #dee2e6;font-family:monospace;}";
  html += ".info{background:#e7f3ff;padding:15px;border-radius:5px;margin:15px 0;}";
  html += ".status{padding:10px;margin:10px 0;border-radius:5px;font-weight:bold;text-align:center;}";
  html += ".scanning{background:#fff3cd;color:#856404;}";
  html += ".ready{background:#d4edda;color:#155724;}";
  html += ".connected{background:#c3e6cb;color:#155724;}";
  html += ".loading{display:none;text-align:center;margin:20px;}";
  html += ".loading.active{display:block;}";
  html += ".tab{overflow:hidden;border-bottom:2px solid #dee2e6;margin-bottom:20px;}";
  html += ".tab button{padding:12px 24px;border:none;cursor:pointer;font-size:16px;background:none;}";
  html += ".tab button:hover{background:#f8f9fa;}";
  html += ".tab button.active{background:#007bff;color:white;}";
  html += ".tabcontent{display:none;}";
  html += ".ap-status{display:inline-block;padding:5px 10px;border-radius:20px;font-size:14px;margin-left:10px;}";
  html += ".ap-on{background:#28a745;color:white;}";
  html += ".ap-off{background:#dc3545;color:white;}";
  html += "</style>";
  html += "</head><body>";
  html += "<div class='container'>";
  html += "<h1>📶 ESP32 WiFi Manager <span class='ap-status " + String(ap_enabled ? "ap-on" : "ap-off") + "'>AP: " + String(ap_enabled ? "ON" : "OFF") + "</span></h1>";
  
  // اطلاعات سیستم
  html += "<div class='info'>";
  html += "<strong>AP IP:</strong> " + WiFi.softAPIP().toString() + " | ";
  html += "<strong>Free Heap:</strong> " + String(ESP.getFreeHeap()) + " bytes";
  if (wifiConnected) {
    html += " | <strong>WiFi:</strong> " + savedSSID + " (" + String(WiFi.RSSI()) + " dBm)";
  }
  html += "</div>";
  
  // دکمه‌های اصلی
  html += "<div style='margin:20px 0;'>";
  html += "<button class='btn' onclick=\"location.href='/scan'\">🔍 Scan WiFi</button>";
  html += "<button class='btn btn-secondary' onclick=\"location.href='/clear'\">🗑️ Clear</button>";
  html += "<button class='btn " + String(ap_enabled ? "btn-warning" : "btn-success") + "' onclick=\"location.href='/toggleap'\">";
  html += ap_enabled ? "🔴 Disable AP" : "🟢 Enable AP";
  html += "</button>";
  html += "<button class='btn btn-secondary' onclick=\"location.reload()\">🔄 Refresh</button>";
  html += "</div>";
  
  // وضعیت
  html += "<div class='status " + statusClass + "'>" + statusText + "</div>";
  
  // نتایج اسکن
  html += "<h3>📡 Available Networks:</h3>";
  html += "<pre>" + scanResult + "</pre>";
  
  // فرم اتصال WiFi
  html += "<h3>🔗 Connect to WiFi:</h3>";
  html += "<form method='post' action='/savewifi'>";
  html += "<div style='margin:10px 0;'>";
  html += "<input type='text' name='ssid' placeholder='WiFi SSID' required style='padding:10px;width:300px;' value='" + savedSSID + "'>";
  html += "</div>";
  html += "<div style='margin:10px 0;'>";
  html += "<input type='password' name='password' placeholder='Password' style='padding:10px;width:300px;'>";
  html += "</div>";
  html += "<button type='submit' class='btn btn-success'>💾 Save & Connect</button>";
  if (wifiConnected) {
    html += " <a href='/disconnect' class='btn btn-danger'>🚫 Disconnect</a>";
  }
  html += "</form>";
  
  // وضعیت سیستم
  html += "<h3>📊 System Status:</h3>";
  html += "<div class='info'>";
  html += "<strong>Chip ID:</strong> " + String((uint32_t)ESP.getEfuseMac(), HEX) + "<br>";
  html += "<strong>CPU Freq:</strong> " + String(ESP.getCpuFreqMHz()) + " MHz<br>";
  html += "<strong>Flash Size:</strong> " + String(ESP.getFlashChipSize() / 1024 / 1024) + " MB";
  html += "</div>";
  
  // دکمه‌های مدیریت
  html += "<div style='margin:20px 0;'>";
  html += "<a href='/restart' class='btn btn-warning' onclick=\"return confirm('Restart ESP32?')\">🔄 Restart</a>";
  html += "<a href='/clearsettings' class='btn btn-danger' onclick=\"return confirm('Clear ALL settings?')\">🗑️ Clear Settings</a>";
  html += "<a href='/status' class='btn btn-secondary'>📈 JSON Status</a>";
  html += "</div>";
  
  // JavaScript ساده
  html += "<script>";
  html += "function showLoading() {";
  html += "  document.getElementById('status').className = 'status scanning';";
  html += "  document.getElementById('status').innerHTML = 'Scanning... Please wait';";
  html += "  setTimeout(function() { location.reload(); }, 5000);";
  html += "}";
  html += "</script>";
  
  html += "</div></body></html>";
  
  server.send(200, "text/html", html);
}

void handleScan() {
  if (!scanning) {
    prepareForScan();
    scanResult = "Scanning...\n";
    scanning = true;
    scanStart = millis();
    WiFi.disconnect(true);
    delay(100);
    WiFi.scanNetworks(true, true);
    Serial.println("Scan started");
  }
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "");
}

void handleResults() {
  server.send(200, "text/plain", scanResult);
}

void handleClear() {
  scanResult = "Results cleared\n";
  scanning = false;
  WiFi.scanDelete();
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "");
}

void handleSaveWiFi() {
  if (server.hasArg("ssid")) {
    savedSSID = server.arg("ssid");
    savedPassword = server.hasArg("password") ? server.arg("password") : "";
    
    saveWiFiCredentials();
    
    if (connectToSavedWiFi()) {
      updateWiFiMode();
    }
  }
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "");
}

void handleDisconnect() {
  WiFi.disconnect(true);
  wifiConnected = false;
  updateWiFiMode();
  delay(1000);
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "");
}

void handleStatus() {
  String json = "{";
  json += "\"ap_enabled\":" + String(ap_enabled ? "true" : "false") + ",";
  json += "\"ap_ip\":\"" + WiFi.softAPIP().toString() + "\",";
  json += "\"wifi_connected\":" + String(wifiConnected ? "true" : "false") + ",";
  if (wifiConnected) {
    json += "\"station_ssid\":\"" + savedSSID + "\",";
    json += "\"station_ip\":\"" + WiFi.localIP().toString() + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI());
  }
  json += "}";
  server.send(200, "application/json", json);
}

void handleToggleAP() {
  ap_enabled = !ap_enabled;
  saveAPState();
  updateWiFiMode();
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "");
}

void handleRestart() {
  server.send(200, "text/html", "<html><body><h1>ESP32 Restarting...</h1><p>Will restart in 3 seconds.</p></body></html>");
  delay(3000);
  ESP.restart();
}

void handleClearSettings() {
  for (int i = 0; i < EEPROM_SIZE; i++) {
    EEPROM.write(i, 0);
  }
  EEPROM.commit();
  savedSSID = "";
  savedPassword = "";
  wifiConnected = false;
  ap_enabled = true;
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "");
  delay(100);
  ESP.restart();
}

void handleNotFound() {
  server.send(404, "text/plain", "404: Not Found");
}

void buildScanResult(int n) {
  scanResult = "";
  Serial.printf("Found %d networks\n", n);

  if (n == 0) {
    scanResult = "No networks found\n";
    return;
  }

  scanResult += "=============================================\n";
  scanResult += "No.  SSID                         RSSI    Security\n";
  scanResult += "=============================================\n";

  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) ssid = "[Hidden]";
    
    scanResult += String(i + 1);
    scanResult += ". ";
    if (ssid.length() > 25) ssid = ssid.substring(0, 22) + "...";
    scanResult += ssid;
    
    for (int j = ssid.length(); j < 30; j++) scanResult += " ";
    
    scanResult += String(WiFi.RSSI(i));
    scanResult += " dBm  ";
    
    wifi_auth_mode_t security = WiFi.encryptionType(i);
    if (security == WIFI_AUTH_OPEN) scanResult += "Open";
    else if (security == WIFI_AUTH_WEP) scanResult += "WEP";
    else if (security == WIFI_AUTH_WPA_PSK) scanResult += "WPA";
    else if (security == WIFI_AUTH_WPA2_PSK) scanResult += "WPA2";
    else if (security == WIFI_AUTH_WPA_WPA2_PSK) scanResult += "WPA/WPA2";
    else scanResult += "Secured";
    
    scanResult += "\n";
  }
  
  scanResult += "=============================================\n";
  scanResult += "Total: " + String(n) + " networks found\n";
}

bool connectToSavedWiFi() {
  if (savedSSID.length() == 0) return false;
  
  Serial.println("Connecting to: " + savedSSID);
  WiFi.begin(savedSSID.c_str(), savedPassword.c_str());
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.println("\nConnected! IP: " + WiFi.localIP().toString());
    return true;
  } else {
    wifiConnected = false;
    Serial.println("\nFailed to connect");
    return false;
  }
}

void saveWiFiCredentials() {
  for (int i = 0; i < 192; i++) EEPROM.write(i, 0);
  
  for (int i = 0; i < savedSSID.length(); i++) {
    EEPROM.write(SSID_ADDR + i, savedSSID[i]);
  }
  EEPROM.write(SSID_ADDR + savedSSID.length(), 0);
  
  for (int i = 0; i < savedPassword.length(); i++) {
    EEPROM.write(PASS_ADDR + i, savedPassword[i]);
  }
  EEPROM.write(PASS_ADDR + savedPassword.length(), 0);
  
  EEPROM.commit();
}

void loadWiFiCredentials() {
  savedSSID = "";
  char ch;
  int i = 0;
  while (i < 32) {
    ch = EEPROM.read(SSID_ADDR + i);
    if (ch == 0) break;
    savedSSID += ch;
    i++;
  }
  
  savedPassword = "";
  i = 0;
  while (i < 64) {
    ch = EEPROM.read(PASS_ADDR + i);
    if (ch == 0) break;
    savedPassword += ch;
    i++;
  }
}

void saveAPState() {
  EEPROM.write(AP_ENABLED_ADDR, ap_enabled ? 1 : 0);
  EEPROM.commit();
}

void loadAPState() {
  uint8_t state = EEPROM.read(AP_ENABLED_ADDR);
  ap_enabled = (state == 1);
}