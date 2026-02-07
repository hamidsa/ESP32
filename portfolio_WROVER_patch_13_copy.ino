/* ============================================================================
   PORTFOLIO MONITOR - ESP32-WROVER-E
   Professional Dual Mode Portfolio Tracking System
   Version: 4.5.2 - Enhanced Volume Control & WiFi
   Hardware: ESP32-WROVER-E + ST7789 240x240 + Dual RGB LEDs + 4 Single LEDs
   Features: Simultaneous Entry/Exit Mode, Smart RGB Visualization
             High Resolution Display, Complete Web Interface
             Auto Reconnect, Battery Monitoring
             Advanced Volume Control System
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

// ===== TFT CONFIGURATION =====
// Edit User_Setup.h in TFT_eSPI library:
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
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_GFXFF
#define SMOOTH_FONT
*/

// ===== DEFINES =====
#define MAX_ALERT_HISTORY 50
// تغییر: افزایش از 40 به 100 موقعیت
#define MAX_POSITIONS_PER_MODE 100
#define MAX_WIFI_NETWORKS 5
#define EEPROM_SIZE 4096
// تغییر: افزایش بافر JSON از 3072 به 8192
#define JSON_BUFFER_SIZE 8192  
#define DISPLAY_CRYPTO_COUNT 8

#define POWER_SOURCE_USB 0
#define POWER_SOURCE_BATTERY 1
#define POWER_SOURCE_EXTERNAL 2

// ===== PIN DEFINITIONS ===== (FIXED)
// RGB LEDs (Common Cathode)
#define RGB1_RED    32
#define RGB1_GREEN  33
#define RGB1_BLUE   25

#define RGB2_RED    26
#define RGB2_GREEN  14
#define RGB2_BLUE   12

// Single Color LEDs
#define LED_MODE1_GREEN   27  // Entry Mode LONG/PROFIT (Changed from 34)
#define LED_MODE1_RED     13  // Entry Mode SHORT/LOSS
#define LED_MODE2_GREEN   21  // Exit Mode PROFIT (Changed from 19)
#define LED_MODE2_RED     19  // Exit Mode LOSS (Changed from 27)

// Buzzer - FIXED: Separate pin to avoid conflict
#define BUZZER_PIN        22  // Changed from 13 to avoid LED conflict
#define RESET_BUTTON_PIN  0
#define TFT_BL_PIN        5
#define BATTERY_PIN       34   // ADC برای خواندن باتری

// ===== TIMING CONSTANTS =====
#define DATA_UPDATE_INTERVAL 15000      // 15 seconds
#define RGB1_HISTORY_CYCLE 23000       // 23 seconds
#define DISPLAY_UPDATE_INTERVAL 2000   // 2 seconds
#define ALERT_DISPLAY_TIME 10000       // 10 seconds
#define WIFI_CONNECT_TIMEOUT 20000
#define RECONNECT_INTERVAL 30000
#define DEBOUNCE_DELAY 50
#define BUTTON_HOLD_TIME 10000
#define BATTERY_CHECK_INTERVAL 60000   // 1 minute
#define SCAN_INTERVAL 60000           // هر 1 دقیقه اسکن کن

// ===== NTP CONFIG =====
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 12600;     // 3.5 hours for Iran
const int   daylightOffset_sec = 0;

// ===== ALERT THRESHOLDS =====
#define DEFAULT_ALERT_THRESHOLD -5.0
#define DEFAULT_SEVERE_THRESHOLD -10.0
#define PORTFOLIO_ALERT_THRESHOLD -7.0
#define DEFAULT_EXIT_ALERT_PERCENT 3.0

// ===== TONE FREQUENCIES =====
#define LONG_NORMAL_TONE 523  // C5
#define LONG_SEVERE_TONE 440  // A4
#define SHORT_NORMAL_TONE 659 // E5
#define SHORT_SEVERE_TONE 784 // G5
#define PORTFOLIO_ALERT_TONE 587 // D5
#define RESET_TONE_1 262      // C4
#define RESET_TONE_2 294      // D4
#define RESET_TONE_3 330      // E4
#define SUCCESS_TONE_1 523    // C5
#define SUCCESS_TONE_2 659    // E5
#define ERROR_TONE_1 349      // F4
#define ERROR_TONE_2 294      // D4
#define CONNECTION_LOST_TONE 392 // G4

// ===== BUZZER SETTINGS =====
#define DEFAULT_VOLUME 50
#define VOLUME_MIN 0
#define VOLUME_MAX 100
#define VOLUME_OFF 0
#define DEFAULT_LED_BRIGHTNESS 100

// ===== BATTERY SETTINGS =====
#define BATTERY_FULL 8.4
#define BATTERY_EMPTY 6.6
#define BATTERY_WARNING 20  // درصد هشدار

bool apEnabled = false;
String apSSID = "ESP_AP";
String apPassword = "12345678";   // حداقل 8 کاراکتر

// ===== STRUCTURES =====
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
    char positionSide[12];
    char marginType[12];
    
    bool exitAlerted;
    float exitAlertLastPrice;
    unsigned long exitAlertTime;
    bool hasAlerted; // فیلد جدید برای پیگیری آلرت
    float lastAlertPercent; // فیلد جدید
} CryptoPosition;

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
    float sharpeRatio;
    float avgPositionSize;
    float riskExposure;
} PortfolioSummary;

// متغیرهای جدید برای اسکن شبکه
WiFiNetwork scannedNetworks[20];  // شبکه‌های اسکن شده
int scannedNetworkCount = 0;
bool isScanning = false;
unsigned long lastScanTime = 0;
//bool apEnabled = true; // وضعیت AP (پیش‌فرض روشن)

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
    byte displayRotation;  // 0-3
    
    float exitAlertPercent;
    bool exitAlertEnabled;
    bool exitAlertBlinkEnabled;
    
    int ledBrightness;
    bool ledEnabled;
    
    // RGB Settings
    bool rgb1Enabled;
    bool rgb2Enabled;
    int rgb1Brightness;
    int rgb2Brightness;
    int rgb1HistorySpeed;
    int rgb2Sensitivity;
    
    // Battery
    bool showBattery;
    int batteryWarningLevel;
    
    // Connection
    bool autoReconnect;
    int reconnectAttempts;
    
    byte magicNumber;
    bool configured;
    unsigned long firstBoot;
    int bootCount;
    unsigned long totalUptime;
} SystemSettings;

// ===== GLOBAL OBJECTS =====
TFT_eSPI tft = TFT_eSPI();
WebServer server(80);
HTTPClient http;

// ===== GLOBAL VARIABLES =====
SystemSettings settings;
byte powerSource = POWER_SOURCE_USB; // پیش‌فرض USB

// Mode Data
// تغییر: افزایش اندازه آرایه از 40 به 100
CryptoPosition cryptoDataMode1[MAX_POSITIONS_PER_MODE];
PortfolioSummary portfolioMode1;
AlertHistory alertHistoryMode1[MAX_ALERT_HISTORY];
int cryptoCountMode1 = 0;
int alertHistoryCountMode1 = 0;

// تغییر: افزایش اندازه آرایه از 40 به 100
CryptoPosition cryptoDataMode2[MAX_POSITIONS_PER_MODE];
PortfolioSummary portfolioMode2;
AlertHistory alertHistoryMode2[MAX_ALERT_HISTORY];
int cryptoCountMode2 = 0;
int alertHistoryCountMode2 = 0;
unsigned long lastAlertTime = 0;
#define ALERT_AUTO_RETURN_TIME 8000  // 8 seconds

// System State
bool isConnectedToWiFi = false;
bool apModeActive = false;
bool showingAlert = false;
bool resetInProgress = false;
bool displayInitialized = false;
bool timeSynced = false;
bool connectionLost = false;
unsigned long connectionLostTime = 0;

// Timing Variables
unsigned long lastDataUpdate = 0;
unsigned long lastRgb1Update = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long lastWiFiCheck = 0;
unsigned long lastAlertCheck = 0;
unsigned long alertDisplayStart = 0;
unsigned long lastBlinkTime = 0;
unsigned long lastRGB2Update = 0;
unsigned long systemStartTime = 0;
unsigned long lastBatteryCheck = 0;
unsigned long lastReconnectAttempt = 0;

// Current State
String currentDateTime = "";
String alertTitle = "";
String alertMessage = "";
String alertSymbol = "";
float alertPrice = 0.0;
bool alertIsLong = false;
bool alertIsSevere = false;
byte alertMode = 0;

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
int rgb1HistoryIndex = 0;
int rgb1ColorIndex = 0;
bool rgb1Active = true;
float rgb2CurrentPercent = 0.0;
bool rgb2AlertActive = false;

// Display Buffer
String displayBuffer[8];
int currentDisplayPage = 0;
int totalDisplayPages = 1;
bool displayNeedsUpdate = true;

// Battery
float batteryVoltage = 0.0;
int batteryPercent = 100;
bool batteryLow = false;

// API Statistics
int apiSuccessCount = 0;
int apiErrorCount = 0;
unsigned long lastApiCallTime = 0;
float apiAverageResponseTime = 0.0;

// Connection Statistics
int connectionLostCount = 0;
int reconnectSuccessCount = 0;
unsigned long totalDowntime = 0;

// ===== FUNCTION PROTOTYPES =====
// Display Functions
void setupDisplay();
void initDisplay();
void updateDisplay();
void showSplashScreen();
void showAlertDisplay();
void showMainDisplay();
void showConnectionScreen();
void showConnectionLostScreen();
void drawProgressBar(int x, int y, int width, int height, float percentage, uint32_t color);
void drawStatBox(int x, int y, int width, int height, String title, String value, uint32_t color);
void showDisplayMessage(String line1, String line2, String line3, String line4);
void drawBatteryIcon(int x, int y, int percent);
void setDisplayBacklight(bool state);
void setDisplayBrightness(int brightness);

// Buzzer Functions - ENHANCED
void setupBuzzer();
void setBuzzerVolume(int volume);
void increaseVolume(int step = 10);
void decreaseVolume(int step = 10);
void toggleBuzzer();
void playTone(int frequency, int duration);
void playVolumeFeedback();
void playLongPositionAlert(bool isSevere);
void playShortPositionAlert(bool isSevere);
void playExitAlertTone(bool isProfit);
void playPortfolioAlert();
void playTestAlertSequence();
void playResetAlertTone();
void playSuccessTone();
void playErrorTone();
void playStartupTone();
void playConnectionLostTone();
void playMelody();
void testVolumeRange();

// LED Functions
void setupLEDs();
void setupRGBLEDs();
void updateLEDs();
void updateRGBLEDs();
void setRGB1Color(uint8_t r, uint8_t g, uint8_t b);
void setRGB2Color(uint8_t r, uint8_t g, uint8_t b);
void turnOffRGB1();
void turnOffRGB2();
void calculateRGB2Color(float percentChange);
void displayHistoryOnRGB1();
void blinkLEDs();
void setAllLEDs(bool state);

// Alert Functions
void showAlert(String title, String symbol, String message, bool isLong, bool isSevere, float price, byte mode);
void showExitAlert(String title, String symbol, String message, bool isProfit, float changePercent, float price);
void checkAlerts(byte mode);
void processEntryAlerts();
void processExitAlerts();
void resetAllAlerts();
void addToAlertHistory(const char* symbol, float pnlPercent, float price, bool isLong, bool isSevere, bool isProfit, byte alertType, byte mode);

// Data Processing Functions
void parseCryptoData(String jsonData, byte mode);
String getPortfolioData(byte mode);
String base64Encode(String data);
void sortPositionsByLoss(CryptoPosition* data, int count);
void calculatePortfolioSummary(byte mode);
void clearCryptoData(byte mode);

// Utility Functions
String getShortSymbol(const char* symbol);
String formatPercent(float percent);
String formatNumber(float number);
String formatPrice(float price);
String formatTime(unsigned long timestamp);
String formatDateTime(unsigned long timestamp);
String getTimeString(unsigned long timestamp);
String urlDecode(String str);
String urlEncode(String str);
String getWiFiQuality(int rssi);
String getUptimeString();

// Time Functions
void updateDateTime();
bool syncTime();
String getCurrentTime();
String getCurrentDate();
unsigned long getCurrentTimestamp();

// Settings Functions
void initializeSettings();
bool loadSettings();
bool saveSettings();
bool verifySettings();
void recoverEEPROM();

// WiFi Functions - ENHANCED
bool addWiFiNetwork(const char* ssid, const char* password);
bool removeWiFiNetwork(const char* ssid);
bool connectToWiFi();
bool connectToBestWiFi();
bool connectToSpecificWiFi(int networkIndex);
bool addOrUpdateWiFiNetwork(const char* ssid, const char* password, byte priority = 5, bool autoConnect = true);
void removeWiFiNetwork(int index);
void reorderWiFiNetworks();
bool prepareForScan();
void scanWiFiNetworks(bool forceScan = false);
void updateWiFiMode();
bool startAPMode();
void handleWiFiConnection();
void checkWiFiStatus();
void checkConnectionStatus();
void attemptReconnect();
void saveAPState();
void loadAPState();

// Web Server Functions
void setupWebServer();
void handleRoot();
void handleSetup();
void handleSaveWiFi();
void handleRemoveWiFi();
void handleWiFiManage();
void handleWiFiScan();
void handleSaveWiFiFromScan();
void handleSaveAPI();
void handleSaveAlert();
void handleSaveMode();
void handleSaveDisplay();
void handleSaveRGB();
void handleSaveEmergency();
void handleRefresh();
void handleTestAlert();
void handleResetAlerts();
void handleSystemInfo();
void handleAPIStatus();
void handleLEDControl();
void handleRGBControl();
void handleDisplayControl();
void handleFactoryReset();
void handleRestart();
void handlePositions();
void handleToggleAP();
void handleSetVolume();
void handleTestVolume();
String generateDashboardHTML();
String generateSystemInfoHTML();
String generateAlertHistoryHTML(byte mode);
String generatePositionListHTML(byte mode);
String generateSetupHTML();

// Reset Button Functions
void setupResetButton();
void checkResetButton();
void factoryReset();
void restartSystem();

// API Functions
void makeAPICall(byte mode);
void handleAPIResponse(String response, byte mode);
void updateAPIStatistics(bool success, unsigned long responseTime);

// Battery Functions
void checkBattery();
float readBatteryVoltage();
int calculateBatteryPercent(float voltage);
void updateBatteryDisplay();
void detectPowerSource();
void calibrateBattery();

// Debug Functions
void printSystemStatus();
void debugMemory();
void logEvent(String event, String details = "");
void emergencyWiFiRecovery();

// ===== NEW: WiFi MODE MANAGEMENT =====
void manageWiFiMode() {
    static bool lastConnectedState = false;
    static bool lastAPState = false;
    static unsigned long lastChangeTime = 0;
    
    unsigned long now = millis();
    
    // جلوگیری از تغییرات مکرر (حداقل 10 ثانیه فاصله)
    if (now - lastChangeTime < 10000) {
        return;
    }
    
    bool connectedNow = (WiFi.status() == WL_CONNECTED);
    
    // فقط اگر واقعاً تغییر کرده باشد
    if (connectedNow != lastConnectedState || apEnabled != lastAPState) {
        lastChangeTime = now;
        
        Serial.println("\n🔄 WiFi Mode Change Detected");
        Serial.println("  Connected: " + String(connectedNow ? "YES" : "NO"));
        Serial.println("  AP Enabled: " + String(apEnabled ? "YES" : "NO"));
        
        // اگر WiFi متصل است و AP غیرفعال است
        if (connectedNow && !apEnabled) {
            WiFi.mode(WIFI_STA);
            Serial.println("  Mode: STA Only");
        }
        // اگر AP فعال است
        else if (apEnabled) {
            if (connectedNow) {
                // حالت Hybrid
                WiFi.mode(WIFI_AP_STA);
                String apSSID = "PortfolioMonitor_" + String((uint32_t)ESP.getEfuseMac(), HEX);
                WiFi.softAP(apSSID.c_str(), "12345678");
                apModeActive = true;
                Serial.println("  Mode: AP+STA Hybrid");
                Serial.println("  AP IP: " + WiFi.softAPIP().toString());
            } else {
                // فقط AP
                WiFi.mode(WIFI_AP);
                String apSSID = "PortfolioMonitor_" + String((uint32_t)ESP.getEfuseMac(), HEX);
                WiFi.softAP(apSSID.c_str(), "12345678");
                apModeActive = true;
                Serial.println("  Mode: AP Only");
                Serial.println("  AP IP: " + WiFi.softAPIP().toString());
            }
        }
        
        lastConnectedState = connectedNow;
        lastAPState = apEnabled;
    }
}




// ===== DISPLAY FUNCTIONS =====
void setupDisplay() {
    Serial.println("Initializing ST7789 240x240 IPS Display...");
    
    pinMode(TFT_BL_PIN, OUTPUT);
    digitalWrite(TFT_BL_PIN, HIGH); // روشن کردن backlight
    delay(100);
    
    tft.init();
    tft.setRotation(settings.displayRotation);
    tft.fillScreen(TFT_BLACK);
    
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(1);
    tft.setTextWrap(false);
    
    displayInitialized = true;
    Serial.println("Display initialized successfully (Backlight on pin " + String(TFT_BL_PIN) + ")");
    
    showSplashScreen();
}

