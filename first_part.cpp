/* ============================================================================
   PORTFOLIO MONITOR - ESP32-WROVER-E (STABLE VERSION)
   Professional Dual Mode Portfolio Tracking System
   Version: 4.6.2 - Fixed Compilation Errors
   Hardware: ESP32-WROVER-E + ST7789 240x240 + Dual RGB LEDs + 4 Single LEDs
   Features: Stable WiFi AP+STA, Clean State Machine, No Destructive Scans
             High Resolution Display, Complete Web Interface
             Enhanced Volume Control, Battery Monitoring
   Author: AI Assistant
   Date: 2024
   ============================================================================ */

#include <SPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <time.h>
#include <Wire.h>

/* ============================================================================
   ------------------------------- CONSTANTS -----------------------------------
   ============================================================================ */

// TFT Configuration - Edit User_Setup.h in TFT_eSPI library:
/*
#define ST7789_DRIVER
#define TFT_WIDTH  240
#define TFT_HEIGHT 240
#define TFT_MOSI 23
#define TFT_SCLK 18
#define TFT_CS   15
#define TFT_DC   2
#define TFT_RST  4
#define TFT_BL   5
*/

// ===== PIN DEFINITIONS =====
// RGB LEDs (Common Cathode)
#define RGB1_RED    32
#define RGB1_GREEN  33
#define RGB1_BLUE   25

#define RGB2_RED    26
#define RGB2_GREEN  14
#define RGB2_BLUE   12

// Single Color LEDs
#define LED_MODE1_GREEN   27
#define LED_MODE1_RED     13
#define LED_MODE2_GREEN   21
#define LED_MODE2_RED     19

// Buzzer
#define BUZZER_PIN        22
#define RESET_BUTTON_PIN  0
#define TFT_BL_PIN        5
#define BATTERY_PIN       34

// ===== SYSTEM CONSTANTS =====
#define MAX_ALERT_HISTORY 50
#define MAX_POSITIONS_PER_MODE 100
#define MAX_WIFI_NETWORKS 5
#define EEPROM_SIZE 4096
#define JSON_BUFFER_SIZE 8192

#define SETTINGS_EEPROM_ADDR 0
#define AP_STATE_EEPROM_ADDR 1024

#define DATA_UPDATE_INTERVAL 15000
#define WIFI_CONNECT_TIMEOUT 20000
#define WIFI_CHECK_INTERVAL 10000
#define DISPLAY_UPDATE_INTERVAL 2000
#define ALERT_DISPLAY_TIME 8000
#define BATTERY_CHECK_INTERVAL 60000
#define DEBOUNCE_DELAY 50

#define DEFAULT_ALERT_THRESHOLD -5.0
#define DEFAULT_SEVERE_THRESHOLD -10.0
#define PORTFOLIO_ALERT_THRESHOLD -7.0
#define DEFAULT_VOLUME 50
#define DEFAULT_LED_BRIGHTNESS 100

#define BATTERY_FULL 8.4
#define BATTERY_EMPTY 6.6
#define BATTERY_WARNING 20

#define POWER_SOURCE_USB 0
#define POWER_SOURCE_BATTERY 1
#define POWER_SOURCE_EXTERNAL 2

// ===== NTP CONFIG =====
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 12600;     // 3.5 hours for Iran
const int   daylightOffset_sec = 0;

/* ============================================================================
   ------------------------------- STRUCTURES ----------------------------------
   ============================================================================ */

typedef struct {
    char ssid[32];
    char password[64];
    bool configured;
    unsigned long lastConnected;
    int connectionAttempts;
    byte priority;      // 1-10 (higher = higher priority)
    int rssi;           // Last signal strength
    bool autoConnect;   // Auto connect
} WiFiNetwork;

typedef struct {
    char symbol[16];
    float changePercent;
    float pnlValue;
    float quantity;
    float entryPrice;
    float currentPrice;
    bool isLong;
    bool alerted;
    bool severeAlerted;
    unsigned long lastAlertTime;
    float lastAlertPrice;
    float alertThreshold;
    float severeThreshold;
    
    bool exitAlerted;
    float exitAlertLastPrice;
    unsigned long exitAlertTime;
    bool hasAlerted;
    float lastAlertPercent;
} CryptoPosition;

typedef struct {
    float totalInvestment;
    float totalCurrentValue;
    float totalPnl;
    float totalPnlPercent;
    int totalPositions;
    int longPositions;
    int shortPositions;
    int winningPositions;
    int losingPositions;
    float maxDrawdown;
} PortfolioSummary;

typedef struct {
    char symbol[16];
    unsigned long alertTime;
    float pnlPercent;
    float alertPrice;
    bool isLong;
    bool isSevere;
    bool isProfit;
    byte alertType;
    char message[64];
    bool acknowledged;
    char timeString[20];
    byte alertMode;
} AlertHistory;

typedef struct {
    WiFiNetwork networks[MAX_WIFI_NETWORKS];
    int networkCount;
    int lastConnectedIndex;
    
    char server[128];
    char username[32];
    char userpass[64];
    char entryPortfolio[32];
    char exitPortfolio[32];
    
    float alertThreshold;
    float severeAlertThreshold;
    float portfolioAlertThreshold;
    int buzzerVolume;
    bool buzzerEnabled;
    bool separateLongShortAlerts;
    bool autoResetAlerts;
    int alertCooldown;
    
    int displayBrightness;
    int displayTimeout;
    bool showDetails;
    bool invertDisplay;
    byte displayRotation;
    
    float exitAlertPercent;
    bool exitAlertEnabled;
    bool exitAlertBlinkEnabled;
    
    int ledBrightness;
    bool ledEnabled;
    
    bool rgb1Enabled;
    bool rgb2Enabled;
    int rgb1Brightness;
    int rgb2Brightness;
    int rgb1HistorySpeed;
    int rgb2Sensitivity;
    
    bool showBattery;
    int batteryWarningLevel;
    
    bool autoReconnect;
    int reconnectAttempts;
    
    byte magicNumber;
    bool configured;
    unsigned long firstBoot;
    int bootCount;
    unsigned long totalUptime;
} SystemSettings;

/* ============================================================================
   ------------------------------- GLOBALS -------------------------------------
   ============================================================================ */

// Global Objects
TFT_eSPI tft = TFT_eSPI();
WebServer server(80);
HTTPClient http;

// System Settings
SystemSettings settings;
byte powerSource = POWER_SOURCE_USB;

// Mode Data
CryptoPosition cryptoDataMode1[MAX_POSITIONS_PER_MODE];
PortfolioSummary portfolioMode1;
AlertHistory alertHistoryMode1[MAX_ALERT_HISTORY];
int cryptoCountMode1 = 0;
int alertHistoryCountMode1 = 0;

CryptoPosition cryptoDataMode2[MAX_POSITIONS_PER_MODE];
PortfolioSummary portfolioMode2;
AlertHistory alertHistoryMode2[MAX_ALERT_HISTORY];
int cryptoCountMode2 = 0;
int alertHistoryCountMode2 = 0;

// WiFi State Machine
enum WiFiState {
    WIFI_STATE_DISCONNECTED,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_AP_MODE,
    WIFI_STATE_AP_STA_MODE
};

WiFiState wifiState = WIFI_STATE_DISCONNECTED;
unsigned long wifiStateTime = 0;
unsigned long lastWifiCheck = 0;
bool apEnabled = true;
bool isConnectedToWiFi = false;
bool apModeActive = false;
bool connectionLost = false;
unsigned long connectionLostTime = 0;

// System State
bool showingAlert = false;
bool displayInitialized = false;
bool timeSynced = false;
String currentDateTime = "";
bool displayNeedsUpdate = true;

// Alert State
String alertTitle = "";
String alertMessage = "";
String alertSymbol = "";
float alertPrice = 0.0;
bool alertIsLong = false;
bool alertIsSevere = false;
byte alertMode = 0;
unsigned long alertStartTime = 0;

// LED States
bool mode1GreenActive = false;
bool mode1RedActive = false;
bool mode2GreenActive = false;
bool mode2RedActive = false;
bool blinkState = false;
unsigned long ledTimeout = 0;

String mode1AlertSymbol = "";
String mode2AlertSymbol = "";
float mode1AlertPercent = 0.0;
float mode2AlertPercent = 0.0;

// RGB States
float rgb2CurrentPercent = 0.0;
bool rgb2AlertActive = false;

// Timing Variables
unsigned long lastDataUpdate = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long lastAlertCheck = 0;
unsigned long lastBatteryCheck = 0;
unsigned long lastReconnectAttempt = 0;
unsigned long systemStartTime = 0;

// Battery
float batteryVoltage = 0.0;
int batteryPercent = 100;
bool batteryLow = false;

// Statistics
int apiSuccessCount = 0;
int apiErrorCount = 0;
unsigned long lastApiCallTime = 0;
float apiAverageResponseTime = 0.0;
int connectionLostCount = 0;
int reconnectSuccessCount = 0;

/* ============================================================================
   ------------------------------- WIFI STATE MACHINE --------------------------
   ============================================================================ */

void wifiStateMachine() {
    unsigned long now = millis();
    
    if (now - lastWifiCheck < WIFI_CHECK_INTERVAL) {
        return;
    }
    lastWifiCheck = now;
    
    switch (wifiState) {
        case WIFI_STATE_DISCONNECTED:
            if (settings.networkCount > 0) {
                wifiState = WIFI_STATE_CONNECTING;
                wifiStateTime = now;
                
                // Start STA mode
                Serial.println("[WiFi] Starting STA mode");
                WiFi.disconnect(true);
                delay(100);
                WiFi.mode(WIFI_STA);
                WiFi.setSleep(false);
                
                // Connect to best network
                int bestIndex = -1;
                byte bestPriority = 0;
                
                for (int i = 0; i < settings.networkCount; i++) {
                    if (settings.networks[i].autoConnect && 
                        settings.networks[i].priority > bestPriority) {
                        bestPriority = settings.networks[i].priority;
                        bestIndex = i;
                    }
                }
                
                if (bestIndex >= 0) {
                    WiFi.begin(settings.networks[bestIndex].ssid, 
                              settings.networks[bestIndex].password);
                    settings.lastConnectedIndex = bestIndex;
                    Serial.println("[WiFi] Connecting to: " + String(settings.networks[bestIndex].ssid));
                }
            } else if (apEnabled) {
                wifiState = WIFI_STATE_AP_MODE;
                wifiStartAP();
            }
            break;
            
        case WIFI_STATE_CONNECTING:
            if (WiFi.status() == WL_CONNECTED) {
                Serial.println("[WiFi] STA Connected");
                Serial.print("[WiFi] IP: ");
                Serial.println(WiFi.localIP());
                
                isConnectedToWiFi = true;
                connectionLost = false;
                
                if (settings.lastConnectedIndex >= 0) {
                    settings.networks[settings.lastConnectedIndex].lastConnected = now;
                    settings.networks[settings.lastConnectedIndex].connectionAttempts++;
                    settings.networks[settings.lastConnectedIndex].rssi = WiFi.RSSI();
                }
                
                if (apEnabled) {
                    wifiState = WIFI_STATE_AP_STA_MODE;
                    wifiStartAPSTA();
                } else {
                    wifiState = WIFI_STATE_CONNECTED;
                    wifiStopAP();
                }
                
                // Sync time
                if (syncTime()) {
                    timeSynced = true;
                }
                
                // Save settings
                saveSettings();
            } 
            else if (now - wifiStateTime > WIFI_CONNECT_TIMEOUT) {
                Serial.println("[WiFi] STA Timeout");
                if (apEnabled) {
                    wifiState = WIFI_STATE_AP_MODE;
                    wifiStartAP();
                } else {
                    wifiState = WIFI_STATE_DISCONNECTED;
                }
            }
            break;
            
        case WIFI_STATE_CONNECTED:
            if (WiFi.status() != WL_CONNECTED) {
                Serial.println("[WiFi] Connection Lost");
                isConnectedToWiFi = false;
                connectionLost = true;
                connectionLostTime = now;
                connectionLostCount++;
                wifiState = WIFI_STATE_DISCONNECTED;
            }
            break;
            
        case WIFI_STATE_AP_MODE:
            // Stay in AP mode until user changes via web
            break;
            
        case WIFI_STATE_AP_STA_MODE:
            if (WiFi.status() != WL_CONNECTED) {
                Serial.println("[WiFi] STA Lost in AP+STA");
                isConnectedToWiFi = false;
                connectionLost = true;
                connectionLostTime = now;
                connectionLostCount++;
                // Stay in AP mode
            }
            break;
    }
}

