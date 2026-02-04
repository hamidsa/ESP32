/* ============================================================================
   PORTFOLIO MONITOR - ESP32-WROVER-E - FIXED VERSION
   Professional Dual Mode Portfolio Tracking System
   Version: 4.6.0 - Stable WiFi & RGB Fix
   Hardware: ESP32-WROVER-E + ST7789 240x240 + Dual RGB LEDs (Common Cathode)
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
#include <Preferences.h>  // برای ذخیره‌سازی مطمئن WiFi

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
#define JSON_BUFFER_SIZE 3072  
#define DISPLAY_CRYPTO_COUNT 8

#define POWER_SOURCE_USB 0
#define POWER_SOURCE_BATTERY 1
#define POWER_SOURCE_EXTERNAL 2

// ===== PIN DEFINITIONS ===== (FIXED)
// RGB LEDs (Common Cathode) - تأیید شده
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
#define SCAN_INTERVAL 60000           // هر 1 دقیقه اسکن کن
#define CONNECTED_SCREEN_TIMEOUT 5000 // 5 ثانیه نمایش Connected

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
bool apEnabled = true; // وضعیت AP (پیش‌فرض روشن)
bool showingConnectedScreen = false; // آیا در حال نمایش صفحه Connected هستیم؟
unsigned long connectedScreenStartTime = 0;

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
    
    // RGB Settings - FIXED FOR COMMON CATHODE
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
Preferences preferences; // برای ذخیره‌سازی مطمئن

// ===== GLOBAL VARIABLES =====
SystemSettings settings;
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
void fixDisplayStuck(); // تابع جدید برای رفع گیر کردن نمایشگر

// Buzzer Functions
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
void testAllRGB(); // تابع جدید برای تست RGB

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

// WiFi Functions - FIXED VERSION
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
void quickSystemTest(); // تابع جدید برای تست سریع

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
    Serial.println("✅ Display initialized successfully");
    
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
    tft.println("Complete v4.6.0");
    
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setCursor(20, 130);
    tft.println("ESP32-WROVER-E");
    tft.setCursor(30, 145);
    tft.println("Stable WiFi Fix");
    
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
    
    // برگشت اتوماتیک از آلرت
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
    
    // برگشت اتوماتیک از صفحه Connected
    if (showingConnectedScreen) {
        if (now - connectedScreenStartTime > CONNECTED_SCREEN_TIMEOUT) {
            showingConnectedScreen = false;
            showMainDisplay();
            return;
        }
        return; // صفحه Connected را تغییر نده
    }
    
    // کنترل backlight
    if (settings.displayTimeout > 0) {
        static unsigned long lastInteraction = millis();
        
        if (now - lastAlertTime < 10000) {
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
    // مطمئن شویم backlight روشن است
    if (settings.displayBrightness > 0) {
        digitalWrite(TFT_BL_PIN, HIGH);
    }
    
    tft.fillScreen(TFT_BLACK);
    
    // هدر
    tft.setTextSize(2);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setCursor(5, 5);
    tft.print("PORTFOLIO");
    
    // وضعیت WiFi
    tft.setTextSize(1);
    tft.setCursor(5, 35);
    tft.print("WiFi:");
    
    if (isConnectedToWiFi) {
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        String ssid = WiFi.SSID();
        if (ssid.length() > 10) {
            ssid = ssid.substring(0, 10) + "..";
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
    
    // زمان
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
    
    // جدول موقعیت‌ها
    tft.drawFastHLine(0, 75, 240, TFT_DARKGREY);
    
    // هدر جدول
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(5, 80);
    tft.print("Mode");
    tft.setCursor(60, 80);
    tft.print("Pos");
    tft.setCursor(100, 80);
    tft.print("P/L %");
    tft.setCursor(160, 80);
    tft.print("Value");
    
    tft.drawFastHLine(0, 95, 240, TFT_DARKGREY);
    
    int yPos = 100;
    
    // Entry Mode
    tft.setTextSize(1);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setCursor(5, yPos);
    tft.print("ENTRY");
    
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(60, yPos);
    tft.print(cryptoCountMode1);
    
    tft.setTextColor(portfolioMode1.totalPnlPercent >= 0 ? TFT_GREEN : TFT_RED, TFT_BLACK);
    tft.setCursor(100, yPos);
    tft.print(formatPercent(portfolioMode1.totalPnlPercent));
    
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setCursor(160, yPos);
    tft.print("$");
    tft.print(formatNumber(portfolioMode1.totalCurrentValue));
    
    // Exit Mode
    yPos += 20;
    
    tft.setTextColor(TFT_ORANGE, TFT_BLACK);
    tft.setCursor(5, yPos);
    tft.print("EXIT");
    
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(60, yPos);
    tft.print(cryptoCountMode2);
    
    tft.setTextColor(portfolioMode2.totalPnlPercent >= 0 ? TFT_GREEN : TFT_RED, TFT_BLACK);
    tft.setCursor(100, yPos);
    tft.print(formatPercent(portfolioMode2.totalPnlPercent));
    
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setCursor(160, yPos);
    tft.print("$");
    tft.print(formatNumber(portfolioMode2.totalCurrentValue));
    
    // مجموع
    yPos += 20;
    tft.drawFastHLine(0, yPos-5, 240, TFT_DARKGREY);
    
    float totalValue = portfolioMode1.totalCurrentValue + portfolioMode2.totalCurrentValue;
    float totalPnlPercent = 0;
    
    if ((portfolioMode1.totalInvestment + portfolioMode2.totalInvestment) > 0) {
        totalPnlPercent = ((totalValue - (portfolioMode1.totalInvestment + portfolioMode2.totalInvestment)) / 
                         (portfolioMode1.totalInvestment + portfolioMode2.totalInvestment)) * 100;
    }
    
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setCursor(5, yPos);
    tft.print("TOTAL");
    
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(60, yPos);
    tft.print(cryptoCountMode1 + cryptoCountMode2);
    
    tft.setTextColor(totalPnlPercent >= 0 ? TFT_GREEN : TFT_RED, TFT_BLACK);
    tft.setCursor(100, yPos);
    tft.print(formatPercent(totalPnlPercent));
    
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setCursor(160, yPos);
    tft.print("$");
    tft.print(formatNumber(totalValue));
    
    // وضعیت سیستم
    yPos += 25;
    tft.drawFastHLine(0, yPos-5, 240, TFT_DARKGREY);
    
    tft.setTextSize(1);
    if (mode1GreenActive || mode1RedActive || mode2GreenActive || mode2RedActive) {
        tft.setTextColor(TFT_ORANGE, TFT_BLACK);
        tft.setCursor(5, yPos);
        tft.print("ALERT!");
    } else if (connectionLost) {
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.setCursor(5, yPos);
        tft.print("NO CONN");
    } else {
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.setCursor(5, yPos);
        tft.print("READY");
    }
    
    // باتری
    if (powerSource == POWER_SOURCE_USB) {
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        tft.setCursor(60, yPos);
        tft.print("USB");
    } else if (settings.showBattery) {
        drawBatteryIcon(60, yPos, batteryPercent);
    }
    
    // حجم
    tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
    tft.setCursor(120, yPos);
    tft.print("Vol:");
    tft.print(settings.buzzerVolume);
    tft.print("%");
    
    // وضعیت
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(180, yPos);
    tft.print("v4.6.0");
}

void showConnectionScreen() {
    showingConnectedScreen = true;
    connectedScreenStartTime = millis();
    
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

void fixDisplayStuck() {
    // اگر بیش از 10 ثانیه در صفحه Connected مانده‌ایم، به صفحه اصلی برگرد
    if (showingConnectedScreen) {
        unsigned long now = millis();
        if (now - connectedScreenStartTime > 10000) { // 10 ثانیه
            showingConnectedScreen = false;
            showMainDisplay();
            Serial.println("Auto-returned to main display from connected screen (timeout)");
        }
    }
}

// ===== BUZZER FUNCTIONS =====
void setupBuzzer() {
    Serial.println("Initializing buzzer...");
    
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
    
    if (settings.buzzerEnabled && settings.buzzerVolume > 0) {
        playVolumeFeedback();
    }
    
    Serial.println("✅ Buzzer initialized");
}

void setBuzzerVolume(int volume) {
    settings.buzzerVolume = constrain(volume, VOLUME_MIN, VOLUME_MAX);
    Serial.print("Buzzer volume set to: ");
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
    
    Serial.println("✅ LEDs initialized");
}

void setupRGBLEDs() {
    Serial.println("Initializing RGB LEDs (Common Cathode)...");
    
    pinMode(RGB1_RED, OUTPUT);
    pinMode(RGB1_GREEN, OUTPUT);
    pinMode(RGB1_BLUE, OUTPUT);
    pinMode(RGB2_RED, OUTPUT);
    pinMode(RGB2_GREEN, OUTPUT);
    pinMode(RGB2_BLUE, OUTPUT);
    
    // Common Cathode: برای خاموش کردن، همه پین‌ها LOW
    digitalWrite(RGB1_RED, LOW);
    digitalWrite(RGB1_GREEN, LOW);
    digitalWrite(RGB1_BLUE, LOW);
    digitalWrite(RGB2_RED, LOW);
    digitalWrite(RGB2_GREEN, LOW);
    digitalWrite(RGB2_BLUE, LOW);
    
    delay(100);
    
    Serial.println("✅ RGB LEDs initialized (Common Cathode)");
}

// ===== RGB FUNCTIONS - COMMON CATHODE FIXED =====
void setRGB1Color(uint8_t r, uint8_t g, uint8_t b) {
    if (!settings.rgb1Enabled || settings.rgb1Brightness == 0) {
        turnOffRGB1();
        return;
    }
    
    // Common Cathode: مقدار مستقیم، هرچه عدد بالاتر، نور بیشتر
    // تنظیم روشنایی
    r = (r * settings.rgb1Brightness) / 100;
    g = (g * settings.rgb1Brightness) / 100;
    b = (b * settings.rgb1Brightness) / 100;
    
    r = constrain(r, 0, 255);
    g = constrain(g, 0, 255);
    b = constrain(b, 0, 255);
    
    analogWrite(RGB1_RED, r);
    analogWrite(RGB1_GREEN, g);
    analogWrite(RGB1_BLUE, b);
}

void setRGB2Color(uint8_t r, uint8_t g, uint8_t b) {
    if (!settings.rgb2Enabled || settings.rgb2Brightness == 0) {
        turnOffRGB2();
        return;
    }
    
    // Common Cathode: مقدار مستقیم
    r = (r * settings.rgb2Brightness) / 100;
    g = (g * settings.rgb2Brightness) / 100;
    b = (b * settings.rgb2Brightness) / 100;
    
    r = constrain(r, 0, 255);
    g = constrain(g, 0, 255);
    b = constrain(b, 0, 255);
    
    analogWrite(RGB2_RED, r);
    analogWrite(RGB2_GREEN, g);
    analogWrite(RGB2_BLUE, b);
}

void turnOffRGB1() {
    // Common Cathode: همه پین‌ها 0 (خاموش)
    analogWrite(RGB1_RED, 0);
    analogWrite(RGB1_GREEN, 0);
    analogWrite(RGB1_BLUE, 0);
}

void turnOffRGB2() {
    // Common Cathode: همه پین‌ها 0 (خاموش)
    analogWrite(RGB2_RED, 0);
    analogWrite(RGB2_GREEN, 0);
    analogWrite(RGB2_BLUE, 0);
}

void testAllRGB() {
    Serial.println("\n🔴 Testing RGB LEDs (Common Cathode)...");
    
    // همه خاموش
    Serial.println("All OFF");
    turnOffRGB1();
    turnOffRGB2();
    delay(1000);
    
    // RGB1: رنگ‌های اصلی
    Serial.println("RGB1: RED");
    setRGB1Color(255, 0, 0);
    delay(1000);
    
    Serial.println("RGB1: GREEN");
    setRGB1Color(0, 255, 0);
    delay(1000);
    
    Serial.println("RGB1: BLUE");
    setRGB1Color(0, 0, 255);
    delay(1000);
    
    Serial.println("RGB1: WHITE");
    setRGB1Color(255, 255, 255);
    delay(1000);
    
    turnOffRGB1();
    
    // RGB2: رنگ‌های اصلی
    Serial.println("RGB2: RED");
    setRGB2Color(255, 0, 0);
    delay(1000);
    
    Serial.println("RGB2: GREEN");
    setRGB2Color(0, 255, 0);
    delay(1000);
    
    Serial.println("RGB2: BLUE");
    setRGB2Color(0, 0, 255);
    delay(1000);
    
    Serial.println("RGB2: WHITE");
    setRGB2Color(255, 255, 255);
    delay(1000);
    
    // همه خاموش
    turnOffRGB1();
    turnOffRGB2();
    
    Serial.println("✅ RGB test complete");
}

// ===== SETTINGS FUNCTIONS - USING PREFERENCES =====
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
    
    settings.showBattery = true;
    settings.batteryWarningLevel = BATTERY_WARNING;
    
    settings.autoReconnect = true;
    settings.reconnectAttempts = 5;
    
    settings.configured = false;
    settings.firstBoot = millis();
    settings.bootCount = 0;
    settings.totalUptime = 0;
    
    Serial.println("✅ Default settings initialized");
}

bool loadSettings() {
    Serial.println("Loading settings using Preferences...");
    
    if (!preferences.begin("portfolio", true)) {
        Serial.println("❌ Failed to open preferences");
        return false;
    }
    
    // بارگذاری تعداد شبکه‌ها
    settings.networkCount = preferences.getInt("networkCount", 0);
    Serial.print("Found ");
    Serial.print(settings.networkCount);
    Serial.println(" saved networks");
    
    // بارگذاری هر شبکه
    for (int i = 0; i < settings.networkCount; i++) {
        String key = "network_" + String(i);
        
        String ssid = preferences.getString((key + "_ssid").c_str(), "");
        String password = preferences.getString((key + "_pass").c_str(), "");
        
        if (ssid.length() > 0) {
            strncpy(settings.networks[i].ssid, ssid.c_str(), 31);
            settings.networks[i].ssid[31] = '\0';
            
            strncpy(settings.networks[i].password, password.c_str(), 63);
            settings.networks[i].password[63] = '\0';
            
            settings.networks[i].priority = preferences.getUChar((key + "_prio").c_str(), 5);
            settings.networks[i].autoConnect = preferences.getBool((key + "_auto").c_str(), true);
            settings.networks[i].configured = true;
            settings.networks[i].connectionAttempts = 0;
            settings.networks[i].lastConnected = 0;
            settings.networks[i].rssi = 0;
            
            Serial.print("  Network ");
            Serial.print(i);
            Serial.print(": ");
            Serial.print(settings.networks[i].ssid);
            Serial.print(" (Priority: ");
            Serial.print(settings.networks[i].priority);
            Serial.println(")");
        }
    }
    
    // بارگذاری سایر تنظیمات
    String server = preferences.getString("server", "");
    strncpy(settings.server, server.c_str(), 127);
    
    String username = preferences.getString("username", "");
    strncpy(settings.username, username.c_str(), 31);
    
    String userpass = preferences.getString("userpass", "");
    strncpy(settings.userpass, userpass.c_str(), 63);
    
    String entryPortfolio = preferences.getString("entryPortfolio", "MainPortfolio");
    strncpy(settings.entryPortfolio, entryPortfolio.c_str(), 31);
    
    String exitPortfolio = preferences.getString("exitPortfolio", "ExitPortfolio");
    strncpy(settings.exitPortfolio, exitPortfolio.c_str(), 31);
    
    settings.buzzerVolume = preferences.getInt("buzzerVolume", DEFAULT_VOLUME);
    settings.buzzerEnabled = preferences.getBool("buzzerEnabled", true);
    
    settings.rgb1Enabled = preferences.getBool("rgb1Enabled", true);
    settings.rgb2Enabled = preferences.getBool("rgb2Enabled", true);
    settings.rgb1Brightness = preferences.getInt("rgb1Brightness", 80);
    settings.rgb2Brightness = preferences.getInt("rgb2Brightness", 80);
    
    settings.alertThreshold = preferences.getFloat("alertThreshold", DEFAULT_ALERT_THRESHOLD);
    settings.severeAlertThreshold = preferences.getFloat("severeThreshold", DEFAULT_SEVERE_THRESHOLD);
    
    settings.configured = preferences.getBool("configured", false);
    settings.bootCount = preferences.getInt("bootCount", 0);
    
    preferences.end();
    
    Serial.println("✅ Settings loaded successfully");
    return true;
}

bool saveSettings() {
    Serial.println("Saving settings using Preferences...");
    
    if (!preferences.begin("portfolio", false)) {
        Serial.println("❌ Failed to open preferences for writing");
        return false;
    }
    
    // ذخیره تعداد شبکه‌ها
    preferences.putInt("networkCount", settings.networkCount);
    
    // ذخیره هر شبکه
    for (int i = 0; i < settings.networkCount; i++) {
        String key = "network_" + String(i);
        
        preferences.putString((key + "_ssid").c_str(), settings.networks[i].ssid);
        preferences.putString((key + "_pass").c_str(), settings.networks[i].password);
        preferences.putUChar((key + "_prio").c_str(), settings.networks[i].priority);
        preferences.putBool((key + "_auto").c_str(), settings.networks[i].autoConnect);
    }
    
    // ذخیره سایر تنظیمات
    preferences.putString("server", settings.server);
    preferences.putString("username", settings.username);
    preferences.putString("userpass", settings.userpass);
    preferences.putString("entryPortfolio", settings.entryPortfolio);
    preferences.putString("exitPortfolio", settings.exitPortfolio);
    
    preferences.putInt("buzzerVolume", settings.buzzerVolume);
    preferences.putBool("buzzerEnabled", settings.buzzerEnabled);
    
    preferences.putBool("rgb1Enabled", settings.rgb1Enabled);
    preferences.putBool("rgb2Enabled", settings.rgb2Enabled);
    preferences.putInt("rgb1Brightness", settings.rgb1Brightness);
    preferences.putInt("rgb2Brightness", settings.rgb2Brightness);
    
    preferences.putFloat("alertThreshold", settings.alertThreshold);
    preferences.putFloat("severeThreshold", settings.severeAlertThreshold);
    
    preferences.putBool("configured", true);
    preferences.putInt("bootCount", settings.bootCount);
    
    preferences.end();
    
    Serial.println("✅ Settings saved successfully");
    Serial.print("Network count saved: ");
    Serial.println(settings.networkCount);
    
    return true;
}

// ===== WIFI FUNCTIONS - FIXED VERSION =====
bool addOrUpdateWiFiNetwork(const char* ssid, const char* password, byte priority = 5, bool autoConnect = true) {
    if (strlen(ssid) == 0) {
        Serial.println("❌ Cannot add network: SSID is empty");
        return false;
    }
    
    Serial.print("➕ Adding/Updating WiFi network: ");
    Serial.println(ssid);
    
    // بررسی وجود شبکه
    int existingIndex = -1;
    for (int i = 0; i < settings.networkCount; i++) {
        if (strcmp(settings.networks[i].ssid, ssid) == 0) {
            existingIndex = i;
            break;
        }
    }
    
    if (existingIndex >= 0) {
        // به‌روزرسانی شبکه موجود
        Serial.println("   Updating existing network");
        
        strncpy(settings.networks[existingIndex].password, password, 63);
        settings.networks[existingIndex].password[63] = '\0';
        settings.networks[existingIndex].priority = priority;
        settings.networks[existingIndex].autoConnect = autoConnect;
        
        Serial.println("   ✅ Network updated in memory");
    } else {
        // افزودن شبکه جدید
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
            
            Serial.print("   Maximum networks reached, removing: ");
            Serial.println(settings.networks[lowestPriorityIndex].ssid);
            
            // حذف شبکه
            for (int i = lowestPriorityIndex; i < settings.networkCount - 1; i++) {
                settings.networks[i] = settings.networks[i + 1];
            }
            settings.networkCount--;
        }
        
        // افزودن شبکه جدید
        WiFiNetwork* network = &settings.networks[settings.networkCount];
        
        strncpy(network->ssid, ssid, 31);
        network->ssid[31] = '\0';
        strncpy(network->password, password, 63);
        network->password[63] = '\0';
        network->priority = priority;
        network->autoConnect = autoConnect;
        network->configured = true;
        network->connectionAttempts = 0;
        network->lastConnected = 0;
        network->rssi = 0;
        
        settings.networkCount++;
        
        Serial.println("   ✅ New network added to memory");
    }
    
    // ذخیره در حافظه
    if (saveSettings()) {
        Serial.println("   💾 Settings saved successfully");
        return true;
    } else {
        Serial.println("   ❌ Failed to save settings");
        return false;
    }
}

bool connectToSpecificWiFi(int networkIndex) {
    if (networkIndex < 0 || networkIndex >= settings.networkCount) {
        Serial.println("❌ Invalid network index");
        return false;
    }
    
    WiFiNetwork* network = &settings.networks[networkIndex];
    
    Serial.print("Attempting to connect to: ");
    Serial.println(network->ssid);
    
    showConnectionScreen();
    
    WiFi.disconnect(true);
    delay(1000);
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(network->ssid, network->password);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        attempts++;
        Serial.print(".");
        
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
        Serial.print("SSID: ");
        Serial.println(WiFi.SSID());
        Serial.print("IP: ");
        Serial.println(WiFi.localIP().toString());
        Serial.print("RSSI: ");
        Serial.print(WiFi.RSSI());
        Serial.println(" dBm");
        
        syncTime();
        
        // نمایش صفحه Connected به مدت کوتاه
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.setTextSize(2);
        tft.setCursor(40, 60);
        tft.println("CONNECTED");
        tft.setTextSize(1);
        tft.setCursor(30, 100);
        tft.print("SSID: ");
        tft.println(String(network->ssid));
        tft.setCursor(30, 120);
        tft.print("IP: ");
        tft.println(WiFi.localIP().toString());
        
        delay(2000); // نمایش به مدت 2 ثانیه
        
        // برگشت به صفحه اصلی
        showingConnectedScreen = false;
        showMainDisplay();
        
        saveSettings();
        return true;
    }
    
    Serial.println("\n❌ Failed to connect");
    network->connectionAttempts++;
    
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
    showMainDisplay();
    return false;
}

// ===== SETUP AND LOOP =====
void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n\n========================================");
    Serial.println("PORTFOLIO MONITOR v4.6.0 - STABLE VERSION");
    Serial.println("ESP32-WROVER-E - Fixed WiFi & RGB");
    Serial.println("========================================");
    
    systemStartTime = millis();
    
    // بارگذاری تنظیمات
    if (!loadSettings()) {
        Serial.println("No saved settings found, initializing defaults");
        initializeSettings();
        saveSettings();
    }
    
    // افزایش شماره بوت
    settings.bootCount++;
    saveSettings();
    
    Serial.print("Boot count: ");
    Serial.println(settings.bootCount);
    Serial.print("Loaded WiFi networks: ");
    Serial.println(settings.networkCount);
    
    // راه‌اندازی سخت‌افزار
    setupDisplay();
    setupBuzzer();
    setupLEDs();
    setupRGBLEDs();
    
    // تست RGB
    testAllRGB();
    
    delay(1000);
    
    // بوق شروع
    if (settings.buzzerEnabled) {
        playStartupTone();
    }
    
    // اتصال به WiFi
    updateWiFiMode();
    
    if (settings.networkCount > 0) {
        Serial.println("Attempting WiFi connection...");
        bool connected = connectToBestWiFi();
        
        if (!connected) {
            Serial.println("Failed to connect to WiFi, starting AP mode");
            startAPMode();
        }
    } else {
        Serial.println("No WiFi networks configured, starting AP mode");
        startAPMode();
    }
    
    // راه‌اندازی وب سرور
    setupWebServer();
    
    Serial.println("\n✅ System initialized successfully!");
    Serial.print("Free memory: ");
    Serial.print(ESP.getFreeHeap() / 1024);
    Serial.println(" KB");
    Serial.print("WiFi Status: ");
    Serial.println(isConnectedToWiFi ? "Connected" : "Disconnected");
    Serial.print("Buzzer Volume: ");
    Serial.print(settings.buzzerVolume);
    Serial.println("%");
    
    lastDataUpdate = millis() - DATA_UPDATE_INTERVAL;
}

void loop() {
    server.handleClient();
    fixDisplayStuck(); // جلوگیری از گیر کردن نمایشگر
    
    unsigned long now = millis();
    
    if (!isConnectedToWiFi && !apModeActive) {
        if (now - lastReconnectAttempt > RECONNECT_INTERVAL) {
            lastReconnectAttempt = now;
            if (settings.autoReconnect && settings.networkCount > 0) {
                Serial.println("Attempting auto-reconnect...");
                connectToBestWiFi();
            }
        }
    }
    
    if (isConnectedToWiFi) {
        if (now - lastDataUpdate > DATA_UPDATE_INTERVAL) {
            lastDataUpdate = now;
            
            if (strlen(settings.entryPortfolio) > 0) {
                String data1 = getPortfolioData(0);
                if (data1 != "{}") {
                    parseCryptoData(data1, 0);
                    calculatePortfolioSummary(0);
                }
            }
            
            if (strlen(settings.exitPortfolio) > 0) {
                String data2 = getPortfolioData(1);
                if (data2 != "{}") {
                    parseCryptoData(data2, 1);
                    calculatePortfolioSummary(1);
                }
            }
        }
        
        updateDateTime();
    }
    
    if (now - lastAlertCheck > 5000) {
        lastAlertCheck = now;
        if (cryptoCountMode1 > 0) checkAlerts(0);
        if (cryptoCountMode2 > 0) checkAlerts(1);
    }
    
    checkResetButton();
    checkBattery();
    updateDisplay();
    updateLEDs();
    updateRGBLEDs();
    
    if (now - lastWiFiCheck > 10000) {
        lastWiFiCheck = now;
        checkWiFiStatus();
        checkConnectionStatus();
    }
}

// ===== توابع باقی مانده (مشابه قبل) =====
// بقیه توابع را از کد قبلی کپی کنید، فقط توابع بالا تغییر کرده‌اند

// توابع زیر را از کد قبلی کپی کنید بدون تغییر:
// - توابع بوق (playLongPositionAlert, playShortPositionAlert, etc.)
// - توابع آلرت (processEntryAlerts, processExitAlerts, etc.)
// - توابع پردازش داده (parseCryptoData, getPortfolioData, etc.)
// - توابع وب سرور (handleRoot, handleSetup, etc.)
// - توابع سودمند (getShortSymbol, formatPercent, etc.)

// فقط مطمئن شوید که این توابع در پروتوتیپ‌ها تعریف شده‌اند