void initDisplay() {
    if (!displayInitialized) {
        setupDisplay();
    }
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
    tft.println("Complete v4.5.2");
    
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setCursor(20, 130);
    tft.println("ESP32-WROVER-E");
    tft.setCursor(30, 145);
    tft.println("Enhanced Volume");
    
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
    
    // برگشت اتوماتیک از آلرت بعد از 8 ثانیه
    if (showingAlert) {
        if (now - alertDisplayStart > ALERT_AUTO_RETURN_TIME) {
            showingAlert = false;
            alertTitle = "";
            alertMessage = "";
            lastAlertTime = 0;
            showMainDisplay();
            return;
        }
        
        showAlertDisplay();
        return;
    }
    
    // پیشگیری از خاموش شدن هنگام آلرت
    static unsigned long lastInteraction = millis();
    
    // اگر آلرت اخیراً فعال بوده، زمان تعامل را به‌روز کن
    if (now - lastAlertTime < 10000) {
        lastInteraction = now;
    }
    
    // کنترل backlight بر اساس timeout
    if (settings.displayTimeout > 0) {
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
    
    int timeLeft = (ALERT_AUTO_RETURN_TIME - (millis() - alertDisplayStart)) / 1000;
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(30, 200);
    tft.print("Auto-close: ");
    tft.print(timeLeft);
    tft.print("s");
}

void showMainDisplay() {
    // کنترل backlight
    if (settings.displayBrightness > 0) {
        digitalWrite(TFT_BL_PIN, HIGH);
    } else {
        digitalWrite(TFT_BL_PIN, LOW);
        return;
    }
    
    tft.fillScreen(TFT_BLACK);
    
    // نمایش هدر
    tft.setTextSize(2);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setCursor(5, 5);
    tft.print("PORTFOLIO");
    
    // نمایش وضعیت WiFi
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
    
    // نمایش زمان
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
    
    // نمایش وضعیت سیستم
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
    tft.print(formatPercent(portfolioMode1.totalPnlPercent));
    
    // Exit Mode
    tft.setTextColor(TFT_ORANGE, TFT_BLACK);
    tft.setCursor(5, 110);
    tft.print("EXIT:");
    
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(60, 110);
    tft.print(String(cryptoCountMode2) + " pos");
    
    tft.setTextColor(portfolioMode2.totalPnlPercent >= 0 ? TFT_GREEN : TFT_RED, TFT_BLACK);
    tft.setCursor(120, 110);
    tft.print(formatPercent(portfolioMode2.totalPnlPercent));
    
    // Separator
    tft.drawFastHLine(0, 130, 240, TFT_DARKGREY);
    
    // نمایش مجموع
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
    
    // نمایش وضعیت سیستم در پایین
    tft.drawFastHLine(0, 170, 240, TFT_DARKGREY);
    
    tft.setTextSize(1);
    
    // وضعیت آلرت
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
    
    // منبع تغذیه
    if (powerSource == POWER_SOURCE_USB) {
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        tft.setCursor(60, 185);
        tft.print("USB");
    } else if (settings.showBattery) {
        drawBatteryIcon(60, 185, batteryPercent);
    }
    
    // حجم بازر
    tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
    tft.setCursor(120, 185);
    tft.print("Vol:");
    tft.print(settings.buzzerVolume);
    tft.print("%");
    
    // وضعیت اتصال
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

// نسخه جایگزین نمایشگر با حالت دو بخشی
void showMainDisplayAlt() {
    if (settings.displayBrightness > 0) {
        digitalWrite(TFT_BL_PIN, HIGH);
    } else {
        digitalWrite(TFT_BL_PIN, LOW);
    }
    
    tft.fillScreen(TFT_BLACK);
    
    // تقسیم صفحه به دو بخش
    tft.drawFastHLine(0, 119, 240, TFT_DARKGREY);
    
    // بخش بالایی - Entry Mode
    tft.setTextSize(2);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setCursor(10, 10);
    tft.print("ENTRY");
    
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(10, 40);
    tft.print("Positions:");
    tft.setCursor(80, 40);
    tft.print(cryptoCountMode1);
    
    tft.setCursor(10, 60);
    tft.print("P/L Total:");
    tft.setTextColor(portfolioMode1.totalPnlPercent >= 0 ? TFT_GREEN : TFT_RED, TFT_BLACK);
    tft.setCursor(80, 60);
    tft.print(formatPercent(portfolioMode1.totalPnlPercent));
    
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(10, 80);
    tft.print("Value:");
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setCursor(80, 80);
    tft.print("$");
    tft.print(formatNumber(portfolioMode1.totalCurrentValue));
    
    // نمایش بهترین و بدترین موقعیت Entry
    if (cryptoCountMode1 > 0) {
        float bestPnl = -9999, worstPnl = 9999;
        int bestIdx = -1, worstIdx = -1;
        
        for (int i = 0; i < cryptoCountMode1; i++) {
            if (cryptoDataMode1[i].changePercent > bestPnl) {
                bestPnl = cryptoDataMode1[i].changePercent;
                bestIdx = i;
            }
            if (cryptoDataMode1[i].changePercent < worstPnl) {
                worstPnl = cryptoDataMode1[i].changePercent;
                worstIdx = i;
            }
        }
        
        tft.setTextSize(1);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.setCursor(10, 100);
        tft.print("Best:");
        
        if (bestIdx >= 0) {
            tft.setCursor(50, 100);
            tft.print(getShortSymbol(cryptoDataMode1[bestIdx].symbol));
            tft.setTextColor(TFT_GREEN, TFT_BLACK);
            tft.setCursor(100, 100);
            tft.print(formatPercent(bestPnl));
        }
    }
    
    // بخش پایینی - Exit Mode
    tft.setTextSize(2);
    tft.setTextColor(TFT_ORANGE, TFT_BLACK);
    tft.setCursor(10, 130);
    tft.print("EXIT");
    
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(10, 160);
    tft.print("Positions:");
    tft.setCursor(80, 160);
    tft.print(cryptoCountMode2);
    
    tft.setCursor(10, 180);
    tft.print("P/L Total:");
    tft.setTextColor(portfolioMode2.totalPnlPercent >= 0 ? TFT_GREEN : TFT_RED, TFT_BLACK);
    tft.setCursor(80, 180);
    tft.print(formatPercent(portfolioMode2.totalPnlPercent));
    
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(10, 200);
    tft.print("Value:");
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setCursor(80, 200);
    tft.print("$");
    tft.print(formatNumber(portfolioMode2.totalCurrentValue));
    
    // نمایش وضعیت WiFi و زمان در گوشه
    tft.setTextSize(1);
    if (isConnectedToWiFi) {
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.setCursor(160, 10);
        tft.print("WiFi");
    } else {
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.setCursor(160, 10);
        tft.print("No WiFi");
    }
    
    if (currentDateTime.length() > 10) {
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        tft.setCursor(160, 25);
        tft.print(currentDateTime.substring(11, 19));
    }
    
    // نمایش حجم
    tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
    tft.setCursor(160, 40);
    tft.print("Vol:");
    tft.print(settings.buzzerVolume);
    tft.print("%");
    
    // نمایش وضعیت باتری
    if (powerSource == POWER_SOURCE_USB) {
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        tft.setCursor(160, 55);
        tft.print("USB");
    } else if (settings.showBattery) {
        drawBatteryIcon(160, 55, batteryPercent);
    }
}

void showConnectionScreen() {
    tft.fillScreen(TFT_BLACK);
    
    tft.drawRect(0, 0, 239, 239, TFT_CYAN);
    tft.drawRect(1, 1, 237, 237, TFT_BLUE);
    
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(30, 50);
    tft.println("CONNECTING");
    tft.setCursor(50, 80);
    tft.println("TO WiFi");
    
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(40, 120);
    tft.println("Please wait...");
    
    // Progress dots area
    tft.fillRect(90, 150, 60, 15, TFT_BLACK);
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
void drawProgressBar(int x, int y, int width, int height, float percentage, uint32_t color) {
    tft.drawRect(x, y, width, height, TFT_WHITE);
    
    int fillWidth = (int)((width - 2) * (percentage / 100.0));
    fillWidth = constrain(fillWidth, 0, width - 2);
    
    if (fillWidth > 0) {
        tft.fillRect(x + 1, y + 1, fillWidth, height - 2, color);
    }
}

void drawStatBox(int x, int y, int width, int height, String title, String value, uint32_t color) {
    tft.drawRect(x, y, width, height, color);
    
    tft.setTextColor(color, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(x + 5, y + 5);
    tft.print(title);
    
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(x + 5, y + 20);
    tft.print(value);
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

void setDisplayBacklight(bool state) {
    if (state && settings.displayBrightness > 0) {
        digitalWrite(TFT_BL_PIN, HIGH);
    } else {
        digitalWrite(TFT_BL_PIN, LOW);
    }
}

void setDisplayBrightness(int brightness) {
    settings.displayBrightness = constrain(brightness, 0, 100);
    
    if (settings.displayBrightness > 0) {
        digitalWrite(TFT_BL_PIN, HIGH);
    } else {
        digitalWrite(TFT_BL_PIN, LOW);
    }
    
    saveSettings();
    Serial.println("Display brightness set to " + String(settings.displayBrightness) + "%");
}

// ===== BUZZER FUNCTIONS (ENHANCED) =====
void setupBuzzer() {
    Serial.println("Initializing buzzer on GPIO " + String(BUZZER_PIN) + "...");
    
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
    
    // Test buzzer
    if (settings.buzzerEnabled && settings.buzzerVolume > 0) {
        playVolumeFeedback();
    }
    
    Serial.println("Buzzer initialized on pin " + String(BUZZER_PIN));
}

void setBuzzerVolume(int volume) {
    settings.buzzerVolume = constrain(volume, VOLUME_MIN, VOLUME_MAX);
    Serial.print("Buzzer volume set to: ");
    Serial.print(settings.buzzerVolume);
    Serial.println("%");
    
    // بازخورد صوتی
    playVolumeFeedback();
    saveSettings();
}

void increaseVolume(int step) {
    int newVolume = settings.buzzerVolume + step;
    if (newVolume > VOLUME_MAX) newVolume = VOLUME_MAX;
    setBuzzerVolume(newVolume);
}

void decreaseVolume(int step) {
    int newVolume = settings.buzzerVolume - step;
    if (newVolume < VOLUME_MIN) newVolume = VOLUME_MIN;
    setBuzzerVolume(newVolume);
}

void toggleBuzzer() {
    settings.buzzerEnabled = !settings.buzzerEnabled;
    Serial.print("Buzzer ");
    Serial.println(settings.buzzerEnabled ? "enabled" : "disabled");
    
    // بازخورد صوتی
    if (settings.buzzerEnabled) {
        playTone(1000, 100);
        delay(120);
        playTone(1200, 100);
    }
    
    saveSettings();
}

void playTone(int frequency, int durationMs) {
    if (!settings.buzzerEnabled || settings.buzzerVolume == 0) {
        return;
    }
    
    // محاسبه مدت زمان واقعی بر اساس حجم
    int actualDuration = map(settings.buzzerVolume, 0, 100, 0, durationMs);
    
    if (actualDuration <= 0) {
        return;
    }
    
    // روش 1: برای حجم‌های پایین - پالس‌های کوتاه
    if (settings.buzzerVolume < 30) {
        int pulseCount = actualDuration / 30;
        int pulseDuration = 20;
        int pauseDuration = 30 - pulseDuration;
        
        for (int i = 0; i < pulseCount; i++) {
            tone(BUZZER_PIN, frequency, pulseDuration);
            delay(pauseDuration);
        }
    }
    // روش 2: برای حجم‌های متوسط
    else if (settings.buzzerVolume < 70) {
        tone(BUZZER_PIN, frequency, actualDuration);
        delay(actualDuration + 10);
    }
    // روش 3: برای حجم‌های بالا - کامل
    else {
        tone(BUZZER_PIN, frequency, durationMs);
        delay(durationMs + 10);
    }
}

void playVolumeFeedback() {
    if (!settings.buzzerEnabled) return;
    
    // فرکانس بر اساس حجم (هر چه بلندتر، فرکانس بالاتر)
    int freq = map(settings.buzzerVolume, 0, 100, 300, 1500);
    
    // مدت زمان بر اساس حجم
    int duration = map(settings.buzzerVolume, 0, 100, 50, 200);
    
    tone(BUZZER_PIN, freq, duration);
    delay(duration + 20);
}

void playLongPositionAlert(bool isSevere) {
    if (!settings.buzzerEnabled || settings.buzzerVolume == 0) return;
    
    Serial.println("Playing LONG alert" + String(isSevere ? " (SEVERE)" : ""));
    
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
    
    Serial.println("Playing SHORT alert" + String(isSevere ? " (SEVERE)" : ""));
    
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
    
    Serial.println("Playing EXIT alert for " + String(isProfit ? "PROFIT" : "LOSS"));
    
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

void playPortfolioAlert() {
    if (!settings.buzzerEnabled) return;
    
    Serial.println("Playing PORTFOLIO alert");
    
    for (int i = 0; i < 3; i++) {
        playTone(587, 200);
        delay(250);
    }
}

void playTestAlertSequence() {
    if (!settings.buzzerEnabled) {
        Serial.println("Buzzer disabled, skipping test");
        return;
    }
    
    Serial.println("Playing test sequence...");
    
    playLongPositionAlert(false);
    delay(800);
    playShortPositionAlert(false);
    delay(800);
    playExitAlertTone(true);
    delay(800);
    playExitAlertTone(false);
    delay(800);
    
    playTone(1047, 100);
    delay(120);
    playTone(1319, 150);
    delay(200);
    
    Serial.println("Test sequence complete");
}

void playResetAlertTone() {
    if (!settings.buzzerEnabled) return;
    
    Serial.println("Playing reset tone");
    
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
    
    Serial.println("Playing connection lost tone");
    
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

void playMelody() {
    Serial.println("🎵 Playing melody...");
    
    // نت‌های ملودی ساده
    int melody[] = {262, 294, 330, 349, 392, 440, 494, 523};
    int durations[] = {200, 200, 200, 200, 300, 300, 200, 400};
    
    for (int i = 0; i < 8; i++) {
        playTone(melody[i], durations[i]);
        delay(50);
    }
}

void testVolumeRange() {
    Serial.println("\n🔊 Testing volume range (0-100%):");
    
    int originalVolume = settings.buzzerVolume;
    
    for (int vol = 0; vol <= 100; vol += 10) {
        settings.buzzerVolume = vol;
        Serial.print("Volume: ");
        Serial.print(vol);
        Serial.print("% | ");
        
        // Test with three different frequencies
        if (vol > 0) {
            playTone(440, 100);  // A
            delay(150);
            playTone(523, 100);  // C
            delay(150);
            playTone(659, 100);  // E
        }
        
        delay(300);
        Serial.println("✅");
    }
    
    // Return to original volume
    settings.buzzerVolume = originalVolume;
}

// ===== LED FUNCTIONS =====
void setupLEDs() {
    Serial.println("Initializing LEDs...");
    
    pinMode(LED_MODE1_GREEN, OUTPUT);
    pinMode(LED_MODE1_RED, OUTPUT);
    pinMode(LED_MODE2_GREEN, OUTPUT);
    pinMode(LED_MODE2_RED, OUTPUT);
    
    digitalWrite(LED_MODE1_GREEN, LOW);
    digitalWrite(LED_MODE1_RED, LOW);
    digitalWrite(LED_MODE2_GREEN, LOW);
    digitalWrite(LED_MODE2_RED, LOW);
    
    Serial.println("LEDs initialized");
}

void setupRGBLEDs() {
    Serial.println("Initializing RGB LEDs...");
    
    pinMode(RGB1_RED, OUTPUT);
    pinMode(RGB1_GREEN, OUTPUT);
    pinMode(RGB1_BLUE, OUTPUT);
    pinMode(RGB2_RED, OUTPUT);
    pinMode(RGB2_GREEN, OUTPUT);
    pinMode(RGB2_BLUE, OUTPUT);
    
    // ⬅️ خاموش کردن LED‌ها در ابتدا
    turnOffRGB1();
    turnOffRGB2();
    
    Serial.println("RGB LEDs initialized and turned OFF");
}

void setRGB2Color(uint8_t r, uint8_t g, uint8_t b) {
    if (!settings.rgb2Enabled) {
        turnOffRGB2();
        return;
    }
    
    r = map(r, 0, 255, 255, 0);
    g = map(g, 0, 255, 255, 0);
    b = map(b, 0, 255, 255, 0);
    
    r = 255 - ((255 - r) * settings.rgb2Brightness / 100);
    g = 255 - ((255 - g) * settings.rgb2Brightness / 100);
    b = 255 - ((255 - b) * settings.rgb2Brightness / 100);
    
    analogWrite(RGB2_RED, constrain(r, 0, 255));
    analogWrite(RGB2_GREEN, constrain(g, 0, 255));
    analogWrite(RGB2_BLUE, constrain(b, 0, 255));
}

void turnOffRGB2() {
    analogWrite(RGB2_RED, 255);
    analogWrite(RGB2_GREEN, 255);
    analogWrite(RGB2_BLUE, 255);
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
    static unsigned long lastBlink = 0;
    static bool blinkState = false;
    unsigned long now = millis();
    
    // اگر WiFi وصل است
    if (isConnectedToWiFi) {
        // 🟢 سبز ثابت برای WiFi متصل
        setRGB1Color(0, 255, 0);  // سبز
        setRGB2Color(0, 255, 0);  // سبز
        return;
    }
    
    // اگر AP فعال است
    if (apModeActive) {
        // 🔴 قرمز ثابت برای AP Mode
        setRGB1Color(255, 0, 0);  // قرمز
        setRGB2Color(255, 0, 0);  // قرمز
        return;
    }
    
    // 🔵 آبی چشمک‌زن برای در حال اتصال
    if (now - lastBlink > 500) {
        lastBlink = now;
        blinkState = !blinkState;
        
        if (blinkState) {
            setRGB1Color(0, 0, 255);  // آبی
            setRGB2Color(0, 0, 255);  // آبی
        } else {
            turnOffRGB1();  // خاموش
            turnOffRGB2();  // خاموش
        }
    }
}

void setRGB1Color(uint8_t r, uint8_t g, uint8_t b) {
    if (!settings.rgb1Enabled) {
        turnOffRGB1();
        return;
    }
    
    r = (r * settings.rgb1Brightness) / 100;
    g = (g * settings.rgb1Brightness) / 100;
    b = (b * settings.rgb1Brightness) / 100;
    
    analogWrite(RGB1_RED, constrain(r, 0, 255));
    analogWrite(RGB1_GREEN, constrain(g, 0, 255));
    analogWrite(RGB1_BLUE, constrain(b, 0, 255));
}

void turnOffRGB1() {
    digitalWrite(RGB1_RED, LOW);
    digitalWrite(RGB1_GREEN, LOW);
    digitalWrite(RGB1_BLUE, LOW);
}

void calculateRGB2Color(float percentChange) {
    if (!settings.rgb2Enabled) {
        turnOffRGB2();
        return;
    }
    
    uint8_t r = 0, g = 0, b = 0;
    float absPercent = abs(percentChange);
    
    if (percentChange >= 0) {
        if (percentChange <= 10.0) {
            float ratio = percentChange / 10.0;
            r = (uint8_t)(255 * (1.0 - ratio));
            g = 255;
            b = (uint8_t)(255 * ratio);
        } else if (percentChange <= 20.0) {
            float ratio = (percentChange - 10.0) / 10.0;
            r = 0;
            g = (uint8_t)(255 * (1.0 - ratio * 0.5));
            b = 255;
        } else {
            r = 0;
            g = 128;
            b = 255;
        }
    } else {
        if (absPercent <= 10.0) {
            float ratio = absPercent / 10.0;
            r = 255;
            g = (uint8_t)(255 * (1.0 - ratio * 0.35));
            b = 0;
        } else if (absPercent <= 20.0) {
            float ratio = (absPercent - 10.0) / 10.0;
            r = 255;
            g = (uint8_t)(165 * (1.0 - ratio));
            b = 0;
        } else {
            r = 255;
            g = 0;
            b = 0;
        }
    }
    
    setRGB2Color(r, g, b);
}

void displayHistoryOnRGB1() {
    if (!settings.rgb1Enabled) {
        turnOffRGB1();
        return;
    }
    
    int totalAlerts = alertHistoryCountMode1 + alertHistoryCountMode2;
    
    if (totalAlerts == 0) {
        static uint8_t breathValue = 0;
        static bool breathingUp = true;
        
        if (breathingUp) {
            breathValue += 5;
            if (breathValue >= 100) breathingUp = false;
        } else {
            breathValue -= 5;
            if (breathValue <= 10) breathingUp = true;
        }
        
        setRGB1Color(0, 0, breathValue);
        return;
    }
    
    rgb1ColorIndex = (rgb1ColorIndex + 1) % 6;
    
    switch (rgb1ColorIndex) {
        case 0:
            setRGB1Color(0, 255, 0);
            break;
        case 1:
            setRGB1Color(255, 0, 0);
            break;
        case 2:
            setRGB1Color(0, 100, 255);
            break;
        case 3:
            setRGB1Color(255, 200, 0);
            break;
        case 4:
            setRGB1Color(255, 0, 255);
            break;
        case 5:
            turnOffRGB1();
            break;
    }
}

void blinkLEDs() {
    static unsigned long lastBlink = 0;
    if (millis() - lastBlink > 500) {
        lastBlink = millis();
        blinkState = !blinkState;
    }
}

void setAllLEDs(bool state) {
    digitalWrite(LED_MODE1_GREEN, state ? HIGH : LOW);
    digitalWrite(LED_MODE1_RED, state ? HIGH : LOW);
    digitalWrite(LED_MODE2_GREEN, state ? HIGH : LOW);
    digitalWrite(LED_MODE2_RED, state ? HIGH : LOW);
}

// ===== ALERT FUNCTIONS =====
void showAlert(String title, String symbol, String message, bool isLong, bool isSevere, float price, byte mode) {
    alertTitle = title;
    alertSymbol = symbol;
    alertMessage = message;
    alertPrice = price;
    alertIsLong = isLong;
    alertIsSevere = isSevere;
    alertMode = mode;
    showingAlert = true;
    alertDisplayStart = millis();
    
    Serial.println("\n🚨 ALERT TRIGGERED 🚨");
    Serial.println("Title: " + title);
    Serial.println("Symbol: " + symbol);
    Serial.println("Message: " + message);
    Serial.println("Price: " + formatPrice(price));
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
    
    addToAlertHistory(symbol.c_str(), 
                     mode == 0 ? portfolioMode1.totalPnlPercent : portfolioMode2.totalPnlPercent,
                     price, 
                     isLong, 
                     isSevere, 
                     message.indexOf("PROFIT") != -1,
                     isSevere ? 2 : 1,
                     mode);
    
    displayNeedsUpdate = true;
    
    lastAlertTime = millis();
}

void showExitAlert(String title, String symbol, String message, bool isProfit, float changePercent, float price) {
    alertTitle = title;
    alertSymbol = symbol;
    alertMessage = message;
    alertPrice = price;
    alertIsLong = isProfit;
    alertMode = 1;
    showingAlert = true;
    alertDisplayStart = millis();
    
    Serial.println("\n💰 EXIT ALERT 💰");
    Serial.println("Title: " + title);
    Serial.println("Symbol: " + symbol);
    Serial.println("Message: " + message);
    Serial.println("Price: " + formatPrice(price));
    Serial.println("Change: " + String(changePercent, 1) + "%");
    Serial.println("Profit: " + String(isProfit ? "YES" : "NO"));
    
    if (settings.buzzerEnabled) {
        playExitAlertTone(isProfit);
    }
    
    mode2AlertSymbol = symbol;
    mode2AlertPercent = isProfit ? changePercent : -changePercent;
    
    if (isProfit) {
        mode2GreenActive = true;
        mode2RedActive = false;
    } else {
        mode2GreenActive = false;
        mode2RedActive = true;
    }
    
    rgb2CurrentPercent = mode2AlertPercent;
    rgb2AlertActive = true;
    
    addToAlertHistory(symbol.c_str(), 
                     isProfit ? changePercent : -changePercent,
                     price, 
                     true,
                     false, 
                     isProfit,
                     isProfit ? 3 : 4,
                     1);
    
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
    unsigned long cooldownPeriod = 300000; // 5 دقیقه خنک‌سازی
    
    for (int i = 0; i < cryptoCountMode1; i++) {
        CryptoPosition* pos = &cryptoDataMode1[i];
        
        // بررسی زمان خنک‌سازی
        if (pos->lastAlertTime > 0 && (currentTime - pos->lastAlertTime) < cooldownPeriod) {
            continue; // در دوره خنک‌سازی هستیم
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
        
        // ریست اتوماتیک اگر بهبود یافت
        float resetThreshold = settings.alertThreshold + 2.0; // 2% بالاتر از آستانه
        if (pos->alerted && pos->changePercent > resetThreshold) {
            pos->alerted = false;
            pos->severeAlerted = false;
            pos->hasAlerted = false;
            pos->lastAlertTime = 0;
            Serial.println("Alert auto-reset for " + getShortSymbol(pos->symbol) + 
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
            
            showExitAlert("PRICE ALERT",
                         getShortSymbol(pos->symbol),
                         message,
                         isProfit,
                         priceChangePercent,
                         pos->currentPrice);
            
            pos->exitAlerted = true;
            pos->exitAlertTime = millis();
            pos->exitAlertLastPrice = pos->currentPrice;
        }
    }
}

void resetAllAlerts() {
    Serial.println("Resetting all alerts...");
    
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
    
    Serial.println("All alerts reset");
}

void addToAlertHistory(const char* symbol, float pnlPercent, float price, bool isLong, bool isSevere, bool isProfit, byte alertType, byte mode) {
    AlertHistory* history;
    int* count;
    
    if (mode == 0) {
        history = alertHistoryMode1;
        count = &alertHistoryCountMode1;
    } else {
        history = alertHistoryMode2;
        count = &alertHistoryCountMode2;
    }
    
    if (*count >= MAX_ALERT_HISTORY) {
        for (int i = 0; i < MAX_ALERT_HISTORY - 1; i++) {
            history[i] = history[i + 1];
        }
        (*count)--;
    }
    
    AlertHistory* newAlert = &history[*count];
    
    strncpy(newAlert->symbol, symbol, 15);
    newAlert->symbol[15] = '\0';
    
    newAlert->pnlPercent = pnlPercent;
    newAlert->alertPrice = price;
    newAlert->alertTime = millis();
    newAlert->isLong = isLong;
    newAlert->isSevere = isSevere;
    newAlert->isProfit = isProfit;
    newAlert->alertType = alertType;
    newAlert->alertMode = mode;
    newAlert->acknowledged = false;
    
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 100)) {
        strftime(newAlert->timeString, 20, "%m/%d %H:%M:%S", &timeinfo);
    } else {
        snprintf(newAlert->timeString, 20, "%lu", millis() / 1000);
    }
    
    (*count)++;
    
    Serial.println("Alert added to history: " + String(symbol) + " (" + String(mode == 0 ? "ENTRY" : "EXIT") + ")");
}

// ===== DATA PROCESSING FUNCTIONS =====
void parseCryptoData(String jsonData, byte mode) {
    if (jsonData.length() < 10 || jsonData == "{}") {
        Serial.println("Empty or invalid JSON data for mode " + String(mode));
        return;
    }
    
    // تغییر: استفاده از بافر بزرگتر 8192 بایت
    DynamicJsonDocument doc(JSON_BUFFER_SIZE);
    DeserializationError error = deserializeJson(doc, jsonData);
    
    if (error) {
        Serial.println("JSON Parse Error for mode " + String(mode) + ": " + String(error.c_str()));
        return;
    }
    
    if (!doc.containsKey("portfolio")) {
        Serial.println("No 'portfolio' field in JSON for mode " + String(mode));
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
    
    memset(targetData, 0, sizeof(CryptoPosition) * MAX_POSITIONS_PER_MODE);
    
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
    }
    
    if (doc.containsKey("summary")) {
        JsonObject summary = doc["summary"];
        
        targetSummary->totalInvestment = summary["total_investment"] | 0.0;
        targetSummary->totalCurrentValue = summary["total_current_value"] | 0.0;
        targetSummary->totalPnl = summary["total_pnl"] | 0.0;
        
        if (targetSummary->totalInvestment > 0) {
            targetSummary->totalPnlPercent = ((targetSummary->totalCurrentValue - targetSummary->totalInvestment) / 
                                            targetSummary->totalInvestment) * 100;
        } else {
            targetSummary->totalPnlPercent = 0.0;
        }
        
        targetSummary->totalPositions = *targetCount;
        targetSummary->longPositions = summary["long_positions"] | 0;
        targetSummary->shortPositions = summary["short_positions"] | 0;
        targetSummary->winningPositions = summary["winning_positions"] | 0;
        targetSummary->losingPositions = summary["losing_positions"] | 0;
        targetSummary->maxDrawdown = summary["max_drawdown"] | 0.0;
        targetSummary->sharpeRatio = summary["sharpe_ratio"] | 0.0;
    }
    
    Serial.println("Mode " + String(mode) + " data parsed: " + String(*targetCount) + " positions");
}

String getPortfolioData(byte mode) {
    if (!isConnectedToWiFi) {
        Serial.println("Cannot fetch data: WiFi not connected");
        return "{}";
    }
    
    if (strlen(settings.server) == 0 || strlen(settings.username) == 0) {
        Serial.println("Cannot fetch data: API not configured");
        return "{}";
    }
    
    String portfolioName = (mode == 0) ? String(settings.entryPortfolio) : String(settings.exitPortfolio);
    String url = String(settings.server) + "/api/device/portfolio/" + 
                String(settings.username) + "?portfolio_name=" + portfolioName;
    
    Serial.println("Fetching data for " + portfolioName + " from: " + url);
    
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
        updateAPIStatistics(true, responseTime);
        Serial.println("Data fetched successfully for " + portfolioName + " (" + String(response.length()) + " bytes)");
    } else {
        updateAPIStatistics(false, responseTime);
        Serial.println("HTTP Error: " + String(httpCode) + " for " + portfolioName);
    }
    
    http.end();
    return response;
}

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

void sortPositionsByLoss(CryptoPosition* data, int count) {
    for (int i = 0; i < count - 1; i++) {
        for (int j = 0; j < count - i - 1; j++) {
            if (data[j].changePercent > data[j + 1].changePercent) {
                CryptoPosition temp = data[j];
                data[j] = data[j + 1];
                data[j + 1] = temp;
            }
        }
    }
}

void calculatePortfolioSummary(byte mode) {
    PortfolioSummary* summary = (mode == 0) ? &portfolioMode1 : &portfolioMode2;
    CryptoPosition* data = (mode == 0) ? cryptoDataMode1 : cryptoDataMode2;
    int count = (mode == 0) ? cryptoCountMode1 : cryptoCountMode2;
    
    if (count == 0) {
        memset(summary, 0, sizeof(PortfolioSummary));
        return;
    }
    
    float totalValue = 0;
    float totalPnl = 0;
    int longCount = 0;
    int shortCount = 0;
    int winningCount = 0;
    int losingCount = 0;
    float maxLoss = 0;
    
    for (int i = 0; i < count; i++) {
        totalValue += data[i].currentPrice * data[i].quantity;
        totalPnl += data[i].pnlValue;
        
        if (data[i].isLong) longCount++;
        else shortCount++;
        
        if (data[i].changePercent >= 0) winningCount++;
        else losingCount++;
        
        if (data[i].changePercent < maxLoss) maxLoss = data[i].changePercent;
    }
    
    summary->totalCurrentValue = totalValue;
    summary->totalPnl = totalPnl;
    summary->totalPositions = count;
    summary->longPositions = longCount;
    summary->shortPositions = shortCount;
    summary->winningPositions = winningCount;
    summary->losingPositions = losingCount;
    summary->maxDrawdown = maxLoss;
    
    summary->totalInvestment = totalValue - totalPnl;
    
    if (summary->totalInvestment > 0) {
        summary->totalPnlPercent = (totalPnl / summary->totalInvestment) * 100;
    } else {
        summary->totalPnlPercent = 0.0;
    }
}

void clearCryptoData(byte mode) {
    if (mode == 0) {
        memset(cryptoDataMode1, 0, sizeof(CryptoPosition) * MAX_POSITIONS_PER_MODE);
        cryptoCountMode1 = 0;
    } else {
        memset(cryptoDataMode2, 0, sizeof(CryptoPosition) * MAX_POSITIONS_PER_MODE);
        cryptoCountMode2 = 0;
    }
}

// ===== UTILITY FUNCTIONS =====
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

String formatTime(unsigned long timestamp) {
    unsigned long seconds = timestamp / 1000;
    unsigned long minutes = seconds / 60;
    unsigned long hours = minutes / 60;
    unsigned long days = hours / 24;
    
    if (days > 0) {
        return String(days) + "d " + String(hours % 24) + "h";
    } else if (hours > 0) {
        return String(hours) + "h " + String(minutes % 60) + "m";
    } else if (minutes > 0) {
        return String(minutes) + "m " + String(seconds % 60) + "s";
    } else {
        return String(seconds) + "s";
    }
}

String formatDateTime(unsigned long timestamp) {
    struct tm timeinfo;
    time_t t = timestamp / 1000;
    
    if (localtime_r(&t, &timeinfo)) {
        char buffer[30];
        strftime(buffer, sizeof(buffer), "%Y/%m/%d %H:%M:%S", &timeinfo);
        return String(buffer);
    }
    
    return "N/A";
}

String getTimeString(unsigned long timestamp) {
    unsigned long diff = millis() - timestamp;
    
    if (diff < 60000) {
        return String(diff / 1000) + "s ago";
    } else if (diff < 3600000) {
        return String(diff / 60000) + "m ago";
    } else if (diff < 86400000) {
        return String(diff / 3600000) + "h ago";
    } else {
        return String(diff / 86400000) + "d ago";
    }
}

String urlDecode(String str) {
    String decoded = "";
    char temp[] = "0x00";
    unsigned int len = str.length();
    
    for (unsigned int i = 0; i < len; i++) {
        char ch = str.charAt(i);
        
        if (ch == '%') {
            if (i + 2 < len) {
                temp[2] = str.charAt(i + 1);
                temp[3] = str.charAt(i + 2);
                decoded += (char)strtol(temp, NULL, 16);
                i += 2;
            }
        } else if (ch == '+') {
            decoded += ' ';
        } else {
            decoded += ch;
        }
    }
    
    return decoded;
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

String getWiFiQuality(int rssi) {
    if (rssi >= -50) return "Excellent";
    if (rssi >= -60) return "Good";
    if (rssi >= -70) return "Fair";
    if (rssi >= -80) return "Weak";
    return "Poor";
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

// ===== TIME FUNCTIONS =====
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
    Serial.println("Synchronizing time with NTP server...");
    
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 5000)) {
        Serial.println("Failed to obtain time");
        return false;
    }
    
    Serial.println("Time synchronized: " + String(asctime(&timeinfo)));
    return true;
}

String getCurrentTime() {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 100)) {
        char buffer[10];
        strftime(buffer, sizeof(buffer), "%H:%M:%S", &timeinfo);
        return String(buffer);
    }
    return "00:00:00";
}

String getCurrentDate() {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 100)) {
        char buffer[12];
        strftime(buffer, sizeof(buffer), "%Y/%m/%d", &timeinfo);
        return String(buffer);
    }
    return "0000/00/00";
}

unsigned long getCurrentTimestamp() {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 100)) {
        return mktime(&timeinfo) * 1000;
    }
    return millis();
}