void wifiStartAP() {
    Serial.println("[WiFi] Starting AP mode");
    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(WIFI_AP);
    
    String apSSID = "PortfolioMonitor_" + String((uint32_t)ESP.getEfuseMac(), HEX);
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1),
                     IPAddress(192, 168, 4, 1),
                     IPAddress(255, 255, 255, 0));
    
    bool apStarted = WiFi.softAP(apSSID.c_str(), "12345678", 1, 0, 4);
    
    if (apStarted) {
        apModeActive = true;
        Serial.print("[AP] IP: ");
        Serial.println(WiFi.softAPIP());
    } else {
        Serial.println("[AP] Failed to start");
        apModeActive = false;
    }
}

void wifiStartAPSTA() {
    Serial.println("[WiFi] Starting AP+STA mode");
    WiFi.mode(WIFI_AP_STA);
    
    String apSSID = "PortfolioMonitor_" + String((uint32_t)ESP.getEfuseMac(), HEX);
    WiFi.softAP(apSSID.c_str(), "12345678", 1, 0, 4);
    
    apModeActive = true;
    Serial.print("[AP+STA] AP IP: ");
    Serial.println(WiFi.softAPIP());
}

void wifiStopAP() {
    if (apModeActive) {
        Serial.println("[WiFi] Stopping AP");
        WiFi.softAPdisconnect(true);
        apModeActive = false;
    }
}

/* ============================================================================
   ------------------------------- DISPLAY FUNCTIONS ---------------------------
   ============================================================================ */

void setupDisplay() {
    Serial.println("[Display] Initializing ST7789 240x240 IPS Display...");
    
    pinMode(TFT_BL_PIN, OUTPUT);
    digitalWrite(TFT_BL_PIN, HIGH);
    delay(100);
    
    tft.init();
    tft.setRotation(settings.displayRotation);
    tft.fillScreen(TFT_BLACK);
    
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(1);
    tft.setTextWrap(false);
    
    displayInitialized = true;
    
    showSplashScreen();
}

void showSplashScreen() {
    tft.fillScreen(TFT_BLACK);
    
    tft.drawRect(0, 0, 239, 239, TFT_CYAN);
    tft.drawRect(1, 1, 237, 237, TFT_BLUE);
    
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(20, 40);
    tft.println("PORTFOLIO");
    tft.setCursor(30, 70);
    tft.println("MONITOR");
    
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(40, 100);
    tft.println("Stable v4.6.2");
    
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setCursor(20, 130);
    tft.println("ESP32-WROVER-E");
    
    for (int i = 0; i < 240; i += 10) {
        tft.drawFastHLine(20, 180, i, TFT_BLUE);
        delay(10);
    }
    
    delay(1500);
}

void updateDisplay() {
    if (!displayInitialized) return;

    static unsigned long lastUpdate = 0;
    unsigned long now = millis();
    
    // Auto-close alert after timeout
    if (showingAlert && now - alertStartTime > ALERT_DISPLAY_TIME) {
        showingAlert = false;
        alertTitle = "";
        alertMessage = "";
        showMainDisplay();
        return;
    }
    
    if (showingAlert) {
        showAlertDisplay();
        return;
    }
    
    // Control backlight based on timeout
    if (settings.displayTimeout > 0) {
        static unsigned long lastInteraction = millis();
        
        // Reset interaction time if alert was recently active
        if (now - alertStartTime < 10000) {
            lastInteraction = now;
        }
        
        if (now - lastInteraction > settings.displayTimeout) {
            digitalWrite(TFT_BL_PIN, LOW);
            return;
        } else {
            if (settings.displayBrightness > 0) {
                digitalWrite(TFT_BL_PIN, HIGH);
            }
        }
    }
    
    if (now - lastUpdate < DISPLAY_UPDATE_INTERVAL) return;
    lastUpdate = now;
    
    if (connectionLost && settings.showDetails) {
        showConnectionLostScreen();
    } else {
        showMainDisplay();
    }
}

void showMainDisplay() {
    // Control backlight
    if (settings.displayBrightness > 0) {
        digitalWrite(TFT_BL_PIN, HIGH);
    } else {
        digitalWrite(TFT_BL_PIN, LOW);
        return;
    }
    
    tft.fillScreen(TFT_BLACK);
    
    // Header
    tft.setTextSize(2);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setCursor(5, 5);
    tft.print("PORTFOLIO");
    
    // WiFi Status
    tft.setTextSize(1);
    tft.setCursor(5, 35);
    tft.print("WiFi:");
    
    if (isConnectedToWiFi) {
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        String ssid = WiFi.SSID();
        if (ssid.length() > 12) {
            ssid = ssid.substring(0, 12) + "...";
        }
        tft.setCursor(35, 35);
        tft.print(ssid);
    } else if (apModeActive) {
        tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        tft.setCursor(35, 35);
        tft.print("AP Mode");
    } else {
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.setCursor(35, 35);
        tft.print("No WiFi");
    }
    
    // Time
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(5, 55);
    tft.print("Time:");
    
    if (currentDateTime.length() > 10) {
        tft.setCursor(35, 55);
        tft.print(currentDateTime.substring(11, 19));
    } else {
        tft.setCursor(35, 55);
        tft.print("--:--:--");
    }
    
    // Separator
    tft.drawFastHLine(0, 75, 240, TFT_DARKGREY);
    
    // Entry Mode
    tft.setTextSize(1);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setCursor(5, 90);
    tft.print("ENTRY:");
    
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(60, 90);
    tft.print(String(cryptoCountMode1) + " pos");
    
    tft.setTextColor(portfolioMode1.totalPnlPercent >= 0 ? TFT_GREEN : TFT_RED, TFT_BLACK);
    tft.setCursor(120, 90);
    if (cryptoCountMode1 > 0) {
        tft.print(formatPercent(portfolioMode1.totalPnlPercent));
    } else {
        tft.print("0.00%");
    }
    
    // Exit Mode
    tft.setTextColor(TFT_ORANGE, TFT_BLACK);
    tft.setCursor(5, 110);
    tft.print("EXIT:");
    
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(60, 110);
    tft.print(String(cryptoCountMode2) + " pos");
    
    tft.setTextColor(portfolioMode2.totalPnlPercent >= 0 ? TFT_GREEN : TFT_RED, TFT_BLACK);
    tft.setCursor(120, 110);
    if (cryptoCountMode2 > 0) {
        tft.print(formatPercent(portfolioMode2.totalPnlPercent));
    } else {
        tft.print("0.00%");
    }
    
    // Separator
    tft.drawFastHLine(0, 130, 240, TFT_DARKGREY);
    
    // Total
    float totalValue = portfolioMode1.totalCurrentValue + portfolioMode2.totalCurrentValue;
    float totalInvestment = portfolioMode1.totalInvestment + portfolioMode2.totalInvestment;
    float totalPnlPercent = 0;
    
    if (totalInvestment > 0) {
        totalPnlPercent = ((totalValue - totalInvestment) / totalInvestment) * 100;
    }
    
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setCursor(5, 145);
    tft.print("TOTAL:");
    
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setCursor(60, 145);
    tft.print("$");
    tft.print(formatNumber(totalValue));
    
    tft.setTextColor(totalPnlPercent >= 0 ? TFT_GREEN : TFT_RED, TFT_BLACK);
    tft.setCursor(150, 145);
    tft.print(formatPercent(totalPnlPercent));
    
    // Status Bar
    tft.drawFastHLine(0, 170, 240, TFT_DARKGREY);
    
    tft.setTextSize(1);
    
    // Alert Status
    if (mode1GreenActive || mode1RedActive || mode2GreenActive || mode2RedActive) {
        tft.setTextColor(TFT_ORANGE, TFT_BLACK);
        tft.setCursor(5, 185);
        tft.print("ALERT!");
    } else if (connectionLost) {
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.setCursor(5, 185);
        tft.print("NO CONN");
    } else {
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.setCursor(5, 185);
        tft.print("READY");
    }
    
    // Power Source
    if (powerSource == POWER_SOURCE_USB) {
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        tft.setCursor(60, 185);
        tft.print("USB");
    } else if (settings.showBattery) {
        drawBatteryIcon(60, 185, batteryPercent);
    }
    
    // Volume
    tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
    tft.setCursor(120, 185);
    tft.print("Vol:");
    tft.print(settings.buzzerVolume);
    tft.print("%");
    
    // Connection Type
    if (apModeActive) {
        tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        tft.setCursor(180, 185);
        tft.print("AP");
    } else if (isConnectedToWiFi) {
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.setCursor(180, 185);
        tft.print("WiFi");
    } else {
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.setCursor(180, 185);
        tft.print("OFF");
    }
}

void showAlertDisplay() {
    tft.fillScreen(TFT_BLACK);
    
    uint32_t bgColor = alertIsSevere ? TFT_MAROON : tft.color565(0, 100, 0);
    
    tft.fillRect(0, 0, 240, 50, bgColor);
    
    tft.setTextColor(TFT_WHITE, bgColor);
    tft.setTextSize(3);
    tft.setCursor(20, 10);
    tft.print(alertTitle);
    
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setTextSize(4);
    tft.setCursor(30, 70);
    tft.print(alertSymbol);
    
    tft.setTextSize(3);
    tft.setCursor(30, 120);
    tft.print("$");
    tft.print(formatPrice(alertPrice));
    
    tft.setTextSize(2);
    tft.setCursor(30, 160);
    tft.print(alertMessage);
    
    int timeLeft = (ALERT_DISPLAY_TIME - (millis() - alertStartTime)) / 1000;
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(30, 200);
    tft.print("Auto-close: ");
    tft.print(timeLeft);
    tft.print("s");
}

void showConnectionLostScreen() {
    tft.fillScreen(TFT_BLACK);
    
    tft.drawRect(0, 0, 239, 239, TFT_RED);
    tft.drawRect(1, 1, 237, 237, TFT_MAROON);
    
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setTextSize(3);
    tft.setCursor(40, 50);
    tft.println("WiFi");
    tft.setCursor(30, 90);
    tft.println("LOST");
    
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(20, 140);
    tft.println("CONNECTION");
    tft.setCursor(60, 170);
    tft.println("LOST");
    
    unsigned long lostTime = (millis() - connectionLostTime) / 1000;
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(50, 210);
    tft.print("Time: ");
    tft.print(lostTime);
    tft.print("s");
}

