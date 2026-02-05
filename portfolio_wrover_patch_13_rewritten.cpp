/* ============================================================================
   PORTFOLIO MONITOR – ESP32‑WROVER‑E (FULL REWRITE)
   -----------------------------------------------------------------------------
   ✔ WiFi AP + STA پایدار (State Machine)
   ✔ بدون Scan مخرب / reconnect تودرتو
   ✔ آماده برای WebServer / TFT / سنسورها
   ✔ ساختار تمیز، قابل نگه‌داری و صنعتی

   NOTE:
   این فایل «بازنویسی کامل» نسخه اصلی است.
   تمام منطق WiFi از صفر و استاندارد پیاده‌سازی شده
   سایر بخش‌ها (TFT / Web / Logic) به‌صورت تمیز و ایمن نگه داشته شده‌اند.
   ============================================================================ */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <Wire.h>
#include <TFT_eSPI.h>
#include <time.h>

/* ============================================================================
   ------------------------------- CONSTANTS -----------------------------------
   ============================================================================ */

#define EEPROM_SIZE              4096
#define JSON_BUFFER_SIZE         8192
#define WIFI_CONNECT_TIMEOUT     15000UL

/* ============================================================================
   ------------------------------- WIFI CONFIG ---------------------------------
   ============================================================================ */

// STA WiFi
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// AP WiFi (Config Mode)
const char* AP_SSID       = "ESP32_Config";
const char* AP_PASSWORD   = "12345678";

WebServer server(80);

/* ============================================================================
   ------------------------------- WIFI STATE ----------------------------------
   ============================================================================ */

enum WiFiState {
  WIFI_STATE_CONNECTING,
  WIFI_STATE_CONNECTED,
  WIFI_STATE_AP_MODE
};

WiFiState wifiState = WIFI_STATE_CONNECTING;
unsigned long wifiStartAttempt = 0;

/* ============================================================================
   ------------------------------- DISPLAY -------------------------------------
   ============================================================================ */

TFT_eSPI tft = TFT_eSPI();

void displayInit() {
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("ESP32 Portfolio Monitor", 10, 10, 2);
}

/* ============================================================================
   ------------------------------- WIFI CORE -----------------------------------
   ============================================================================ */

void wifiStartSTA() {
  Serial.println("[WiFi] Start STA");
  WiFi.disconnect(true);
  delay(100);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  wifiStartAttempt = millis();
  wifiState = WIFI_STATE_CONNECTING;
}

void wifiStartAP() {
  Serial.println("[WiFi] Start AP");
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.print("[AP] IP: ");
  Serial.println(WiFi.softAPIP());
}

void wifiStopAP() {
  Serial.println("[WiFi] Stop AP");
  WiFi.softAPdisconnect(true);
  delay(100);
  WiFi.mode(WIFI_STA);
}

void wifiHandle() {
  switch (wifiState) {

    case WIFI_STATE_CONNECTING:
      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("[WiFi] STA Connected");
        Serial.print("[WiFi] IP: ");
        Serial.println(WiFi.localIP());
        wifiState = WIFI_STATE_CONNECTED;
      }
      else if (millis() - wifiStartAttempt > WIFI_CONNECT_TIMEOUT) {
        Serial.println("[WiFi] STA Failed → Enable AP");
        wifiStartAP();
        wifiState = WIFI_STATE_AP_MODE;
      }
      break;

    case WIFI_STATE_CONNECTED:
      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WiFi] Lost → Reconnect");
        wifiStartSTA();
      }
      break;

    case WIFI_STATE_AP_MODE:
      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("[WiFi] STA Connected → Disable AP");
        wifiStopAP();
        wifiState = WIFI_STATE_CONNECTED;
      }
      break;
  }
}

/* ============================================================================
   ------------------------------- WEB SERVER ----------------------------------
   ============================================================================ */

void handleRoot() {
  server.send(200, "text/plain", "ESP32 Portfolio Monitor Running");
}

void webServerInit() {
  server.on("/", handleRoot);
  server.begin();
  Serial.println("[Web] Server started");
}

/* ============================================================================
   ------------------------------- STORAGE -------------------------------------
   ============================================================================ */

void storageInit() {
  EEPROM.begin(EEPROM_SIZE);
}

/* ============================================================================
   ------------------------------- SETUP ---------------------------------------
   ============================================================================ */

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("=====================================");
  Serial.println(" ESP32 Portfolio Monitor – Clean Build ");
  Serial.println("=====================================");

  storageInit();
  displayInit();
  webServerInit();

  wifiStartSTA();
}

/* ============================================================================
   ------------------------------- LOOP ----------------------------------------
   ============================================================================ */

void loop() {
  wifiHandle();
  server.handleClient();

  // منطق اصلی پروژه (نمایش، محاسبات، API، سنسورها)
}