// ===== SETTINGS FUNCTIONS =====
void initializeSettings() {
    Serial.println("Initializing default settings...");
    
    memset(&settings, 0, sizeof(SystemSettings));
    
    settings.networkCount = 0;
    settings.lastConnectedIndex = -1;
    
    strcpy(settings.server, "");
    strcpy(settings.username, "");
    strcpy(settings.userpass, "");
    strcpy(settings.entryPortfolio, "Arduino");
    strcpy(settings.exitPortfolio, "MyExit");
    
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
    
    settings.exitAlertPercent = DEFAULT_EXIT_ALERT_PERCENT;
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
    
    Serial.println("Default settings initialized");
}

bool loadSettings() {
    Serial.println("Loading settings from EEPROM...");
    
    EEPROM.begin(EEPROM_SIZE);
    
    // ابتدا اندازه ساختار را بررسی کنید
    Serial.print("Size of SystemSettings: ");
    Serial.println(sizeof(SystemSettings));
    
    // خواندن از EEPROM
    EEPROM.get(0, settings);
    EEPROM.end();
    
    // بررسی magic number
    if (settings.magicNumber != 0xAA) {
        Serial.println("Invalid or no settings found, using defaults");
        initializeSettings();
        return false;
    }
    
    // بررسی شبکه‌های WiFi
    Serial.print("Loaded WiFi networks count: ");
    Serial.println(settings.networkCount);
    
    for (int i = 0; i < settings.networkCount; i++) {
        Serial.print("Network ");
        Serial.print(i);
        Serial.print(": ");
        Serial.print(settings.networks[i].ssid);
        Serial.print(" (Priority: ");
        Serial.print(settings.networks[i].priority);
        Serial.println(")");
    }
    
    Serial.println("Settings loaded successfully");
    return true;
}

bool saveSettings() {
    settings.magicNumber = 0xAA;
    
    EEPROM.begin(EEPROM_SIZE);
    EEPROM.put(0, settings);
    
    // حتماً commit را بررسی کنید
    bool result = EEPROM.commit();
    EEPROM.end();
    
    if (result) {
        Serial.println("✅ Settings saved successfully");
        Serial.print("Network count saved: ");
        Serial.println(settings.networkCount);
        
        // تایید ذخیره‌سازی
        for (int i = 0; i < settings.networkCount; i++) {
            Serial.print("  Saved network ");
            Serial.print(i);
            Serial.print(": ");
            Serial.println(settings.networks[i].ssid);
        }
        return true;
    } else {
        Serial.println("❌ FAILED to save settings - commit failed!");
        return false;
    }
}

bool verifySettings() {
    SystemSettings tempSettings;
    
    EEPROM.begin(EEPROM_SIZE);
    EEPROM.get(0, tempSettings);
    EEPROM.end();
    
    if (tempSettings.magicNumber != 0xAA) {
        Serial.println("Verification failed: Magic number mismatch");
        return false;
    }
    
    if (tempSettings.networkCount != settings.networkCount) {
        Serial.println("Verification failed: Network count mismatch");
        return false;
    }
    
    for (int i = 0; i < tempSettings.networkCount; i++) {
        if (strcmp(tempSettings.networks[i].ssid, settings.networks[i].ssid) != 0) {
            Serial.print("Verification failed: SSID mismatch at index ");
            Serial.println(i);
            return false;
        }
    }
    
    Serial.println("✅ Settings verification passed");
    return true;
}

void recoverEEPROM() {
    Serial.println("Attempting EEPROM recovery...");
    
    // ابتدا EEPROM را پاک کنید
    EEPROM.begin(EEPROM_SIZE);
    for (int i = 0; i < EEPROM_SIZE; i++) {
        EEPROM.write(i, 0);
    }
    bool result = EEPROM.commit();
    EEPROM.end();
    
    if (result) {
        Serial.println("EEPROM cleared");
        
        // تنظیمات پیش‌فرض را اعمال کنید
        initializeSettings();
        
        // ذخیره کنید
        if (saveSettings()) {
            Serial.println("✅ Default settings saved to EEPROM");
        } else {
            Serial.println("❌ Failed to save default settings");
        }
    } else {
        Serial.println("❌ Failed to clear EEPROM");
    }
}

// ===== WIFI FUNCTIONS (ENHANCED) =====
bool prepareForScan() {
    Serial.println("Preparing for WiFi scan...");
    
    // بررسی حالت فعلی WiFi
    wifi_mode_t currentMode = WiFi.getMode();
    Serial.print("Current WiFi mode: ");
    
    switch(currentMode) {
        case WIFI_MODE_NULL:
            Serial.println("WIFI_MODE_NULL");
            break;
        case WIFI_MODE_STA:
            Serial.println("WIFI_MODE_STA");
            break;
        case WIFI_MODE_AP:
            Serial.println("WIFI_MODE_AP");
            break;
        case WIFI_MODE_APSTA:
            Serial.println("WIFI_MODE_APSTA");
            break;
        default:
            Serial.println("UNKNOWN");
    }
    
    // If in a mode that can't scan, change mode
    if (currentMode == WIFI_MODE_NULL) {
        Serial.println("WiFi is OFF, turning on for scan...");
        if (apEnabled) {
            WiFi.mode(WIFI_AP);
            String apSSID = "PortfolioMonitor_" + String((uint32_t)ESP.getEfuseMac(), HEX);
            WiFi.softAP(apSSID.c_str(), "12345678", 1, 0, 4);
        } else {
            // If AP is off, at least be in Station mode to scan
            WiFi.mode(WIFI_STA);
        }
        delay(100);
        return true;
    }
    
    // If in AP mode and WiFi is not connected, no problem
    if (currentMode == WIFI_MODE_AP) {
        Serial.println("In AP mode, can scan");
        return true;
    }
    
    // If in Station mode
    if (currentMode == WIFI_MODE_STA) {
        Serial.println("In STA mode, can scan");
        return true;
    }
    
    // If in AP+STA mode
    if (currentMode == WIFI_MODE_APSTA) {
        Serial.println("In AP+STA mode, can scan");
        return true;
    }
    
    Serial.println("Cannot scan in current mode");
    return false;
}

void scanWiFiNetworks(bool forceScan) {
    static unsigned long lastScan = 0;
    unsigned long now = millis();
    
    if (!forceScan && (now - lastScan < SCAN_INTERVAL)) {
        return;
    }
    
    lastScan = now;
    isScanning = true;
    
    Serial.println("📡 Scanning for WiFi networks...");
    showDisplayMessage("Scanning", "WiFi Networks", "Please wait...", "");
    
    // Prepare for scanning
    if (!prepareForScan()) {
        Serial.println("Failed to prepare for scan");
        isScanning = false;
        return;
    }
    
    // Start scan
    int n = WiFi.scanNetworks(false, true, false, 300);
    scannedNetworkCount = 0;
    
    if (n == 0) {
        Serial.println("No networks found");
        scannedNetworkCount = 0;
    } else {
        Serial.println(String(n) + " networks found:");
        scannedNetworkCount = min(n, 20);
        
        for (int i = 0; i < scannedNetworkCount; i++) {
            memset(&scannedNetworks[i], 0, sizeof(WiFiNetwork));
            
            String ssid = WiFi.SSID(i);
            ssid.trim();
            
            if (ssid.length() == 0) continue;
            
            strncpy(scannedNetworks[i].ssid, ssid.c_str(), 31);
            scannedNetworks[i].ssid[31] = '\0';
            scannedNetworks[i].rssi = WiFi.RSSI(i);
            scannedNetworks[i].configured = false;
            scannedNetworks[i].autoConnect = true;
            
            for (int j = 0; j < settings.networkCount; j++) {
                if (strcmp(scannedNetworks[i].ssid, settings.networks[j].ssid) == 0) {
                    scannedNetworks[i].configured = true;
                    strncpy(scannedNetworks[i].password, settings.networks[j].password, 63);
                    scannedNetworks[i].password[63] = '\0';
                    scannedNetworks[i].priority = settings.networks[j].priority;
                    break;
                }
            }
            
            Serial.print(String(i + 1) + ": ");
            Serial.print(scannedNetworks[i].ssid);
            Serial.print(" (");
            Serial.print(scannedNetworks[i].rssi);
            Serial.print(" dBm)");
            Serial.println(scannedNetworks[i].configured ? " [SAVED]" : "");
        }
    }
    
    WiFi.scanDelete();
    isScanning = false;
    
    // Wait for scan to complete
    delay(500);
}