void showDisplayMessage(String line1, String line2, String line3, String line4) {
    digitalWrite(TFT_BL_PIN, HIGH);
    
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(20, 30);
    tft.print(line1);
    tft.setCursor(20, 70);
    tft.print(line2);
    tft.setTextSize(1);
    tft.setCursor(20, 110);
    tft.print(line3);
    tft.setCursor(20, 130);
    tft.print(line4);
}

void drawBatteryIcon(int x, int y, int percent) {
    if (!settings.showBattery) {
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        tft.setTextSize(1);
        tft.setCursor(x, y);
        tft.print("USB");
        return;
    }
    
    tft.drawRect(x, y, 30, 15, TFT_WHITE);
    tft.drawRect(x + 30, y + 4, 3, 7, TFT_WHITE);
    
    int fillWidth = (28 * percent) / 100;
    fillWidth = constrain(fillWidth, 0, 28);
    
    uint32_t fillColor;
    if (percent > 50) fillColor = TFT_GREEN;
    else if (percent > 20) fillColor = TFT_YELLOW;
    else fillColor = TFT_RED;
    
    if (fillWidth > 0) {
        tft.fillRect(x + 1, y + 1, fillWidth, 13, fillColor);
    }
    
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(x + 35, y + 4);
    tft.print(String(percent) + "%");
}

void setDisplayBrightness(int brightness) {
    settings.displayBrightness = constrain(brightness, 0, 100);
    
    if (settings.displayBrightness > 0) {
        digitalWrite(TFT_BL_PIN, HIGH);
    } else {
        digitalWrite(TFT_BL_PIN, LOW);
    }
    
    saveSettings();
    Serial.println("[Display] Brightness: " + String(settings.displayBrightness) + "%");
}

/* ============================================================================
   ------------------------------- BUZZER FUNCTIONS ----------------------------
   ============================================================================ */

void setupBuzzer() {
    Serial.println("[Buzzer] Initializing...");
    
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
    
    // Test buzzer
    if (settings.buzzerEnabled && settings.buzzerVolume > 0) {
        playVolumeFeedback();
    }
    
    Serial.println("[Buzzer] Initialized");
}

void setBuzzerVolume(int volume) {
    settings.buzzerVolume = constrain(volume, 0, 100);
    Serial.print("[Buzzer] Volume: ");
    Serial.print(settings.buzzerVolume);
    Serial.println("%");
    
    playVolumeFeedback();
    saveSettings();
}

void playTone(int frequency, int durationMs) {
    if (!settings.buzzerEnabled || settings.buzzerVolume == 0) {
        return;
    }
    
    int actualDuration = map(settings.buzzerVolume, 0, 100, 0, durationMs);
    
    if (actualDuration <= 0) {
        return;
    }
    
    if (settings.buzzerVolume < 30) {
        int pulseCount = actualDuration / 30;
        int pulseDuration = 20;
        int pauseDuration = 30 - pulseDuration;
        
        for (int i = 0; i < pulseCount; i++) {
            tone(BUZZER_PIN, frequency, pulseDuration);
            delay(pauseDuration);
        }
    } else if (settings.buzzerVolume < 70) {
        tone(BUZZER_PIN, frequency, actualDuration);
        delay(actualDuration + 10);
    } else {
        tone(BUZZER_PIN, frequency, durationMs);
        delay(durationMs + 10);
    }
}

void playVolumeFeedback() {
    if (!settings.buzzerEnabled) return;
    
    int freq = map(settings.buzzerVolume, 0, 100, 300, 1500);
    int duration = map(settings.buzzerVolume, 0, 100, 50, 200);
    
    tone(BUZZER_PIN, freq, duration);
    delay(duration + 20);
}

void playLongPositionAlert(bool isSevere) {
    if (!settings.buzzerEnabled || settings.buzzerVolume == 0) return;
    
    Serial.println("[Buzzer] LONG alert" + String(isSevere ? " (SEVERE)" : ""));
    
    if (isSevere) {
        playTone(440, 200);
        delay(250);
        playTone(349, 250);
        delay(300);
    } else {
        playTone(523, 300);
        delay(350);
    }
}

void playShortPositionAlert(bool isSevere) {
    if (!settings.buzzerEnabled || settings.buzzerVolume == 0) return;
    
    Serial.println("[Buzzer] SHORT alert" + String(isSevere ? " (SEVERE)" : ""));
    
    if (isSevere) {
        for (int i = 0; i < 3; i++) {
            playTone(784, 100);
            delay(120);
        }
    } else {
        playTone(659, 250);
        delay(300);
    }
}

void playExitAlertTone(bool isProfit) {
    if (!settings.buzzerEnabled) return;
    
    Serial.println("[Buzzer] EXIT alert for " + String(isProfit ? "PROFIT" : "LOSS"));
    
    if (isProfit) {
        playTone(1047, 200);
        delay(250);
        playTone(1319, 250);
        delay(300);
    } else {
        playTone(349, 300);
        delay(350);
    }
}

void playResetAlertTone() {
    if (!settings.buzzerEnabled) return;
    
    Serial.println("[Buzzer] Reset tone");
    
    playTone(262, 100);
    delay(120);
    playTone(294, 100);
    delay(120);
    playTone(330, 150);
    delay(200);
}

void playSuccessTone() {
    if (!settings.buzzerEnabled) return;
    
    playTone(523, 150);
    delay(200);
    playTone(659, 200);
    delay(250);
}

void playErrorTone() {
    if (!settings.buzzerEnabled) return;
    
    playTone(349, 200);
    delay(250);
    playTone(294, 250);
    delay(300);
}

void playConnectionLostTone() {
    if (!settings.buzzerEnabled) return;
    
    Serial.println("[Buzzer] Connection lost tone");
    
    for (int i = 0; i < 2; i++) {
        playTone(392, 300);
        delay(350);
        playTone(392, 300);
        delay(350);
    }
}

void playStartupTone() {
    if (!settings.buzzerEnabled) return;
    
    playTone(600, 100);
    delay(150);
    playTone(800, 150);
    delay(200);
    playTone(1000, 200);
    delay(250);
}

void testVolumeRange() {
    Serial.println("\n[Buzzer] Testing volume range (0-100%):");
    
    int originalVolume = settings.buzzerVolume;
    
    for (int vol = 0; vol <= 100; vol += 10) {
        settings.buzzerVolume = vol;
        Serial.print("Volume: ");
        Serial.print(vol);
        Serial.print("% | ");
        
        if (vol > 0) {
            playTone(440, 100);
            delay(150);
            playTone(523, 100);
            delay(150);
            playTone(659, 100);
        }
        
        delay(300);
        Serial.println("OK");
    }
    
    settings.buzzerVolume = originalVolume;
}

/* ============================================================================
   ------------------------------- LED FUNCTIONS -------------------------------
   ============================================================================ */

void setupLEDs() {
    Serial.println("[LEDs] Initializing...");
    
    pinMode(LED_MODE1_GREEN, OUTPUT);
    pinMode(LED_MODE1_RED, OUTPUT);
    pinMode(LED_MODE2_GREEN, OUTPUT);
    pinMode(LED_MODE2_RED, OUTPUT);
    
    digitalWrite(LED_MODE1_GREEN, LOW);
    digitalWrite(LED_MODE1_RED, LOW);
    digitalWrite(LED_MODE2_GREEN, LOW);
    digitalWrite(LED_MODE2_RED, LOW);
    
    Serial.println("[LEDs] Initialized");
}

void setupRGBLEDs() {
    Serial.println("[RGB] Initializing...");
    
    pinMode(RGB1_RED, OUTPUT);
    pinMode(RGB1_GREEN, OUTPUT);
    pinMode(RGB1_BLUE, OUTPUT);
    pinMode(RGB2_RED, OUTPUT);
    pinMode(RGB2_GREEN, OUTPUT);
    pinMode(RGB2_BLUE, OUTPUT);
    
    // Turn off LEDs
    digitalWrite(RGB1_RED, LOW);
    digitalWrite(RGB1_GREEN, LOW);
    digitalWrite(RGB1_BLUE, LOW);
    digitalWrite(RGB2_RED, LOW);
    digitalWrite(RGB2_GREEN, LOW);
    digitalWrite(RGB2_BLUE, LOW);
    
    Serial.println("[RGB] Initialized");
}

void updateLEDs() {
    if (ledTimeout > 0 && millis() > ledTimeout) {
        mode1GreenActive = false;
        mode1RedActive = false;
        mode2GreenActive = false;
        mode2RedActive = false;
        ledTimeout = 0;
    }
    
    static unsigned long lastBlinkUpdate = 0;
    unsigned long now = millis();
    
    if (now - lastBlinkUpdate > 500) {
        lastBlinkUpdate = now;
        blinkState = !blinkState;
    }
    
    if (mode1GreenActive) {
        digitalWrite(LED_MODE1_GREEN, blinkState ? HIGH : LOW);
    } else {
        digitalWrite(LED_MODE1_GREEN, LOW);
    }
    
    if (mode1RedActive) {
        digitalWrite(LED_MODE1_RED, blinkState ? HIGH : LOW);
    } else {
        digitalWrite(LED_MODE1_RED, LOW);
    }
    
    if (mode2GreenActive) {
        digitalWrite(LED_MODE2_GREEN, blinkState ? HIGH : LOW);
    } else {
        digitalWrite(LED_MODE2_GREEN, LOW);
    }
    
    if (mode2RedActive) {
        digitalWrite(LED_MODE2_RED, blinkState ? HIGH : LOW);
    } else {
        digitalWrite(LED_MODE2_RED, LOW);
    }
}

void updateRGBLEDs() {
    if (!settings.rgb1Enabled && !settings.rgb2Enabled) return;
    
    // RGB1: System Status - Use analogWrite for PWM
    if (settings.rgb1Enabled) {
        if (isConnectedToWiFi) {
            // Green for WiFi connected
            analogWrite(RGB1_RED, 0);
            analogWrite(RGB1_GREEN, map(settings.rgb1Brightness, 0, 100, 0, 255));
            analogWrite(RGB1_BLUE, 0);
        } else if (apModeActive) {
            // Blue for AP mode
            analogWrite(RGB1_RED, 0);
            analogWrite(RGB1_GREEN, 0);
            analogWrite(RGB1_BLUE, map(settings.rgb1Brightness, 0, 100, 0, 255));
        } else {
            // Red for disconnected
            analogWrite(RGB1_RED, map(settings.rgb1Brightness, 0, 100, 0, 255));
            analogWrite(RGB1_GREEN, 0);
            analogWrite(RGB1_BLUE, 0);
        }
    }
    
    // RGB2: Portfolio Status
    if (settings.rgb2Enabled && cryptoCountMode1 > 0) {
        // Calculate average P/L
        float avgPercent = 0;
        for (int i = 0; i < cryptoCountMode1; i++) {
            avgPercent += cryptoDataMode1[i].changePercent;
        }
        avgPercent /= cryptoCountMode1;
        
        // Set color based on performance
        int brightness = map(settings.rgb2Brightness, 0, 100, 0, 255);
        
        if (avgPercent >= 5.0) {
            // Bright green for high profit
            analogWrite(RGB2_RED, 0);
            analogWrite(RGB2_GREEN, brightness);
            analogWrite(RGB2_BLUE, 0);
        } else if (avgPercent >= 0) {
            // Green for profit
            analogWrite(RGB2_RED, 0);
            analogWrite(RGB2_GREEN, brightness * 0.7);
            analogWrite(RGB2_BLUE, 0);
        } else if (avgPercent >= -5.0) {
            // Yellow for small loss
            analogWrite(RGB2_RED, brightness);
            analogWrite(RGB2_GREEN, brightness * 0.7);
            analogWrite(RGB2_BLUE, 0);
        } else {
            // Red for big loss
            analogWrite(RGB2_RED, brightness);
            analogWrite(RGB2_GREEN, 0);
            analogWrite(RGB2_BLUE, 0);
        }
    }
}

