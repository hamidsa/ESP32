/* ============================================================================
   PORTFOLIO MONITOR - ESP32-WROVER-E
   Professional Dual Mode Portfolio Tracking System
   Version: 4.5.0 - Complete Professional Edition
   Hardware: ESP32-WROVER-E + ST7789 240x240 + Dual RGB LEDs + 4 Single LEDs
   Features: Simultaneous Entry/Exit Mode, Smart RGB Visualization
             High Resolution Display, Complete Web Interface
             Auto Reconnect, Battery Monitoring
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
#define MAX_POSITIONS_PER_MODE 40
#define MAX_WIFI_NETWORKS 5
#define EEPROM_SIZE 4096
#define JSON_BUFFER_SIZE 8192  
#define DISPLAY_CRYPTO_COUNT 8

#define POWER_SOURCE_USB 0
#define POWER_SOURCE_BATTERY 1
#define POWER_SOURCE_EXTERNAL 2

// ===== PIN DEFINITIONS =====
// RGB LEDs (Common Cathode)
#define RGB1_RED    32
#define RGB1_GREEN  33
#define RGB1_BLUE   25

#define RGB2_RED    26
#define RGB2_GREEN  14
#define RGB2_BLUE   12

// Single Color LEDs
#define LED_MODE1_GREEN   27  // Entry Mode LONG/PROFIT
#define LED_MODE1_RED     13  // Entry Mode SHORT/LOSS
#define LED_MODE2_GREEN   21  // Exit Mode PROFIT
#define LED_MODE2_RED     19  // Exit Mode LOSS

// Buzzer
#define BUZZER_PIN        22
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
#define DEFAULT_VOLUME 5
#define VOLUME_MIN 0
#define VOLUME_MAX 20
#define VOLUME_OFF 0
#define DEFAULT_LED_BRIGHTNESS 100

// ===== BATTERY SETTINGS =====
#define BATTERY_FULL 8.4
#define BATTERY_EMPTY 6.6
#define BATTERY_WARNING 20  // درصد هشدار

// اضافه کردن این defineها:
#define POWER_SOURCE_USB 0
#define POWER_SOURCE_BATTERY 1
#define POWER_SOURCE_EXTERNAL 2

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

// متغیرهای جدید:
WiFiNetwork scannedNetworks[20];  // شبکه‌های اسکن شده
int scannedNetworkCount = 0;
bool isScanning = false;
unsigned long lastScanTime = 0;
#define SCAN_INTERVAL 60000  // هر 1 دقیقه اسکن کن

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

// اضافه کردن این خط:
byte powerSource = POWER_SOURCE_USB; // پیش‌فرض USB

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
unsigned long lastAlertTime = 0;
#define ALERT_AUTO_RETURN_TIME 8000  // 8 
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

void setupBuzzer();
void playTone(int frequency, int duration, int volumePercent);
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

void showAlert(String title, String symbol, String message, bool isLong, bool isSevere, float price, byte mode);
void showExitAlert(String title, String symbol, String message, bool isProfit, float changePercent, float price);
void checkAlerts(byte mode);
void processEntryAlerts();
void processExitAlerts();
void resetAllAlerts();
void addToAlertHistory(const char* symbol, float pnlPercent, float price, bool isLong, bool isSevere, bool isProfit, byte alertType, byte mode);
void displayAlertHistory();

void parseCryptoData(String jsonData, byte mode);
String getPortfolioData(byte mode);
String base64Encode(String data);
void sortPositionsByLoss(CryptoPosition* data, int count);
void calculatePortfolioSummary(byte mode);
void clearCryptoData(byte mode);

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

void updateDateTime();
bool syncTime();
String getCurrentTime();
String getCurrentDate();
unsigned long getCurrentTimestamp();

void initializeSettings();
bool loadSettings();
bool saveSettings();
bool addWiFiNetwork(const char* ssid, const char* password);
bool removeWiFiNetwork(const char* ssid);
bool connectToWiFi();
bool connectToBestWiFi();
bool connectToSpecificWiFi(int networkIndex);
bool addOrUpdateWiFiNetwork(const char* ssid, const char* password, byte priority = 5, bool autoConnect = true);
void removeWiFiNetwork(int index);
void reorderWiFiNetworks();
void scanWiFiNetworks(bool forceScan = false);
bool startAPMode();
void handleWiFiConnection();
void checkWiFiStatus();
void checkConnectionStatus();
void attemptReconnect();

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
String generateDashboardHTML();
String generateSetupHTML();
String generateSystemInfoHTML();
String generateAlertHistoryHTML(byte mode);
String generatePositionListHTML(byte mode);

void setupResetButton();
void checkResetButton();
void factoryReset();
void restartSystem();
void enterDeepSleep();
void printSystemStatus();
void debugMemory();
void logEvent(String event, String details = "");

void makeAPICall(byte mode);
void handleAPIResponse(String response, byte mode);
void updateAPIStatistics(bool success, unsigned long responseTime);

void checkBattery();
float readBatteryVoltage();
int calculateBatteryPercent(float voltage);
void updateBatteryDisplay();
void detectPowerSource();  // اضافه کردن این خط
void calibrateBattery();   // اضافه کردن این خط

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
    tft.println("Complete v4.5");
    
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setCursor(20, 130);
    tft.println("ESP32-WROVER-E");
    tft.setCursor(30, 145);
    tft.println("Dual Mode Pro");
    
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
    
    // 🆕 **برگشت اتوماتیک از آلرت بعد از 8 ثانیه**
    if (showingAlert) {
        // اگر زمان آلرت گذشته باشد، به صفحه اصلی برگرد
        if (now - alertDisplayStart > ALERT_AUTO_RETURN_TIME) {
            showingAlert = false;
            alertTitle = "";
            alertMessage = "";
            lastAlertTime = 0;
            showMainDisplay();
            return;
        }
        
        // نمایش آلرت
        showAlertDisplay();
        return;
    }
    
    // 🆕 **پیشگیری از خاموش شدن هنگام آلرت** 
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
    
    // 🆕 **فونت بزرگ برای آلرت**
    tft.setTextColor(TFT_WHITE, bgColor);
    tft.setTextSize(3);
    tft.setCursor(20, 10);
    tft.print(alertTitle);
    
    // نماد با فونت بسیار بزرگ
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setTextSize(4);
    tft.setCursor(30, 70);
    tft.print(alertSymbol);
    
    // قیمت
    tft.setTextSize(3);
    tft.setCursor(30, 120);
    tft.print("$");
    tft.print(formatPrice(alertPrice));
    
    // پیام
    tft.setTextSize(2);
    tft.setCursor(30, 160);
    tft.print(alertMessage);
    
    // زمان باقی‌مانده
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
    }
    
    tft.fillScreen(TFT_BLACK);
    
    // 🆕 **افزایش قابل توجه اندازه فونت‌ها**
    
    // عنوان اصلی - فونت بزرگ
    tft.setTextSize(3); // از 2 به 3 افزایش دادیم
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setCursor(10, 5);
    tft.print("PORTFOLIO");
    
    // وضعیت WiFi
    tft.setTextSize(1);
    tft.setCursor(10, 40);
    tft.print("WiFi:");
    
    if (isConnectedToWiFi) {
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        String ssid = WiFi.SSID();
        if (ssid.length() > 12) {
            ssid = ssid.substring(0, 12) + "...";
        }
        tft.setCursor(50, 40);
        tft.print(ssid);
    } else if (apModeActive) {
        tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        tft.setCursor(50, 40);
        tft.print("AP Mode");
    } else {
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.setCursor(50, 40);
        tft.print("Disconnected");
    }
    
    // 🆕 **مقادیر با فونت بزرگتر**
    
    // Mode 1
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(10, 70);
    tft.print("ENTRY:");
    
    tft.setCursor(20, 85);
    tft.print("Pos:");
    tft.setCursor(70, 85);
    tft.setTextSize(2); // بزرگتر برای مقادیر
    tft.print(cryptoCountMode1);
    
    tft.setTextSize(1);
    tft.setCursor(20, 110);
    tft.print("P/L:");
    tft.setCursor(70, 110);
    tft.setTextSize(2);
    tft.setTextColor(portfolioMode1.totalPnlPercent >= 0 ? TFT_GREEN : TFT_RED, TFT_BLACK);
    tft.print(formatPercent(portfolioMode1.totalPnlPercent));
    
    // Mode 2
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(140, 70);
    tft.print("EXIT:");
    
    tft.setCursor(150, 85);
    tft.print("Pos:");
    tft.setCursor(200, 85);
    tft.setTextSize(2);
    tft.print(cryptoCountMode2);
    
    tft.setTextSize(1);
    tft.setCursor(150, 110);
    tft.print("P/L:");
    tft.setCursor(200, 110);
    tft.setTextSize(2);
    tft.setTextColor(portfolioMode2.totalPnlPercent >= 0 ? TFT_GREEN : TFT_RED, TFT_BLACK);
    tft.print(formatPercent(portfolioMode2.totalPnlPercent));
    
    // زمان - با فونت خوانا
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(10, 150);
    tft.print("Time:");
    
    if (currentDateTime.length() > 10) {
        tft.setCursor(80, 150);
        tft.print(currentDateTime.substring(11, 19));
    } else {
        tft.setCursor(80, 150);
        tft.print("--:--:--");
    }
    
    // وضعیت سیستم
    tft.setTextSize(1);
    tft.setCursor(10, 180);
    
    if (mode1GreenActive || mode1RedActive || mode2GreenActive || mode2RedActive) {
        tft.setTextColor(TFT_ORANGE, TFT_BLACK);
        tft.print("ALERT!");
    } else if (connectionLost) {
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.print("NO CONN");
    } else {
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.print("READY");
    }
    
    // Battery/USB indicator
    if (powerSource == POWER_SOURCE_USB) {
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        tft.setCursor(180, 180);
        tft.print("USB");
    } else if (settings.showBattery) {
        drawBatteryIcon(180, 180, batteryPercent);
    }
    
    // ارزش کل پرتفوی
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(10, 200);
    tft.print("Total:");
    
    float totalValue = portfolioMode1.totalCurrentValue + portfolioMode2.totalCurrentValue;
    tft.setCursor(60, 200);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.print("$");
    tft.print(formatNumber(totalValue));
}

void showConnectionScreen() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(30, 40);
    tft.println("CONNECTING");
    tft.setCursor(50, 70);
    tft.println("TO WiFi");
    
    tft.setTextSize(1);
    tft.setCursor(40, 120);
    tft.println("Please wait...");
}

void showConnectionLostScreen() {
    tft.fillScreen(TFT_BLACK);
    
    tft.drawRect(0, 0, 239, 239, TFT_RED);
    
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setTextSize(3);
    tft.setCursor(40, 50);
    tft.println("NO");
    tft.setCursor(30, 90);
    tft.println("INTERNET");
    
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(30, 140);
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
        // 🆕 حالت USB را نشان بده
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        tft.setTextSize(1);
        tft.setCursor(x, y);
        tft.print("USB");
        return;
    }
    
    // اگر نمایش باتری فعال است، آیکون باتری بکش
    // بدنه باتری
    tft.drawRect(x, y, 30, 15, TFT_WHITE);
    tft.drawRect(x + 30, y + 4, 3, 7, TFT_WHITE);
    
    // سطح شارژ
    int fillWidth = (28 * percent) / 100;
    fillWidth = constrain(fillWidth, 0, 28);
    
    uint32_t fillColor;
    if (percent > 50) fillColor = TFT_GREEN;
    else if (percent > 20) fillColor = TFT_YELLOW;
    else fillColor = TFT_RED;
    
    if (fillWidth > 0) {
        tft.fillRect(x + 1, y + 1, fillWidth, 13, fillColor);
    }
    
    // درصد
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(x + 35, y + 4);
    tft.print(String(percent) + "%");
}

void showDisplayMessage(String line1, String line2, String line3, String line4) {
    // روشن کردن backlight برای پیام‌ها
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
        digitalWrite(TFT_BL_PIN, HIGH);  // استفاده از TFT_BL_PIN
    } else {
        digitalWrite(TFT_BL_PIN, LOW);
    }
}

void setDisplayBrightness(int brightness) {
    settings.displayBrightness = constrain(brightness, 0, 100);
    
    if (settings.displayBrightness > 0) {
        digitalWrite(TFT_BL_PIN, HIGH);  // استفاده از TFT_BL_PIN
    } else {
        digitalWrite(TFT_BL_PIN, LOW);
    }
    
    saveSettings();
    Serial.println("Display brightness set to " + String(settings.displayBrightness) + "%");
}

// ===== BUZZER FUNCTIONS =====
void setupBuzzer() {
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
    
    Serial.println("Buzzer initialized");
}

void playTone(int frequency, int duration, int volumePercent) {
    if (!settings.buzzerEnabled || settings.buzzerVolume == VOLUME_OFF) {
        return;
    }
    
    if (frequency == 0 || duration == 0) {
        noTone(BUZZER_PIN);
        return;
    }
    
    // محاسبه حجم صدا
    int volume = map(settings.buzzerVolume, VOLUME_MIN, VOLUME_MAX, 0, 100);
    volume = (volume * volumePercent) / 100;
    
    if (volume > 0) {
        tone(BUZZER_PIN, frequency, duration);
        
        // اگر حجم صدا کم است، PWM ساده استفاده می‌کنیم
        if (volume <= 30) {
            // برای حجم کم، پالس‌های کوتاه
            for (int i = 0; i < duration; i += 20) {
                digitalWrite(BUZZER_PIN, HIGH);
                delay(5);
                digitalWrite(BUZZER_PIN, LOW);
                delay(15);
            }
        } else {
            // برای حجم معمولی، tone استاندارد
            tone(BUZZER_PIN, frequency, duration);
            delay(duration);
            noTone(BUZZER_PIN);
        }
    } else {
        noTone(BUZZER_PIN);
    }
}

void playLongPositionAlert(bool isSevere) {
    if (!settings.buzzerEnabled || settings.buzzerVolume == 0) return;
    
    Serial.println("Playing LONG alert" + String(isSevere ? " (SEVERE)" : ""));
    
    if (isSevere) {
        // آلرت شدید: سه بوق با فرکانس پایین
        playTone(440, 200, 80); // A4
        delay(150);
        playTone(392, 200, 80); // G4
        delay(150);
        playTone(349, 300, 80); // F4
    } else {
        // آلرت عادی: دو بوق
        playTone(523, 300, 70); // C5
        delay(200);
        playTone(523, 300, 70); // C5
    }
}

void playShortPositionAlert(bool isSevere) {
    if (!settings.buzzerEnabled || settings.buzzerVolume == 0) return;
    
    Serial.println("Playing SHORT alert" + String(isSevere ? " (SEVERE)" : ""));
    
    if (isSevere) {
        // آلرت شدید: سه بوق با فرکانس بالا
        playTone(784, 100, 85); // G5
        delay(80);
        playTone(880, 100, 85); // A5
        delay(80);
        playTone(988, 150, 85); // B5
    } else {
        // آلرت عادی: یک بوق
        playTone(659, 250, 70); // E5
    }
}

void playExitAlertTone(bool isProfit) {
    if (!settings.buzzerEnabled) return;
    
    Serial.println("Playing EXIT alert for " + String(isProfit ? "PROFIT" : "LOSS"));
    
    if (isProfit) {
        playTone(LONG_NORMAL_TONE, 300, 80);
        delay(200);
        playTone(LONG_NORMAL_TONE, 300, 80);
    } else {
        playTone(SHORT_NORMAL_TONE, 250, 80);
    }
}

void playPortfolioAlert() {
    if (!settings.buzzerEnabled) return;
    
    Serial.println("Playing PORTFOLIO alert");
    
    for (int i = 0; i < 3; i++) {
        playTone(PORTFOLIO_ALERT_TONE, 200, 75);
        delay(150);
    }
}

void playTestAlertSequence() {
    if (!settings.buzzerEnabled) {
        Serial.println("Buzzer disabled, skipping test");
        return;
    }
    
    Serial.println("Playing test sequence...");
    
    playLongPositionAlert(false);
    delay(500);
    playShortPositionAlert(false);
    delay(500);
    playExitAlertTone(true);
    delay(500);
    playExitAlertTone(false);
    delay(500);
    playSuccessTone();
    
    Serial.println("Test sequence complete");
}

void playResetAlertTone() {
    if (!settings.buzzerEnabled) return;
    
    Serial.println("Playing reset tone");
    
    playTone(RESET_TONE_1, 80, 50);
    delay(50);
    playTone(RESET_TONE_2, 60, 50);
    delay(50);
    playTone(RESET_TONE_3, 100, 50);
}

void playSuccessTone() {
    if (!settings.buzzerEnabled) return;
    
    playTone(SUCCESS_TONE_1, 150, 60);
    delay(100);
    playTone(SUCCESS_TONE_2, 200, 60);
}

void playErrorTone() {
    if (!settings.buzzerEnabled) return;
    
    playTone(ERROR_TONE_1, 200, 60);
    delay(100);
    playTone(ERROR_TONE_2, 250, 60);
}

void playConnectionLostTone() {
    if (!settings.buzzerEnabled) return;
    
    Serial.println("Playing connection lost tone");
    
    for (int i = 0; i < 2; i++) {
        playTone(CONNECTION_LOST_TONE, 300, 70);
        delay(200);
        playTone(CONNECTION_LOST_TONE, 300, 70);
        delay(500);
    }
}

void playStartupTone() {
    if (!settings.buzzerEnabled) return;
    
    playTone(600, 100, 50);
    delay(50);
    playTone(800, 150, 50);
    delay(50);
    playTone(1000, 200, 50);
}

// ===== LED FUNCTIONS =====
void setupLEDs() {
    Serial.println("Initializing LEDs...");
    
    pinMode(LED_MODE1_GREEN, OUTPUT);
    pinMode(LED_MODE1_RED, OUTPUT);
    pinMode(LED_MODE2_GREEN, OUTPUT);
    pinMode(LED_MODE2_RED, OUTPUT);
    
    // Test LEDs
    digitalWrite(LED_MODE1_GREEN, HIGH); delay(100); digitalWrite(LED_MODE1_GREEN, LOW);
    digitalWrite(LED_MODE1_RED, HIGH); delay(100); digitalWrite(LED_MODE1_RED, LOW);
    digitalWrite(LED_MODE2_GREEN, HIGH); delay(100); digitalWrite(LED_MODE2_GREEN, LOW);
    digitalWrite(LED_MODE2_RED, HIGH); delay(100); digitalWrite(LED_MODE2_RED, LOW);
    
    Serial.println("LEDs initialized");
}

void setupRGBLEDs() {
    Serial.println("Initializing RGB LEDs (Common Cathode)...");
    
    // ابتدا همه به عنوان OUTPUT تنظیم شوند
    pinMode(RGB1_RED, OUTPUT);
    pinMode(RGB1_GREEN, OUTPUT);
    pinMode(RGB1_BLUE, OUTPUT);
    pinMode(RGB2_RED, OUTPUT);
    pinMode(RGB2_GREEN, OUTPUT);
    pinMode(RGB2_BLUE, OUTPUT);
    
    // فوراً همه را خاموش کن (برای Common Cathode)
    // روش 1: با digitalWrite (اگر از مقاومت محدود کننده جریان استفاده می‌کنید)
    digitalWrite(RGB1_RED, HIGH);
    digitalWrite(RGB1_GREEN, HIGH);
    digitalWrite(RGB1_BLUE, HIGH);
    digitalWrite(RGB2_RED, HIGH);
    digitalWrite(RGB2_GREEN, HIGH);
    digitalWrite(RGB2_BLUE, HIGH);
    
    delay(100); // زمان برای تنظیم شدن
    
    Serial.println("RGB LEDs initialized (all off)");
}

void setRGB2Color(uint8_t r, uint8_t g, uint8_t b) {
    if (!settings.rgb2Enabled) {
        turnOffRGB2();
        return;
    }
    
    // برای Common Cathode: مقدار معکوس می‌شود
    // 255 = خاموش، 0 = کامل روشن
    r = map(r, 0, 255, 255, 0);
    g = map(g, 0, 255, 255, 0);
    b = map(b, 0, 255, 255, 0);
    
    // اعمال روشنایی
    r = 255 - ((255 - r) * settings.rgb2Brightness / 100);
    g = 255 - ((255 - g) * settings.rgb2Brightness / 100);
    b = 255 - ((255 - b) * settings.rgb2Brightness / 100);
    
    analogWrite(RGB2_RED, constrain(r, 0, 255));
    analogWrite(RGB2_GREEN, constrain(g, 0, 255));
    analogWrite(RGB2_BLUE, constrain(b, 0, 255));
}

void turnOffRGB2() {
    // برای Common Cathode: 255 = خاموش
    analogWrite(RGB2_RED, 255);
    analogWrite(RGB2_GREEN, 255);
    analogWrite(RGB2_BLUE, 255);
}

void updateLEDs() {
    // Check LED timeout
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
    
    // Mode 1 LEDs
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
    
    // Mode 2 LEDs
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
    unsigned long now = millis();
    
    // RGB1: History display
    if (now - lastRgb1Update > RGB1_HISTORY_CYCLE) {
        lastRgb1Update = now;
        displayHistoryOnRGB1();
    }
    
    // RGB2: Update for smooth transitions
    if (now - lastRGB2Update > 100) {
        lastRGB2Update = now;
        calculateRGB2Color(rgb2CurrentPercent);
    }
}

void setRGB1Color(uint8_t r, uint8_t g, uint8_t b) {
    if (!settings.rgb1Enabled) {
        turnOffRGB1();
        return;
    }
    
    // اعمال روشنایی
    r = (r * settings.rgb1Brightness) / 100;
    g = (g * settings.rgb1Brightness) / 100;
    b = (b * settings.rgb1Brightness) / 100;
    
    // برای Common Cathode:
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
        // POSITIVE (سبز به آبی)
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
        // NEGATIVE (زرد به قرمز)
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
        // Breathing effect
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
    
    // Cycle through color sequence
    rgb1ColorIndex = (rgb1ColorIndex + 1) % 6;
    
    switch (rgb1ColorIndex) {
        case 0: // Entry Mode LONG - Green
            setRGB1Color(0, 255, 0);
            break;
        case 1: // Entry Mode SHORT - Red
            setRGB1Color(255, 0, 0);
            break;
        case 2: // Exit Mode PROFIT - Blue
            setRGB1Color(0, 100, 255);
            break;
        case 3: // Exit Mode LOSS - Yellow
            setRGB1Color(255, 200, 0);
            break;
        case 4: // Mixed/Recent - Purple
            setRGB1Color(255, 0, 255);
            break;
        case 5: // Off/separator
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
    
    // تنظیم تایم‌اوت برای نمایش آلرت (8 ثانیه)
    alertDisplayStart = millis();
    
    Serial.println("\n🚨 ALERT TRIGGERED 🚨");
    Serial.println("Title: " + title);
    Serial.println("Symbol: " + symbol);
    Serial.println("Message: " + message);
    Serial.println("Price: " + formatPrice(price));
    Serial.println("Type: " + String(isLong ? "LONG" : "SHORT"));
    Serial.println("Severe: " + String(isSevere ? "YES" : "NO"));
    Serial.println("Mode: " + String(mode == 0 ? "ENTRY" : "EXIT"));
    
    // اگر بازر فعال است، صدا پخش کن
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
    
    // کنترل LEDها
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
    
    // 🆕 تایمر برای برگشت اتوماتیک به صفحه اصلی
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
    
    // Portfolio level alert
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
    
    // Individual positions
    for (int i = 0; i < cryptoCountMode1; i++) {
        CryptoPosition* pos = &cryptoDataMode1[i];
        
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
            pos->lastAlertTime = millis();
            pos->lastAlertPrice = pos->currentPrice;
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
    
    // Reset Mode 1 alerts
    for (int i = 0; i < cryptoCountMode1; i++) {
        cryptoDataMode1[i].alerted = false;
        cryptoDataMode1[i].severeAlerted = false;
        cryptoDataMode1[i].lastAlertTime = 0;
    }
    
    // Reset Mode 2 alerts
    for (int i = 0; i < cryptoCountMode2; i++) {
        cryptoDataMode2[i].exitAlerted = false;
        cryptoDataMode2[i].exitAlertLastPrice = cryptoDataMode2[i].currentPrice;
        cryptoDataMode2[i].exitAlertTime = 0;
    }
    
    // Reset LED states
    mode1GreenActive = false;
    mode1RedActive = false;
    mode2GreenActive = false;
    mode2RedActive = false;
    
    mode1AlertSymbol = "";
    mode2AlertSymbol = "";
    mode1AlertPercent = 0.0;
    mode2AlertPercent = 0.0;
    
    // Reset RGB
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
        
        // Initialize alert thresholds
        pos->alertThreshold = settings.alertThreshold;
        pos->severeThreshold = settings.severeAlertThreshold;
        
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

void checkBattery() {
    // 🆕 **غیرفعال کردن کامل هشدار باتری**
    
    static unsigned long lastCheck = 0;
    unsigned long now = millis();
    
    if (now - lastCheck < BATTERY_CHECK_INTERVAL) return;
    lastCheck = now;
    
    // فقط خواندن برای نمایش
    int raw = analogRead(BATTERY_PIN);
    batteryVoltage = raw * (3.3 / 4095.0) * 2.0;
    batteryPercent = calculateBatteryPercent(batteryVoltage);
    
    // 🚫 کامنت کردن هشدار صوتی
    // if (batteryPercent <= settings.batteryWarningLevel && !batteryLow) {
    //     batteryLow = true;
    //     Serial.println("⚠️ Battery Low: " + String(batteryPercent) + "%");
    //     
    //     if (settings.buzzerEnabled && settings.buzzerVolume > VOLUME_OFF) {
    //         playErrorTone();  // این باعث کرش می‌شود
    //     }
    // }
    
    // فقط بروزرسانی مقدار
    batteryPercent = 100;
    batteryLow = false;
}

void updateBatteryDisplay() {
    // این تابع برای به‌روزرسانی آیکون باتری استفاده می‌شود
    // در حال حاضر در showMainDisplay() کار می‌کند
    // اگر نیاز به به‌روزرسانی جداگانه داشتید، اینجا کد بگذارید
}

// تابع تشخیص منبع تغذیه:
void detectPowerSource() {
    float vbat = readBatteryVoltage();
    
    if (vbat < 1.0) {
        // ولتاژ بسیار کم = USB بدون باتری
        powerSource = POWER_SOURCE_USB;
        batteryPercent = 100;
        settings.showBattery = false;
        Serial.println("Power: USB (no battery detected)");
    } else if (vbat >= 3.0 && vbat <= 4.5) {
        // ولتاژ باتری لیتیوم
        powerSource = POWER_SOURCE_BATTERY;
        settings.showBattery = true;
        Serial.println("Power: Li-Ion Battery (" + String(vbat, 2) + "V)");
    } else {
        // حالت دیگر
        powerSource = POWER_SOURCE_EXTERNAL;
        batteryPercent = 100;
        settings.showBattery = false;
        Serial.println("Power: External Source (" + String(vbat, 2) + "V)");
    }
    
    // ذخیره تنظیمات
    saveSettings();
}

float readBatteryVoltage() {
    // خواندن ADC و تبدیل به ولتاژ
    int raw = analogRead(BATTERY_PIN);
    float voltage = raw * (3.3 / 4095.0) * 2.0; // تقسیم کننده ولتاژ 1:2
    
    // فیلتر ساده برای کاهش نویز
    static float filteredVoltage = 0.0;
    filteredVoltage = filteredVoltage * 0.9 + voltage * 0.1;
    
    return filteredVoltage;
}

int calculateBatteryPercent(float voltage) {
    // تبدیل ولتاژ به درصد (4.2V = 100%, 3.3V = 0%)
    voltage = constrain(voltage, BATTERY_EMPTY, BATTERY_FULL);
    int percent = (int)((voltage - BATTERY_EMPTY) / (BATTERY_FULL - BATTERY_EMPTY) * 100.0);
    return constrain(percent, 0, 100);
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
    
    // خاموش کردن WiFi برای اسکن بهتر
    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(WIFI_STA);
    delay(100);
    
    int n = WiFi.scanNetworks(false, true, false, 300); // اسکن سریع
    scannedNetworkCount = 0;
    
    if (n == 0) {
        Serial.println("No networks found");
        scannedNetworkCount = 0;
    } else {
        Serial.println(String(n) + " networks found:");
        scannedNetworkCount = min(n, 20); // حداکثر 20 شبکه
        
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
            
            // چک کن اگر این شبکه ذخیره شده است
            for (int j = 0; j < settings.networkCount; j++) {
                if (strcmp(scannedNetworks[i].ssid, settings.networks[j].ssid) == 0) {
                    scannedNetworks[i].configured = true;
                    strncpy(scannedNetworks[i].password, settings.networks[j].password, 63);
                    scannedNetworks[i].password[63] = '\0';
                    scannedNetworks[i].priority = settings.networks[j].priority;
                    break;
                }
            }
            
            // نمایش در Serial
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
    
    // برگشت به حالت قبلی
    if (apModeActive) {
        startAPMode();
    } else if (settings.networkCount > 0) {
        connectToBestWiFi();
    }
    
    Serial.println("Scan complete");
}

bool connectToBestWiFi() {
    if (settings.networkCount == 0) {
        Serial.println("No WiFi networks configured");
        return false;
    }
    
    Serial.println("🔍 Looking for best WiFi network...");
    
    // اسکن شبکه‌های موجود
    scanWiFiNetworks(true);
    
    // پیدا کردن بهترین شبکه ذخیره شده که موجود است
    int bestNetworkIndex = -1;
    int bestPriority = -1;
    int bestRssi = -100;
    
    for (int i = 0; i < settings.networkCount; i++) {
        if (!settings.networks[i].autoConnect) continue;
        
        // چک کن اگر این شبکه در شبکه‌های اسکن شده وجود دارد
        for (int j = 0; j < scannedNetworkCount; j++) {
            if (strcmp(settings.networks[i].ssid, scannedNetworks[j].ssid) == 0) {
                // محاسبه امتیاز: اولویت × 10 + RSSI
                int score = (settings.networks[i].priority * 10) + scannedNetworks[j].rssi;
                
                if (score > bestPriority * 10 + bestRssi) {
                    bestNetworkIndex = i;
                    bestPriority = settings.networks[i].priority;
                    bestRssi = scannedNetworks[j].rssi;
                }
                break;
            }
        }
    }
    
    if (bestNetworkIndex == -1) {
        Serial.println("No saved networks available");
        return false;
    }
    
    // اتصال به بهترین شبکه
    WiFiNetwork* network = &settings.networks[bestNetworkIndex];
    
    Serial.println("✅ Best network found: " + String(network->ssid) + 
                  " (Priority: " + String(network->priority) + 
                  ", RSSI: " + String(bestRssi) + " dBm)");
    
    return connectToSpecificWiFi(bestNetworkIndex);
}

bool connectToSpecificWiFi(int networkIndex) {
    if (networkIndex < 0 || networkIndex >= settings.networkCount) {
        return false;
    }
    
    WiFiNetwork* network = &settings.networks[networkIndex];
    
    Serial.println("Attempting to connect to: " + String(network->ssid));
    
    showConnectionScreen();
    
    WiFi.disconnect(true);
    delay(1000);
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(network->ssid, network->password);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) { // 10 ثانیه
        delay(500);
        attempts++;
        Serial.print(".");
        
        // نمایش نقطه‌های متحرک روی صفحه
        if (attempts % 4 == 0) {
            tft.fillRect(100, 150, 40, 10, TFT_BLACK);
        }
        tft.setCursor(100 + (attempts % 4) * 10, 150);
        tft.print(".");
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        settings.lastConnectedIndex = networkIndex;
        network->lastConnected = millis();
        network->connectionAttempts++;
        network->rssi = WiFi.RSSI();
        
        isConnectedToWiFi = true;
        apModeActive = false;
        connectionLost = false;
        
        Serial.println("\n✅ WiFi Connected!");
        Serial.println("SSID: " + WiFi.SSID());
        Serial.println("IP: " + WiFi.localIP().toString());
        Serial.println("RSSI: " + String(WiFi.RSSI()) + " dBm");
        
        // همگام‌سازی زمان
        syncTime();
        
        // نمایش موفقیت
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.setTextSize(2);
        tft.setCursor(40, 60);
        tft.println("CONNECTED");
        tft.setTextSize(1);
        tft.setCursor(30, 100);
        tft.println("SSID: " + String(network->ssid));
        tft.setCursor(30, 120);
        tft.println("IP: " + WiFi.localIP().toString());
        
        delay(2000);
        
        saveSettings();
        return true;
    }
    
    Serial.println("\n❌ Failed to connect to: " + String(network->ssid));
    network->connectionAttempts++;
    
    // نمایش خطا
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(40, 60);
    tft.println("FAILED");
    tft.setTextSize(1);
    tft.setCursor(20, 100);
    tft.println("Could not connect to:");
    tft.setCursor(30, 120);
    tft.println(String(network->ssid));
    
    delay(2000);
    return false;
}

bool addOrUpdateWiFiNetwork(const char* ssid, const char* password, byte priority, bool autoConnect) {
    // چک کردن محدودیت
    if (strlen(ssid) == 0) {
        Serial.println("Cannot add network: SSID is empty");
        return false;
    }
    
    // چک کردن اگر شبکه از قبل وجود دارد
    for (int i = 0; i < settings.networkCount; i++) {
        if (strcmp(settings.networks[i].ssid, ssid) == 0) {
            // آپدیت شبکه موجود
            strncpy(settings.networks[i].password, password, 63);
            settings.networks[i].password[63] = '\0';
            settings.networks[i].priority = priority;
            settings.networks[i].autoConnect = autoConnect;
            settings.networks[i].lastConnected = 0;
            
            Serial.println("Updated existing WiFi network: " + String(ssid));
            return saveSettings();
        }
    }
    
    // چک کردن حداکثر تعداد شبکه‌ها
    if (settings.networkCount >= MAX_WIFI_NETWORKS) {
        // حذف شبکه با کمترین اولویت
        int lowestPriorityIndex = 0;
        byte lowestPriority = 10;
        
        for (int i = 0; i < settings.networkCount; i++) {
            if (settings.networks[i].priority < lowestPriority) {
                lowestPriority = settings.networks[i].priority;
                lowestPriorityIndex = i;
            }
        }
        
        Serial.println("Maximum networks reached, removing: " + String(settings.networks[lowestPriorityIndex].ssid));
        
        // Shift networks
        for (int i = lowestPriorityIndex; i < settings.networkCount - 1; i++) {
            settings.networks[i] = settings.networks[i + 1];
        }
        
        settings.networkCount--;
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
    
    Serial.println("Added new WiFi network: " + String(ssid) + " (Priority: " + String(priority) + ")");
    
    return saveSettings();
}

void removeWiFiNetwork(int index) {
    if (index < 0 || index >= settings.networkCount) {
        return;
    }
    
    Serial.println("Removing WiFi network: " + String(settings.networks[index].ssid));
    
    // Shift remaining networks
    for (int i = index; i < settings.networkCount - 1; i++) {
        settings.networks[i] = settings.networks[i + 1];
    }
    
    settings.networkCount--;
    memset(&settings.networks[settings.networkCount], 0, sizeof(WiFiNetwork));
    
    saveSettings();
}

void reorderWiFiNetworks() {
    // مرتب‌سازی شبکه‌ها بر اساس اولویت (بیشتر به کمتر)
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

void handleWiFiScan() {
    scanWiFiNetworks(true);
    
    String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
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
        
        // محاسبه قدرت سیگنال (0-100)
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

void handleWiFiManage() {
    String action = server.arg("action");
    String ssid = server.arg("ssid");
    
    if (action == "connect" && ssid.length() > 0) {
        // اتصال به شبکه خاص
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
        // حذف شبکه
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
    
    // نمایش صفحه مدیریت WiFi
    String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>WiFi Manager</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; background: #1a1a1a; color: #fff; }
        .container { max-width: 1000px; margin: 0 auto; }
        .section { background: #2d2d2d; padding: 20px; border-radius: 10px; margin-bottom: 20px; }
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
        .btn-scan { background: #ff9900; }
        .btn-success { background: #00cc00; }
        .btn-danger { background: #ff3333; }
        table { width: 100%; border-collapse: collapse; margin: 15px 0; }
        th, td { padding: 12px; text-align: left; border-bottom: 1px solid #444; }
        th { background: #1a1a1a; }
        .signal-good { color: #00ff00; }
        .signal-fair { color: #ff9900; }
        .signal-weak { color: #ff3333; }
        .priority-high { background: rgba(0, 255, 0, 0.1); }
        .priority-medium { background: rgba(255, 165, 0, 0.1); }
        .priority-low { background: rgba(255, 0, 0, 0.1); }
    </style>
</head>
<body>
    <div class="container">
        <h1>📶 WiFi Manager</h1>
        
        <div class="section">
            <h2>Current Status</h2>
            <p><strong>Status:</strong> )rawliteral";
    
    if (isConnectedToWiFi) {
        html += "<span style='color: #00ff00;'>Connected</span>";
        html += "<br><strong>SSID:</strong> " + WiFi.SSID();
        html += "<br><strong>IP:</strong> " + WiFi.localIP().toString();
        html += "<br><strong>RSSI:</strong> " + String(WiFi.RSSI()) + " dBm";
    } else if (apModeActive) {
        html += "<span style='color: #ff9900;'>AP Mode</span>";
        html += "<br><strong>AP IP:</strong> 192.168.4.1";
    } else {
        html += "<span style='color: #ff3333;'>Disconnected</span>";
    }
    
    html += R"rawliteral(</p>
            
            <a href="/wifiscan" class="btn btn-scan">📡 Scan Networks</a>
            <a href="/wifimanage?action=rescan" class="btn">🔄 Rescan</a>
            )rawliteral";
    
    if (isConnectedToWiFi) {
        html += "<a href='/wifimanage?action=disconnect' class='btn btn-danger'>Disconnect</a>";
    } else if (settings.networkCount > 0) {
        html += "<a href='/wifimanage?action=reconnect' class='btn btn-success'>Reconnect</a>";
    }
    
    html += R"rawliteral(
        </div>
        
        <div class="section">
            <h2>Saved Networks ()rawliteral";
    html += String(settings.networkCount);
    html += R"rawliteral( / )rawliteral";
    html += String(MAX_WIFI_NETWORKS);
    html += R"rawliteral()</h2>
            
            <table>
                <thead>
                    <tr>
                        <th>SSID</th>
                        <th>Priority</th>
                        <th>Last Connected</th>
                        <th>Attempts</th>
                        <th>Auto Connect</th>
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
        
        html += "<tr class='" + priorityClass + "'>";
        html += "<td><strong>" + String(net->ssid) + "</strong></td>";
        html += "<td>" + String(net->priority) + "</td>";
        html += "<td>";
        if (net->lastConnected > 0) {
            html += getTimeString(net->lastConnected);
        } else {
            html += "Never";
        }
        html += "</td>";
        html += "<td>" + String(net->connectionAttempts) + "</td>";
        html += "<td>" + String(net->autoConnect ? "✅" : "❌") + "</td>";
        html += "<td>";
        html += "<a href='/wifimanage?action=connect&ssid=" + urlEncode(net->ssid) + "' class='btn' style='padding: 5px 10px;'>Connect</a> ";
        html += "<a href='/wifimanage?action=remove&ssid=" + urlEncode(net->ssid) + "' class='btn' style='padding: 5px 10px; background: #ff3333;'>Remove</a>";
        html += "</td>";
        html += "</tr>";
    }
    
    html += R"rawliteral(
                </tbody>
            </table>
        </div>
        
        <div class="section">
            <h2>Add New Network</h2>
            <form action="/savewifi" method="post">
                <input type="text" name="ssid" placeholder="SSID" required style="width: 300px; padding: 10px; margin: 5px;">
                <input type="password" name="password" placeholder="Password" required style="width: 300px; padding: 10px; margin: 5px;">
                <br>
                <label>Priority: 
                    <select name="priority" style="padding: 10px; margin: 5px;">
                        <option value="10">Highest (10)</option>
                        <option value="9">High (9)</option>
                        <option value="7" selected>Medium (7)</option>
                        <option value="5">Normal (5)</option>
                        <option value="3">Low (3)</option>
                        <option value="1">Lowest (1)</option>
                    </select>
                </label>
                <label style="margin-left: 20px;">
                    <input type="checkbox" name="autoconnect" checked> Auto Connect
                </label>
                <br>
                <button type="submit" class="btn btn-success" style="margin-top: 10px;">Save Network</button>
            </form>
        </div>
        
        <div style="margin-top: 30px;">
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
            // تلاش برای اتصال
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

// ===== WIFI FUNCTIONS =====
void initializeSettings() {
    Serial.println("Initializing default settings...");
    
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
    settings.displayRotation = 1; // Landscape
    
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
    EEPROM.get(0, settings);
    EEPROM.end();
    
    if (settings.magicNumber != 0xAA) {
        Serial.println("Invalid or no settings found, using defaults");
        initializeSettings();
        return false;
    }
    
    if (settings.buzzerVolume < VOLUME_MIN || settings.buzzerVolume > VOLUME_MAX) {
        settings.buzzerVolume = DEFAULT_VOLUME;
    }
    
    if (settings.ledBrightness < 0 || settings.ledBrightness > 255) {
        settings.ledBrightness = DEFAULT_LED_BRIGHTNESS;
    }
    
    if (settings.exitAlertPercent < 1 || settings.exitAlertPercent > 50) {
        settings.exitAlertPercent = DEFAULT_EXIT_ALERT_PERCENT;
    }
    
    if (strlen(settings.entryPortfolio) == 0) {
        strcpy(settings.entryPortfolio, "MainPortfolio");
    }
    
    if (strlen(settings.exitPortfolio) == 0) {
        strcpy(settings.exitPortfolio, "ExitPortfolio");
    }
    
    Serial.println("Settings loaded successfully");
    return true;
}

bool saveSettings() {
    settings.magicNumber = 0xAA;
    settings.configured = true;
    
    EEPROM.begin(EEPROM_SIZE);
    EEPROM.put(0, settings);
    bool result = EEPROM.commit();
    EEPROM.end();
    
    if (result) {
        Serial.println("Settings saved successfully");
    } else {
        Serial.println("Failed to save settings!");
    }
    
    return result;
}

bool addWiFiNetwork(const char* ssid, const char* password) {
    if (settings.networkCount >= MAX_WIFI_NETWORKS) {
        Serial.println("Cannot add network: Maximum networks reached");
        return false;
    }
    
    for (int i = 0; i < settings.networkCount; i++) {
        if (strcmp(settings.networks[i].ssid, ssid) == 0) {
            strncpy(settings.networks[i].password, password, 63);
            settings.networks[i].password[63] = '\0';
            Serial.println("Updated existing WiFi network: " + String(ssid));
            return saveSettings();
        }
    }
    
    WiFiNetwork* network = &settings.networks[settings.networkCount];
    strncpy(network->ssid, ssid, 31);
    network->ssid[31] = '\0';
    strncpy(network->password, password, 63);
    network->password[63] = '\0';
    network->configured = true;
    network->connectionAttempts = 0;
    network->priority = 1;
    
    settings.networkCount++;
    Serial.println("Added new WiFi network: " + String(ssid));
    
    return saveSettings();
}

bool removeWiFiNetwork(const char* ssid) {
    for (int i = 0; i < settings.networkCount; i++) {
        if (strcmp(settings.networks[i].ssid, ssid) == 0) {
            Serial.println("Removing WiFi network: " + String(ssid));
            
            for (int j = i; j < settings.networkCount - 1; j++) {
                settings.networks[j] = settings.networks[j + 1];
            }
            
            settings.networkCount--;
            memset(&settings.networks[settings.networkCount], 0, sizeof(WiFiNetwork));
            
            return saveSettings();
        }
    }
    
    Serial.println("WiFi network not found: " + String(ssid));
    return false;
}

bool connectToWiFi() {
    if (settings.networkCount == 0) {
        Serial.println("No WiFi networks configured");
        return false;
    }
    
    Serial.println("Connecting to WiFi...");
    showConnectionScreen();
    
    for (int i = 0; i < settings.networkCount; i++) {
        if (!settings.networks[i].configured) continue;
        
        const char* ssid = settings.networks[i].ssid;
        const char* password = settings.networks[i].password;
        
        if (strlen(ssid) == 0) continue;
        
        Serial.println("Attempting to connect to: " + String(ssid));
        
        WiFi.disconnect(true);
        delay(1000);
        
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid, password);
        
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 30) {
            delay(500);
            attempts++;
            Serial.print(".");
        }
        
        if (WiFi.status() == WL_CONNECTED) {
            settings.lastConnectedIndex = i;
            settings.networks[i].lastConnected = millis();
            settings.networks[i].connectionAttempts++;
            
            isConnectedToWiFi = true;
            apModeActive = false;
            connectionLost = false;
            
            Serial.println("\n✅ WiFi Connected!");
            Serial.println("SSID: " + WiFi.SSID());
            Serial.println("IP: " + WiFi.localIP().toString());
            Serial.println("RSSI: " + String(WiFi.RSSI()) + " dBm");
            
            saveSettings();
            return true;
        }
        
        Serial.println("\nFailed to connect to: " + String(ssid));
        settings.networks[i].connectionAttempts++;
    }
    
    Serial.println("Failed to connect to any WiFi network");
    return false;
}

bool startAPMode() {
    Serial.println("Starting Access Point mode...");
    
    WiFi.disconnect(true);
    delay(1000);
    
    WiFi.mode(WIFI_AP);
    if (!WiFi.softAP("ESP32-Portfolio-Monitor", "12345678")) {
        Serial.println("Failed to start AP mode!");
        return false;
    }
    
    apModeActive = true;
    isConnectedToWiFi = false;
    
    Serial.println("✅ AP Mode Started");
    Serial.println("SSID: ESP32-Portfolio-Monitor");
    Serial.println("Password: 12345678");
    Serial.println("IP: " + WiFi.softAPIP().toString());
    
    return true;
}

void handleWiFiConnection() {
    static unsigned long lastConnectionCheck = 0;
    unsigned long now = millis();
    
    if (now - lastConnectionCheck < 30000) return;
    lastConnectionCheck = now;
    
    if (isConnectedToWiFi) {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("WiFi connection lost");
            isConnectedToWiFi = false;
            connectionLost = true;
            connectionLostTime = millis();
            connectionLostCount++;
            
            if (settings.buzzerEnabled) {
                playConnectionLostTone();
            }
            
            if (settings.autoReconnect) {
                attemptReconnect();
            }
        }
    } else if (!apModeActive && settings.autoReconnect) {
        attemptReconnect();
    }
}

void checkConnectionStatus() {
    // تشخیص قطعی اتصال با چک کردن وضعیت WiFi
    if (isConnectedToWiFi) {
        if (WiFi.status() != WL_CONNECTED) {
            if (!connectionLost) {
                Serial.println("Connection lost - WiFi disconnected");
                connectionLost = true;
                connectionLostTime = millis();
                connectionLostCount++;
                
                if (settings.buzzerEnabled) {
                    playConnectionLostTone();
                }
            }
        } else {
            if (connectionLost) {
                Serial.println("Connection restored");
                connectionLost = false;
                unsigned long downtime = millis() - connectionLostTime;
                totalDowntime += downtime;
                reconnectSuccessCount++;
                
                if (settings.buzzerEnabled) {
                    playSuccessTone();
                }
            }
        }
    }
}

void attemptReconnect() {
    static int reconnectAttempt = 0;
    static unsigned long lastReconnectTime = 0;
    
    unsigned long now = millis();
    
    if (now - lastReconnectTime < RECONNECT_INTERVAL) return;
    lastReconnectTime = now;
    
    if (reconnectAttempt >= settings.reconnectAttempts) {
        Serial.println("Max reconnect attempts reached, starting AP mode");
        startAPMode();
        reconnectAttempt = 0;
        return;
    }
    
    reconnectAttempt++;
    Serial.println("Reconnection attempt " + String(reconnectAttempt) + " of " + String(settings.reconnectAttempts));
    
    if (connectToWiFi()) {
        reconnectAttempt = 0;
        connectionLost = false;
        Serial.println("✅ Reconnection successful");
    } else {
        Serial.println("❌ Reconnection failed");
    }
}

void checkWiFiStatus() {
    static unsigned long lastStatusPrint = 0;
    unsigned long now = millis();
    
    if (now - lastStatusPrint > 60000) {
        lastStatusPrint = now;
        
        if (isConnectedToWiFi) {
            Serial.println("WiFi Status: Connected to " + WiFi.SSID() + 
                          " (" + String(WiFi.RSSI()) + " dBm)");
        } else if (apModeActive) {
            Serial.println("WiFi Status: AP Mode Active");
        } else {
            Serial.println("WiFi Status: Disconnected");
        }
    }
}

// ===== WEB SERVER FUNCTIONS =====
void setupWebServer() {
    server.on("/", handleRoot);
    server.on("/setup", handleSetup);
    server.on("/savewifi", HTTP_POST, handleSaveWiFi);
    server.on("/removewifi", handleRemoveWiFi);
    server.on("/wifimanage", handleWiFiManage);
    server.on("/wifiscan", handleWiFiScan);
    server.on("/savewififromscan", HTTP_POST, handleSaveWiFiFromScan);
    server.on("/saveapi", HTTP_POST, handleSaveAPI);
    server.on("/savealert", HTTP_POST, handleSaveAlert);
    server.on("/savemode", HTTP_POST, handleSaveMode);
    server.on("/savedisplay", HTTP_POST, handleSaveDisplay);
    server.on("/savergb", HTTP_POST, handleSaveRGB);
    server.on("/saveemergency", HTTP_POST, handleSaveEmergency);
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
    
    server.begin();
    Serial.println("Web server started on port 80");
}

void handleRoot() {
    server.send(200, "text/html", generateDashboardHTML());
}

void handleSetup() {
    server.send(200, "text/html", generateSetupHTML());
}

void handleSaveWiFi() {
    if (server.hasArg("ssid") && server.hasArg("password")) {
        String ssid = server.arg("ssid");
        String password = server.arg("password");
        
        if (addWiFiNetwork(ssid.c_str(), password.c_str())) {
            String html = "<h1>✅ WiFi Saved!</h1><p>Network: " + ssid + "</p><a href='/setup'>Back</a>";
            server.send(200, "text/html", html);
        }
    }
}

void handleRemoveWiFi() {
    if (server.hasArg("ssid")) {
        String ssid = urlDecode(server.arg("ssid"));
        removeWiFiNetwork(ssid.c_str());
    }
    server.sendHeader("Location", "/setup", true);
    server.send(302, "text/plain", "");
}

void handleSaveAPI() {
    if (server.hasArg("server")) strcpy(settings.server, server.arg("server").c_str());
    if (server.hasArg("username")) strcpy(settings.username, server.arg("username").c_str());
    if (server.hasArg("userpass")) strcpy(settings.userpass, server.arg("userpass").c_str());
    if (server.hasArg("entryportfolio")) strcpy(settings.entryPortfolio, server.arg("entryportfolio").c_str());
    if (server.hasArg("exitportfolio")) strcpy(settings.exitPortfolio, server.arg("exitportfolio").c_str());
    
    saveSettings();
    server.sendHeader("Location", "/setup", true);
    server.send(302, "text/plain", "");
}

void handleSaveAlert() {
    if (server.hasArg("threshold")) settings.alertThreshold = server.arg("threshold").toFloat();
    if (server.hasArg("severethreshold")) settings.severeAlertThreshold = server.arg("severethreshold").toFloat();
    if (server.hasArg("portfoliothreshold")) settings.portfolioAlertThreshold = server.arg("portfoliothreshold").toFloat();
    if (server.hasArg("exitalertpercent")) settings.exitAlertPercent = server.arg("exitalertpercent").toFloat();
    
    // 🆕 کنترل حجم صدا
    if (server.hasArg("volume")) {
        settings.buzzerVolume = server.arg("volume").toInt();
        settings.buzzerVolume = constrain(settings.buzzerVolume, VOLUME_MIN, VOLUME_MAX);
        
        // تست صدا اگر کاربر مقدار داده است
        if (settings.buzzerVolume > 0) {
            playTone(523, 100, 50); // صدای تست کوتاه
        }
    }
    
    if (server.hasArg("buzzerenabled")) settings.buzzerEnabled = true; else settings.buzzerEnabled = false;
    if (server.hasArg("separatealerts")) settings.separateLongShortAlerts = true; else settings.separateLongShortAlerts = false;
    if (server.hasArg("autoreset")) settings.autoResetAlerts = true; else settings.autoResetAlerts = false;
    if (server.hasArg("alertcooldown")) settings.alertCooldown = server.arg("alertcooldown").toInt();
    if (server.hasArg("exitalertenabled")) settings.exitAlertEnabled = true; else settings.exitAlertEnabled = false;
    if (server.hasArg("exitblinkenabled")) settings.exitAlertBlinkEnabled = true; else settings.exitAlertBlinkEnabled = false;
    
    saveSettings();
    server.sendHeader("Location", "/setup", true);
    server.send(302, "text/plain", "");
}

void handleSaveMode() {
    if (server.hasArg("ledenabled")) settings.ledEnabled = true; else settings.ledEnabled = false;
    if (server.hasArg("ledbrightness")) settings.ledBrightness = server.arg("ledbrightness").toInt();
    
    saveSettings();
    server.sendHeader("Location", "/setup", true);
    server.send(302, "text/plain", "");
}

void handleSaveDisplay() {
    if (server.hasArg("brightness")) {
        int brightness = server.arg("brightness").toInt();
        setDisplayBrightness(brightness);
    }
    
    if (server.hasArg("timeout")) settings.displayTimeout = server.arg("timeout").toInt();
    if (server.hasArg("showdetails")) settings.showDetails = true; else settings.showDetails = false;
    if (server.hasArg("invertdisplay")) settings.invertDisplay = true; else settings.invertDisplay = false;
    if (server.hasArg("rotation")) {
        settings.displayRotation = server.arg("rotation").toInt() % 4;
        tft.setRotation(settings.displayRotation);
    }
    
    saveSettings();
    server.sendHeader("Location", "/setup", true);
    server.send(302, "text/plain", "");
}

void handleSaveRGB() {
    if (server.hasArg("rgb1enabled")) settings.rgb1Enabled = true; else settings.rgb1Enabled = false;
    if (server.hasArg("rgb2enabled")) settings.rgb2Enabled = true; else settings.rgb2Enabled = false;
    if (server.hasArg("rgb1brightness")) settings.rgb1Brightness = server.arg("rgb1brightness").toInt();
    if (server.hasArg("rgb2brightness")) settings.rgb2Brightness = server.arg("rgb2brightness").toInt();
    if (server.hasArg("rgb1historyspeed")) settings.rgb1HistorySpeed = server.arg("rgb1historyspeed").toInt();
    if (server.hasArg("rgb2sensitivity")) settings.rgb2Sensitivity = server.arg("rgb2sensitivity").toInt();
    
    saveSettings();
    server.sendHeader("Location", "/setup", true);
    server.send(302, "text/plain", "");
}

void handleSaveEmergency() {
    if (server.hasArg("showbattery")) settings.showBattery = true; else settings.showBattery = false;
    if (server.hasArg("batterywarning")) settings.batteryWarningLevel = server.arg("batterywarning").toInt();
    if (server.hasArg("autoreconnect")) settings.autoReconnect = true; else settings.autoReconnect = false;
    if (server.hasArg("reconnectattempts")) settings.reconnectAttempts = server.arg("reconnectattempts").toInt();
    
    saveSettings();
    server.sendHeader("Location", "/setup", true);
    server.send(302, "text/plain", "");
}

void handleRefresh() {
    lastDataUpdate = 0;
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
}

void handleTestAlert() {
    playTestAlertSequence();
    
    String html = "<h1>🔊 Test Alert Played!</h1><a href='/'>Back</a>";
    server.send(200, "text/html", html);
}

void handleResetAlerts() {
    resetAllAlerts();
    
    String html = "<h1>♻️ Alerts Reset!</h1><a href='/'>Back</a>";
    server.send(200, "text/html", html);
}

void handleSystemInfo() {
    server.send(200, "text/html", generateSystemInfoHTML());
}

void handleAPIStatus() {
    String html = "<h1>📡 API Status</h1>";
    html += "<p>Successful Calls: " + String(apiSuccessCount) + "</p>";
    html += "<p>Failed Calls: " + String(apiErrorCount) + "</p>";
    html += "<p>Last API Call: " + getTimeString(lastApiCallTime) + "</p>";
    html += "<p><a href='/'>Back</a></p>";
    
    server.send(200, "text/html", html);
}

void handleLEDControl() {
    if (server.hasArg("action")) {
        String action = server.arg("action");
        
        if (action == "testmode1green") {
            digitalWrite(LED_MODE1_GREEN, HIGH); delay(1000); digitalWrite(LED_MODE1_GREEN, LOW);
        } else if (action == "testmode1red") {
            digitalWrite(LED_MODE1_RED, HIGH); delay(1000); digitalWrite(LED_MODE1_RED, LOW);
        } else if (action == "testmode2green") {
            digitalWrite(LED_MODE2_GREEN, HIGH); delay(1000); digitalWrite(LED_MODE2_GREEN, LOW);
        } else if (action == "testmode2red") {
            digitalWrite(LED_MODE2_RED, HIGH); delay(1000); digitalWrite(LED_MODE2_RED, LOW);
        } else if (action == "allon") {
            setAllLEDs(HIGH);
        } else if (action == "alloff") {
            setAllLEDs(LOW);
        }
    }
    
    String html = "<h1>💡 LED Control</h1>";
    html += "<p><a href='/ledcontrol?action=testmode1green'>Test MODE1 GREEN</a></p>";
    html += "<p><a href='/ledcontrol?action=testmode1red'>Test MODE1 RED</a></p>";
    html += "<p><a href='/ledcontrol?action=testmode2green'>Test MODE2 GREEN</a></p>";
    html += "<p><a href='/ledcontrol?action=testmode2red'>Test MODE2 RED</a></p>";
    html += "<p><a href='/ledcontrol?action=allon'>ALL ON</a></p>";
    html += "<p><a href='/ledcontrol?action=alloff'>ALL OFF</a></p>";
    html += "<p><a href='/'>Back</a></p>";
    
    server.send(200, "text/html", html);
}

void handleRGBControl() {
    if (server.hasArg("action")) {
        String action = server.arg("action");
        
        if (action == "rgb1red") {
            setRGB1Color(255, 0, 0); delay(2000); turnOffRGB1();
        } else if (action == "rgb1green") {
            setRGB1Color(0, 255, 0); delay(2000); turnOffRGB1();
        } else if (action == "rgb1blue") {
            setRGB1Color(0, 0, 255); delay(2000); turnOffRGB1();
        } else if (action == "rgb2test") {
            for (int i = -30; i <= 30; i += 5) {
                rgb2CurrentPercent = i;
                delay(200);
            }
            rgb2CurrentPercent = 0;
        }
    }
    
    String html = "<h1>🌈 RGB Control</h1>";
    html += "<p>RGB1 (History):</p>";
    html += "<p><a href='/rgbcontrol?action=rgb1red'>Red</a></p>";
    html += "<p><a href='/rgbcontrol?action=rgb1green'>Green</a></p>";
    html += "<p><a href='/rgbcontrol?action=rgb1blue'>Blue</a></p>";
    html += "<p>RGB2 (Live):</p>";
    html += "<p><a href='/rgbcontrol?action=rgb2test'>Test Gradient</a></p>";
    html += "<p>Current Percent: " + String(rgb2CurrentPercent, 1) + "%</p>";
    html += "<p><a href='/'>Back</a></p>";
    
    server.send(200, "text/html", html);
}

void handleDisplayControl() {
    if (server.hasArg("action")) {
        String action = server.arg("action");
        
        if (action == "testpattern") {
            setDisplayBacklight(true);
            tft.fillScreen(TFT_RED); delay(500);
            tft.fillScreen(TFT_GREEN); delay(500);
            tft.fillScreen(TFT_BLUE); delay(500);
            tft.fillScreen(TFT_YELLOW); delay(500);
            tft.fillScreen(TFT_MAGENTA); delay(500);
            tft.fillScreen(TFT_CYAN); delay(500);
            tft.fillScreen(TFT_WHITE); delay(500);
            tft.fillScreen(TFT_BLACK);
            
            tft.setTextColor(TFT_WHITE, TFT_BLACK);
            tft.setTextSize(2);
            tft.setCursor(60, 100);
            tft.println("TEST OK");
            delay(1000);
            tft.fillScreen(TFT_BLACK);
            
            Serial.println("Display test pattern completed");
        } 
        else if (action == "brightnessup") {
            int newBrightness = settings.displayBrightness + 10;
            if (newBrightness > 100) newBrightness = 100;
            setDisplayBrightness(newBrightness);
            Serial.println("Brightness increased to " + String(newBrightness) + "%");
        } 
        else if (action == "brightnessdown") {
            int newBrightness = settings.displayBrightness - 10;
            if (newBrightness < 0) newBrightness = 0;
            setDisplayBrightness(newBrightness);
            Serial.println("Brightness decreased to " + String(newBrightness) + "%");
        } 
        else if (action == "brightness50") {
            setDisplayBrightness(50);
            Serial.println("Brightness set to 50%");
        } 
        else if (action == "brightness100") {
            setDisplayBrightness(100);
            Serial.println("Brightness set to 100%");
        } 
        else if (action == "brightness0") {
            setDisplayBrightness(0);
            Serial.println("Brightness set to 0% (OFF)");
        }
        else if (action == "rotation0") {
            settings.displayRotation = 0;
            tft.setRotation(0);
            saveSettings();
            Serial.println("Display rotation set to 0°");
            
            // نمایش تأیید
            tft.fillScreen(TFT_BLACK);
            tft.setTextColor(TFT_GREEN, TFT_BLACK);
            tft.setTextSize(2);
            tft.setCursor(80, 100);
            tft.println("0°");
            delay(1000);
            tft.fillScreen(TFT_BLACK);
        } 
        else if (action == "rotation1") {
            settings.displayRotation = 1;
            tft.setRotation(1);
            saveSettings();
            Serial.println("Display rotation set to 90°");
            
            tft.fillScreen(TFT_BLACK);
            tft.setTextColor(TFT_GREEN, TFT_BLACK);
            tft.setTextSize(2);
            tft.setCursor(80, 100);
            tft.println("90°");
            delay(1000);
            tft.fillScreen(TFT_BLACK);
        } 
        else if (action == "rotation2") {
            settings.displayRotation = 2;
            tft.setRotation(2);
            saveSettings();
            Serial.println("Display rotation set to 180°");
            
            tft.fillScreen(TFT_BLACK);
            tft.setTextColor(TFT_GREEN, TFT_BLACK);
            tft.setTextSize(2);
            tft.setCursor(80, 100);
            tft.println("180°");
            delay(1000);
            tft.fillScreen(TFT_BLACK);
        } 
        else if (action == "rotation3") {
            settings.displayRotation = 3;
            tft.setRotation(3);
            saveSettings();
            Serial.println("Display rotation set to 270°");
            
            tft.fillScreen(TFT_BLACK);
            tft.setTextColor(TFT_GREEN, TFT_BLACK);
            tft.setTextSize(2);
            tft.setCursor(80, 100);
            tft.println("270°");
            delay(1000);
            tft.fillScreen(TFT_BLACK);
        }
        else if (action == "inverton") {
            settings.invertDisplay = true;
            saveSettings();
            Serial.println("Display inversion ON");
            
            tft.fillScreen(TFT_BLACK);
            tft.setTextColor(TFT_WHITE, TFT_BLACK);
            tft.setTextSize(2);
            tft.setCursor(50, 100);
            tft.println("INVERT ON");
            delay(1000);
            tft.fillScreen(TFT_BLACK);
        }
        else if (action == "invertoff") {
            settings.invertDisplay = false;
            saveSettings();
            Serial.println("Display inversion OFF");
            
            tft.fillScreen(TFT_BLACK);
            tft.setTextColor(TFT_WHITE, TFT_BLACK);
            tft.setTextSize(2);
            tft.setCursor(50, 100);
            tft.println("INVERT OFF");
            delay(1000);
            tft.fillScreen(TFT_BLACK);
        }
        else if (action == "on") {
            setDisplayBacklight(true);
            Serial.println("Backlight turned ON");
        } 
        else if (action == "off") {
            setDisplayBacklight(false);
            Serial.println("Backlight turned OFF");
        }
        else if (action == "clearscreen") {
            tft.fillScreen(TFT_BLACK);
            Serial.println("Screen cleared");
        }
        else if (action == "showinfo") {
            tft.fillScreen(TFT_BLACK);
            tft.setTextColor(TFT_CYAN, TFT_BLACK);
            tft.setTextSize(1);
            
            tft.setCursor(10, 20);
            tft.print("DISPLAY INFO");
            tft.setCursor(10, 40);
            tft.print("Brightness: " + String(settings.displayBrightness) + "%");
            tft.setCursor(10, 60);
            tft.print("Rotation: " + String(settings.displayRotation));
            tft.setCursor(10, 80);
            tft.print("Timeout: " + String(settings.displayTimeout/1000) + "s");
            tft.setCursor(10, 100);
            tft.print("Backlight Pin: " + String(TFT_BL_PIN));
            tft.setCursor(10, 120);
            tft.print("Invert: " + String(settings.invertDisplay ? "ON" : "OFF"));
            
            delay(3000);
            tft.fillScreen(TFT_BLACK);
        }
        
        // برگشت به صفحه کنترل
        server.sendHeader("Location", "/displaycontrol", true);
        server.send(302, "text/plain", "");
        return;
    }
    
    // ===== GENERATE HTML PAGE =====
    String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>📺 Display Control</title>
    <style>
        body { 
            font-family: Arial, sans-serif; 
            margin: 20px; 
            background: #1a1a1a; 
            color: #fff; 
        }
        .container { 
            max-width: 800px; 
            margin: 0 auto; 
        }
        .card { 
            background: #2d2d2d; 
            padding: 20px; 
            border-radius: 10px; 
            margin-bottom: 20px;
            border-left: 5px solid #0088ff;
        }
        .btn { 
            background: #0088ff; 
            color: white; 
            padding: 12px 24px; 
            border: none; 
            border-radius: 5px; 
            cursor: pointer; 
            margin: 5px; 
            text-decoration: none; 
            display: inline-block;
            font-size: 16px;
            transition: background 0.3s;
        }
        .btn:hover { 
            background: #0066cc; 
        }
        .btn-green { background: #00cc00; }
        .btn-green:hover { background: #00aa00; }
        .btn-red { background: #ff3333; }
        .btn-red:hover { background: #cc0000; }
        .btn-yellow { background: #ff9900; }
        .btn-yellow:hover { background: #cc7700; }
        .btn-group { margin: 15px 0; }
        .status { 
            background: #333; 
            padding: 10px; 
            border-radius: 5px; 
            margin: 10px 0;
        }
        h2 { color: #0088ff; margin-top: 0; }
        h3 { color: #00cc00; }
    </style>
</head>
<body>
    <div class="container">
        <h1>📺 Display Control Panel</h1>
        
        <div class="card">
            <h2>Display Status</h2>
            <div class="status">
                <p><strong>Backlight Pin:</strong> )rawliteral";
    
    html += String(TFT_BL_PIN);
    html += R"rawliteral(</p>
                <p><strong>Brightness:</strong> )rawliteral";
    html += String(settings.displayBrightness);
    html += R"rawliteral(%</p>
                <p><strong>Rotation:</strong> )rawliteral";
    html += String(settings.displayRotation);
    html += R"rawliteral(°</p>
                <p><strong>Timeout:</strong> )rawliteral";
    html += String(settings.displayTimeout / 1000);
    html += R"rawliteral( seconds</p>
                <p><strong>Invert Display:</strong> )rawliteral";
    html += String(settings.invertDisplay ? "ON" : "OFF");
    html += R"rawliteral(</p>
            </div>
        </div>
        
        <div class="card">
            <h2>🖥️ Display Test</h2>
            <div class="btn-group">
                <a href="/displaycontrol?action=testpattern" class="btn btn-yellow">Test Pattern</a>
                <a href="/displaycontrol?action=clearscreen" class="btn">Clear Screen</a>
                <a href="/displaycontrol?action=showinfo" class="btn">Show Info</a>
            </div>
        </div>
        
        <div class="card">
            <h2>💡 Backlight Control</h2>
            <div class="btn-group">
                <a href="/displaycontrol?action=on" class="btn btn-green">Backlight ON</a>
                <a href="/displaycontrol?action=off" class="btn btn-red">Backlight OFF</a>
            </div>
            
            <h3>Brightness Levels</h3>
            <div class="btn-group">
                <a href="/displaycontrol?action=brightness0" class="btn">0% (OFF)</a>
                <a href="/displaycontrol?action=brightness50" class="btn">50%</a>
                <a href="/displaycontrol?action=brightness100" class="btn">100%</a>
                <a href="/displaycontrol?action=brightnessup" class="btn btn-green">+ Increase</a>
                <a href="/displaycontrol?action=brightnessdown" class="btn btn-red">- Decrease</a>
            </div>
        </div>
        
        <div class="card">
            <h2>🔄 Rotation Control</h2>
            <p>Current rotation: )rawliteral";
    html += String(settings.displayRotation);
    html += R"rawliteral(°</p>
            <div class="btn-group">
                <a href="/displaycontrol?action=rotation0" class="btn">0° (Normal)</a>
                <a href="/displaycontrol?action=rotation1" class="btn">90°</a>
                <a href="/displaycontrol?action=rotation2" class="btn">180°</a>
                <a href="/displaycontrol?action=rotation3" class="btn">270°</a>
            </div>
        </div>
        
        <div class="card">
            <h2>⚙️ Display Settings</h2>
            <div class="btn-group">
                <a href="/displaycontrol?action=invertON" class="btn btn-yellow">Invert ON</a>
                <a href="/displaycontrol?action=invertOFF" class="btn">Invert OFF</a>
            </div>
            
            <p style="margin-top: 20px;">
                <a href="/savedisplay" class="btn">⚙️ Advanced Settings</a>
            </p>
        </div>
        
        <div style="margin-top: 30px; text-align: center;">
            <a href="/" class="btn">← Back to Dashboard</a>
            <a href="/setup" class="btn">← Back to Setup</a>
        </div>
        
        <div style="margin-top: 30px; color: #888; font-size: 14px; text-align: center;">
            <p>ST7789 240x240 IPS Display | Backlight on Pin )rawliteral";
    html += String(TFT_BL_PIN);
    html += R"rawliteral(</p>
        </div>
    </div>
</body>
</html>)rawliteral";
    
    server.send(200, "text/html", html);
}

void handleFactoryReset() {
    if (server.hasArg("confirm") && server.arg("confirm") == "yes") {
        factoryReset();
    } else {
        String html = "<h1>⚠️ Factory Reset</h1>";
        html += "<p>This will erase ALL settings. Are you sure?</p>";
        html += "<p><a href='/factoryreset?confirm=yes'>YES, Reset Everything</a></p>";
        html += "<p><a href='/'>NO, Cancel</a></p>";
        server.send(200, "text/html", html);
    }
}

void handleRestart() {
    if (server.hasArg("confirm") && server.arg("confirm") == "yes") {
        server.send(200, "text/html", "<h1>Restarting...</h1>");
        delay(1000);
        ESP.restart();
    } else {
        String html = "<h1>🔄 Restart System</h1>";
        html += "<p>Restart the system?</p>";
        html += "<p><a href='/restart?confirm=yes'>Restart Now</a></p>";
        html += "<p><a href='/'>Cancel</a></p>";
        server.send(200, "text/html", html);
    }
}

void handlePositions() {
    String modeParam = server.arg("mode");
    byte mode = (modeParam == "1") ? 1 : 0;
    
    server.send(200, "text/html", generatePositionListHTML(mode));
}

String generateDashboardHTML() {
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<title>Portfolio Dashboard</title><style>";
    html += "body { font-family: Arial, sans-serif; margin: 20px; background: #1a1a1a; color: #fff; }";
    html += ".container { max-width: 1200px; margin: 0 auto; }";
    html += ".header { text-align: center; margin-bottom: 30px; }";
    html += ".mode-container { display: flex; gap: 20px; margin-bottom: 30px; }";
    html += ".mode-box { flex: 1; background: #2d2d2d; padding: 20px; border-radius: 10px; }";
    html += ".mode1 { border-left: 5px solid #00ff00; }";
    html += ".mode2 { border-left: 5px solid #ff9900; }";
    html += ".btn { background: #0088ff; color: white; padding: 12px 24px; border: none; border-radius: 5px; cursor: pointer; margin: 5px; text-decoration: none; display: inline-block; }";
    html += ".btn:hover { background: #0066cc; }";
    html += ".positive { color: #00ff00; }";
    html += ".negative { color: #ff3333; }";
    html += "</style></head><body>";
    html += "<div class='container'>";
    html += "<div class='header'><h1>📊 Portfolio Dashboard v4.5</h1>";
    html += "<p>Complete Edition - Auto Reconnect + Battery Monitor</p>";
    html += "<p>WiFi: " + String(isConnectedToWiFi ? "Connected" : "Disconnected") + "</p>";
    html += "<p>Battery: " + String(batteryPercent) + "%</p>";
    html += "</div>";
    
    html += "<div class='mode-container'>";
    html += "<div class='mode-box mode1'><h3>📈 MODE 1 - ENTRY</h3>";
    html += "<p>Portfolio: " + String(settings.entryPortfolio) + "</p>";
    html += "<p>Positions: " + String(cryptoCountMode1) + "</p>";
    html += "<p>P/L: <span class='" + String(portfolioMode1.totalPnlPercent >= 0 ? "positive" : "negative") + "'>" + formatPercent(portfolioMode1.totalPnlPercent) + "</span></p>";
    html += "<p>Value: $" + formatNumber(portfolioMode1.totalCurrentValue) + "</p>";
    html += "<a href='/positions?mode=0' class='btn'>View Positions</a></div>";
    
    html += "<div class='mode-box mode2'><h3>📉 MODE 2 - EXIT</h3>";
    html += "<p>Portfolio: " + String(settings.exitPortfolio) + "</p>";
    html += "<p>Positions: " + String(cryptoCountMode2) + "</p>";
    html += "<p>P/L: <span class='" + String(portfolioMode2.totalPnlPercent >= 0 ? "positive" : "negative") + "'>" + formatPercent(portfolioMode2.totalPnlPercent) + "</span></p>";
    html += "<p>Value: $" + formatNumber(portfolioMode2.totalCurrentValue) + "</p>";
    html += "<a href='/positions?mode=1' class='btn'>View Positions</a></div>";
    html += "</div>";
    
    html += "<div style='text-align: center; margin-top: 30px;'>";
    html += "<a href='/refresh' class='btn'>🔄 Refresh</a>";
    html += "<a href='/wifimanage' class='btn'>📶 WiFi Manager</a>";
    html += "<a href='/testalert' class='btn'>🔊 Test</a>";
    html += "<a href='/resetalerts' class='btn'>Reset Alerts</a>";
    html += "<a href='/systeminfo' class='btn'>📊 Info</a>";
    html += "<a href='/setup' class='btn'>Settings</a>";
    html += "</div>";
    
    html += "<div style='text-align: center; margin-top: 20px; color: #888; font-size: 14px;'>";
    html += "<p>Last Update: " + getTimeString(lastDataUpdate) + " | Uptime: " + getUptimeString() + "</p>";
    if (connectionLost) {
        html += "<p style='color: #ff3333;'>⚠️ Connection Lost: " + String((millis() - connectionLostTime) / 1000) + "s ago</p>";
    }
    html += "</div></div></body></html>";
    
    return html;
}

String generateSetupHTML() {
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<title>Setup</title><style>";
    html += "body { font-family: Arial, sans-serif; margin: 20px; background: #1a1a1a; color: #fff; }";
    html += ".container { max-width: 800px; margin: 0 auto; }";
    html += "input, select { width: 100%; padding: 10px; margin: 5px 0; }";
    html += ".btn { background: #0088ff; color: white; padding: 12px 24px; border: none; border-radius: 5px; cursor: pointer; margin: 10px; }";
    html += ".volume-slider { width: 100%; margin: 10px 0; }";
    html += ".volume-value { display: inline-block; margin-left: 10px; font-weight: bold; }";
    html += "</style>";
    html += "<script>";
    html += "function updateVolume(value) {";
    html += "  document.getElementById('volumeValue').innerText = value;";
    html += "  if(value > 0) {";
    html += "    document.getElementById('volumeValue').style.color = '#00ff00';";
    html += "  } else {";
    html += "    document.getElementById('volumeValue').style.color = '#888';";
    html += "  }";
    html += "}";
    html += "function testSound() {";
    html += "  var volume = document.getElementById('volume').value;";
    html += "  alert('Testing sound at volume: ' + volume + '\\nIn production, this would play a test tone.');";
    html += "}";
    html += "</script>";
    html += "</head><body>";
    html += "<div class='container'><h1>⚙️ Setup</h1>";
    
    // ... بخش‌های دیگر
    
    // بخش آلرت با کنترل حجم صدا
    html += "<h2>🚨 Alerts & Sound</h2>";
    html += "<form action='/savealert' method='post'>";
    html += "<input type='number' step='0.1' name='threshold' value='" + String(settings.alertThreshold, 1) + "' placeholder='Alert Threshold %'><br>";
    html += "<input type='number' step='0.1' name='severethreshold' value='" + String(settings.severeAlertThreshold, 1) + "' placeholder='Severe Alert Threshold %'><br>";
    html += "<input type='number' step='0.1' name='exitalertpercent' value='" + String(settings.exitAlertPercent, 1) + "' placeholder='Exit Alert %'><br>";
    
    // کنترل حجم صدا
    html += "<h3>🔊 Sound Volume</h3>";
    html += "<p>Volume: <span id='volumeValue' class='volume-value' style='color:" + String(settings.buzzerVolume == 0 ? "#888" : "#00ff00") + "'>" + String(settings.buzzerVolume) + "</span></p>";
    html += "<input type='range' id='volume' name='volume' class='volume-slider' min='0' max='20' value='" + String(settings.buzzerVolume) + "' oninput='updateVolume(this.value)'><br>";
    html += "<p style='color: #888; font-size: 12px;'>0 = Off, 5 = Low, 10 = Medium, 15 = High, 20 = Max</p>";
    html += "<button type='button' class='btn' onclick='testSound()' style='background: #ff9900;'>Test Sound</button><br><br>";
    
    html += "<label><input type='checkbox' name='buzzerenabled' " + String(settings.buzzerEnabled ? "checked" : "") + "> Buzzer Enabled</label><br>";
    html += "<label><input type='checkbox' name='exitalertenabled' " + String(settings.exitAlertEnabled ? "checked" : "") + "> Exit Alerts Enabled</label><br>";
    html += "<button class='btn' type='submit'>Save Alert Settings</button>";
    html += "</form>";
    
    html += "<p><a href='/' class='btn'>Back to Dashboard</a></p>";
    html += "</div></body></html>";
    
    return html;
}

String generateSystemInfoHTML() {
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<title>System Info</title><style>";
    html += "body { font-family: Arial, sans-serif; margin: 20px; background: #1a1a1a; color: #fff; }";
    html += ".info { background: #2d2d2d; padding: 15px; margin: 10px 0; border-radius: 5px; }";
    html += "</style></head><body>";
    html += "<h1>📊 System Information</h1>";
    
    html += "<div class='info'><h3>🔧 Hardware</h3>";
    html += "<p>Chip ID: " + String((uint32_t)ESP.getEfuseMac(), HEX) + "</p>";
    html += "<p>Free Heap: " + String(ESP.getFreeHeap() / 1024) + " KB</p>";
    html += "<p>Battery: " + String(batteryVoltage, 2) + "V (" + String(batteryPercent) + "%)</p></div>";
    
    html += "<div class='info'><h3>🌐 Network</h3>";
    html += "<p>WiFi: " + String(isConnectedToWiFi ? "Connected" : "Disconnected") + "</p>";
    if (isConnectedToWiFi) {
        html += "<p>SSID: " + WiFi.SSID() + "</p>";
        html += "<p>IP: " + WiFi.localIP().toString() + "</p>";
        html += "<p>RSSI: " + String(WiFi.RSSI()) + " dBm</p>";
    }
    html += "<p>Connection Lost Count: " + String(connectionLostCount) + "</p></div>";
    
    html += "<div class='info'><h3>⏱️ System</h3>";
    html += "<p>Uptime: " + getUptimeString() + "</p>";
    html += "<p>Boot Count: " + String(settings.bootCount) + "</p>";
    html += "<p>Mode 1 Positions: " + String(cryptoCountMode1) + "</p>";
    html += "<p>Mode 2 Positions: " + String(cryptoCountMode2) + "</p></div>";
    
    html += "<p><a href='/'>Back to Dashboard</a></p>";
    html += "</body></html>";
    
    return html;
}

String generateAlertHistoryHTML(byte mode) {
    String html = "";
    AlertHistory* history;
    int count;
    
    if (mode == 0) {
        history = alertHistoryMode1;
        count = alertHistoryCountMode1;
    } else {
        history = alertHistoryMode2;
        count = alertHistoryCountMode2;
    }
    
    if (count == 0) {
        html = "<p style='color: #888;'>No alerts yet</p>";
    } else {
        for (int i = count - 1; i >= 0 && i >= count - 10; i--) {
            String color = history[i].isProfit ? "#00ff00" : history[i].isLong ? "#0088ff" : "#ff3333";
            html += "<div style='background: #2d2d2d; padding: 10px; margin: 5px; border-left: 4px solid " + color + ";'>";
            html += "<strong>" + String(history[i].symbol) + "</strong> - " + formatPercent(history[i].pnlPercent);
            html += "<br><small>" + String(history[i].timeString) + "</small>";
            html += "</div>";
        }
    }
    
    return html;
}

String generatePositionListHTML(byte mode) {
    CryptoPosition* positions;
    int count;
    PortfolioSummary* summary;
    
    if (mode == 0) {
        positions = cryptoDataMode1;
        count = cryptoCountMode1;
        summary = &portfolioMode1;
    } else {
        positions = cryptoDataMode2;
        count = cryptoCountMode2;
        summary = &portfolioMode2;
    }
    
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<title>Positions</title><style>";
    html += "body { font-family: Arial, sans-serif; margin: 20px; background: #1a1a1a; color: #fff; }";
    html += "table { width: 100%; border-collapse: collapse; }";
    html += "th, td { padding: 10px; border-bottom: 1px solid #444; text-align: left; }";
    html += ".positive { color: #00ff00; }";
    html += ".negative { color: #ff3333; }";
    html += "</style></head><body>";
    html += "<h1>📋 Positions - " + String(mode == 0 ? "Mode 1 (Entry)" : "Mode 2 (Exit)") + "</h1>";
    
    if (count == 0) {
        html += "<p>No positions found</p>";
    } else {
        html += "<table><tr><th>Symbol</th><th>Side</th><th>Quantity</th><th>Entry</th><th>Current</th><th>P/L</th><th>P/L %</th></tr>";
        
        for (int i = 0; i < count; i++) {
            html += "<tr>";
            html += "<td>" + getShortSymbol(positions[i].symbol) + "</td>";
            html += "<td>" + String(positions[i].isLong ? "LONG" : "SHORT") + "</td>";
            html += "<td>" + String(positions[i].quantity, 4) + "</td>";
            html += "<td>$" + formatPrice(positions[i].entryPrice) + "</td>";
            html += "<td>$" + formatPrice(positions[i].currentPrice) + "</td>";
            html += "<td class='" + String(positions[i].pnlValue >= 0 ? "positive" : "negative") + "'>$" + formatNumber(positions[i].pnlValue) + "</td>";
            html += "<td class='" + String(positions[i].changePercent >= 0 ? "positive" : "negative") + "'>" + formatPercent(positions[i].changePercent) + "</td>";
            html += "</tr>";
        }
        
        html += "</table>";
        html += "<h3>Summary</h3>";
        html += "<p>Total Value: $" + formatNumber(summary->totalCurrentValue) + "</p>";
        html += "<p>Total P/L: <span class='" + String(summary->totalPnlPercent >= 0 ? "positive" : "negative") + "'>" + formatPercent(summary->totalPnlPercent) + "</span></p>";
    }
    
    html += "<p><a href='/'>Back to Dashboard</a></p>";
    html += "</body></html>";
    
    return html;
}

// ===== SYSTEM FUNCTIONS =====
void setupResetButton() {
    pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);
    Serial.println("Reset button initialized");
}

void checkResetButton() {
    static bool buttonPressed = false;
    static unsigned long pressStart = 0;
    
    int buttonState = digitalRead(RESET_BUTTON_PIN);
    
    if (buttonState == LOW) {
        if (!buttonPressed) {
            buttonPressed = true;
            pressStart = millis();
        } else {
            unsigned long holdTime = millis() - pressStart;
            
            if (holdTime >= BUTTON_HOLD_TIME && !resetInProgress) {
                resetInProgress = true;
                Serial.println("Factory reset initiated");
                factoryReset();
            }
        }
    } else {
        if (buttonPressed) {
            buttonPressed = false;
        }
    }
}

void factoryReset() {
    Serial.println("Performing factory reset...");
    
    showDisplayMessage("FACTORY RESET", "Erasing all data", "Please wait...", "");
    
    if (settings.buzzerEnabled) {
        playErrorTone();
        delay(500);
        playErrorTone();
    }
    
    EEPROM.begin(EEPROM_SIZE);
    for (int i = 0; i < EEPROM_SIZE; i++) {
        EEPROM.write(i, 0);
    }
    EEPROM.commit();
    EEPROM.end();
    
    initializeSettings();
    
    showDisplayMessage("Reset Complete", "Restarting...", "", "");
    delay(2000);
    
    ESP.restart();
}

void restartSystem() {
    Serial.println("Restarting system...");
    showDisplayMessage("Restarting", "System...", "", "");
    delay(1000);
    ESP.restart();
}

void enterDeepSleep() {
    Serial.println("Entering deep sleep...");
    showDisplayMessage("Deep Sleep", "Mode Activated", "", "");
    delay(1000);
    
    esp_sleep_enable_timer_wakeup(30 * 1000000);
    esp_deep_sleep_start();
}

void printSystemStatus() {
    static unsigned long lastStatusPrint = 0;
    unsigned long now = millis();
    
    if (now - lastStatusPrint > 30000) {
        lastStatusPrint = now;
        
        Serial.println("\n=== SYSTEM STATUS ===");
        Serial.println("Uptime: " + getUptimeString());
        Serial.println("Free Heap: " + String(ESP.getFreeHeap()) + " bytes");
        Serial.println("WiFi: " + String(isConnectedToWiFi ? "Connected" : "Disconnected"));
        Serial.println("Battery: " + String(batteryPercent) + "% (" + String(batteryVoltage, 2) + "V)");
        Serial.println("Mode 1 Positions: " + String(cryptoCountMode1));
        Serial.println("Mode 2 Positions: " + String(cryptoCountMode2));
        Serial.println("Connection Lost: " + String(connectionLost ? "YES" : "NO"));
        Serial.println("====================\n");
    }
}

void debugMemory() {
    Serial.println("=== MEMORY DEBUG ===");
    Serial.println("Total Heap: " + String(ESP.getHeapSize()) + " bytes");
    Serial.println("Free Heap: " + String(ESP.getFreeHeap()) + " bytes");
    Serial.println("Min Free Heap: " + String(ESP.getMinFreeHeap()) + " bytes");
    Serial.println("Max Alloc Heap: " + String(ESP.getMaxAllocHeap()) + " bytes");
    Serial.println("====================\n");
}

void logEvent(String event, String details) {
    unsigned long now = millis();
    String timestamp = getCurrentTime();
    
    Serial.println("[" + timestamp + "] " + event + ": " + details);
}

// ===== API FUNCTIONS =====
void makeAPICall(byte mode) {
    if (!isConnectedToWiFi) return;
    
    String data = getPortfolioData(mode);
    parseCryptoData(data, mode);
}

void handleAPIResponse(String response, byte mode) {
    // Currently handled in parseCryptoData
}

void updateAPIStatistics(bool success, unsigned long responseTime) {
    if (success) {
        apiSuccessCount++;
    } else {
        apiErrorCount++;
    }
    
    if (apiAverageResponseTime == 0) {
        apiAverageResponseTime = responseTime;
    } else {
        apiAverageResponseTime = (apiAverageResponseTime * 0.9) + (responseTime * 0.1);
    }
    
    lastApiCallTime = millis();
}

// ===== MAIN SETUP AND LOOP =====
void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n\n==========================================");
    Serial.println("   COMPLETE PORTFOLIO MONITOR v4.5");
    Serial.println("   ESP32-WROVER-E + ST7789 240x240");
    Serial.println("   AUTO RECONNECT + BATTERY MONITOR");
    Serial.println("==========================================\n");
    
    systemStartTime = millis();
    
    // Initialize hardware
    setupDisplay();
    setupBuzzer();
    setupLEDs();
    setupRGBLEDs();
    setupResetButton();
    
    // Setup battery pin (ADC input)
    pinMode(BATTERY_PIN, INPUT);
    
    // 🚨 تنظیمات پیش‌فرض صدا
    settings.buzzerVolume = 0; // پیش‌فرض خاموش
    settings.buzzerEnabled = true; // اما حجم صفر
    
    // 🚨 غیرفعال کردن کامل سیستم باتری
    settings.showBattery = false;
    batteryPercent = 100;
    batteryLow = false;
    powerSource = POWER_SOURCE_USB;
    
    // Load settings
    if (!loadSettings()) {
        Serial.println("First boot or corrupted settings");
        initializeSettings();
        saveSettings();
    }
    
    // بازنشانی تنظیمات صدا در صورتی که در تنظیمات ذخیره شده مقدار غیرمعقول داشته باشد
    if (settings.buzzerVolume < VOLUME_MIN || settings.buzzerVolume > VOLUME_MAX) {
        settings.buzzerVolume = 0; // خاموش
    }
    
    // 🆕 **تشخیص منبع تغذیه**
    detectPowerSource();

    // Increment boot count
    settings.bootCount++;
    saveSettings();
    
    // Test hardware
    if (settings.buzzerEnabled && settings.buzzerVolume > 0) {
        playStartupTone();
    } else {
        Serial.println("Buzzer disabled or volume is 0");
    }

    setAllLEDs(HIGH);
    delay(200);
    setAllLEDs(LOW);
    
    // Show system info
    Serial.println("System Information:");
    Serial.println("Chip ID: " + String((uint32_t)ESP.getEfuseMac(), HEX));
    Serial.println("Free Heap: " + String(ESP.getFreeHeap()) + " bytes");
    Serial.println("Boot Count: " + String(settings.bootCount));
    Serial.println("Battery Mode: " + String(settings.showBattery ? "ENABLED" : "DISABLED"));
    
    // تنظیمات نمایشگر
    tft.setTextSize(1);
    tft.setRotation(settings.displayRotation);
    
    // Start AP mode first
    Serial.println("\n=== STARTING AP MODE ===");
    showDisplayMessage("AP Mode", "SSID: ESP32-Pfolio", "Pass: 12345678", "192.168.4.1");
    
    WiFi.disconnect(true);
    delay(1000);
    startAPMode();
    delay(1000);
    
    // Setup web server
    setupWebServer();
    Serial.println("Web server started. AP IP: " + WiFi.softAPIP().toString());
    
    delay(3000);
    
    // Try to connect to saved WiFi
    if (settings.networkCount > 0 && settings.autoReconnect) {
        Serial.println("\n=== TRYING SAVED WIFI ===");
        showDisplayMessage("Trying", "Saved WiFi...", "Please wait", "");
        
        if (connectToWiFi()) {
            Serial.println("✅ WiFi Connected!");
            syncTime();
            showDisplayMessage("WiFi Connected", WiFi.SSID(), "IP: " + WiFi.localIP().toString(), "");
            delay(2000);
        } else {
            Serial.println("❌ Failed to connect to WiFi");
            showDisplayMessage("WiFi Failed", "Returning to AP", "AP IP: 192.168.4.1", "");
            delay(2000);
        }
    } else {
        Serial.println("No saved WiFi networks or auto-reconnect disabled");
    }
    
    // Read battery voltage for debugging
    int raw = analogRead(BATTERY_PIN);
    float voltage = raw * (3.3 / 4095.0) * 2.0;
    Serial.println("Battery ADC Debug: " + String(raw) + " = " + String(voltage, 2) + "V");
    
    // نمایش وضعیت صدا
    Serial.println("Buzzer Status: " + String(settings.buzzerEnabled ? "Enabled" : "Disabled"));
    Serial.println("Buzzer Volume: " + String(settings.buzzerVolume) + "/" + String(VOLUME_MAX));
    
    // Show main display with improved fonts
    showMainDisplay();
    
    Serial.println("\n=== SYSTEM READY ===");
    Serial.println("Mode 1 Portfolio: " + String(settings.entryPortfolio));
    Serial.println("Mode 2 Portfolio: " + String(settings.exitPortfolio));
    Serial.println("Alert Threshold: " + String(settings.alertThreshold, 1) + "%");
    Serial.println("Buzzer Volume: " + String(settings.buzzerVolume));
    Serial.println("Auto Reconnect: " + String(settings.autoReconnect ? "Enabled" : "Disabled"));
    Serial.println("Display Brightness: " + String(settings.displayBrightness) + "%");
    Serial.println("Web Interface: http://" + (isConnectedToWiFi ? WiFi.localIP().toString() : "192.168.4.1"));
    Serial.println("==========================================\n");
    
    // 🆕 تست اولیه نمایشگر با فونت‌های بزرگ
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(3);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setCursor(40, 100);
    tft.print("READY");
    delay(1000);
    
    // نمایش اطلاعات اولیه
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setCursor(30, 50);
    tft.print("Dual Mode");
    tft.setCursor(50, 80);
    tft.print("Monitor");
    
    tft.setTextSize(1);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setCursor(30, 120);
    tft.print("v4.5 - Improved");
    tft.setCursor(40, 140);
    tft.print("Font & Sound");
    
    delay(1500);
    showMainDisplay();
}

void loop() {
    // Handle web server requests
    server.handleClient();
    
    // Check reset button
    checkResetButton();
    if (resetInProgress) return;
    
    // Update time
    static unsigned long lastTimeUpdate = 0;
    if (millis() - lastTimeUpdate > 60000) {
        lastTimeUpdate = millis();
        updateDateTime();
    }
    
    // Update data every 15 seconds (only if connected)
    if (isConnectedToWiFi && millis() - lastDataUpdate > DATA_UPDATE_INTERVAL) {
        lastDataUpdate = millis();
        
        if (strlen(settings.server) > 0) {
            makeAPICall(0);
            makeAPICall(1);
            
            checkAlerts(0);
            checkAlerts(1);
            
            Serial.println("Data updated - Mode1: " + String(cryptoCountMode1) + 
                          ", Mode2: " + String(cryptoCountMode2));
        }
    }
    
    // Handle WiFi and connection
    handleWiFiConnection();
    checkConnectionStatus();
    
    // Check battery
    static unsigned long lastBatteryCheckTime = 0;
    if (millis() - lastBatteryCheckTime > BATTERY_CHECK_INTERVAL) {
        lastBatteryCheckTime = millis();
        checkBattery();
    }
    
    // Update display
    updateDisplay();
    
    // Update LEDs
    updateLEDs();
    updateRGBLEDs();
    
    // Print system status periodically
    printSystemStatus();
    
    // Small delay
    delay(100);
}