void updateWiFiMode() {
    Serial.println("\n🔄 Updating WiFi mode...");
    Serial.println("   AP Enabled: " + String(apEnabled ? "YES" : "NO"));
    Serial.println("   WiFi Connected: " + String(isConnectedToWiFi ? "YES" : "NO"));
    Serial.println("   AP Active: " + String(apModeActive ? "YES" : "NO"));
    Serial.println("   Network Count: " + String(settings.networkCount));
    
    // حالت 1: AP فعال و WiFi متصل است (AP + Station)
    if (apEnabled && isConnectedToWiFi) {
        Serial.println("   Mode: AP + Station (Hybrid)");
        if (WiFi.getMode() != WIFI_AP_STA) {
            WiFi.mode(WIFI_AP_STA);
            delay(100);
        }
        
        // راه‌اندازی AP
        String apSSID = "PortfolioMonitor_" + String((uint32_t)ESP.getEfuseMac(), HEX);
        if (!WiFi.softAP(apSSID.c_str(), "12345678", 1, 0, 4)) {
            Serial.println("   ❌ Failed to start AP in hybrid mode");
        } else {
            Serial.println("   ✅ AP started in hybrid mode");
            Serial.println("   AP IP: " + WiFi.softAPIP().toString());
            apModeActive = true;
        }
    }
    // حالت 2: فقط AP فعال است
    else if (apEnabled && !isConnectedToWiFi) {
        Serial.println("   Mode: AP Only");
        if (WiFi.getMode() != WIFI_AP) {
            WiFi.mode(WIFI_AP);
            delay(100);
        }
        
        // راه‌اندازی AP
        String apSSID = "PortfolioMonitor_" + String((uint32_t)ESP.getEfuseMac(), HEX);
        if (!WiFi.softAP(apSSID.c_str(), "12345678", 1, 0, 4)) {
            Serial.println("   ❌ Failed to start AP");
        } else {
            Serial.println("   ✅ AP started successfully");
            Serial.println("   AP IP: " + WiFi.softAPIP().toString());
            apModeActive = true;
        }
    }
    // حالت 3: فقط WiFi متصل است
    else if (!apEnabled && isConnectedToWiFi) {
        Serial.println("   Mode: Station Only");
        if (WiFi.getMode() != WIFI_STA) {
            WiFi.mode(WIFI_STA);
            delay(100);
        }
        apModeActive = false;
    }
    // حالت 4: هیچ‌کدام (پیش‌فرض AP)
    else {
        Serial.println("   Mode: AP Only (Default)");
        if (WiFi.getMode() != WIFI_AP) {
            WiFi.mode(WIFI_AP);
            delay(100);
        }
        
        // راه‌اندازی AP
        String apSSID = "PortfolioMonitor_" + String((uint32_t)ESP.getEfuseMac(), HEX);
        if (!WiFi.softAP(apSSID.c_str(), "12345678", 1, 0, 4)) {
            Serial.println("   ❌ Failed to start default AP");
        } else {
            Serial.println("   ✅ Default AP started");
            Serial.println("   AP IP: " + WiFi.softAPIP().toString());
            apEnabled = true;
            apModeActive = true;
            saveAPState();
        }
    }
    
    Serial.println("✅ WiFi mode updated");
}

void saveAPState() {
    int address = EEPROM_SIZE - 10;
    EEPROM.begin(EEPROM_SIZE);
    
    EEPROM.write(address, apEnabled ? 1 : 0);
    unsigned long saveTime = millis();
    EEPROM.put(address + 1, saveTime);
    
    // حتماً commit کنید
    bool result = EEPROM.commit();
    EEPROM.end();
    
    if (result) {
        Serial.println("✅ AP state saved: " + String(apEnabled ? "ENABLED" : "DISABLED"));
    } else {
        Serial.println("❌ Failed to save AP state");
    }
}


void loadAPState() {
    // Load AP state from EEPROM
    int address = EEPROM_SIZE - 10;
    EEPROM.begin(EEPROM_SIZE);
    
    uint8_t state = EEPROM.read(address);
    unsigned long lastSaveTime = 0;
    EEPROM.get(address + 1, lastSaveTime);
    
    EEPROM.end();
    
    // اگر داده معتبر نیست، از پیش‌فرض استفاده کن
    if (state != 0 && state != 1) {
        apEnabled = true; // پیش‌فرض فعال
        Serial.println("⚠️ Invalid AP state in EEPROM, using default (ENABLED)");
    } else {
        apEnabled = (state == 1);
    }
    
    Serial.print("📥 Loaded AP state: ");
    Serial.println(apEnabled ? "ENABLED" : "DISABLED");
    
    if (lastSaveTime > 0) {
        unsigned long timeSinceSave = (millis() - lastSaveTime) / 1000;
        Serial.println("Last save: " + String(timeSinceSave) + " seconds ago");
    }
}

bool connectToBestWiFi() {
    if (settings.networkCount == 0) {
        Serial.println("📭 No WiFi networks configured");
        return false;
    }
    
    Serial.println("🔍 Scanning for available networks...");
    
    // نمایش روی صفحه
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(30, 50);
    tft.println("SCANNING");
    tft.setTextSize(1);
    tft.setCursor(40, 90);
    tft.println("WiFi Networks...");
    
    // اسکن شبکه‌ها
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);
    
    Serial.println("Starting WiFi scan...");
    int n = WiFi.scanNetworks(false, true, false, 300);
    Serial.println("Scan complete, found " + String(n) + " networks");
    
    if (n == 0) {
        Serial.println("📭 No networks found in range");
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.setTextSize(2);
        tft.setCursor(30, 50);
        tft.println("NO NETWORKS");
        tft.setTextSize(1);
        tft.setCursor(20, 90);
        tft.println("Found in range");
        delay(1000);
        WiFi.scanDelete();
        return false;
    }
    
    int bestNetworkIndex = -1;
    int bestPriority = -1;
    int bestRssi = -100;
    
    Serial.println("Evaluating available networks...");
    
    // ارزیابی همه شبکه‌های ذخیره شده
    for (int i = 0; i < settings.networkCount; i++) {
        if (!settings.networks[i].autoConnect) {
            Serial.println("Skipping network " + String(settings.networks[i].ssid) + " (auto-connect disabled)");
            continue;
        }
        
        // جستجوی این شبکه در شبکه‌های اسکن شده
        for (int j = 0; j < n; j++) {
            String scannedSSID = WiFi.SSID(j);
            scannedSSID.trim();
            
            if (scannedSSID.equals(settings.networks[i].ssid)) {
                int rssi = WiFi.RSSI(j);
                int score = (settings.networks[i].priority * 10) + rssi;
                
                Serial.println("Network " + String(settings.networks[i].ssid) + 
                              " found, RSSI: " + String(rssi) + 
                              " dBm, Priority: " + String(settings.networks[i].priority) +
                              ", Score: " + String(score));
                
                if (score > bestPriority * 10 + bestRssi) {
                    bestNetworkIndex = i;
                    bestPriority = settings.networks[i].priority;
                    bestRssi = rssi;
                }
                break;
            }
        }
    }
    
    WiFi.scanDelete();
    
    if (bestNetworkIndex == -1) {
        Serial.println("📭 No saved networks available in range");
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.setTextSize(2);
        tft.setCursor(20, 50);
        tft.println("NO SAVED");
        tft.setCursor(40, 80);
        tft.println("NETWORKS");
        tft.setTextSize(1);
        tft.setCursor(10, 120);
        tft.println("No configured networks");
        tft.setCursor(10, 140);
        tft.println("found in range");
        delay(1000);
        return false;
    }
    
    WiFiNetwork* network = &settings.networks[bestNetworkIndex];
    
    Serial.println("✅ Best network selected: " + String(network->ssid) + 
                  " (Priority: " + String(network->priority) + 
                  ", RSSI: " + String(bestRssi) + " dBm)");
    
    return connectToSpecificWiFi(bestNetworkIndex);
}

bool connectToSpecificWiFi(int networkIndex) {
    if (networkIndex < 0 || networkIndex >= settings.networkCount) {
        Serial.println("❌ Invalid network index");
        return false;
    }
    
    WiFiNetwork* network = &settings.networks[networkIndex];
    
    Serial.println("\n🔗 ================================");
    Serial.println("   CONNECTING TO: " + String(network->ssid));
    Serial.println("   ================================");
    
    // نمایش صفحه اتصال
    showConnectionScreen();
    delay(100);
    
    // قطع اتصال قبلی
    WiFi.disconnect(true);
    delay(500);
    
    // تنظیم حالت WiFi
    WiFi.mode(WIFI_STA);
    WiFi.persistent(false);
    
    // تنظیمات بهبود اتصال
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(false);
    
    Serial.println("1. WiFi mode set to STA");
    Serial.println("2. Disabled WiFi sleep");
    Serial.println("3. Enabled auto-reconnect");
    
    // شروع اتصال
    WiFi.begin(network->ssid, network->password);
    
    Serial.print("4. Connection initiated");
    
    int attempts = 0;
    unsigned long startTime = millis();
    bool connected = false;
    
    // نمایش پیشرفت
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setTextSize(1);
    
    while (attempts < 40) {
        delay(500);
        attempts++;
        
        // نمایش نقطه‌های پیشرفت
        if (attempts % 4 == 0) {
            tft.fillRect(90, 150, 60, 15, TFT_BLACK);
        }
        tft.setCursor(90 + (attempts % 4) * 15, 150);
        tft.print(".");
        
        Serial.print(".");
        
        // بررسی وضعیت - فقط از WL_CONNECTED استفاده می‌کنیم
        if (WiFi.status() == WL_CONNECTED) {
            connected = true;
            Serial.println("\n✅ CONNECTED!");
            break;
        }
        
        // اگر 10 تلاش انجام شد و هنوز وصل نیست، دوباره سعی کن
        if (attempts == 10) {
            Serial.println("\n⚠️ Retrying connection...");
            WiFi.disconnect();
            delay(100);
            WiFi.begin(network->ssid, network->password);
        }
        
        // timeout
        if (millis() - startTime > 20000) {
            Serial.println("\n⏰ Connection timeout");
            break;
        }
    }
    
    if (connected) {
        // موفقیت‌آمیز
        settings.lastConnectedIndex = networkIndex;
        network->lastConnected = millis();
        network->connectionAttempts++;
        network->rssi = WiFi.RSSI();
        
        isConnectedToWiFi = true;
        connectionLost = false;
        
        // نمایش موفقیت
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.setTextSize(2);
        tft.setCursor(40, 60);
        tft.println("CONNECTED!");
        
        tft.setTextSize(1);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.setCursor(20, 100);
        tft.println("SSID: " + String(network->ssid));
        
        tft.setCursor(20, 120);
        tft.println("IP: " + WiFi.localIP().toString());
        
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        tft.setCursor(20, 140);
        tft.println("Signal: " + String(WiFi.RSSI()) + " dBm");
        
        tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        tft.setCursor(20, 160);
        tft.println("Gateway: " + WiFi.gatewayIP().toString());
        
        Serial.println("\n📡 CONNECTION DETAILS:");
        Serial.println("   SSID: " + WiFi.SSID());
        Serial.println("   IP: " + WiFi.localIP().toString());
        Serial.println("   Gateway: " + WiFi.gatewayIP().toString());
        Serial.println("   Subnet: " + WiFi.subnetMask().toString());
        Serial.println("   DNS: " + WiFi.dnsIP().toString());
        Serial.println("   MAC: " + WiFi.macAddress());
        Serial.println("   RSSI: " + String(WiFi.RSSI()) + " dBm");
        Serial.println("   Channel: " + String(WiFi.channel()));
        
        // همگام‌سازی زمان
        if (syncTime()) {
            Serial.println("   Time synchronized with NTP");
            tft.setTextColor(TFT_GREEN, TFT_BLACK);
            tft.setCursor(20, 180);
            tft.println("Time synchronized");
        }
        
        // پخش صدای موفقیت
        if (settings.buzzerEnabled) {
            playSuccessTone();
        }
        
        delay(2000);
        
        // ذخیره تنظیمات
        if (saveSettings()) {
            Serial.println("✅ Settings saved");
        }
        
        return true;
    } else {
        // شکست در اتصال
        Serial.println("\n❌ CONNECTION FAILED!");
        Serial.println("   Final status: " + String(WiFi.status()));
        
        network->connectionAttempts++;
        
        // نمایش خطا
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.setTextSize(2);
        tft.setCursor(40, 60);
        tft.println("FAILED!");
        
        tft.setTextSize(1);
        tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        tft.setCursor(20, 100);
        tft.println("Network: " + String(network->ssid));
        
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.setCursor(20, 130);
        tft.println("Possible issues:");
        
        tft.setCursor(30, 150);
        tft.println("1. Wrong password");
        
        tft.setCursor(30, 165);
        tft.println("2. Network out of range");
        
        tft.setCursor(30, 180);
        tft.println("3. Router issues");
        
        tft.setCursor(30, 195);
        tft.println("4. Hidden network config");
        
        // پخش صدای خطا
        if (settings.buzzerEnabled) {
            playErrorTone();
        }
        
        delay(3000);
        
        // اگر AP فعال است، برگرد به AP
        if (apEnabled && !apModeActive) {
            Serial.println("Starting AP mode after connection failure");
            startAPMode();
        }
        
        return false;
    }
}



bool addOrUpdateWiFiNetwork(const char* ssid, const char* password, byte priority, bool autoConnect) {
    if (strlen(ssid) == 0) {
        Serial.println("Cannot add network: SSID is empty");
        return false;
    }
    
    Serial.println("Adding/Updating WiFi network: " + String(ssid));
    
    // بررسی آیا شبکه قبلاً ذخیره شده است
    for (int i = 0; i < settings.networkCount; i++) {
        if (strcmp(settings.networks[i].ssid, ssid) == 0) {
            // به‌روزرسانی شبکه موجود
            Serial.println("Updating existing network");
            strncpy(settings.networks[i].password, password, 63);
            settings.networks[i].password[63] = '\0';
            settings.networks[i].priority = priority;
            settings.networks[i].autoConnect = autoConnect;
            
            if (saveSettings()) {
                Serial.println("✅ Network updated successfully");
                return true;
            } else {
                Serial.println("❌ Failed to save updated network");
                return false;
            }
        }
    }
    
    // اگر به حداکثر تعداد رسیده‌ایم، شبکه با کمترین اولویت را حذف کن
    if (settings.networkCount >= MAX_WIFI_NETWORKS) {
        int lowestPriorityIndex = 0;
        byte lowestPriority = 10;
        
        for (int i = 0; i < settings.networkCount; i++) {
            if (settings.networks[i].priority < lowestPriority) {
                lowestPriority = settings.networks[i].priority;
                lowestPriorityIndex = i;
            }
        }
        
        Serial.println("Maximum networks reached, removing: " + String(settings.networks[lowestPriorityIndex].ssid));
        removeWiFiNetwork(lowestPriorityIndex);
    }
    
    // اضافه کردن شبکه جدید
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
    
    Serial.println("New network added: " + String(ssid) + " (Priority: " + String(priority) + ")");
    
    if (saveSettings()) {
        Serial.println("✅ New network saved successfully");
        return true;
    } else {
        Serial.println("❌ Failed to save new network");
        // بازگرداندن تغییرات
        settings.networkCount--;
        memset(network, 0, sizeof(WiFiNetwork));
        return false;
    }
}

void removeWiFiNetwork(int index) {
    if (index < 0 || index >= settings.networkCount) {
        return;
    }
    
    Serial.println("Removing WiFi network: " + String(settings.networks[index].ssid));
    
    for (int i = index; i < settings.networkCount - 1; i++) {
        settings.networks[i] = settings.networks[i + 1];
    }
    
    settings.networkCount--;
    memset(&settings.networks[settings.networkCount], 0, sizeof(WiFiNetwork));
    
    if (saveSettings()) {
        Serial.println("✅ Network removed and saved");
    } else {
        Serial.println("❌ Failed to save after removal");
    }
}

void reorderWiFiNetworks() {
    for (int i = 0; i < settings.networkCount - 1; i++) {
        for (int j = 0; j < settings.networkCount - i - 1; j++) {
            if (settings.networks[j].priority < settings.networks[j + 1].priority) {
                WiFiNetwork temp = settings.networks[j];
                settings.networks[j] = settings.networks[j + 1];
                settings.networks[j + 1] = temp;
            }
        }
    }
}

bool startAPMode() {
    Serial.println("🚀 Starting Access Point mode...");
    
    // قطع کامل WiFi قبلی
    WiFi.disconnect(true);
    delay(100);
    
    // حالت فقط AP (خیلی مهم)
    WiFi.mode(WIFI_AP);
    delay(100);
    
    String apSSID = "PortfolioMonitor_" + String((uint32_t)ESP.getEfuseMac(), HEX);
    const char* apPassword = "12345678";
    
    // تنظیمات IP
    if (!WiFi.softAPConfig(
        IPAddress(192, 168, 4, 1),
        IPAddress(192, 168, 4, 1),
        IPAddress(255, 255, 255, 0))) {
        Serial.println("❌ Failed to configure AP IP");
    }
    
    bool apResult = WiFi.softAP(apSSID.c_str(), apPassword, 1, 0, 4);
    
    if (!apResult) {
        Serial.println("❌ Failed to start AP");
        apModeActive = false;
        return false;  // ⬅️ اصلاح: false برگردون
    }
    
    apModeActive = true;
    isConnectedToWiFi = false;
    connectionLost = false;
    
    IPAddress ip = WiFi.softAPIP();
    Serial.println("✅ AP Started Successfully");
    Serial.println("   SSID: " + apSSID);
    Serial.println("   Password: " + String(apPassword));
    Serial.println("   IP: " + ip.toString());
    Serial.println("   MAC: " + WiFi.softAPmacAddress());
    
    return true;  // ⬅️ موفقیت
}

void checkWiFiStatus() {
    if (isConnectedToWiFi && WiFi.status() != WL_CONNECTED) {
        Serial.println("⚠️ WiFi connection lost!");
        
        isConnectedToWiFi = false;
        connectionLost = true;
        connectionLostTime = millis();
        connectionLostCount++;
        
        if (settings.buzzerEnabled) {
            playConnectionLostTone();
        }
        
        // نمایش روی صفحه
        if (!showingAlert) {
            showConnectionLostScreen();
        }
        
        // تلاش برای reconnect اگر تنظیم شده باشد
        if (settings.autoReconnect) {
            Serial.println("Attempting reconnect...");
            attemptReconnect();
        } else if (apEnabled && !apModeActive) {
            Serial.println("Starting AP mode...");
            startAPMode();
        }
    }
}

void checkConnectionStatus() {
    if (connectionLost && WiFi.status() == WL_CONNECTED) {
        Serial.println("✅ WiFi connection restored!");
        
        connectionLost = false;
        isConnectedToWiFi = true;
        reconnectSuccessCount++;
        
        unsigned long downtime = millis() - connectionLostTime;
        totalDowntime += downtime;
        
        Serial.println("Downtime: " + String(downtime / 1000) + " seconds");
        
        if (settings.buzzerEnabled) {
            playSuccessTone();
        }
        
        // همگام‌سازی زمان
        syncTime();
        
        // بازگشت به صفحه اصلی
        showMainDisplay();
    }
}

void attemptReconnect() {
    if (!settings.autoReconnect || settings.networkCount == 0) {
        return;
    }
    
    static int attempts = 0;
    static unsigned long lastAttemptTime = 0;
    
    unsigned long now = millis();
    
    if (attempts < settings.reconnectAttempts) {
        if (now - lastAttemptTime > 10000) { // هر 10 ثانیه یکبار تلاش کن
            attempts++;
            lastAttemptTime = now;
            
            Serial.println("🔁 Reconnection attempt " + String(attempts) + 
                          " of " + String(settings.reconnectAttempts));
            
            if (connectToBestWiFi()) {
                attempts = 0;
                Serial.println("✅ Reconnection successful!");
            } else {
                Serial.println("❌ Reconnection failed");
            }
        }
    } else {
        Serial.println("⏹️ Max reconnection attempts reached");
        if (!apModeActive && apEnabled) {
            Serial.println("Starting AP mode...");
            startAPMode();
        }
        attempts = 0;
    }
}// ===== WEB SERVER HANDLERS =====
void handleRoot() {
    if (!isConnectedToWiFi && !apModeActive) {
        server.sendHeader("Location", "/setup", true);
        server.send(302, "text/plain", "");
        return;
    }
    
    String html = generateDashboardHTML();
    server.send(200, "text/html", html);
}

String generateDashboardHTML() {
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
                <div style="margin-top: 15px;">
                    <a href="/positions?mode=entry" class="btn">View Positions</a>
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
                <div style="margin-top: 15px;">
                    <a href="/positions?mode=exit" class="btn">View Positions</a>
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
    
    return html;
}

void handleSetup() {
    String html = generateSetupHTML();
    server.send(200, "text/html", html);
}