void turnOffRGB1() {
    digitalWrite(RGB1_RED, LOW);
    digitalWrite(RGB1_GREEN, LOW);
    digitalWrite(RGB1_BLUE, LOW);
}

void turnOffRGB2() {
    digitalWrite(RGB2_RED, LOW);
    digitalWrite(RGB2_GREEN, LOW);
    digitalWrite(RGB2_BLUE, LOW);
}

void setAllLEDs(bool state) {
    digitalWrite(LED_MODE1_GREEN, state ? HIGH : LOW);
    digitalWrite(LED_MODE1_RED, state ? HIGH : LOW);
    digitalWrite(LED_MODE2_GREEN, state ? HIGH : LOW);
    digitalWrite(LED_MODE2_RED, state ? HIGH : LOW);
}

/* ============================================================================
   ------------------------------- ALERT FUNCTIONS -----------------------------
   ============================================================================ */

void showAlert(String title, String symbol, String message, bool isLong, bool isSevere, float price, byte mode) {
    alertTitle = title;
    alertSymbol = symbol;
    alertMessage = message;
    alertPrice = price;
    alertIsLong = isLong;
    alertIsSevere = isSevere;
    alertMode = mode;
    showingAlert = true;
    alertStartTime = millis();
    
    Serial.println("\n[Alert] TRIGGERED");
    Serial.println("Title: " + title);
    Serial.println("Symbol: " + symbol);
    Serial.println("Message: " + message);
    Serial.println("Price: $" + formatPrice(price));
    Serial.println("Type: " + String(isLong ? "LONG" : "SHORT"));
    Serial.println("Severe: " + String(isSevere ? "YES" : "NO"));
    Serial.println("Mode: " + String(mode == 0 ? "ENTRY" : "EXIT"));
    
    if (settings.buzzerEnabled && settings.buzzerVolume > 0) {
        if (mode == 0) {
            if (isLong) {
                playLongPositionAlert(isSevere);
            } else {
                playShortPositionAlert(isSevere);
            }
        } else {
            bool isProfit = message.indexOf("PROFIT") != -1;
            playExitAlertTone(isProfit);
        }
    }
    
    if (mode == 0) {
        mode1AlertSymbol = symbol;
        mode1AlertPercent = portfolioMode1.totalPnlPercent;
        
        if (isLong) {
            mode1GreenActive = true;
            mode1RedActive = false;
        } else {
            mode1GreenActive = false;
            mode1RedActive = true;
        }
        
        ledTimeout = millis() + 30000;
    } else {
        mode2AlertSymbol = symbol;
        
        bool isProfit = message.indexOf("PROFIT") != -1;
        if (isProfit) {
            mode2GreenActive = true;
            mode2RedActive = false;
            mode2AlertPercent = abs(portfolioMode2.totalPnlPercent);
        } else {
            mode2GreenActive = false;
            mode2RedActive = true;
            mode2AlertPercent = -abs(portfolioMode2.totalPnlPercent);
        }
        
        rgb2CurrentPercent = mode2AlertPercent;
        rgb2AlertActive = true;
        ledTimeout = millis() + 30000;
    }
    
    displayNeedsUpdate = true;
}

void checkAlerts(byte mode) {
    if (mode == 0) {
        processEntryAlerts();
    } else {
        processExitAlerts();
    }
}

void processEntryAlerts() {
    if (cryptoCountMode1 == 0) return;
    
    // Portfolio alert
    if (portfolioMode1.totalPnlPercent <= settings.portfolioAlertThreshold) {
        bool isSevere = portfolioMode1.totalPnlPercent <= (settings.portfolioAlertThreshold * 1.5);
        
        if (!showingAlert) {
            showAlert("PORTFOLIO ALERT",
                     "PORTFOLIO",
                     "Total P/L: " + formatPercent(portfolioMode1.totalPnlPercent),
                     true,
                     isSevere,
                     portfolioMode1.totalCurrentValue,
                     0);
        }
    }
    
    unsigned long currentTime = millis();
    unsigned long cooldownPeriod = 300000;
    
    for (int i = 0; i < cryptoCountMode1; i++) {
        CryptoPosition* pos = &cryptoDataMode1[i];
        
        if (pos->lastAlertTime > 0 && (currentTime - pos->lastAlertTime) < cooldownPeriod) {
            continue;
        }
        
        if (!pos->alerted && pos->changePercent <= settings.alertThreshold) {
            bool isSevere = pos->changePercent <= settings.severeAlertThreshold;
            
            showAlert(isSevere ? "SEVERE ALERT" : "POSITION ALERT",
                     getShortSymbol(pos->symbol),
                     "P/L: " + formatPercent(pos->changePercent),
                     pos->isLong,
                     isSevere,
                     pos->currentPrice,
                     0);
            
            pos->alerted = true;
            pos->severeAlerted = isSevere;
            pos->hasAlerted = true;
            pos->lastAlertTime = currentTime;
            pos->lastAlertPrice = pos->currentPrice;
            pos->lastAlertPercent = pos->changePercent;
        }
        
        float resetThreshold = settings.alertThreshold + 2.0;
        if (pos->alerted && pos->changePercent > resetThreshold) {
            pos->alerted = false;
            pos->severeAlerted = false;
            pos->hasAlerted = false;
            pos->lastAlertTime = 0;
            Serial.println("[Alert] Auto-reset for " + getShortSymbol(pos->symbol) + 
                          " (P/L improved to " + formatPercent(pos->changePercent) + ")");
        }
    }
}

void processExitAlerts() {
    if (cryptoCountMode2 == 0 || !settings.exitAlertEnabled) return;
    
    for (int i = 0; i < cryptoCountMode2; i++) {
        CryptoPosition* pos = &cryptoDataMode2[i];
        
        if (pos->exitAlertLastPrice == 0) {
            pos->exitAlertLastPrice = pos->currentPrice;
            continue;
        }
        
        float priceChangePercent = fabs((pos->currentPrice - pos->exitAlertLastPrice) / 
                                      pos->exitAlertLastPrice * 100);
        
        if (priceChangePercent >= settings.exitAlertPercent) {
            bool isProfit = (pos->currentPrice > pos->exitAlertLastPrice);
            float changeFromEntry = 0.0;
            
            if (pos->entryPrice > 0) {
                if (pos->isLong) {
                    changeFromEntry = ((pos->currentPrice - pos->entryPrice) / pos->entryPrice) * 100;
                } else {
                    changeFromEntry = ((pos->entryPrice - pos->currentPrice) / pos->entryPrice) * 100;
                }
            }
            
            String message = "Change: " + String(priceChangePercent, 1) + "%";
            if (changeFromEntry != 0.0) {
                message += " | Total: " + formatPercent(changeFromEntry);
            }
            
            showAlert("PRICE ALERT",
                     getShortSymbol(pos->symbol),
                     message,
                     isProfit,
                     false,
                     pos->currentPrice,
                     1);
            
            pos->exitAlerted = true;
            pos->exitAlertTime = millis();
            pos->exitAlertLastPrice = pos->currentPrice;
        }
    }
}

void resetAllAlerts() {
    Serial.println("[Alert] Resetting all alerts...");
    
    for (int i = 0; i < cryptoCountMode1; i++) {
        cryptoDataMode1[i].alerted = false;
        cryptoDataMode1[i].severeAlerted = false;
        cryptoDataMode1[i].hasAlerted = false;
        cryptoDataMode1[i].lastAlertTime = 0;
    }
    
    for (int i = 0; i < cryptoCountMode2; i++) {
        cryptoDataMode2[i].exitAlerted = false;
        cryptoDataMode2[i].exitAlertLastPrice = cryptoDataMode2[i].currentPrice;
        cryptoDataMode2[i].exitAlertTime = 0;
    }
    
    mode1GreenActive = false;
    mode1RedActive = false;
    mode2GreenActive = false;
    mode2RedActive = false;
    
    mode1AlertSymbol = "";
    mode2AlertSymbol = "";
    mode1AlertPercent = 0.0;
    mode2AlertPercent = 0.0;
    
    rgb2CurrentPercent = 0.0;
    rgb2AlertActive = false;
    turnOffRGB1();
    turnOffRGB2();
    
    if (settings.buzzerEnabled) {
        playResetAlertTone();
    }
    
    Serial.println("[Alert] All alerts reset");
}

/* ============================================================================
   ------------------------------- DATA FUNCTIONS ------------------------------
   ============================================================================ */

String base64Encode(String data) {
    const char* base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    String encoded = "";
    int i = 0;
    int len = data.length();
    
    while (i < len) {
        uint32_t octet_a = i < len ? (unsigned char)data[i++] : 0;
        uint32_t octet_b = i < len ? (unsigned char)data[i++] : 0;
        uint32_t octet_c = i < len ? (unsigned char)data[i++] : 0;
        
        uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;
        
        encoded += base64_chars[(triple >> 3 * 6) & 0x3F];
        encoded += base64_chars[(triple >> 2 * 6) & 0x3F];
        encoded += base64_chars[(triple >> 1 * 6) & 0x3F];
        encoded += base64_chars[(triple >> 0 * 6) & 0x3F];
    }
    
    if (len % 3 == 1) {
        encoded[encoded.length() - 1] = '=';
        encoded[encoded.length() - 2] = '=';
    } else if (len % 3 == 2) {
        encoded[encoded.length() - 1] = '=';
    }
    
    return encoded;
}

String getPortfolioData(byte mode) {
    if (!isConnectedToWiFi) {
        Serial.println("[API] Cannot fetch: WiFi not connected");
        return "{}";
    }
    
    if (strlen(settings.server) == 0 || strlen(settings.username) == 0) {
        Serial.println("[API] Cannot fetch: API not configured");
        return "{}";
    }
    
    String portfolioName = (mode == 0) ? String(settings.entryPortfolio) : String(settings.exitPortfolio);
    String url = String(settings.server) + "/api/device/portfolio/" + 
                String(settings.username) + "?portfolio_name=" + portfolioName;
    
    Serial.println("[API] Fetching: " + portfolioName + " from: " + url);
    
    unsigned long startTime = millis();
    
    http.begin(url);
    http.setTimeout(10000);
    
    String auth = base64Encode(String(settings.username) + ":" + String(settings.userpass));
    http.addHeader("Authorization", "Basic " + auth);
    http.addHeader("Content-Type", "application/json");
    
    int httpCode = http.GET();
    String response = "{}";
    
    unsigned long responseTime = millis() - startTime;
    
    if (httpCode == HTTP_CODE_OK) {
        response = http.getString();
        apiSuccessCount++;
        Serial.println("[API] Success: " + portfolioName + " (" + String(response.length()) + " bytes)");
    } else {
        apiErrorCount++;
        Serial.println("[API] Error: " + String(httpCode) + " for " + portfolioName);
    }
    
    http.end();
    
    if (apiAverageResponseTime == 0) {
        apiAverageResponseTime = responseTime;
    } else {
        apiAverageResponseTime = (apiAverageResponseTime * 0.9) + (responseTime * 0.1);
    }
    
    lastApiCallTime = millis();
    return response;
}