String generateSetupHTML() {
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
        .btn-warning { background: #ff9900; }
        .volume-slider { width: 300px; }
        .volume-display { font-size: 18px; font-weight: bold; margin-left: 10px; }
        .ap-status { display: inline-block; padding: 5px 10px; border-radius: 20px; font-size: 14px; font-weight: bold; margin-left: 10px; }
        .ap-on { background-color: #28a745; color: white; }
        .ap-off { background-color: #dc3545; color: white; }
    </style>
</head>
<body>
    <div class="container">
        <h1>⚙️ Portfolio Monitor Setup 
            <span class="ap-status )rawliteral";
    html += apEnabled ? "ap-on" : "ap-off";
    html += R"rawliteral(">AP: )rawliteral";
    html += apEnabled ? "ON" : "OFF";
    html += R"rawliteral(</span>
        </h1>
        
        <div class="tab-container">
            <div class="tab-buttons">
                <button class="tab-button active" onclick="openTab(event, 'wifi')">WiFi</button>
                <button class="tab-button" onclick="openTab(event, 'api')">API</button>
                <button class="tab-button" onclick="openTab(event, 'alert')">Alerts</button>
                <button class="tab-button" onclick="openTab(event, 'volume')">Volume</button>
                <button class="tab-button" onclick="openTab(event, 'mode')">Mode</button>
                <button class="tab-button" onclick="openTab(event, 'display')">Display</button>
                <button class="tab-button" onclick="openTab(event, 'rgb')">RGB</button>
                <button class="tab-button" onclick="openTab(event, 'emergency')">Emergency</button>
            </div>
            
            <!-- WiFi Tab -->
            <div id="wifi" class="tab-content active">
                <h2>WiFi Settings</h2>
                <div style="margin-bottom: 20px;">
                    <a href="/wifimanage" class="btn btn-warning">📶 WiFi Manager</a>
                    <a href="/wifiscan" class="btn btn-warning">📡 Scan Networks</a>
                    <a href="/toggleap" class="btn )rawliteral";
    html += apEnabled ? "btn-warning" : "btn-success";
    html += R"rawliteral(">)rawliteral";
    html += apEnabled ? "🔴 Disable AP" : "🟢 Enable AP";
    html += R"rawliteral(</a>
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
                    <button type="submit" class="btn btn-success">Save WiFi Settings</button>
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
                    <button type="submit" class="btn btn-success">Save API Settings</button>
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
                    <div class="form-group">
                        <label>
                            <input type="checkbox" id="separatealerts" name="separatealerts" )rawliteral";
    html += (settings.separateLongShortAlerts ? "checked" : "");
    html += R"rawliteral(> Separate Long/Short Alerts
                        </label>
                    </div>
                    <button type="submit" class="btn btn-success">Save Alert Settings</button>
                </form>
            </div>
            
            <!-- Volume Tab -->
            <div id="volume" class="tab-content">
                <h2>🎚️ Buzzer Volume Control</h2>
                
                <div class="form-group">
                    <label for="buzzervolume">Buzzer Volume: 
                        <span id="volumeValue" class="volume-display">)rawliteral";
    html += String(settings.buzzerVolume);
    html += R"rawliteral(%</span>
                    </label>
                    <input type="range" id="buzzervolume" name="buzzervolume" 
                           min="0" max="100" value=")rawliteral";
    html += String(settings.buzzerVolume);
    html += R"rawliteral(" class="volume-slider" 
                           oninput="updateVolumeValue(this.value)">
                </div>
                
                <div class="form-group">
                    <label>
                        <input type="checkbox" id="buzzerenable" name="buzzerenable" )rawliteral";
    html += (settings.buzzerEnabled ? "checked" : "");
    html += R"rawliteral(> Enable Buzzer
                    </label>
                </div>
                
                <div class="form-group">
                    <button type="button" class="btn" onclick="testVolume()">
                        🔊 Test Current Volume
                    </button>
                    <button type="button" class="btn" onclick="saveVolume()">
                        💾 Save Volume
                    </button>
                </div>
                
                <div style="margin-top: 30px; background: #3a3a3a; padding: 15px; border-radius: 5px;">
                    <h3>Volume Presets</h3>
                    <div>
                        <button class="btn" onclick="setVolume(0)">🔇 Mute (0%)</button>
                        <button class="btn" onclick="setVolume(25)">Quiet (25%)</button>
                        <button class="btn" onclick="setVolume(50)">Medium (50%)</button>
                        <button class="btn" onclick="setVolume(75)">Loud (75%)</button>
                        <button class="btn" onclick="setVolume(100)">🔊 Max (100%)</button>
                    </div>
                </div>
            </div>
            
            <!-- Mode Tab -->
            <div id="mode" class="tab-content">
                <h2>Mode Settings</h2>
                <form action="/savemode" method="post">
                    <div class="form-group">
                        <label for="exitalertpercent">Exit Alert Percentage (%)</label>
                        <input type="number" step="0.1" id="exitalertpercent" name="exitalertpercent" value=")rawliteral";
    html += String(settings.exitAlertPercent, 1);
    html += R"rawliteral(">
                    </div>
                    <div class="form-group">
                        <label>
                            <input type="checkbox" id="exitalertenable" name="exitalertenable" )rawliteral";
    html += (settings.exitAlertEnabled ? "checked" : "");
    html += R"rawliteral(> Enable Exit Alerts
                        </label>
                    </div>
                    <div class="form-group">
                        <label>
                            <input type="checkbox" id="exitblinkenable" name="exitblinkenable" )rawliteral";
    html += (settings.exitAlertBlinkEnabled ? "checked" : "");
    html += R"rawliteral(> Enable Exit Alert Blinking
                        </label>
                    </div>
                    <button type="submit" class="btn btn-success">Save Mode Settings</button>
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
                    <div class="form-group">
                        <label>
                            <input type="checkbox" id="showdetails" name="showdetails" )rawliteral";
    html += (settings.showDetails ? "checked" : "");
    html += R"rawliteral(> Show Detailed Information
                        </label>
                    </div>
                    <div class="form-group">
                        <label>
                            <input type="checkbox" id="invertdisplay" name="invertdisplay" )rawliteral";
    html += (settings.invertDisplay ? "checked" : "");
    html += R"rawliteral(> Invert Display Colors
                        </label>
                    </div>
                    <button type="submit" class="btn btn-success">Save Display Settings</button>
                </form>
            </div>
            
            <!-- RGB Tab -->
            <div id="rgb" class="tab-content">
                <h2>RGB LED Settings</h2>
                <form action="/savergb" method="post">
                    <div class="form-group">
                        <label>
                            <input type="checkbox" id="rgb1enable" name="rgb1enable" )rawliteral";
    html += (settings.rgb1Enabled ? "checked" : "");
    html += R"rawliteral(> Enable RGB1 (History Display)
                        </label>
                    </div>
                    <div class="form-group">
                        <label for="rgb1brightness">RGB1 Brightness (0-100)</label>
                        <input type="number" id="rgb1brightness" name="rgb1brightness" min="0" max="100" value=")rawliteral";
    html += String(settings.rgb1Brightness);
    html += R"rawliteral(">
                    </div>
                    <div class="form-group">
                        <label>
                            <input type="checkbox" id="rgb2enable" name="rgb2enable" )rawliteral";
    html += (settings.rgb2Enabled ? "checked" : "");
    html += R"rawliteral(> Enable RGB2 (Portfolio Status)
                        </label>
                    </div>
                    <div class="form-group">
                        <label for="rgb2brightness">RGB2 Brightness (0-100)</label>
                        <input type="number" id="rgb2brightness" name="rgb2brightness" min="0" max="100" value=")rawliteral";
    html += String(settings.rgb2Brightness);
    html += R"rawliteral(">
                    </div>
                    <button type="submit" class="btn btn-success">Save RGB Settings</button>
                </form>
            </div>
            
            <!-- Emergency Tab -->
            <div id="emergency" class="tab-content">
                <h2>Emergency Settings</h2>
                <form action="/saveemergency" method="post">
                    <div class="form-group">
                        <label>
                            <input type="checkbox" id="ledenabled" name="ledenabled" )rawliteral";
    html += (settings.ledEnabled ? "checked" : "");
    html += R"rawliteral(> Enable Status LEDs
                        </label>
                    </div>
                    <div class="form-group">
                        <label>
                            <input type="checkbox" id="showbattery" name="showbattery" )rawliteral";
    html += (settings.showBattery ? "checked" : "");
    html += R"rawliteral(> Show Battery Status
                        </label>
                    </div>
                    <div class="form-group">
                        <label for="batterywarning">Battery Warning Level (%)</label>
                        <input type="number" id="batterywarning" name="batterywarning" min="5" max="50" value=")rawliteral";
    html += String(settings.batteryWarningLevel);
    html += R"rawliteral(">
                    </div>
                    <div class="form-group">
                        <label>
                            <input type="checkbox" id="autoreconnect" name="autoreconnect" )rawliteral";
    html += (settings.autoReconnect ? "checked" : "");
    html += R"rawliteral(> Auto Reconnect to WiFi
                        </label>
                    </div>
                    <div class="form-group">
                        <label for="reconnectattempts">Reconnect Attempts</label>
                        <input type="number" id="reconnectattempts" name="reconnectattempts" min="1" max="20" value=")rawliteral";
    html += String(settings.reconnectAttempts);
    html += R"rawliteral(">
                    </div>
                    <button type="submit" class="btn btn-success">Save Emergency Settings</button>
                </form>
            </div>
            
        </div>
        
        <div style="margin-top: 30px;">
            <a href="/" class="btn">← Back to Dashboard</a>
            <a href="/testalert" class="btn btn-warning">🔊 Test All Alerts</a>
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
        
        function updateVolumeValue(value) {
            document.getElementById('volumeValue').textContent = value + '%';
        }
        
        function setVolume(volume) {
            document.getElementById('buzzervolume').value = volume;
            updateVolumeValue(volume);
            testVolume();
        }
        
        function testVolume() {
            let volume = document.getElementById('buzzervolume').value;
            fetch('/testvolume?v=' + volume)
                .then(response => response.text())
                .then(text => {
                    console.log('Volume test: ' + text);
                });
        }
        
        function saveVolume() {
            let volume = document.getElementById('buzzervolume').value;
            let buzzerEnabled = document.getElementById('buzzerenable').checked;
            
            let formData = new FormData();
            formData.append('buzzervolume', volume);
            formData.append('buzzerenable', buzzerEnabled ? '1' : '0');
            
            fetch('/savealert', {
                method: 'POST',
                body: formData
            })
            .then(response => {
                alert('Volume settings saved!');
                location.reload();
            });
        }
        
        // Initialize
        document.getElementById('buzzervolume').oninput();
    </script>
</body>
</html>)rawliteral";
    
    return html;
}

void handleSaveWiFi() {
    if (server.hasArg("ssid") && server.hasArg("password")) {
        String ssid = server.arg("ssid");
        String password = server.arg("password");
        byte priority = server.hasArg("priority") ? server.arg("priority").toInt() : 7;
        bool autoConnect = server.hasArg("autoconnect");
        
        if (addOrUpdateWiFiNetwork(ssid.c_str(), password.c_str(), priority, autoConnect)) {
            playSuccessTone();
            server.sendHeader("Location", "/setup", true);
            server.send(302, "text/plain", "");
        } else {
            playErrorTone();
            server.send(500, "text/plain", "Failed to save WiFi settings");
        }
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
    settings.buzzerVolume = constrain(server.arg("buzzervolume").toInt(), VOLUME_MIN, VOLUME_MAX);
    settings.buzzerEnabled = server.hasArg("buzzerenable");
    settings.separateLongShortAlerts = server.hasArg("separatealerts");
    settings.autoResetAlerts = server.hasArg("autoreset");
    settings.alertCooldown = server.arg("cooldown").toInt();
    
    if (saveSettings()) {
        playSuccessTone();
        server.sendHeader("Location", "/setup", true);
        server.send(302, "text/plain", "");
    } else {
        playErrorTone();
        server.send(500, "text/plain", "Failed to save alert settings");
    }
}

void handleSaveMode() {
    settings.exitAlertPercent = constrain(server.arg("exitalertpercent").toFloat(), 1.0, 50.0);
    settings.exitAlertEnabled = server.hasArg("exitalertenable");
    settings.exitAlertBlinkEnabled = server.hasArg("exitblinkenable");
    
    if (saveSettings()) {
        playSuccessTone();
        server.sendHeader("Location", "/setup", true);
        server.send(302, "text/plain", "");
    } else {
        playErrorTone();
        server.send(500, "text/plain", "Failed to save mode settings");
    }
}

void handleSaveDisplay() {
    settings.displayBrightness = constrain(server.arg("brightness").toInt(), 0, 100);
    settings.displayTimeout = server.arg("timeout").toInt();
    settings.showDetails = server.hasArg("showdetails");
    settings.invertDisplay = server.hasArg("invertdisplay");
    settings.displayRotation = constrain(server.arg("rotation").toInt(), 0, 3);
    
    setDisplayBrightness(settings.displayBrightness);
    tft.setRotation(settings.displayRotation);
    
    if (saveSettings()) {
        playSuccessTone();
        server.sendHeader("Location", "/setup", true);
        server.send(302, "text/plain", "");
    } else {
        playErrorTone();
        server.send(500, "text/plain", "Failed to save display settings");
    }
}

void handleSaveRGB() {
    settings.rgb1Enabled = server.hasArg("rgb1enable");
    settings.rgb2Enabled = server.hasArg("rgb2enable");
    settings.rgb1Brightness = constrain(server.arg("rgb1brightness").toInt(), 0, 100);
    settings.rgb2Brightness = constrain(server.arg("rgb2brightness").toInt(), 0, 100);
    
    if (saveSettings()) {
        playSuccessTone();
        server.sendHeader("Location", "/setup", true);
        server.send(302, "text/plain", "");
    } else {
        playErrorTone();
        server.send(500, "text/plain", "Failed to save RGB settings");
    }
}

void handleSaveEmergency() {
    settings.ledEnabled = server.hasArg("ledenabled");
    settings.showBattery = server.hasArg("showbattery");
    settings.batteryWarningLevel = constrain(server.arg("batterywarning").toInt(), 5, 50);
    settings.autoReconnect = server.hasArg("autoreconnect");
    settings.reconnectAttempts = constrain(server.arg("reconnectattempts").toInt(), 1, 20);
    
    if (saveSettings()) {
        playSuccessTone();
        server.sendHeader("Location", "/setup", true);
        server.send(302, "text/plain", "");
    } else {
        playErrorTone();
        server.send(500, "text/plain", "Failed to save emergency settings");
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
    playTestAlertSequence();
    server.send(200, "text/plain", "Test alert sequence played");
}

void handleResetAlerts() {
    resetAllAlerts();
    server.send(200, "text/plain", "All alerts reset");
}

void handleSystemInfo() {
    String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial=1.0">
    <title>System Information</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; background: #1a1a1a; color: #fff; }
        .container { max-width: 800px; margin: 0 auto; }
        .card { background: #2d2d2d; padding: 20px; border-radius: 10px; margin-bottom: 20px; }
        .card-header { font-size: 18px; font-weight: bold; margin-bottom: 15px; color: #0088ff; }
        .info-grid { display: grid; grid-template-columns: repeat(2, 1fr); gap: 10px; }
        .info-item { background: #3a3a3a; padding: 10px; border-radius: 5px; }
        .info-label { font-size: 12px; color: #aaa; }
        .info-value { font-size: 14px; }
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
        .positive { color: #00ff00; }
        .negative { color: #ff3333; }
        .ap-status { display: inline-block; padding: 5px 10px; border-radius: 20px; font-size: 14px; font-weight: bold; }
        .ap-on { background-color: #28a745; color: white; }
        .ap-off { background-color: #dc3545; color: white; }
    </style>
</head>
<body>
    <div class="container">
        <h1>📊 System Information 
            <span class="ap-status )rawliteral";
    html += apEnabled ? "ap-on" : "ap-off";
    html += R"rawliteral(">AP: )rawliteral";
    html += apEnabled ? "ON" : "OFF";
    html += R"rawliteral(</span>
        </h1>
        
        <div class="card">
            <div class="card-header">Device Info</div>
            <div class="info-grid">
                <div class="info-item">
                    <div class="info-label">Device Name</div>
                    <div class="info-value">Portfolio Monitor v4.5.2</div>
                </div>
                <div class="info-item">
                    <div class="info-label">ESP32 Model</div>
                    <div class="info-value">ESP32-WROVER-E</div>
                </div>
                <div class="info-item">
                    <div class="info-label">Chip ID</div>
                    <div class="info-value">0x)rawliteral";
    html += String((uint32_t)ESP.getEfuseMac(), HEX);
    html += R"rawliteral(</div>
                </div>
                <div class="info-item">
                    <div class="info-label">CPU Frequency</div>
                    <div class="info-value">)rawliteral";
    html += String(ESP.getCpuFreqMHz()) + " MHz";
    html += R"rawliteral(</div>
                </div>
            </div>
        </div>
        
        <div class="card">
            <div class="card-header">System Status</div>
            <div class="info-grid">
                <div class="info-item">
                    <div class="info-label">Uptime</div>
                    <div class="info-value">)rawliteral";
    html += getUptimeString();
    html += R"rawliteral(</div>
                </div>
                <div class="info-item">
                    <div class="info-label">Boot Count</div>
                    <div class="info-value">)rawliteral";
    html += String(settings.bootCount);
    html += R"rawliteral(</div>
                </div>
                <div class="info-item">
                    <div class="info-label">Free Heap</div>
                    <div class="info-value">)rawliteral";
    html += String(ESP.getFreeHeap() / 1024) + " KB";
    html += R"rawliteral(</div>
                </div>
                <div class="info-item">
                    <div class="info-label">Buzzer Volume</div>
                    <div class="info-value">)rawliteral";
    html += String(settings.buzzerVolume) + "%";
    html += R"rawliteral(</div>
                </div>
            </div>
        </div>
        
        <div class="card">
            <div class="card-header">Network Status</div>
            <div class="info-grid">
                <div class="info-item">
                    <div class="info-label">WiFi Status</div>
                    <div class="info-value">)rawliteral";
    if (isConnectedToWiFi) {
        html += "<span class='positive'>Connected</span>";
    } else if (apModeActive) {
        html += "<span class='positive'>AP Mode</span>";
    } else {
        html += "<span class='negative'>Disconnected</span>";
    }
    html += R"rawliteral(</div>
                </div>
                <div class="info-item">
                    <div class="info-label">Connection Type</div>
                    <div class="info-value">)rawliteral";
    if (isConnectedToWiFi) {
        html += String(WiFi.SSID());
    } else if (apModeActive) {
        html += "Access Point";
    } else {
        html += "None";
    }
    html += R"rawliteral(</div>
                </div>
                <div class="info-item">
                    <div class="info-label">IP Address</div>
                    <div class="info-value">)rawliteral";
    if (isConnectedToWiFi) {
        html += WiFi.localIP().toString();
    } else if (apModeActive) {
        html += WiFi.softAPIP().toString();
    } else {
        html += "N/A";
    }
    html += R"rawliteral(</div>
                </div>
                <div class="info-item">
                    <div class="info-label">Saved Networks</div>
                    <div class="info-value">)rawliteral";
    html += String(settings.networkCount);
    html += R"rawliteral(</div>
                </div>
            </div>
        </div>
        
        <div style="margin-top: 30px;">
            <a href="/" class="btn">← Back to Dashboard</a>
            <a href="/setup" class="btn">← Back to Setup</a>
            <button onclick="location.reload()" class="btn">🔄 Refresh</button>
        </div>
    </div>
</body>
</html>)rawliteral";
    
    server.send(200, "text/html", html);
}

void handleAPIStatus() {
    String html = "<h1>API Status</h1>";
    html += "<p>Success Count: " + String(apiSuccessCount) + "</p>";
    html += "<p>Error Count: " + String(apiErrorCount) + "</p>";
    html += "<p>Success Rate: " + String(apiSuccessCount * 100.0 / (apiSuccessCount + apiErrorCount), 1) + "%</p>";
    html += "<p>Avg Response Time: " + String(apiAverageResponseTime, 0) + " ms</p>";
    html += "<a href='/'>Back to Dashboard</a>";
    server.send(200, "text/html", html);
}

void handleLEDControl() {
    String action = server.arg("action");
    
    if (action == "test") {
        setAllLEDs(true);
        delay(1000);
        setAllLEDs(false);
    } else if (action == "on") {
        setAllLEDs(true);
    } else if (action == "off") {
        setAllLEDs(false);
    }
    
    server.sendHeader("Location", "/setup", true);
    server.send(302, "text/plain", "");
}

void handleRGBControl() {
    String action = server.arg("action");
    
    if (action == "test") {
        setRGB1Color(255, 0, 0);
        setRGB2Color(0, 255, 0);
        delay(1000);
        setRGB1Color(0, 255, 0);
        setRGB2Color(0, 0, 255);
        delay(1000);
        turnOffRGB1();
        turnOffRGB2();
    } else if (action == "off") {
        turnOffRGB1();
        turnOffRGB2();
    }
    
    server.sendHeader("Location", "/setup", true);
    server.send(302, "text/plain", "");
}

void handleDisplayControl() {
    String action = server.arg("action");
    
    if (action == "test") {
        showDisplayMessage("Test Message", "Line 2", "Line 3", "Line 4");
        delay(3000);
    } else if (action == "off") {
        setDisplayBacklight(false);
    } else if (action == "on") {
        setDisplayBacklight(true);
    }
    
    server.sendHeader("Location", "/setup", true);
    server.send(302, "text/plain", "");
}

void handleFactoryReset() {
    factoryReset();
    server.send(200, "text/plain", "Factory reset completed. Restarting...");
    delay(1000);
    ESP.restart();
}

void handleRestart() {
    server.send(200, "text/plain", "Restarting system...");
    delay(1000);
    ESP.restart();
}

void handlePositions() {
    String modeStr = server.arg("mode");
    byte mode = (modeStr == "exit") ? 1 : 0;
    
    String html = generatePositionListHTML(mode);
    server.send(200, "text/html", html);
}

String generatePositionListHTML(byte mode) {
    String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial=1.0">
    <title>)rawliteral";
    
    if (mode == 0) {
        html += "Entry Mode Positions";
    } else {
        html += "Exit Mode Positions";
    }
    
    html += R"rawliteral(</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; background: #1a1a1a; color: #fff; }
        .container { max-width: 1200px; margin: 0 auto; }
        table { width: 100%; border-collapse: collapse; margin: 15px 0; }
        th, td { padding: 12px; text-align: left; border-bottom: 1px solid #444; }
        th { background: #1a1a1a; color: #0088ff; }
        .positive { color: #00ff00; }
        .negative { color: #ff3333; }
        .long { color: #00ff00; background: rgba(0, 255, 0, 0.1); }
        .short { color: #ff3333; background: rgba(255, 0, 0, 0.1); }
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
        .alert-active { background: rgba(255, 165, 0, 0.2); }
        .severe-alert { background: rgba(255, 0, 0, 0.2); }
    </style>
</head>
<body>
    <div class="container">
        <h1>)rawliteral";
    
    if (mode == 0) {
        html += "📈 Entry Mode Positions: " + String(settings.entryPortfolio);
    } else {
        html += "📉 Exit Mode Positions: " + String(settings.exitPortfolio);
    }
    
    html += R"rawliteral(</h1>
        <p>Total Positions: )rawliteral";
    
    if (mode == 0) {
        html += String(cryptoCountMode1);
    } else {
        html += String(cryptoCountMode2);
    }
    
    html += R"rawliteral(</p>
        
        <table>
            <thead>
                <tr>
                    <th>Symbol</th>
                    <th>Side</th>
                    <th>Quantity</th>
                    <th>Entry Price</th>
                    <th>Current Price</th>
                    <th>P/L Value</th>
                    <th>P/L %</th>
                    <th>Alert Status</th>
                </tr>
            </thead>
            <tbody>
    )rawliteral";
    
    CryptoPosition* data;
    int count;
    
    if (mode == 0) {
        data = cryptoDataMode1;
        count = cryptoCountMode1;
    } else {
        data = cryptoDataMode2;
        count = cryptoCountMode2;
    }
    
    for (int i = 0; i < count; i++) {
        CryptoPosition* pos = &data[i];
        
        String rowClass = "";
        if (pos->alerted) {
            if (pos->severeAlerted) {
                rowClass = "severe-alert";
            } else {
                rowClass = "alert-active";
            }
        }
        
        html += "<tr class='" + rowClass + "'>";
        
        // Symbol
        html += "<td><strong>" + getShortSymbol(pos->symbol) + "</strong></td>";
        
        // Side
        html += "<td class='" + String(pos->isLong ? "long" : "short") + "'>";
        html += pos->isLong ? "LONG" : "SHORT";
        html += "</td>";
        
        // Quantity
        html += "<td>" + formatNumber(pos->quantity) + "</td>";
        
        // Entry Price
        html += "<td>$" + formatPrice(pos->entryPrice) + "</td>";
        
        // Current Price
        html += "<td>$" + formatPrice(pos->currentPrice) + "</td>";
        
        // P/L Value
        html += "<td class='" + String(pos->pnlValue >= 0 ? "positive" : "negative") + "'>";
        html += "$" + formatNumber(pos->pnlValue);
        html += "</td>";
        
        // P/L %
        html += "<td class='" + String(pos->changePercent >= 0 ? "positive" : "negative") + "'>";
        html += formatPercent(pos->changePercent);
        html += "</td>";
        
        // Alert Status
        html += "<td>";
        if (mode == 0) {
            if (pos->alerted) {
                html += "⚠️ ";
                if (pos->severeAlerted) {
                    html += "<span class='negative'>SEVERE</span>";
                } else {
                    html += "<span class='negative'>Alerted</span>";
                }
            } else if (pos->changePercent <= settings.alertThreshold) {
                html += "⚠️ <span class='negative'>Pending</span>";
            } else {
                html += "✅ OK";
            }
        } else {
            if (pos->exitAlerted) {
                html += "💰 <span class='positive'>Exit Alert</span>";
            } else {
                html += "✅ OK";
            }
        }
        html += "</td>";
        
        html += "</tr>";
    }
    
    html += R"rawliteral(
            </tbody>
        </table>
        
        <div style="margin-top: 30px;">
            <a href="/" class="btn">← Back to Dashboard</a>
            )rawliteral";
    
    if (mode == 0) {
        html += "<a href=\"/positions?mode=exit\" class=\"btn\">View Exit Mode Positions →</a>";
    } else {
        html += "<a href=\"/positions?mode=entry\" class=\"btn\">View Entry Mode Positions →</a>";
    }
    
    html += R"rawliteral(
        </div>
    </div>
</body>
</html>)rawliteral";
    
    return html;
}