void parseCryptoData(String jsonData, byte mode) {
    if (jsonData.length() < 10 || jsonData == "{}") {
        Serial.println("[Data] Empty JSON for mode " + String(mode));
        return;
    }
    
    DynamicJsonDocument doc(JSON_BUFFER_SIZE);
    DeserializationError error = deserializeJson(doc, jsonData);
    
    if (error) {
        Serial.println("[Data] JSON Parse Error: " + String(error.c_str()));
        return;
    }
    
    if (!doc.containsKey("portfolio")) {
        Serial.println("[Data] No 'portfolio' field in JSON");
        return;
    }
    
    JsonArray portfolio = doc["portfolio"];
    int itemCount = portfolio.size();
    
    CryptoPosition* targetData;
    int* targetCount;
    PortfolioSummary* targetSummary;
    
    if (mode == 0) {
        targetData = cryptoDataMode1;
        targetCount = &cryptoCountMode1;
        targetSummary = &portfolioMode1;
    } else {
        targetData = cryptoDataMode2;
        targetCount = &cryptoCountMode2;
        targetSummary = &portfolioMode2;
    }
    
    *targetCount = min(itemCount, MAX_POSITIONS_PER_MODE);
    
    // Clear old data
    memset(targetData, 0, sizeof(CryptoPosition) * MAX_POSITIONS_PER_MODE);
    
    float totalValue = 0;
    float totalPnl = 0;
    int longCount = 0;
    int shortCount = 0;
    int winningCount = 0;
    int losingCount = 0;
    float maxLoss = 0;
    
    for (int i = 0; i < *targetCount; i++) {
        JsonObject item = portfolio[i];
        CryptoPosition* pos = &targetData[i];
        
        const char* symbol = item["symbol"] | "UNKNOWN";
        strncpy(pos->symbol, symbol, 15);
        pos->symbol[15] = '\0';
        
        pos->changePercent = item["pnl_percent"] | 0.0;
        pos->currentPrice = item["current_price"] | 0.0;
        pos->entryPrice = item["entry_price"] | 0.0;
        pos->quantity = item["quantity"] | 0.0;
        pos->pnlValue = item["pnl"] | 0.0;
        
        pos->isLong = true;
        
        if (item.containsKey("position")) {
            const char* position = item["position"];
            if (strcasecmp(position, "short") == 0) pos->isLong = false;
        } else if (item.containsKey("position_side")) {
            const char* positionSide = item["position_side"];
            if (strcasecmp(positionSide, "short") == 0) pos->isLong = false;
        } else if (item.containsKey("side")) {
            const char* side = item["side"];
            if (strcasecmp(side, "sell") == 0) pos->isLong = false;
        }
        
        pos->alertThreshold = settings.alertThreshold;
        pos->severeThreshold = settings.severeAlertThreshold;
        pos->hasAlerted = false;
        pos->lastAlertPercent = 0.0;
        
        if (mode == 1) {
            pos->exitAlertLastPrice = pos->currentPrice;
        }
        
        totalValue += pos->currentPrice * pos->quantity;
        totalPnl += pos->pnlValue;
        
        if (pos->isLong) longCount++;
        else shortCount++;
        
        if (pos->changePercent >= 0) winningCount++;
        else losingCount++;
        
        if (pos->changePercent < maxLoss) maxLoss = pos->changePercent;
    }
    
    targetSummary->totalCurrentValue = totalValue;
    targetSummary->totalPnl = totalPnl;
    targetSummary->totalPositions = *targetCount;
    targetSummary->longPositions = longCount;
    targetSummary->shortPositions = shortCount;
    targetSummary->winningPositions = winningCount;
    targetSummary->losingPositions = losingCount;
    targetSummary->maxDrawdown = maxLoss;
    
    targetSummary->totalInvestment = totalValue - totalPnl;
    
    if (targetSummary->totalInvestment > 0) {
        targetSummary->totalPnlPercent = (totalPnl / targetSummary->totalInvestment) * 100;
    } else {
        targetSummary->totalPnlPercent = 0.0;
    }
    
    if (doc.containsKey("summary")) {
        JsonObject summary = doc["summary"];
        
        targetSummary->totalInvestment = summary["total_investment"] | targetSummary->totalInvestment;
        targetSummary->totalCurrentValue = summary["total_current_value"] | targetSummary->totalCurrentValue;
        targetSummary->totalPnl = summary["total_pnl"] | targetSummary->totalPnl;
        
        if (targetSummary->totalInvestment > 0) {
            targetSummary->totalPnlPercent = ((targetSummary->totalCurrentValue - targetSummary->totalInvestment) / 
                                            targetSummary->totalInvestment) * 100;
        }
        
        targetSummary->winningPositions = summary["winning_positions"] | targetSummary->winningPositions;
        targetSummary->losingPositions = summary["losing_positions"] | targetSummary->losingPositions;
        targetSummary->maxDrawdown = summary["max_drawdown"] | targetSummary->maxDrawdown;
    }
    
    Serial.println("[Data] Parsed: " + String(*targetCount) + " positions for mode " + String(mode));
}

void updateDateTime() {
    static unsigned long lastSyncAttempt = 0;
    
    if (!isConnectedToWiFi) {
        currentDateTime = "No WiFi";
        return;
    }
    
    if (!timeSynced || (millis() - lastSyncAttempt > 3600000)) {
        if (syncTime()) {
            timeSynced = true;
            lastSyncAttempt = millis();
        }
    }
    
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 100)) {
        char buffer[30];
        strftime(buffer, sizeof(buffer), "%Y/%m/%d %H:%M:%S", &timeinfo);
        currentDateTime = String(buffer);
    }
}

bool syncTime() {
    Serial.println("[Time] Synchronizing with NTP server...");
    
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 5000)) {
        Serial.println("[Time] Failed to obtain time");
        return false;
    }
    
    Serial.println("[Time] Synchronized: " + String(asctime(&timeinfo)));
    return true;
}

/* ============================================================================
   ------------------------------- UTILITY FUNCTIONS ---------------------------
   ============================================================================ */

String getShortSymbol(const char* symbol) {
    String s = String(symbol);
    
    if (s.endsWith("USDT")) {
        s = s.substring(0, s.length() - 4);
    } else if (s.endsWith("_USDT")) {
        s = s.substring(0, s.length() - 5);
    } else if (s.endsWith("PERP")) {
        s = s.substring(0, s.length() - 4);
    }
    
    if (s.length() > 8) {
        s = s.substring(0, 8);
    }
    
    return s;
}

String formatPercent(float percent) {
    if (percent > 0) {
        return "+" + String(percent, 2) + "%";
    } else if (percent < 0) {
        return String(percent, 2) + "%";
    } else {
        return "0.00%";
    }
}

String formatNumber(float number) {
    if (number == 0) return "0";
    
    float absNumber = fabs(number);
    
    if (absNumber >= 1000000) {
        return String(number / 1000000, 2) + "M";
    } else if (absNumber >= 10000) {
        return String(number / 1000, 1) + "K";
    } else if (absNumber >= 1000) {
        return String(number / 1000, 2) + "K";
    } else if (absNumber >= 1) {
        return String(number, 2);
    } else if (absNumber >= 0.01) {
        return String(number, 4);
    } else if (absNumber >= 0.0001) {
        return String(number, 6);
    } else {
        return String(number, 8);
    }
}

String formatPrice(float price) {
    if (price <= 0) return "0.00";
    
    if (price >= 1000) {
        return String(price, 2);
    } else if (price >= 1) {
        return String(price, 4);
    } else if (price >= 0.01) {
        return String(price, 6);
    } else if (price >= 0.0001) {
        return String(price, 8);
    } else {
        return String(price, 10);
    }
}

String getUptimeString() {
    unsigned long uptime = millis() - systemStartTime;
    unsigned long seconds = uptime / 1000;
    unsigned long minutes = seconds / 60;
    unsigned long hours = minutes / 60;
    unsigned long days = hours / 24;
    
    String result = "";
    
    if (days > 0) result += String(days) + "d ";
    if (hours % 24 > 0) result += String(hours % 24) + "h ";
    if (minutes % 60 > 0) result += String(minutes % 60) + "m ";
    result += String(seconds % 60) + "s";
    
    return result;
}

String urlEncode(String str) {
    String encoded = "";
    char c;
    char hex[4];
    
    for (unsigned int i = 0; i < str.length(); i++) {
        c = str.charAt(i);
        
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += c;
        } else if (c == ' ') {
            encoded += '+';
        } else {
            sprintf(hex, "%%%02X", (unsigned char)c);
            encoded += hex;
        }
    }
    return encoded;
}

/* ============================================================================
   ------------------------------- SETTINGS FUNCTIONS --------------------------
   ============================================================================ */

void initializeSettings() {
    Serial.println("[Settings] Initializing defaults...");
    
    memset(&settings, 0, sizeof(SystemSettings));
    
    settings.networkCount = 0;
    settings.lastConnectedIndex = -1;
    
    strcpy(settings.server, "");
    strcpy(settings.username, "");
    strcpy(settings.userpass, "");
    strcpy(settings.entryPortfolio, "MainPortfolio");
    strcpy(settings.exitPortfolio, "ExitPortfolio");
    
    settings.alertThreshold = DEFAULT_ALERT_THRESHOLD;
    settings.severeAlertThreshold = DEFAULT_SEVERE_THRESHOLD;
    settings.portfolioAlertThreshold = PORTFOLIO_ALERT_THRESHOLD;
    settings.buzzerVolume = DEFAULT_VOLUME;
    settings.buzzerEnabled = true;
    settings.separateLongShortAlerts = true;
    settings.autoResetAlerts = false;
    settings.alertCooldown = 300000;
    
    settings.displayBrightness = 100;
    settings.displayTimeout = 30000;
    settings.showDetails = true;
    settings.invertDisplay = false;
    settings.displayRotation = 0;
    
    settings.exitAlertPercent = 3.0;
    settings.exitAlertEnabled = true;
    settings.exitAlertBlinkEnabled = true;
    
    settings.ledBrightness = DEFAULT_LED_BRIGHTNESS;
    settings.ledEnabled = true;
    
    settings.rgb1Enabled = true;
    settings.rgb2Enabled = true;
    settings.rgb1Brightness = 80;
    settings.rgb2Brightness = 80;
    settings.rgb1HistorySpeed = 50;
    settings.rgb2Sensitivity = 50;
    
    settings.showBattery = true;
    settings.batteryWarningLevel = BATTERY_WARNING;
    
    settings.autoReconnect = true;
    settings.reconnectAttempts = 5;
    
    settings.magicNumber = 0xAA;
    settings.configured = false;
    settings.firstBoot = millis();
    settings.bootCount = 0;
    settings.totalUptime = 0;
    
    Serial.println("[Settings] Defaults initialized");
}

bool loadSettings() {
    Serial.println("[Settings] Loading from EEPROM...");
    
    EEPROM.begin(EEPROM_SIZE);
    EEPROM.get(SETTINGS_EEPROM_ADDR, settings);
    
    // Load AP state separately
    uint8_t apState = EEPROM.read(AP_STATE_EEPROM_ADDR);
    EEPROM.end();
    
    if (settings.magicNumber != 0xAA) {
        Serial.println("[Settings] Invalid or no settings found");
        initializeSettings();
        return false;
    }
    
    if (apState == 0 || apState == 1) {
        apEnabled = (apState == 1);
    } else {
        apEnabled = true;
    }
    
    Serial.print("[Settings] Loaded WiFi networks: ");
    Serial.println(settings.networkCount);
    Serial.print("[AP] State: ");
    Serial.println(apEnabled ? "ENABLED" : "DISABLED");
    
    return true;
}