void handleSetVolume() {
    if (server.hasArg("volume")) {
        int newVolume = server.arg("volume").toInt();
        setBuzzerVolume(newVolume);
        
        if (saveSettings()) {
            server.send(200, "text/plain", "Volume set to " + String(settings.buzzerVolume) + "%");
        } else {
            server.send(500, "text/plain", "Failed to save volume");
        }
    } else {
        server.send(400, "text/plain", "Missing volume parameter");
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

void handleToggleAP() {
    apEnabled = !apEnabled;
    Serial.println(apEnabled ? "📶 AP requested ON" : "🚫 AP requested OFF");
}


// ===== WEB SERVER HANDLERS FOR WiFi Management =====
void handleWiFiManage() {
    String action = server.arg("action");
    String ssid = server.arg("ssid");
    
    if (action == "connect" && ssid.length() > 0) {
        for (int i = 0; i < settings.networkCount; i++) {
            if (strcmp(settings.networks[i].ssid, ssid.c_str()) == 0) {
                connectToSpecificWiFi(i);
                break;
            }
        }
        server.sendHeader("Location", "/wifimanage", true);
        server.send(302, "text/plain", "");
        return;
    }
    
    if (action == "remove" && ssid.length() > 0) {
        for (int i = 0; i < settings.networkCount; i++) {
            if (strcmp(settings.networks[i].ssid, ssid.c_str()) == 0) {
                removeWiFiNetwork(i);
                break;
            }
        }
        server.sendHeader("Location", "/wifimanage", true);
        server.send(302, "text/plain", "");
        return;
    }
    
    if (action == "rescan") {
        scanWiFiNetworks(true);
        server.sendHeader("Location", "/wifimanage", true);
        server.send(302, "text/plain", "");
        return;
    }
    
    if (action == "disconnect") {
        WiFi.disconnect(true);
        isConnectedToWiFi = false;
        if (apEnabled) {
            startAPMode();
        }
        server.sendHeader("Location", "/wifimanage", true);
        server.send(302, "text/plain", "");
        return;
    }
    
    if (action == "reconnect") {
        connectToBestWiFi();
        server.sendHeader("Location", "/wifimanage", true);
        server.send(302, "text/plain", "");
        return;
    }
    
    String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>WiFi Manager - Enhanced</title>
    <style>
        body { 
            font-family: Arial, sans-serif; 
            margin: 20px; 
            background: #1a1a1a; 
            color: #fff; 
        }
        .container { 
            max-width: 1200px; 
            margin: 0 auto; 
        }
        .section { 
            background: #2d2d2d; 
            padding: 20px; 
            border-radius: 10px; 
            margin-bottom: 20px; 
            box-shadow: 0 4px 6px rgba(0,0,0,0.3);
        }
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
            font-size: 14px;
            transition: background 0.3s;
        }
        .btn:hover { 
            background: #0066cc; 
            transform: translateY(-2px);
        }
        .btn-scan { 
            background: #ff9900; 
        }
        .btn-success { 
            background: #00cc00; 
        }
        .btn-danger { 
            background: #ff3333; 
        }
        .btn-warning { 
            background: #ff9900; 
        }
        .btn-info { 
            background: #17a2b8; 
        }
        table { 
            width: 100%; 
            border-collapse: collapse; 
            margin: 15px 0; 
        }
        th, td { 
            padding: 12px 15px; 
            text-align: left; 
            border-bottom: 1px solid #444; 
        }
        th { 
            background: #1a1a1a; 
            color: #0088ff;
            font-weight: bold;
        }
        tr:hover { 
            background: #3a3a3a; 
        }
        .signal-good { 
            color: #00ff00; 
        }
        .signal-fair { 
            color: #ff9900; 
        }
        .signal-weak { 
            color: #ff3333; 
        }
        .priority-high { 
            background: rgba(0, 255, 0, 0.15); 
            border-left: 4px solid #00ff00;
        }
        .priority-medium { 
            background: rgba(255, 165, 0, 0.15); 
            border-left: 4px solid #ff9900;
        }
        .priority-low { 
            background: rgba(255, 0, 0, 0.15); 
            border-left: 4px solid #ff3333;
        }
        .ap-status { 
            display: inline-block; 
            padding: 5px 15px; 
            border-radius: 20px; 
            font-size: 14px; 
            font-weight: bold; 
            margin-left: 10px; 
        }
        .ap-on { 
            background: linear-gradient(135deg, #28a745, #20c997); 
            color: white; 
        }
        .ap-off { 
            background: linear-gradient(135deg, #dc3545, #fd7e14); 
            color: white; 
        }
        .signal-indicator {
            display: inline-block;
            width: 100px;
            height: 20px;
            background: #333;
            border-radius: 3px;
            overflow: hidden;
            position: relative;
        }
        .signal-level {
            height: 100%;
            background: linear-gradient(90deg, #ff3333, #ff9900, #00ff00);
            border-radius: 3px;
        }
        .status-badge {
            display: inline-block;
            padding: 3px 8px;
            border-radius: 12px;
            font-size: 12px;
            font-weight: bold;
            margin-left: 5px;
        }
        .connected-badge {
            background: #28a745;
            color: white;
        }
        .saved-badge {
            background: #17a2b8;
            color: white;
        }
        .available-badge {
            background: #6c757d;
            color: white;
        }
        .form-group {
            margin-bottom: 15px;
        }
        .form-group label {
            display: block;
            margin-bottom: 5px;
            color: #ccc;
            font-weight: bold;
        }
        .form-group input,
        .form-group select {
            width: 100%;
            max-width: 400px;
            padding: 10px;
            background: #3a3a3a;
            border: 1px solid #555;
            border-radius: 5px;
            color: white;
            font-size: 14px;
        }
        .form-group input:focus,
        .form-group select:focus {
            outline: none;
            border-color: #0088ff;
            box-shadow: 0 0 0 2px rgba(0, 136, 255, 0.3);
        }
        .tab-container {
            margin-bottom: 20px;
        }
        .tab-buttons {
            display: flex;
            flex-wrap: wrap;
            margin-bottom: 20px;
            border-bottom: 2px solid #444;
        }
        .tab-button {
            background: #2d2d2d;
            color: #ccc;
            padding: 12px 24px;
            border: none;
            border-right: 1px solid #444;
            cursor: pointer;
            font-size: 14px;
            font-weight: bold;
            transition: all 0.3s;
        }
        .tab-button:hover {
            background: #3a3a3a;
            color: white;
        }
        .tab-button.active {
            background: #0088ff;
            color: white;
            border-bottom: 3px solid #00ff00;
        }
        .tab-content {
            display: none;
            background: #2d2d2d;
            padding: 25px;
            border-radius: 0 10px 10px 10px;
            animation: fadeIn 0.5s ease;
        }
        .tab-content.active {
            display: block;
        }
        @keyframes fadeIn {
            from { opacity: 0; transform: translateY(10px); }
            to { opacity: 1; transform: translateY(0); }
        }
        .alert {
            padding: 15px;
            border-radius: 5px;
            margin: 15px 0;
            background: rgba(255, 193, 7, 0.2);
            border-left: 4px solid #ffc107;
            color: #ffc107;
        }
        .alert-success {
            background: rgba(40, 167, 69, 0.2);
            border-left-color: #28a745;
            color: #28a745;
        }
        .alert-info {
            background: rgba(23, 162, 184, 0.2);
            border-left-color: #17a2b8;
            color: #17a2b8;
        }
        .stats-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 15px;
            margin: 20px 0;
        }
        .stat-card {
            background: #3a3a3a;
            padding: 15px;
            border-radius: 8px;
            text-align: center;
        }
        .stat-value {
            font-size: 24px;
            font-weight: bold;
            color: #0088ff;
        }
        .stat-label {
            font-size: 12px;
            color: #aaa;
            margin-top: 5px;
        }
        .action-buttons {
            display: flex;
            flex-wrap: wrap;
            gap: 10px;
            margin: 20px 0;
        }
    </style>
    <script>
        function openTab(evt, tabName) {
            var i, tabcontent, tabbuttons;
            tabcontent = document.getElementsByClassName("tab-content");
            for (i = 0; i < tabcontent.length; i++) {
                tabcontent[i].classList.remove("active");
            }
            tabbuttons = document.getElementsByClassName("tab-button");
            for (i = 0; i < tabbuttons.length; i++) {
                tabbuttons[i].classList.remove("active");
            }
            document.getElementById(tabName).classList.add("active");
            evt.currentTarget.classList.add("active");
            return false;
        }
        
        function confirmRemove(ssid) {
            if (confirm("Are you sure you want to remove '" + ssid + "' from saved networks?")) {
                window.location.href = "/wifimanage?action=remove&ssid=" + encodeURIComponent(ssid);
            }
            return false;
        }
        
        function connectNetwork(ssid) {
            if (confirm("Connect to '" + ssid + "' now?")) {
                window.location.href = "/wifimanage?action=connect&ssid=" + encodeURIComponent(ssid);
            }
            return false;
        }
        
        function toggleAutoConnect(ssid, currentState) {
            var newState = !currentState;
            var form = document.createElement('form');
            form.method = 'POST';
            form.action = '/updatewifi';
            
            var ssidInput = document.createElement('input');
            ssidInput.type = 'hidden';
            ssidInput.name = 'ssid';
            ssidInput.value = ssid;
            form.appendChild(ssidInput);
            
            var autoconnectInput = document.createElement('input');
            autoconnectInput.type = 'hidden';
            autoconnectInput.name = 'autoconnect';
            autoconnectInput.value = newState ? '1' : '0';
            form.appendChild(autoconnectInput);
            
            document.body.appendChild(form);
            form.submit();
        }
        
        function updatePriority(ssid, priority) {
            var form = document.createElement('form');
            form.method = 'POST';
            form.action = '/updatewifi';
            
            var ssidInput = document.createElement('input');
            ssidInput.type = 'hidden';
            ssidInput.name = 'ssid';
            ssidInput.value = ssid;
            form.appendChild(ssidInput);
            
            var priorityInput = document.createElement('input');
            priorityInput.type = 'hidden';
            priorityInput.name = 'priority';
            priorityInput.value = priority;
            form.appendChild(priorityInput);
            
            document.body.appendChild(form);
            form.submit();
        }
        
        function showPassword(id) {
            var input = document.getElementById(id);
            if (input.type === "password") {
                input.type = "text";
            } else {
                input.type = "password";
            }
        }
        
        function testConnection(ssid) {
            var btn = event.target;
            var originalText = btn.innerHTML;
            btn.innerHTML = 'Testing...';
            btn.disabled = true;
            
            fetch('/testwifi?ssid=' + encodeURIComponent(ssid))
                .then(response => response.text())
                .then(text => {
                    alert(text);
                    btn.innerHTML = originalText;
                    btn.disabled = false;
                })
                .catch(error => {
                    alert('Connection test failed: ' + error);
                    btn.innerHTML = originalText;
                    btn.disabled = false;
                });
        }
    </script>
</head>
<body>
    <div class="container">
        <h1 style="border-bottom: 2px solid #0088ff; padding-bottom: 10px;">
            📶 WiFi Manager Pro 
            <span class="ap-status )rawliteral";
    html += apEnabled ? "ap-on" : "ap-off";
    html += R"rawliteral(">AP: )rawliteral";
    html += apEnabled ? "ON" : "OFF";
    html += R"rawliteral(</span>
        </h1>
        
        <div class="stats-grid">
            <div class="stat-card">
                <div class="stat-value">)rawliteral";
    html += String(settings.networkCount);
    html += R"rawliteral(</div>
                <div class="stat-label">Saved Networks</div>
            </div>
            <div class="stat-card">
                <div class="stat-value">)rawliteral";
    html += scannedNetworkCount;
    html += R"rawliteral(</div>
                <div class="stat-label">Available Networks</div>
            </div>
            <div class="stat-card">
                <div class="stat-value">)rawliteral";
    html += String(isConnectedToWiFi ? "Connected" : "Disconnected");
    html += R"rawliteral(</div>
                <div class="stat-label">Current Status</div>
            </div>
            <div class="stat-card">
                <div class="stat-value">)rawliteral";
    html += String(WiFi.RSSI());
    html += R"rawliteral( dBm</div>
                <div class="stat-label">Signal Strength</div>
            </div>
        </div>
        
        <div class="action-buttons">
            <a href="/wifiscan" class="btn btn-scan">📡 Scan All Networks</a>
            <a href="/wifimanage?action=rescan" class="btn btn-info">🔄 Quick Rescan</a>
            )rawliteral";
    
    if (isConnectedToWiFi) {
        html += "<a href='/wifimanage?action=disconnect' class='btn btn-danger'>🔌 Disconnect WiFi</a>";
        html += "<a href='/wifimanage?action=reconnect' class='btn btn-warning'>🔄 Reconnect</a>";
    } else if (settings.networkCount > 0) {
        html += "<a href='/wifimanage?action=reconnect' class='btn btn-success'>🔗 Connect to Best Network</a>";
    }
    
    html += R"rawliteral(
            <a href="/setup" class="btn">⚙️ System Setup</a>
            <a href="/" class="btn">🏠 Dashboard</a>
        </div>
        
        <div class="tab-container">
            <div class="tab-buttons">
                <button class="tab-button active" onclick="openTab(event, 'saved')">💾 Saved Networks</button>
                <button class="tab-button" onclick="openTab(event, 'available')">📶 Available Networks</button>
                <button class="tab-button" onclick="openTab(event, 'add')">➕ Add New Network</button>
                <button class="tab-button" onclick="openTab(event, 'hidden')">👁️ Hidden Network</button>
                <button class="tab-button" onclick="openTab(event, 'advanced')">⚙️ Advanced Settings</button>
            </div>
            
            <!-- Saved Networks Tab -->
            <div id="saved" class="tab-content active">
                <h2>💾 Saved WiFi Networks</h2>
                )rawliteral";
    
    if (settings.networkCount == 0) {
        html += "<div class='alert alert-info'>No WiFi networks saved yet. Add networks using the 'Add New Network' tab.</div>";
    } else {
        html += R"rawliteral(
                <table>
                    <thead>
                        <tr>
                            <th>SSID</th>
                            <th>Priority</th>
                            <th>Auto Connect</th>
                            <th>Last Connection</th>
                            <th>Attempts</th>
                            <th>Status</th>
                            <th>Actions</th>
                        </tr>
                    </thead>
                    <tbody>
        )rawliteral";
        
        for (int i = 0; i < settings.networkCount; i++) {
            WiFiNetwork* net = &settings.networks[i];
            
            String priorityClass;
            if (net->priority >= 8) priorityClass = "priority-high";
            else if (net->priority >= 5) priorityClass = "priority-medium";
            else priorityClass = "priority-low";
            
            // Check if this network is currently connected
            bool isCurrentlyConnected = isConnectedToWiFi && WiFi.SSID() == String(net->ssid);
            
            html += "<tr class='" + priorityClass + "'>";
            html += "<td><strong>" + String(net->ssid) + "</strong>";
            if (isCurrentlyConnected) {
                html += " <span class='status-badge connected-badge'>CONNECTED</span>";
            }
            html += "</td>";
            
            // Priority with dropdown
            html += "<td>";
            html += "<select onchange='updatePriority(\"" + String(net->ssid) + "\", this.value)' style='padding: 5px; background: #3a3a3a; color: white; border: 1px solid #555; border-radius: 3px;'>";
            for (int p = 1; p <= 10; p++) {
                html += "<option value='" + String(p) + "'" + (p == net->priority ? " selected" : "") + ">" + String(p) + "</option>";
            }
            html += "</select>";
            html += "</td>";
            
            // Auto Connect toggle
            html += "<td>";
            html += "<button onclick='toggleAutoConnect(\"" + String(net->ssid) + "\", " + (net->autoConnect ? "true" : "false") + ")' class='btn' style='padding: 5px 10px; font-size: 12px; background: " + (net->autoConnect ? "#00cc00" : "#ff3333") + ";'>";
            html += net->autoConnect ? "✅ ON" : "❌ OFF";
            html += "</button>";
            html += "</td>";
            
            // Last connection time
            html += "<td>";
            if (net->lastConnected > 0) {
                html += getTimeString(net->lastConnected);
            } else {
                html += "Never";
            }
            html += "</td>";
            
            // Connection attempts
            html += "<td>" + String(net->connectionAttempts) + "</td>";
            
            // Signal strength if available
            html += "<td>";
            if (isCurrentlyConnected) {
                html += "<div class='signal-indicator'><div class='signal-level' style='width: " + String(map(constrain(WiFi.RSSI(), -100, -50), -100, -50, 0, 100)) + "%'></div></div>";
                html += " " + String(WiFi.RSSI()) + " dBm";
            } else {
                html += "—";
            }
            html += "</td>";
            
            // Actions
            html += "<td>";
            html += "<div style='display: flex; gap: 5px;'>";
            if (!isCurrentlyConnected) {
                html += "<a href='javascript:void(0)' onclick='connectNetwork(\"" + String(net->ssid) + "\")' class='btn' style='padding: 5px 10px; font-size: 12px;'>Connect</a>";
            }
            html += "<a href='javascript:void(0)' onclick='confirmRemove(\"" + String(net->ssid) + "\")' class='btn' style='padding: 5px 10px; font-size: 12px; background: #ff3333;'>Remove</a>";
            html += "<a href='javascript:void(0)' onclick='testConnection(\"" + String(net->ssid) + "\")' class='btn' style='padding: 5px 10px; font-size: 12px; background: #ff9900;'>Test</a>";
            html += "</div>";
            html += "</td>";
            
            html += "</tr>";
        }
        
        html += R"rawliteral(
                    </tbody>
                </table>
        )rawliteral";
    }
    
    html += R"rawliteral(
            </div>
            
            <!-- Available Networks Tab -->
            <div id="available" class="tab-content">
                <h2>📶 Available Networks</h2>
                <div class="alert">
                    <strong>Note:</strong> Click on any network to add it to your saved networks.
                </div>
                )rawliteral";
    
    if (scannedNetworkCount == 0) {
        html += "<p>No networks found. Click the 'Rescan' button to scan for networks.</p>";
    } else {
        html += R"rawliteral(
                <table>
                    <thead>
                        <tr>
                            <th>SSID</th>
                            <th>Signal</th>
                            <th>Security</th>
                            <th>Channel</th>
                            <th>Status</th>
                            <th>Action</th>
                        </tr>
                    </thead>
                    <tbody>
        )rawliteral";
        
        for (int i = 0; i < scannedNetworkCount; i++) {
            bool isSaved = scannedNetworks[i].configured;
            int signalPercent = map(constrain(scannedNetworks[i].rssi, -100, -50), -100, -50, 0, 100);
            
            html += "<tr>";
            html += "<td><strong>" + String(scannedNetworks[i].ssid) + "</strong>";
            if (isSaved) {
                html += " <span class='status-badge saved-badge'>SAVED</span>";
            }
            html += "</td>";
            
            html += "<td>";
            html += "<div class='signal-indicator'><div class='signal-level' style='width: " + String(signalPercent) + "%'></div></div>";
            html += " " + String(scannedNetworks[i].rssi) + " dBm";
            html += "</td>";
            
            html += "<td>WPA2</td>"; // Assuming all are WPA2 for simplicity
            html += "<td>Auto</td>";
            
            html += "<td>";
            if (isSaved) {
                html += "<span style='color: #00ff00;'>✅ Saved</span>";
            } else {
                html += "<span style='color: #ff9900;'>📶 Available</span>";
            }
            html += "</td>";
            
            html += "<td>";
            if (isSaved) {
                html += "<button disabled class='btn' style='padding: 5px 10px; font-size: 12px; background: #6c757d;'>Already Saved</button>";
            } else {
                html += "<a href='#add' onclick='openTab(event, \"add\"); document.getElementById(\"add_ssid\").value = \"" + String(scannedNetworks[i].ssid) + "\";' class='btn' style='padding: 5px 10px; font-size: 12px;'>➕ Add</a>";
            }
            html += "</td>";
            
            html += "</tr>";
        }
        
        html += R"rawliteral(
                    </tbody>
                </table>
        )rawliteral";
    }
    
    html += R"rawliteral(
                <div style="margin-top: 20px;">
                    <a href="/wifiscan" class="btn btn-scan">📡 Full Network Scan</a>
                    <a href="/wifimanage?action=rescan" class="btn">🔄 Update List</a>
                </div>
            </div>
            
            <!-- Add New Network Tab -->
            <div id="add" class="tab-content">
                <h2>➕ Add New WiFi Network</h2>
                <form action="/savewifi" method="post" onsubmit="return validateForm()">
                    <div class="form-group">
                        <label for="add_ssid">Network SSID (Name)</label>
                        <input type="text" id="add_ssid" name="ssid" placeholder="Enter WiFi network name" required>
                        <small style="color: #aaa;">Enter the exact name of the WiFi network</small>
                    </div>
                    
                    <div class="form-group">
                        <label for="add_password">Password</label>
                        <div style="position: relative;">
                            <input type="password" id="add_password" name="password" placeholder="Enter WiFi password" required style="width: calc(100% - 50px);">
                            <button type="button" onclick="showPassword('add_password')" class="btn" style="position: absolute; right: 0; top: 0; height: 100%; background: #6c757d; border-radius: 0 5px 5px 0;">
                                👁️
                            </button>
                        </div>
                        <small style="color: #aaa;">Case-sensitive password</small>
                    </div>
                    
                    <div class="form-group">
                        <label for="add_priority">Connection Priority</label>
                        <select id="add_priority" name="priority">
                            <option value="10">🔥 Highest Priority (10) - Always connect first</option>
                            <option value="9" selected>⭐ High Priority (9)</option>
                            <option value="7">⚡ Medium Priority (7) - Recommended</option>
                            <option value="5">📶 Normal Priority (5)</option>
                            <option value="3">📡 Low Priority (3)</option>
                            <option value="1">🐌 Lowest Priority (1)</option>
                        </select>
                        <small style="color: #aaa;">Higher priority networks will be tried first</small>
                    </div>
                    
                    <div class="form-group">
                        <label>
                            <input type="checkbox" id="add_autoconnect" name="autoconnect" checked>
                            <span style="margin-left: 5px;">Auto Connect to this network</span>
                        </label>
                    </div>
                    
                    <div class="form-group">
                        <label>
                            <input type="checkbox" id="add_connectnow" name="connectnow" checked>
                            <span style="margin-left: 5px;">Connect immediately after saving</span>
                        </label>
                    </div>
                    
                    <div style="margin-top: 20px;">
                        <button type="submit" class="btn btn-success" style="padding: 12px 30px; font-size: 16px;">
                            💾 Save & Connect
                        </button>
                        <button type="reset" class="btn" style="padding: 12px 30px; font-size: 16px;">
                            🔄 Reset Form
                        </button>
                    </div>
                </form>
                
                <script>
                    function validateForm() {
                        var ssid = document.getElementById('add_ssid').value;
                        var password = document.getElementById('add_password').value;
                        
                        if (ssid.trim() === '') {
                            alert('Please enter the WiFi network name (SSID)');
                            return false;
                        }
                        
                        if (password.length < 8) {
                            if (!confirm('Password is less than 8 characters. Some networks may use shorter passwords. Continue anyway?')) {
                                return false;
                            }
                        }
                        
                        return true;
                    }
                </script>
            </div>
            
            <!-- Hidden Network Tab -->
            <div id="hidden" class="tab-content">
                <h2>👁️ Add Hidden WiFi Network</h2>
                <div class="alert alert-info">
                    <strong>Hidden Networks:</strong> These networks don't broadcast their SSID. You must know the exact network name and password.
                </div>
                
                <form action="/savehiddenwifi" method="post" onsubmit="return validateHiddenForm()">
                    <div class="form-group">
                        <label for="hidden_ssid">Hidden Network SSID</label>
                        <input type="text" id="hidden_ssid" name="hidden_ssid" placeholder="Enter exact hidden network name" required>
                        <small style="color: #aaa;">Case-sensitive. Must match exactly what you entered on your router.</small>
                    </div>
                    
                    <div class="form-group">
                        <label for="hidden_password">Password</label>
                        <div style="position: relative;">
                            <input type="password" id="hidden_password" name="hidden_password" placeholder="Enter hidden network password" required style="width: calc(100% - 50px);">
                            <button type="button" onclick="showPassword('hidden_password')" class="btn" style="position: absolute; right: 0; top: 0; height: 100%; background: #6c757d; border-radius: 0 5px 5px 0;">
                                👁️
                            </button>
                        </div>
                        <small style="color: #aaa;">Enter the exact password for the hidden network</small>
                    </div>
                    
                    <div class="form-group">
                        <label for="hidden_security">Security Type</label>
                        <select id="hidden_security" name="hidden_security">
                            <option value="WPA2_PSK" selected>WPA2 Personal (Most Common)</option>
                            <option value="WPA_PSK">WPA Personal</option>
                            <option value="WEP">WEP (Older/Insecure)</option>
                            <option value="OPEN">Open Network (No Password)</option>
                        </select>
                        <small style="color: #aaa;">Select the security type used by your hidden network</small>
                    </div>
                    
                    <div class="form-group">
                        <label for="hidden_priority">Connection Priority</label>
                        <select id="hidden_priority" name="hidden_priority">
                            <option value="10" selected>🔥 Highest Priority (10)</option>
                            <option value="9">⭐ High Priority (9)</option>
                            <option value="7">⚡ Medium Priority (7)</option>
                            <option value="5">📶 Normal Priority (5)</option>
                        </select>
                        <small style="color: #aaa;">Hidden networks often get higher priority since they're intentional choices</small>
                    </div>
                    
                    <div class="form-group">
                        <label>
                            <input type="checkbox" id="hidden_autoconnect" name="hidden_autoconnect" checked>
                            <span style="margin-left: 5px;">Always try to connect to this hidden network</span>
                        </label>
                    </div>
                    
                    <div class="form-group">
                        <label>
                            <input type="checkbox" id="hidden_broadcast" name="hidden_broadcast">
                            <span style="margin-left: 5px">Broadcast SSID (temporarily make visible for testing)</span>
                        </label>
                        <small style="color: #aaa; display: block;">If connection fails, try enabling this option</small>
                    </div>
                    
                    <div style="margin-top: 20px;">
                        <button type="submit" class="btn btn-success" style="padding: 12px 30px; font-size: 16px; background: linear-gradient(135deg, #28a745, #20c997);">
                            🔒 Save Hidden Network
                        </button>
                        <button type="button" onclick="testHiddenConnection()" class="btn btn-warning" style="padding: 12px 30px; font-size: 16px;">
                            🧪 Test Connection
                        </button>
                    </div>
                </form>
                
                <div class="alert" style="margin-top: 30px;">
                    <h4>💡 Tips for Hidden Networks:</h4>
                    <ul>
                        <li>Double-check SSID spelling and case sensitivity</li>
                        <li>Hidden networks may take longer to connect</li>
                        <li>Ensure your device is within range of the hidden network</li>
                        <li>Some routers have issues with hidden networks - consider making it visible temporarily</li>
                    </ul>
                </div>
                
                <script>
                    function validateHiddenForm() {
                        var ssid = document.getElementById('hidden_ssid').value;
                        var password = document.getElementById('hidden_password').value;
                        var security = document.getElementById('hidden_security').value;
                        
                        if (ssid.trim() === '') {
                            alert('Please enter the hidden network SSID');
                            return false;
                        }
                        
                        if (security !== 'OPEN' && password.trim() === '') {
                            alert('Please enter the password for the hidden network');
                            return false;
                        }
                        
                        if (confirm('Save and connect to hidden network "' + ssid + '"?')) {
                            return true;
                        }
                        
                        return false;
                    }
                    
                    function testHiddenConnection() {
                        var ssid = document.getElementById('hidden_ssid').value;
                        var password = document.getElementById('hidden_password').value;
                        
                        if (!ssid || !password) {
                            alert('Please enter both SSID and password for testing');
                            return;
                        }
                        
                        alert('Test connection to "' + ssid + '". This feature requires backend implementation.');
                        // In a real implementation, this would call a test endpoint
                    }
                </script>
            </div>
            
            <!-- Advanced Settings Tab -->
            <div id="advanced" class="tab-content">
                <h2>⚙️ Advanced WiFi Settings</h2>
                
                <form action="/savewifisettings" method="post">
                    <div class="form-group">
                        <label for="scan_interval">Network Scan Interval (seconds)</label>
                        <input type="number" id="scan_interval" name="scan_interval" min="30" max="3600" value="60">
                        <small style="color: #aaa;">How often to scan for networks (30-3600 seconds)</small>
                    </div>
                    
                    <div class="form-group">
                        <label for="connect_timeout">Connection Timeout (seconds)</label>
                        <input type="number" id="connect_timeout" name="connect_timeout" min="10" max="120" value="20">
                        <small style="color: #aaa;">How long to wait for connection (10-120 seconds)</small>
                    </div>
                    
                    <div class="form-group">
                        <label for="reconnect_attempts">Max Reconnection Attempts</label>
                        <input type="number" id="reconnect_attempts" name="reconnect_attempts" min="1" max="20" value="5">
                        <small style="color: #aaa;">Number of times to retry failed connections</small>
                    </div>
                    
                    <div class="form-group">
                        <label for="rssi_threshold">Minimum RSSI Threshold (dBm)</label>
                        <input type="number" id="rssi_threshold" name="rssi_threshold" min="-100" max="-30" value="-75">
                        <small style="color: #aaa;">Don't connect to networks weaker than this (-100 to -30)</small>
                    </div>
                    
                    <div class="form-group">
                        <label>
                            <input type="checkbox" id="auto_reconnect" name="auto_reconnect" checked>
                            <span style="margin-left: 5px;">Automatic Reconnection</span>
                        </label>
                        <small style="color: #aaa; display: block;">Automatically reconnect when connection is lost</small>
                    </div>
                    
                    <div class="form-group">
                        <label>
                            <input type="checkbox" id="prefer_5ghz" name="prefer_5ghz">
                            <span style="margin-left: 5px;">Prefer 5GHz Networks</span>
                        </label>
                        <small style="color: #aaa; display: block;">Connect to 5GHz networks over 2.4GHz when available</small>
                    </div>
                    
                    <div class="form-group">
                        <label>
                            <input type="checkbox" id="keep_ap_active" name="keep_ap_active" )rawliteral";
    html += apEnabled ? "checked" : "";
    html += R"rawliteral(>
                            <span style="margin-left: 5px;">Keep AP Active When Connected</span>
                        </label>
                        <small style="color: #aaa; display: block;">Allow web access via AP even when connected to WiFi</small>
                    </div>
                    
                    <div style="margin-top: 20px;">
                        <button type="submit" class="btn btn-success">💾 Save Advanced Settings</button>
                        <button type="button" onclick="resetAdvancedSettings()" class="btn btn-danger">🔄 Reset to Defaults</button>
                    </div>
                </form>
                
                <div class="alert" style="margin-top: 30px;">
                    <h4>🔧 Advanced Tools:</h4>
                    <div style="display: flex; gap: 10px; margin-top: 10px;">
                        <a href="/diagnostics" class="btn btn-info">🩺 Run Diagnostics</a>
                        <a href="/clearcache" class="btn btn-warning">🧹 Clear WiFi Cache</a>
                        <a href="/wifilog" class="btn">📋 View Connection Log</a>
                        <a href="/exportwifi" class="btn">💾 Export WiFi Config</a>
                    </div>
                </div>
                
                <script>
                    function resetAdvancedSettings() {
                        if (confirm('Reset all advanced settings to defaults?')) {
                            document.getElementById('scan_interval').value = '60';
                            document.getElementById('connect_timeout').value = '20';
                            document.getElementById('reconnect_attempts').value = '5';
                            document.getElementById('rssi_threshold').value = '-75';
                            document.getElementById('auto_reconnect').checked = true;
                            document.getElementById('prefer_5ghz').checked = false;
                            document.getElementById('keep_ap_active').checked = true;
                        }
                    }
                </script>
            </div>
        </div>
        
        <div style="margin-top: 30px; padding: 20px; background: #2d2d2d; border-radius: 10px;">
            <h3>📊 Current Connection Info</h3>
            <div style="display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 15px;">
                <div>
                    <strong>Connection Status:</strong><br>
                    )rawliteral";
    if (isConnectedToWiFi) {
        html += "<span style='color: #00ff00; font-weight: bold;'>✅ CONNECTED</span>";
        html += "<br><strong>SSID:</strong> " + WiFi.SSID();
        html += "<br><strong>IP Address:</strong> " + WiFi.localIP().toString();
        html += "<br><strong>Gateway:</strong> " + WiFi.gatewayIP().toString();
        html += "<br><strong>Subnet Mask:</strong> " + WiFi.subnetMask().toString();
        html += "<br><strong>DNS:</strong> " + WiFi.dnsIP().toString();
    } else if (apModeActive) {
        html += "<span style='color: #ff9900; font-weight: bold;'>📡 AP MODE ACTIVE</span>";
        html += "<br><strong>AP IP:</strong> 192.168.4.1";
        html += "<br><strong>AP SSID:</strong> PortfolioMonitor_" + String((uint32_t)ESP.getEfuseMac(), HEX);
    } else {
        html += "<span style='color: #ff3333; font-weight: bold;'>❌ DISCONNECTED</span>";
    }
    html += R"rawliteral(
                </div>
                <div>
                    <strong>Signal & Hardware:</strong><br>
                    <strong>RSSI:</strong> )rawliteral";
    html += String(WiFi.RSSI()) + " dBm";
    html += R"rawliteral(<br>
                    <strong>MAC Address:</strong> )rawliteral";
    html += WiFi.macAddress();
    html += R"rawliteral(<br>
                    <strong>Channel:</strong> )rawliteral";
    html += String(WiFi.channel());
    html += R"rawliteral(<br>
                    <strong>Free Memory:</strong> )rawliteral";
    html += String(ESP.getFreeHeap() / 1024) + " KB";
    html += R"rawliteral(
                </div>
                <div>
                    <strong>Connection Statistics:</strong><br>
                    <strong>Uptime:</strong> )rawliteral";
    html += getUptimeString();
    html += R"rawliteral(<br>
                    <strong>Lost Connections:</strong> )rawliteral";
    html += String(connectionLostCount);
    html += R"rawliteral(<br>
                    <strong>Reconnections:</strong> )rawliteral";
    html += String(reconnectSuccessCount);
    html += R"rawliteral(<br>
                    <strong>Last Update:</strong> )rawliteral";
    html += getTimeString(lastDataUpdate);
    html += R"rawliteral(
                </div>
            </div>
        </div>
        
        <div style="margin-top: 30px; text-align: center;">
            <a href="/setup" class="btn">← Back to Setup</a>
            <a href="/" class="btn">← Back to Dashboard</a>
            <a href="/wifimanage?action=rescan" class="btn">🔄 Refresh Page</a>
        </div>
    </div>
</body>
</html>
)rawliteral";
    
    server.send(200, "text/html", html);
}

void handleWiFiScan() {
    scanWiFiNetworks(true);
    
    String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial=1.0">
    <title>WiFi Scan Results</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; background: #1a1a1a; color: #fff; }
        .container { max-width: 800px; margin: 0 auto; }
        .network-list { margin-top: 20px; }
        .network-item { 
            background: #2d2d2d; 
            padding: 15px; 
            margin: 10px 0; 
            border-radius: 8px;
            border-left: 4px solid #0088ff;
        }
        .network-saved { border-left-color: #00cc00; }
        .network-new { border-left-color: #ff9900; }
        .btn { 
            background: #0088ff; 
            color: white; 
            padding: 8px 16px; 
            border: none; 
            border-radius: 5px; 
            cursor: pointer;
            text-decoration: none;
            display: inline-block;
            margin: 5px;
        }
        .btn:hover { background: #0066cc; }
        .btn-save { background: #00cc00; }
        .signal-bar { 
            display: inline-block; 
            width: 100px; 
            height: 20px; 
            background: #333; 
            border-radius: 3px;
            overflow: hidden;
            margin: 0 10px;
        }
        .signal-fill { 
            height: 100%; 
            background: linear-gradient(90deg, #00cc00, #ff9900, #ff3333);
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>📡 WiFi Scan Results</h1>
        <p>Found: )rawliteral";
    
    html += String(scannedNetworkCount);
    html += R"rawliteral( networks</p>
        
        <div class="network-list">
    )rawliteral";
    
    for (int i = 0; i < scannedNetworkCount; i++) {
        String cssClass = scannedNetworks[i].configured ? "network-item network-saved" : "network-item network-new";
        
        int signalStrength = map(constrain(scannedNetworks[i].rssi, -100, -50), -100, -50, 0, 100);
        signalStrength = constrain(signalStrength, 0, 100);
        
        html += "<div class='" + cssClass + "'>";
        html += "<h3>" + String(scannedNetworks[i].ssid) + "</h3>";
        html += "<p>Signal: " + String(scannedNetworks[i].rssi) + " dBm</p>";
        html += "<div class='signal-bar'>";
        html += "<div class='signal-fill' style='width: " + String(signalStrength) + "%;'></div>";
        html += "</div>";
        
        if (scannedNetworks[i].configured) {
            html += "<p>✅ Already saved (Priority: " + String(scannedNetworks[i].priority) + ")</p>";
            html += "<a href='/wifimanage?action=connect&ssid=" + urlEncode(scannedNetworks[i].ssid) + "' class='btn'>Connect Now</a>";
            html += "<a href='/wifimanage?action=remove&ssid=" + urlEncode(scannedNetworks[i].ssid) + "' class='btn' style='background: #ff3333;'>Remove</a>";
        } else {
            html += "<form action='/savewififromscan' method='post' style='display: inline;'>";
            html += "<input type='hidden' name='ssid' value='" + String(scannedNetworks[i].ssid) + "'>";
            html += "<input type='password' name='password' placeholder='Password' required style='padding: 8px; margin-right: 10px;'>";
            html += "<select name='priority' style='padding: 8px; margin-right: 10px;'>";
            html += "<option value='10'>Highest Priority (10)</option>";
            html += "<option value='9' selected>High (9)</option>";
            html += "<option value='7'>Medium (7)</option>";
            html += "<option value='5'>Normal (5)</option>";
            html += "<option value='3'>Low (3)</option>";
            html += "<option value='1'>Lowest (1)</option>";
            html += "</select>";
            html += "<button type='submit' class='btn btn-save'>Save & Connect</button>";
            html += "</form>";
        }
        
        html += "</div>";
    }
    
    html += R"rawliteral(
        </div>
        
        <div style="margin-top: 30px;">
            <a href="/wifimanage" class="btn">← Back to WiFi Manager</a>
            <a href="/setup" class="btn">← Back to Setup</a>
            <a href="/" class="btn">← Back to Dashboard</a>
        </div>
    </div>
</body>
</html>)rawliteral";
    
    server.send(200, "text/html", html);
}

void handleSaveWiFiFromScan() {
    if (server.hasArg("ssid") && server.hasArg("password")) {
        String ssid = server.arg("ssid");
        String password = server.arg("password");
        byte priority = server.hasArg("priority") ? server.arg("priority").toInt() : 7;
        bool autoConnect = server.hasArg("autoconnect");
        
        if (addOrUpdateWiFiNetwork(ssid.c_str(), password.c_str(), priority, autoConnect)) {
            for (int i = 0; i < settings.networkCount; i++) {
                if (strcmp(settings.networks[i].ssid, ssid.c_str()) == 0) {
                    connectToSpecificWiFi(i);
                    break;
                }
            }
            
            String html = "<h1>✅ WiFi Network Saved!</h1>";
            html += "<p>Network: " + ssid + "</p>";
            html += "<p>Priority: " + String(priority) + "</p>";
            html += "<p>Attempting to connect...</p>";
            html += "<a href='/wifimanage'>Go to WiFi Manager</a>";
            server.send(200, "text/html", html);
        }
    }
}

// ===== RESET BUTTON FUNCTIONS =====
void setupResetButton() {
    pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);
    Serial.println("Reset button configured on GPIO 0");
}

void checkResetButton() {
    static unsigned long pressStart = 0;
    static bool buttonPressed = false;
    
    if (digitalRead(RESET_BUTTON_PIN) == LOW) {
        if (!buttonPressed) {
            buttonPressed = true;
            pressStart = millis();
        }
        
        unsigned long holdTime = millis() - pressStart;
        
        if (holdTime > 10000 && !resetInProgress) {
            resetInProgress = true;
            Serial.println("Factory reset triggered (10s hold)");
            factoryReset();
        } else if (holdTime > 3000 && holdTime <= 5000) {
            Serial.println("Button held for " + String(holdTime / 1000) + " seconds");
        }
    } else {
        if (buttonPressed) {
            buttonPressed = false;
            unsigned long holdTime = millis() - pressStart;
            
            if (holdTime > 500 && holdTime < 3000) {
                Serial.println("Short press detected, resetting alerts");
                resetAllAlerts();
            }
        }
    }
}

void factoryReset() {
    Serial.println("Performing factory reset...");
    
    showDisplayMessage("Factory Reset", "In Progress", "Please wait...", "Do not power off");
    
    EEPROM.begin(EEPROM_SIZE);
    for (int i = 0; i < EEPROM_SIZE; i++) {
        EEPROM.write(i, 0);
    }
    bool result = EEPROM.commit();
    EEPROM.end();
    
    if (result) {
        Serial.println("EEPROM cleared");
        playResetAlertTone();
        
        delay(1000);
        
        showDisplayMessage("Factory Reset", "Complete", "Restarting...", "");
        
        delay(2000);
        
        ESP.restart();
    } else {
        Serial.println("Failed to clear EEPROM");
        showDisplayMessage("Factory Reset", "Failed", "EEPROM Error", "");
        delay(3000);
        resetInProgress = false;
    }
}

// ===== API FUNCTIONS =====
void updateAPIStatistics(bool success, unsigned long responseTime) {
    if (success) {
        apiSuccessCount++;
    } else {
        apiErrorCount++;
    }
    
    lastApiCallTime = millis();
    
    if (apiAverageResponseTime == 0) {
        apiAverageResponseTime = responseTime;
    } else {
        apiAverageResponseTime = (apiAverageResponseTime * 0.9) + (responseTime * 0.1);
    }
}

// ===== BATTERY FUNCTIONS =====
void checkBattery() {
    static unsigned long lastCheck = 0;
    unsigned long now = millis();
    
    if (now - lastCheck < BATTERY_CHECK_INTERVAL) return;
    lastCheck = now;
    
    int raw = analogRead(BATTERY_PIN);
    batteryVoltage = raw * (3.3 / 4095.0) * 2.0;
    batteryPercent = calculateBatteryPercent(batteryVoltage);
    
    batteryPercent = 100;
    batteryLow = false;
}

float readBatteryVoltage() {
    int raw = analogRead(BATTERY_PIN);
    float voltage = raw * (3.3 / 4095.0) * 2.0;
    
    static float filteredVoltage = 0.0;
    filteredVoltage = filteredVoltage * 0.9 + voltage * 0.1;
    
    return filteredVoltage;
}

int calculateBatteryPercent(float voltage) {
    voltage = constrain(voltage, BATTERY_EMPTY, BATTERY_FULL);
    int percent = (int)((voltage - BATTERY_EMPTY) / (BATTERY_FULL - BATTERY_EMPTY) * 100.0);
    return constrain(percent, 0, 100);
}

void updateBatteryDisplay() {
}

void detectPowerSource() {
    float vbat = readBatteryVoltage();
    
    if (vbat < 1.0) {
        powerSource = POWER_SOURCE_USB;
        batteryPercent = 100;
        settings.showBattery = false;
        Serial.println("Power: USB (no battery detected)");
    } else if (vbat >= 3.0 && vbat <= 4.5) {
        powerSource = POWER_SOURCE_BATTERY;
        settings.showBattery = true;
        Serial.println("Power: Li-Ion Battery (" + String(vbat, 2) + "V)");
    } else {
        powerSource = POWER_SOURCE_EXTERNAL;
        batteryPercent = 100;
        settings.showBattery = false;
        Serial.println("Power: External Source (" + String(vbat, 2) + "V)");
    }
    
    saveSettings();
}

// ===== NEW WiFi HANDLERS =====

void handleSaveHiddenWiFi() {
    if (server.hasArg("hidden_ssid") && server.hasArg("hidden_password")) {
        String ssid = server.arg("hidden_ssid");
        String password = server.arg("hidden_password");
        byte priority = server.hasArg("hidden_priority") ? server.arg("hidden_priority").toInt() : 10;
        bool autoConnect = server.hasArg("hidden_autoconnect");
        
        Serial.println("\n🔒 SAVING HIDDEN NETWORK: " + ssid);
        
        if (addOrUpdateWiFiNetwork(ssid.c_str(), password.c_str(), priority, autoConnect)) {
            String html = "<!DOCTYPE html><html><head>";
            html += "<meta http-equiv='refresh' content='3;url=/wifimanage' />";
            html += "<title>Hidden Network Added</title>";
            html += "</head><body>";
            html += "<h1>✅ Hidden Network Saved!</h1>";
            html += "<p>Network: <strong>" + ssid + "</strong></p>";
            html += "<p>Redirecting to WiFi manager in 3 seconds.</p>";
            html += "</body></html>";
            
            server.send(200, "text/html", html);
        } else {
            server.send(500, "text/html", "<h1>❌ Failed to save</h1><a href='/wifimanage'>Go Back</a>");
        }
    }
}

void handleUpdateWiFi() {
    if (server.hasArg("ssid")) {
        String ssid = server.arg("ssid");
        
        for (int i = 0; i < settings.networkCount; i++) {
            if (strcmp(settings.networks[i].ssid, ssid.c_str()) == 0) {
                if (server.hasArg("autoconnect")) {
                    settings.networks[i].autoConnect = server.arg("autoconnect") == "1";
                }
                
                if (server.hasArg("priority")) {
                    settings.networks[i].priority = server.arg("priority").toInt();
                }
                
                saveSettings();
                server.send(200, "text/plain", "Updated " + ssid);
                return;
            }
        }
    }
    
    server.send(404, "text/plain", "Not found");
}

void handleTestWiFi() {
    if (server.hasArg("ssid")) {
        String ssid = server.arg("ssid");
        
        for (int i = 0; i < settings.networkCount; i++) {
            if (strcmp(settings.networks[i].ssid, ssid.c_str()) == 0) {
                Serial.println("Testing: " + String(settings.networks[i].ssid));
                
                WiFi.disconnect();
                delay(500);
                WiFi.begin(settings.networks[i].ssid, settings.networks[i].password);
                
                int attempts = 0;
                bool connected = false;
                
                while (attempts < 10) {
                    delay(500);
                    attempts++;
                    if (WiFi.status() == WL_CONNECTED) {
                        connected = true;
                        break;
                    }
                }
                
                String result;
                if (connected) {
                    int rssi = WiFi.RSSI();
                    result = "✅ Success! RSSI: " + String(rssi) + " dBm";
                    WiFi.disconnect();
                    
                    // Reconnect to previous network if any
                    if (settings.lastConnectedIndex >= 0) {
                        WiFi.begin(settings.networks[settings.lastConnectedIndex].ssid, 
                                  settings.networks[settings.lastConnectedIndex].password);
                    }
                } else {
                    result = "❌ Failed. Status: " + String(WiFi.status());
                }
                
                server.send(200, "text/plain", result);
                return;
            }
        }
    }
    
    server.send(404, "text/plain", "Not found");
}

void handleSaveWiFiSettings() {
    if (server.hasArg("keep_ap_active")) {
        bool keepAP = server.hasArg("keep_ap_active");
        apEnabled = keepAP;
        saveAPState();
        
        server.sendHeader("Location", "/wifimanage", true);
        server.send(302, "text/plain", "AP setting saved");
    }
}

// ===== SETUP AND LOOP =====
void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n\n========================================");
    Serial.println("PORTFOLIO MONITOR v4.5.3");
    Serial.println("ESP32-WROVER-E with Enhanced WiFi");
    Serial.println("========================================");
    
    systemStartTime = millis();
    
    // 1. ابتدا فقط تنظیمات ضروری را بارگذاری کنید
    EEPROM.begin(EEPROM_SIZE);
    if (!loadSettings()) {
        Serial.println("Using default settings");
        initializeSettings();
    }
    EEPROM.end();
    
    // Load AP state
    loadAPState();
    
    settings.bootCount++;
    settings.totalUptime += (millis() - settings.firstBoot);
    saveSettings();
    
    // 2. راه‌اندازی سخت‌افزار اولیه
    setupDisplay();
    setupBuzzer();
    setupLEDs();  // اضافه شده
    setupRGBLEDs();  // اضافه شده
    setupResetButton();  // اضافه شده
    
    // 3. تشخیص منبع تغذیه
    detectPowerSource();
    
    // 4. Play startup tone
    Serial.print("Playing startup tone with volume: ");
    Serial.print(settings.buzzerVolume);
    Serial.println("%");
    playStartupTone();
    
    delay(500);
    
    // 5. تنظیم حالت WiFi
    updateWiFiMode();
    
    // 6. ابتدا سریع AP را راه‌اندازی کنید (اگر فعال است)
    if (apEnabled && !isConnectedToWiFi) {
        Serial.println("Quick AP startup...");
        startAPMode();
    }
    
    // 7. سپس سعی کنید به WiFi وصل شوید (اگر شبکه‌ای ذخیره شده باشد)
    // تصمیم‌گیری برای حالت WiFi
    if (settings.networkCount > 0) {
        // ابتدا سعی کن به WiFi وصل شوی
        Serial.println("Attempting WiFi connection...");
        bool wifiConnected = connectToBestWiFi();
        
        if (!wifiConnected && apEnabled) {
            // اگر WiFi وصل نشد و AP فعال است
            Serial.println("WiFi connection failed, starting AP mode");
            startAPMode();
        }
    } else if (apEnabled) {
        // اگر شبکه‌ای ذخیره نشده و AP فعال است
        Serial.println("No saved networks, starting AP mode");
        startAPMode();
    }
    
    // 8. راه‌اندازی وب سرور
    setupWebServer();
    
    // 9. تست اولیه
    if (settings.buzzerEnabled) {
        testVolumeRange();
    }
    
    Serial.println("\n✅ System initialized successfully!");
    Serial.println("Free memory: " + String(ESP.getFreeHeap()) + " bytes");
    Serial.println("WiFi Status: " + String(isConnectedToWiFi ? "Connected" : "Disconnected"));
    Serial.println("AP Mode: " + String(apModeActive ? "Active" : "Inactive"));
    Serial.println("AP Enabled: " + String(apEnabled ? "Yes" : "No"));
    if (apModeActive) {
        Serial.println("AP IP: " + WiFi.softAPIP().toString());
    }
    if (isConnectedToWiFi) {
        Serial.println("Station IP: " + WiFi.localIP().toString());
        Serial.println("SSID: " + WiFi.SSID());
        syncTime();  // همگام‌سازی زمان
    }
    
    // 10. مقداردهی اولیه متغیرهای زمانی
    lastDataUpdate = millis() - DATA_UPDATE_INTERVAL;
    lastAlertCheck = millis();
    lastDisplayUpdate = millis();
    lastWiFiCheck = millis();
    lastBatteryCheck = millis();
}

void loop() {
    // 1. هندل کردن کلاینت‌های وب سرور
    server.handleClient();
    
    // NEW: مدیریت حالت WiFi
    //manageWiFiMode();
    static unsigned long lastWiFiManage = 0;

    if (millis() - lastWiFiManage > 5000) {
        lastWiFiManage = millis();
        manageWiFiMode();
    }
    unsigned long now = millis();
    
    // 2. کنترل backlight نمایشگر
    if (settings.displayBrightness > 0) {
        digitalWrite(TFT_BL_PIN, HIGH);
    }
    
    // 3. مدیریت اتصال WiFi
    if (!isConnectedToWiFi && !apModeActive) {
        if (now - lastReconnectAttempt > RECONNECT_INTERVAL) {
            lastReconnectAttempt = now;
            if (settings.autoReconnect && settings.networkCount > 0) {
                Serial.println("Attempting auto-reconnect...");
                connectToBestWiFi();
            } else if (apEnabled && !apModeActive) {
                Serial.println("Starting AP mode after failed reconnection...");
                startAPMode();
            }
        }
    }
    
    // 4. بررسی وضعیت WiFi
    if (now - lastWiFiCheck > 10000) {
        lastWiFiCheck = now;
        checkWiFiStatus();
        checkConnectionStatus();
    }
    
    // 5. به‌روزرسانی داده‌ها از API (اگر متصل باشد)
    if (isConnectedToWiFi) {
        if (now - lastDataUpdate > DATA_UPDATE_INTERVAL) {
            lastDataUpdate = now;
            
            // دریافت داده برای Entry Mode
            if (strlen(settings.entryPortfolio) > 0) {
                String data1 = getPortfolioData(0);
                if (data1 != "{}") {
                    parseCryptoData(data1, 0);
                    calculatePortfolioSummary(0);
                }
            }
            
            // دریافت داده برای Exit Mode
            if (strlen(settings.exitPortfolio) > 0) {
                String data2 = getPortfolioData(1);
                if (data2 != "{}") {
                    parseCryptoData(data2, 1);
                    calculatePortfolioSummary(1);
                }
            }
        }
        
        // به‌روزرسانی زمان
        updateDateTime();
    }
    
    // 6. بررسی آلرت‌ها
    if (now - lastAlertCheck > 5000) {
        lastAlertCheck = now;
        if (cryptoCountMode1 > 0) checkAlerts(0);
        if (cryptoCountMode2 > 0) checkAlerts(1);
    }
    
    // 7. بررسی باتری
    if (now - lastBatteryCheck > BATTERY_CHECK_INTERVAL) {
        lastBatteryCheck = now;
        checkBattery();
    }
    
    // 8. به‌روزرسانی نمایشگر
    if (now - lastDisplayUpdate > DISPLAY_UPDATE_INTERVAL) {
        lastDisplayUpdate = now;
        updateDisplay();
    }
    
    // 9. به‌روزرسانی LEDها
    updateLEDs();
    updateRGBLEDs();
    
    // 10. بررسی دکمه ریست
    checkResetButton();
    
    // 11. تنظیم backlight بر اساس timeout
    if (settings.displayTimeout > 0) {
        static unsigned long lastInteraction = millis();
        
        // اگر آلرت اخیراً فعال بوده، زمان تعامل را به‌روز کن
        if (now - lastAlertTime < 10000) {
            lastInteraction = now;
        }
        
        // اگر زمان تعامل گذشته باشد، backlight را خاموش کن
        if (now - lastInteraction > settings.displayTimeout) {
            digitalWrite(TFT_BL_PIN, LOW);
        }
    }
}






void setupWebServer() {
    Serial.println("Setting up web server...");
    
    // Routeهای اصلی
    server.on("/", handleRoot);
    server.on("/setup", handleSetup);
    server.on("/savewifi", HTTP_POST, handleSaveWiFi);
    server.on("/saveapi", HTTP_POST, handleSaveAPI);
    server.on("/savealert", HTTP_POST, handleSaveAlert);
    server.on("/savemode", HTTP_POST, handleSaveMode);
    server.on("/savedisplay", HTTP_POST, handleSaveDisplay);
    server.on("/savergb", HTTP_POST, handleSaveRGB);
    server.on("/saveemergency", HTTP_POST, handleSaveEmergency);
    
    // WiFi management routes
    server.on("/wifimanage", handleWiFiManage);
    server.on("/wifiscan", handleWiFiScan);
    server.on("/savewififromscan", HTTP_POST, handleSaveWiFiFromScan);
    
    // New WiFi routes
    server.on("/savehiddenwifi", HTTP_POST, handleSaveHiddenWiFi);
    server.on("/updatewifi", HTTP_POST, handleUpdateWiFi);
    server.on("/testwifi", handleTestWiFi);
    server.on("/savewifisettings", HTTP_POST, handleSaveWiFiSettings);
            
    // System routes
    server.on("/refresh", handleRefresh);
    server.on("/testalert", handleTestAlert);
    server.on("/resetalerts", handleResetAlerts);
    server.on("/systeminfo", handleSystemInfo);
    server.on("/apistatus", handleAPIStatus);
    server.on("/ledcontrol", handleLEDControl);
    server.on("/rgbcontrol", handleRGBControl);
    server.on("/displaycontrol", handleDisplayControl);
    server.on("/factoryreset", handleFactoryReset);
    server.on("/restart", handleRestart);
    server.on("/positions", handlePositions);
    server.on("/toggleap", handleToggleAP);
    server.on("/setvolume", handleSetVolume);
    server.on("/testvolume", handleTestVolume);
    
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
    
    // شروع سرور
    server.begin();
    Serial.println("✅ Web server started on port 80");
    
    if (apModeActive) {
        Serial.println("🌐 AP Access Point: http://" + WiFi.softAPIP().toString());
    }
    if (isConnectedToWiFi) {
        Serial.println("🌐 Station IP: http://" + WiFi.localIP().toString());
    }
}