bool saveSettings() {
    settings.magicNumber = 0xAA;
    
    EEPROM.begin(EEPROM_SIZE);
    EEPROM.put(SETTINGS_EEPROM_ADDR, settings);
    
    // Save AP state separately
    EEPROM.write(AP_STATE_EEPROM_ADDR, apEnabled ? 1 : 0);
    
    bool result = EEPROM.commit();
    EEPROM.end();
    
    if (result) {
        Serial.println("[Settings] Saved successfully");
        return true;
    } else {
        Serial.println("[Settings] Save failed!");
        return false;
    }
}

void addOrUpdateWiFiNetwork(const char* ssid, const char* password, byte priority = 5, bool autoConnect = true) {
    if (strlen(ssid) == 0) {
        Serial.println("[WiFi] Cannot add: SSID is empty");
        return;
    }
    
    Serial.println("[WiFi] Adding/Updating: " + String(ssid));
    
    // Check if network already exists
    for (int i = 0; i < settings.networkCount; i++) {
        if (strcmp(settings.networks[i].ssid, ssid) == 0) {
            Serial.println("[WiFi] Updating existing network");
            strncpy(settings.networks[i].password, password, 63);
            settings.networks[i].password[63] = '\0';
            settings.networks[i].priority = priority;
            settings.networks[i].autoConnect = autoConnect;
            saveSettings();
            return;
        }
    }
    
    // If max networks reached, remove lowest priority
    if (settings.networkCount >= MAX_WIFI_NETWORKS) {
        int lowestPriorityIndex = 0;
        byte lowestPriority = 10;
        
        for (int i = 0; i < settings.networkCount; i++) {
            if (settings.networks[i].priority < lowestPriority) {
                lowestPriority = settings.networks[i].priority;
                lowestPriorityIndex = i;
            }
        }
        
        Serial.println("[WiFi] Max networks reached, removing: " + String(settings.networks[lowestPriorityIndex].ssid));
        
        for (int i = lowestPriorityIndex; i < settings.networkCount - 1; i++) {
            settings.networks[i] = settings.networks[i + 1];
        }
        settings.networkCount--;
    }
    
    // Add new network
    WiFiNetwork* network = &settings.networks[settings.networkCount];
    
    strncpy(network->ssid, ssid, 31);
    network->ssid[31] = '\0';
    strncpy(network->password, password, 63);
    network->password[63] = '\0';
    network->configured = true;
    network->priority = priority;
    network->autoConnect = autoConnect;
    network->connectionAttempts = 0;
    network->lastConnected = 0;
    network->rssi = 0;
    
    settings.networkCount++;
    
    Serial.println("[WiFi] New network added: " + String(ssid) + " (Priority: " + String(priority) + ")");
    
    saveSettings();
}

/* ============================================================================
   ------------------------------- WEB SERVER HANDLERS -------------------------
   ============================================================================ */

void handleRoot() {
    if (!isConnectedToWiFi && !apModeActive) {
        server.sendHeader("Location", "/setup", true);
        server.send(302, "text/plain", "");
        return;
    }
    
    String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial=1.0">
    <title>Portfolio Monitor Dashboard</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; background: #1a1a1a; color: #fff; }
        .container { max-width: 1200px; margin: 0 auto; }
        .dashboard-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); gap: 20px; }
        .card { background: #2d2d2d; padding: 20px; border-radius: 10px; }
        .card-header { font-size: 18px; font-weight: bold; margin-bottom: 15px; color: #0088ff; }
        .stats-grid { display: grid; grid-template-columns: repeat(2, 1fr); gap: 10px; }
        .stat-item { background: #3a3a3a; padding: 10px; border-radius: 5px; }
        .stat-label { font-size: 12px; color: #aaa; }
        .stat-value { font-size: 18px; font-weight: bold; }
        .positive { color: #00ff00; }
        .negative { color: #ff3333; }
        .btn { 
            background: #0088ff; 
            color: white; 
            padding: 10px 20px; 
            border: none; 
            border-radius: 5px; 
            cursor: pointer;
            text-decoration: none;
            display: inline-block;
            margin: 5px;
        }
        .btn:hover { background: #0066cc; }
        .btn-success { background: #00cc00; }
        .btn-danger { background: #ff3333; }
        .btn-warning { background: #ff9900; }
        .ap-status { display: inline-block; padding: 5px 10px; border-radius: 20px; font-size: 14px; font-weight: bold; margin-left: 10px; }
        .ap-on { background-color: #28a745; color: white; }
        .ap-off { background-color: #dc3545; color: white; }
    </style>
</head>
<body>
    <div class="container">
        <h1>📊 Portfolio Monitor Dashboard 
            <span class="ap-status )rawliteral";
    html += apEnabled ? "ap-on" : "ap-off";
    html += R"rawliteral(">AP: )rawliteral";
    html += apEnabled ? "ON" : "OFF";
    html += R"rawliteral(</span>
        </h1>
        
        <div style="margin-bottom: 20px;">
            <a href="/refresh" class="btn">🔄 Refresh Data</a>
            <a href="/setup" class="btn">⚙️ Setup</a>
            <a href="/systeminfo" class="btn">📊 System Info</a>
            <a href="/testalert" class="btn btn-warning">🔊 Test Alert</a>
            <a href="/resetalerts" class="btn btn-danger">🔄 Reset Alerts</a>
            <a href="/toggleap" class="btn )rawliteral";
    html += apEnabled ? "btn-warning" : "btn-success";
    html += R"rawliteral(">)rawliteral";
    html += apEnabled ? "🔴 Disable AP" : "🟢 Enable AP";
    html += R"rawliteral(</a>
        </div>
        
        <div class="dashboard-grid">
            <!-- Entry Mode Card -->
            <div class="card">
                <div class="card-header">📈 Entry Mode: )rawliteral";
    html += String(settings.entryPortfolio);
    html += R"rawliteral(</div>
                <div class="stats-grid">
                    <div class="stat-item">
                        <div class="stat-label">Positions</div>
                        <div class="stat-value">)rawliteral";
    html += String(cryptoCountMode1);
    html += R"rawliteral(</div>
                    </div>
                    <div class="stat-item">
                        <div class="stat-label">Total P/L</div>
                        <div class="stat-value )rawliteral";
    html += (portfolioMode1.totalPnlPercent >= 0 ? "positive" : "negative");
    html += R"rawliteral(">)rawliteral";
    html += formatPercent(portfolioMode1.totalPnlPercent);
    html += R"rawliteral(</div>
                    </div>
                    <div class="stat-item">
                        <div class="stat-label">Total Value</div>
                        <div class="stat-value">$)rawliteral";
    html += formatNumber(portfolioMode1.totalCurrentValue);
    html += R"rawliteral(</div>
                    </div>
                    <div class="stat-item">
                        <div class="stat-label">Win Rate</div>
                        <div class="stat-value">)rawliteral";
    if (portfolioMode1.totalPositions > 0) {
        float winRate = (portfolioMode1.winningPositions * 100.0) / portfolioMode1.totalPositions;
        html += String(winRate, 1) + "%";
    } else {
        html += "0%";
    }
    html += R"rawliteral(</div>
                    </div>
                </div>
            </div>
            
            <!-- Exit Mode Card -->
            <div class="card">
                <div class="card-header">📉 Exit Mode: )rawliteral";
    html += String(settings.exitPortfolio);
    html += R"rawliteral(</div>
                <div class="stats-grid">
                    <div class="stat-item">
                        <div class="stat-label">Positions</div>
                        <div class="stat-value">)rawliteral";
    html += String(cryptoCountMode2);
    html += R"rawliteral(</div>
                    </div>
                    <div class="stat-item">
                        <div class="stat-label">Total P/L</div>
                        <div class="stat-value )rawliteral";
    html += (portfolioMode2.totalPnlPercent >= 0 ? "positive" : "negative");
    html += R"rawliteral(">)rawliteral";
    html += formatPercent(portfolioMode2.totalPnlPercent);
    html += R"rawliteral(</div>
                    </div>
                    <div class="stat-item">
                        <div class="stat-label">Total Value</div>
                        <div class="stat-value">$)rawliteral";
    html += formatNumber(portfolioMode2.totalCurrentValue);
    html += R"rawliteral(</div>
                    </div>
                    <div class="stat-item">
                        <div class="stat-label">Max Drawdown</div>
                        <div class="stat-value negative">)rawliteral";
    html += formatPercent(portfolioMode2.maxDrawdown);
    html += R"rawliteral(</div>
                    </div>
                </div>
            </div>
            
            <!-- System Status Card -->
            <div class="card">
                <div class="card-header">⚡ System Status</div>
                <div class="stats-grid">
                    <div class="stat-item">
                        <div class="stat-label">WiFi Status</div>
                        <div class="stat-value )rawliteral";
    html += (isConnectedToWiFi ? "positive" : "negative");
    html += R"rawliteral(">)rawliteral";
    if (isConnectedToWiFi) {
        html += "Connected";
    } else if (apModeActive) {
        html += "AP Mode";
    } else {
        html += "Disconnected";
    }
    html += R"rawliteral(</div>
                    </div>
                    <div class="stat-item">
                        <div class="stat-label">Uptime</div>
                        <div class="stat-value">)rawliteral";
    html += getUptimeString();
    html += R"rawliteral(</div>
                    </div>
                    <div class="stat-item">
                        <div class="stat-label">Memory Free</div>
                        <div class="stat-value">)rawliteral";
    html += String(ESP.getFreeHeap() / 1024) + " KB";
    html += R"rawliteral(</div>
                    </div>
                    <div class="stat-item">
                        <div class="stat-label">Battery</div>
                        <div class="stat-value">)rawliteral";
    if (powerSource == POWER_SOURCE_USB) {
        html += "USB";
    } else {
        html += String(batteryPercent) + "%";
    }
    html += R"rawliteral(</div>
                    </div>
                </div>
            </div>
            
            <!-- Quick Actions Card -->
            <div class="card">
                <div class="card-header">🚀 Quick Actions</div>
                <div style="display: flex; flex-wrap: wrap; gap: 10px; margin-top: 15px;">
                    <a href="/ledcontrol?action=test" class="btn btn-warning">Test LEDs</a>
                    <a href="/rgbcontrol?action=test" class="btn btn-warning">Test RGB</a>
                    <a href="/displaycontrol?action=test" class="btn btn-warning">Test Display</a>
                    <a href="/wifimanage" class="btn">WiFi Manager</a>
                    <a href="/apistatus" class="btn">API Status</a>
                    <a href="/factoryreset" class="btn btn-danger">Factory Reset</a>
                    <a href="/restart" class="btn">Restart</a>
                </div>
                <div style="margin-top: 15px;">
                    <h4>🎚️ Volume Control</h4>
                    <div>
                        <button onclick="setVolume(0)" class="btn">🔇 Mute</button>
                        <button onclick="setVolume(25)" class="btn">Quiet</button>
                        <button onclick="setVolume(50)" class="btn">Medium</button>
                        <button onclick="setVolume(75)" class="btn">Loud</button>
                        <button onclick="setVolume(100)" class="btn">🔊 Max</button>
                        <button onclick="testCurrentVolume()" class="btn btn-warning">Test</button>
                    </div>
                    <div style="margin-top: 10px;">
                        <span id="currentVolume">Current: )rawliteral";
    html += String(settings.buzzerVolume);
    html += R"rawliteral(%</span>
                    </div>
                </div>
            </div>
        </div>
    </div>
    
    <script>
        function setVolume(volume) {
            fetch('/setvolume?volume=' + volume)
                .then(response => response.text())
                .then(text => {
                    document.getElementById('currentVolume').textContent = 'Current: ' + volume + '%';
                    alert(text);
                });
        }
        
        function testCurrentVolume() {
            fetch('/testvolume')
                .then(response => response.text())
                .then(text => {
                    alert(text);
                });
        }
        
        setTimeout(function() {
            location.reload();
        }, 30000);
    </script>
</body>
</html>)rawliteral";
    
    server.send(200, "text/html", html);
}

void handleSetup() {
    String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial=1.0">
    <title>Portfolio Monitor Setup</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; background: #1a1a1a; color: #fff; }
        .container { max-width: 1000px; margin: 0 auto; }
        .tab-container { margin-bottom: 20px; }
        .tab-buttons { display: flex; flex-wrap: wrap; margin-bottom: 20px; }
        .tab-button { 
            background: #2d2d2d; 
            color: #ccc; 
            padding: 10px 20px; 
            border: none; 
            border-right: 1px solid #444; 
            cursor: pointer;
        }
        .tab-button.active { background: #0088ff; color: white; }
        .tab-content { display: none; background: #2d2d2d; padding: 20px; border-radius: 0 10px 10px 10px; }
        .tab-content.active { display: block; }
        .form-group { margin-bottom: 15px; }
        label { display: block; margin-bottom: 5px; color: #ccc; }
        input, select { 
            width: 100%; 
            max-width: 400px; 
            padding: 8px; 
            background: #3a3a3a; 
            border: 1px solid #555; 
            border-radius: 5px; 
            color: white; 
        }
        .btn { 
            background: #0088ff; 
            color: white; 
            padding: 10px 20px; 
            border: none; 
            border-radius: 5px; 
            cursor: pointer; 
        }
        .btn:hover { background: #0066cc; }
        .btn-success { background: #00cc00; }
    </style>
</head>
<body>
    <div class="container">
        <h1>⚙️ Portfolio Monitor Setup</h1>
        
        <div class="tab-container">
            <div class="tab-buttons">
                <button class="tab-button active" onclick="openTab(event, 'wifi')">WiFi</button>
                <button class="tab-button" onclick="openTab(event, 'api')">API</button>
                <button class="tab-button" onclick="openTab(event, 'alert')">Alerts</button>
                <button class="tab-button" onclick="openTab(event, 'display')">Display</button>
            </div>
            
            <!-- WiFi Tab -->
            <div id="wifi" class="tab-content active">
                <h2>WiFi Settings</h2>
                <div style="margin-bottom: 20px;">
                    <a href="/wifimanage" class="btn">📶 WiFi Manager</a>
                    <a href="/toggleap" class="btn">AP Toggle</a>
                </div>
                <form action="/savewifi" method="post">
                    <div class="form-group">
                        <label for="ssid">SSID</label>
                        <input type="text" id="ssid" name="ssid" required>
                    </div>
                    <div class="form-group">
                        <label for="password">Password</label>
                        <input type="password" id="password" name="password" required>
                    </div>
                    <div class="form-group">
                        <label for="priority">Priority (1-10, 10=highest)</label>
                        <input type="number" id="priority" name="priority" min="1" max="10" value="7">
                    </div>
                    <div class="form-group">
                        <label>
                            <input type="checkbox" id="autoconnect" name="autoconnect" checked> Auto Connect
                        </label>
                    </div>
                    <button type="submit" class="btn btn-success">Save WiFi</button>
                </form>
            </div>
            
            <!-- API Tab -->
            <div id="api" class="tab-content">
                <h2>API Settings</h2>
                <form action="/saveapi" method="post">
                    <div class="form-group">
                        <label for="server">API Server URL</label>
                        <input type="text" id="server" name="server" value=")rawliteral";
    html += String(settings.server);
    html += R"rawliteral(" required>
                    </div>
                    <div class="form-group">
                        <label for="username">Username</label>
                        <input type="text" id="username" name="username" value=")rawliteral";
    html += String(settings.username);
    html += R"rawliteral(" required>
                    </div>
                    <div class="form-group">
                        <label for="userpass">Password</label>
                        <input type="password" id="userpass" name="userpass" value=")rawliteral";
    html += String(settings.userpass);
    html += R"rawliteral(" required>
                    </div>
                    <div class="form-group">
                        <label for="entryportfolio">Entry Portfolio Name</label>
                        <input type="text" id="entryportfolio" name="entryportfolio" value=")rawliteral";
    html += String(settings.entryPortfolio);
    html += R"rawliteral(" required>
                    </div>
                    <div class="form-group">
                        <label for="exitportfolio">Exit Portfolio Name</label>
                        <input type="text" id="exitportfolio" name="exitportfolio" value=")rawliteral";
    html += String(settings.exitPortfolio);
    html += R"rawliteral(" required>
                    </div>
                    <button type="submit" class="btn btn-success">Save API</button>
                </form>
            </div>
            
            <!-- Alert Tab -->
            <div id="alert" class="tab-content">
                <h2>Alert Settings</h2>
                <form action="/savealert" method="post">
                    <div class="form-group">
                        <label for="alertthreshold">Alert Threshold (%)</label>
                        <input type="number" step="0.1" id="alertthreshold" name="alertthreshold" value=")rawliteral";
    html += String(settings.alertThreshold, 1);
    html += R"rawliteral(">
                    </div>
                    <div class="form-group">
                        <label for="severethreshold">Severe Alert Threshold (%)</label>
                        <input type="number" step="0.1" id="severethreshold" name="severethreshold" value=")rawliteral";
    html += String(settings.severeAlertThreshold, 1);
    html += R"rawliteral(">
                    </div>
                    <div class="form-group">
                        <label for="portfolioalert">Portfolio Alert Threshold (%)</label>
                        <input type="number" step="0.1" id="portfolioalert" name="portfolioalert" value=")rawliteral";
    html += String(settings.portfolioAlertThreshold, 1);
    html += R"rawliteral(">
                    </div>
                    <div class="form-group">
                        <label for="buzzervolume">Buzzer Volume (0-100)</label>
                        <input type="number" id="buzzervolume" name="buzzervolume" min="0" max="100" value=")rawliteral";
    html += String(settings.buzzerVolume);
    html += R"rawliteral(">
                    </div>
                    <div class="form-group">
                        <label>
                            <input type="checkbox" id="buzzerenable" name="buzzerenable" )rawliteral";
    html += (settings.buzzerEnabled ? "checked" : "");
    html += R"rawliteral(> Enable Buzzer
                        </label>
                    </div>
                    <button type="submit" class="btn btn-success">Save Alerts</button>
                </form>
            </div>
            
            <!-- Display Tab -->
            <div id="display" class="tab-content">
                <h2>Display Settings</h2>
                <form action="/savedisplay" method="post">
                    <div class="form-group">
                        <label for="brightness">Display Brightness (0-100)</label>
                        <input type="number" id="brightness" name="brightness" min="0" max="100" value=")rawliteral";
    html += String(settings.displayBrightness);
    html += R"rawliteral(">
                    </div>
                    <div class="form-group">
                        <label for="timeout">Display Timeout (ms, 0=never)</label>
                        <input type="number" id="timeout" name="timeout" min="0" value=")rawliteral";
    html += String(settings.displayTimeout);
    html += R"rawliteral(">
                    </div>
                    <div class="form-group">
                        <label for="rotation">Display Rotation</label>
                        <select id="rotation" name="rotation">
                            <option value="0" )rawliteral";
    html += (settings.displayRotation == 0 ? "selected" : "");
    html += R"rawliteral(>0°</option>
                            <option value="1" )rawliteral";
    html += (settings.displayRotation == 1 ? "selected" : "");
    html += R"rawliteral(>90°</option>
                            <option value="2" )rawliteral";
    html += (settings.displayRotation == 2 ? "selected" : "");
    html += R"rawliteral(>180°</option>
                            <option value="3" )rawliteral";
    html += (settings.displayRotation == 3 ? "selected" : "");
    html += R"rawliteral(>270°</option>
                        </select>
                    </div>
                    <button type="submit" class="btn btn-success">Save Display</button>
                </form>
            </div>
        </div>
        
        <div style="margin-top: 30px;">
            <a href="/" class="btn">← Back to Dashboard</a>
        </div>
    </div>
    
    <script>
        function openTab(evt, tabName) {
            var i, tabcontent, tabbuttons;
            tabcontent = document.getElementsByClassName("tab-content");
            for (i = 0; i < tabcontent.length; i++) {
                tabcontent[i].className = tabcontent[i].className.replace(" active", "");
            }
            tabbuttons = document.getElementsByClassName("tab-button");
            for (i = 0; i < tabbuttons.length; i++) {
                tabbuttons[i].className = tabbuttons[i].className.replace(" active", "");
            }
            document.getElementById(tabName).className += " active";
            evt.currentTarget.className += " active";
        }
    </script>
</body>
</html>)rawliteral";
    
    server.send(200, "text/html", html);
}

void handleSaveWiFi() {
    if (server.hasArg("ssid") && server.hasArg("password")) {
        String ssid = server.arg("ssid");
        String password = server.arg("password");
        byte priority = server.hasArg("priority") ? server.arg("priority").toInt() : 7;
        bool autoConnect = server.hasArg("autoconnect");
        
        addOrUpdateWiFiNetwork(ssid.c_str(), password.c_str(), priority, autoConnect);
        playSuccessTone();
        
        // Trigger reconnection
        wifiState = WIFI_STATE_DISCONNECTED;
        wifiStateTime = millis();
        
        server.sendHeader("Location", "/setup", true);
        server.send(302, "text/plain", "");
    }
}

void handleSaveAPI() {
    if (server.hasArg("server") && server.hasArg("username") && server.hasArg("userpass")) {
        String serverUrl = server.arg("server");
        String username = server.arg("username");
        String userpass = server.arg("userpass");
        String entryPortfolio = server.arg("entryportfolio");
        String exitPortfolio = server.arg("exitportfolio");
        
        strncpy(settings.server, serverUrl.c_str(), 127);
        strncpy(settings.username, username.c_str(), 31);
        strncpy(settings.userpass, userpass.c_str(), 63);
        strncpy(settings.entryPortfolio, entryPortfolio.c_str(), 31);
        strncpy(settings.exitPortfolio, exitPortfolio.c_str(), 31);
        
        settings.configured = true;
        
        if (saveSettings()) {
            playSuccessTone();
            server.sendHeader("Location", "/setup", true);
            server.send(302, "text/plain", "");
        } else {
            playErrorTone();
            server.send(500, "text/plain", "Failed to save API settings");
        }
    }
}

void handleSaveAlert() {
    settings.alertThreshold = server.arg("alertthreshold").toFloat();
    settings.severeAlertThreshold = server.arg("severethreshold").toFloat();
    settings.portfolioAlertThreshold = server.arg("portfolioalert").toFloat();
    settings.buzzerVolume = constrain(server.arg("buzzervolume").toInt(), 0, 100);
    settings.buzzerEnabled = server.hasArg("buzzerenable");
    
    if (saveSettings()) {
        playSuccessTone();
        server.sendHeader("Location", "/setup", true);
        server.send(302, "text/plain", "");
    }
}

void handleSaveDisplay() {
    settings.displayBrightness = constrain(server.arg("brightness").toInt(), 0, 100);
    settings.displayTimeout = server.arg("timeout").toInt();
    settings.displayRotation = constrain(server.arg("rotation").toInt(), 0, 3);
    
    setDisplayBrightness(settings.displayBrightness);
    tft.setRotation(settings.displayRotation);
    
    if (saveSettings()) {
        playSuccessTone();
        server.sendHeader("Location", "/setup", true);
        server.send(302, "text/plain", "");
    }
}

void handleRefresh() {
    if (isConnectedToWiFi) {
        lastDataUpdate = millis() - DATA_UPDATE_INTERVAL;
        playSuccessTone();
    } else {
        playErrorTone();
    }
    
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
}

void handleTestAlert() {
    playLongPositionAlert(false);
    delay(800);
    playShortPositionAlert(false);
    delay(800);
    playExitAlertTone(true);
    delay(800);
    playExitAlertTone(false);
    
    server.send(200, "text/plain", "Test alert sequence played");
}

void handleResetAlerts() {
    resetAllAlerts();
    server.send(200, "text/plain", "All alerts reset");
}

void handleSystemInfo() {
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta charset='UTF-8'><title>System Info</title>";
    html += "<style>body{font-family:Arial;margin:20px;background:#1a1a1a;color:#fff;}</style>";
    html += "</head><body>";
    html += "<h1>📊 System Information</h1>";
    
    html += "<h3>Device Info</h3>";
    html += "<p>ESP32 Model: ESP32-WROVER-E</p>";
    html += "<p>Chip ID: 0x" + String((uint32_t)ESP.getEfuseMac(), HEX) + "</p>";
    html += "<p>CPU Frequency: " + String(ESP.getCpuFreqMHz()) + " MHz</p>";
    html += "<p>Free Heap: " + String(ESP.getFreeHeap() / 1024) + " KB</p>";
    
    html += "<h3>System Status</h3>";
    html += "<p>Uptime: " + getUptimeString() + "</p>";
    html += "<p>Boot Count: " + String(settings.bootCount) + "</p>";
    html += "<p>Buzzer Volume: " + String(settings.buzzerVolume) + "%</p>";
    
    html += "<h3>Network Status</h3>";
    html += "<p>WiFi Status: ";
    if (isConnectedToWiFi) {
        html += "<span style='color:#0f0'>Connected to " + WiFi.SSID() + "</span>";
    } else if (apModeActive) {
        html += "<span style='color:#ff0'>AP Mode</span>";
    } else {
        html += "<span style='color:#f00'>Disconnected</span>";
    }
    html += "</p>";
    
    html += "<p>IP Address: ";
    if (isConnectedToWiFi) {
        html += WiFi.localIP().toString();
    } else if (apModeActive) {
        html += WiFi.softAPIP().toString();
    } else {
        html += "N/A";
    }
    html += "</p>";
    
    html += "<p>Saved Networks: " + String(settings.networkCount) + "</p>";
    
    html += "<h3>API Statistics</h3>";
    html += "<p>Success Count: " + String(apiSuccessCount) + "</p>";
    html += "<p>Error Count: " + String(apiErrorCount) + "</p>";
    html += "<p>Success Rate: ";
    if (apiSuccessCount + apiErrorCount > 0) {
        float rate = (apiSuccessCount * 100.0) / (apiSuccessCount + apiErrorCount);
        html += String(rate, 1) + "%";
    } else {
        html += "N/A";
    }
    html += "</p>";
    
    html += "<p>Avg Response Time: " + String(apiAverageResponseTime, 0) + " ms</p>";
    
    html += "<p><a href='/'>← Back to Dashboard</a></p>";
    html += "</body></html>";
    
    server.send(200, "text/html", html);
}

void handleToggleAP() {
    apEnabled = !apEnabled;
    saveSettings();
    
    // Trigger WiFi state change
    wifiState = WIFI_STATE_DISCONNECTED;
    wifiStateTime = millis();
    
    Serial.print("[AP] Toggled: ");
    Serial.println(apEnabled ? "ENABLED" : "DISABLED");
    
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
}

void handleSetVolume() {
    if (server.hasArg("volume")) {
        int newVolume = server.arg("volume").toInt();
        setBuzzerVolume(newVolume);
        server.send(200, "text/plain", "Volume set to " + String(settings.buzzerVolume) + "%");
    }
}

void handleTestVolume() {
    String volStr = server.arg("v");
    int testVol = volStr.toInt();
    if (testVol == 0) testVol = settings.buzzerVolume;
    
    int savedVol = settings.buzzerVolume;
    settings.buzzerVolume = testVol;
    
    playLongPositionAlert(false);
    delay(500);
    playShortPositionAlert(false);
    
    settings.buzzerVolume = savedVol;
    server.send(200, "text/plain", "Test completed with volume " + String(testVol) + "%");
}

void handleWiFiManage() {
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta charset='UTF-8'><title>WiFi Manager</title>";
    html += "<style>body{font-family:Arial;margin:20px;background:#1a1a1a;color:#fff;}</style>";
    html += "</head><body>";
    html += "<h1>📶 WiFi Manager</h1>";
    
    html += "<h3>Saved Networks</h3>";
    if (settings.networkCount == 0) {
        html += "<p>No WiFi networks saved yet.</p>";
    } else {
        html += "<table border='1' cellpadding='5' style='border-collapse:collapse;'>";
        html += "<tr><th>SSID</th><th>Priority</th><th>Auto Connect</th><th>Last Connected</th><th>Actions</th></tr>";
        
        for (int i = 0; i < settings.networkCount; i++) {
            html += "<tr>";
            html += "<td>" + String(settings.networks[i].ssid) + "</td>";
            html += "<td>" + String(settings.networks[i].priority) + "</td>";
            html += "<td>" + String(settings.networks[i].autoConnect ? "Yes" : "No") + "</td>";
            html += "<td>";
            if (settings.networks[i].lastConnected > 0) {
                html += String((millis() - settings.networks[i].lastConnected) / 1000) + "s ago";
            } else {
                html += "Never";
            }
            html += "</td>";
            html += "<td>";
            html += "<a href='/wifiremove?ssid=" + urlEncode(settings.networks[i].ssid) + "' style='color:#f00;margin-right:10px;'>Remove</a>";
            html += "<a href='/wificonnect?index=" + String(i) + "' style='color:#0f0;'>Connect</a>";
            html += "</td>";
            html += "</tr>";
        }
        html += "</table>";
    }
    
    html += "<h3>Current Connection</h3>";
    if (isConnectedToWiFi) {
        html += "<p>Connected to: " + WiFi.SSID() + "</p>";
        html += "<p>IP: " + WiFi.localIP().toString() + "</p>";
        html += "<p>RSSI: " + String(WiFi.RSSI()) + " dBm</p>";
        html += "<p><a href='/wifidisconnect' style='color:#f00;'>Disconnect</a></p>";
    } else if (apModeActive) {
        html += "<p>AP Mode Active</p>";
        html += "<p>AP IP: " + WiFi.softAPIP().toString() + "</p>";
    } else {
        html += "<p>Not connected</p>";
    }
    
    html += "<p><a href='/setup'>← Back to Setup</a> | <a href='/'>← Back to Dashboard</a></p>";
    html += "</body></html>";
    
    server.send(200, "text/html", html);
}

void handleWiFiConnect() {
    if (server.hasArg("index")) {
        int index = server.arg("index").toInt();
        if (index >= 0 && index < settings.networkCount) {
            wifiState = WIFI_STATE_CONNECTING;
            wifiStateTime = millis();
            
            WiFi.disconnect(true);
            delay(100);
            WiFi.mode(WIFI_STA);
            WiFi.begin(settings.networks[index].ssid, settings.networks[index].password);
            settings.lastConnectedIndex = index;
        }
    }
    
    server.sendHeader("Location", "/wifimanage", true);
    server.send(302, "text/plain", "");
}

void handleWiFiDisconnect() {
    WiFi.disconnect(true);
    isConnectedToWiFi = false;
    wifiState = WIFI_STATE_DISCONNECTED;
    wifiStateTime = millis();
    
    if (apEnabled) {
        wifiStartAP();
    }
    
    server.sendHeader("Location", "/wifimanage", true);
    server.send(302, "text/plain", "");
}

void handleWiFiRemove() {
    if (server.hasArg("ssid")) {
        String ssid = server.arg("ssid");
        
        for (int i = 0; i < settings.networkCount; i++) {
            if (strcmp(settings.networks[i].ssid, ssid.c_str()) == 0) {
                for (int j = i; j < settings.networkCount - 1; j++) {
                    settings.networks[j] = settings.networks[j + 1];
                }
                settings.networkCount--;
                memset(&settings.networks[settings.networkCount], 0, sizeof(WiFiNetwork));
                saveSettings();
                break;
            }
        }
    }
    
    server.sendHeader("Location", "/wifimanage", true);
    server.send(302, "text/plain", "");
}

void handleFactoryReset() {
    showDisplayMessage("Factory Reset", "In Progress", "Please wait...", "Do not power off");
    
    EEPROM.begin(EEPROM_SIZE);
    for (int i = 0; i < EEPROM_SIZE; i++) {
        EEPROM.write(i, 0);
    }
    bool result = EEPROM.commit();
    EEPROM.end();
    
    if (result) {
        playResetAlertTone();
        delay(1000);
        showDisplayMessage("Factory Reset", "Complete", "Restarting...", "");
        delay(2000);
        ESP.restart();
    } else {
        showDisplayMessage("Factory Reset", "Failed", "EEPROM Error", "");
        delay(3000);
    }
    
    server.send(200, "text/plain", "Factory reset completed. Restarting...");
}

void handleRestart() {
    server.send(200, "text/plain", "Restarting system...");
    delay(1000);
    ESP.restart();
}

/* ============================================================================
   ------------------------------- WEB SERVER SETUP ----------------------------
   ============================================================================ */

void setupWebServer() {
    Serial.println("[Web] Setting up web server...");
    
    // Main routes
    server.on("/", handleRoot);
    server.on("/setup", handleSetup);
    server.on("/savewifi", HTTP_POST, handleSaveWiFi);
    server.on("/saveapi", HTTP_POST, handleSaveAPI);
    server.on("/savealert", HTTP_POST, handleSaveAlert);
    server.on("/savedisplay", HTTP_POST, handleSaveDisplay);
    
    // WiFi management
    server.on("/wifimanage", handleWiFiManage);
    server.on("/wificonnect", handleWiFiConnect);
    server.on("/wifidisconnect", handleWiFiDisconnect);
    server.on("/wifiremove", handleWiFiRemove);
    server.on("/toggleap", handleToggleAP);
    
    // System functions
    server.on("/refresh", handleRefresh);
    server.on("/testalert", handleTestAlert);
    server.on("/resetalerts", handleResetAlerts);
    server.on("/systeminfo", handleSystemInfo);
    server.on("/setvolume", handleSetVolume);
    server.on("/testvolume", handleTestVolume);
    server.on("/factoryreset", handleFactoryReset);
    server.on("/restart", handleRestart);
    
    // 404 Handler
    server.onNotFound([]() {
        String message = "404: Not Found\n\n";
        message += "URI: ";
        message += server.uri();
        message += "\nMethod: ";
        message += (server.method() == HTTP_GET) ? "GET" : "POST";
        message += "\nArguments: ";
        message += server.args();
        message += "\n";
        for (uint8_t i = 0; i < server.args(); i++) {
            message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
        }
        server.send(404, "text/plain", message);
    });
    
    server.begin();
    Serial.println("[Web] Server started on port 80");
    
    if (apModeActive) {
        Serial.println("[Web] AP Access Point: http://" + WiFi.softAPIP().toString());
    }
    if (isConnectedToWiFi) {
        Serial.println("[Web] Station IP: http