/* ============================================================================
   PORTFOLIO MONITOR - ESP32-WROVER
   Dual Mode Simultaneous Tracking
   Version: 4.0.2 - Display Rotation Fix
   ============================================================================ */

#include <SPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <time.h>

// ===== DEFINES =====
#define MAX_ALERT_HISTORY 50
#define MAX_CRYPTO 60
#define MAX_WIFI_NETWORKS 5
#define EEPROM_SIZE 4096
#define JSON_BUFFER_SIZE 8192

// ===== LED CONFIGURATION =====
// RGB LEDs for mode 1 (Alert History Visualization)
#define RGB1_RED    32
#define RGB1_GREEN  33
#define RGB1_BLUE   25

// RGB LEDs for mode 2 (Threshold Gradient Visualization)
#define RGB2_RED    26
#define RGB2_GREEN  14
#define RGB2_BLUE   12

// Normal LEDs for mode 1
#define LED_GREEN_1 22
#define LED_RED_1   21

// Normal LEDs for mode 2
#define LED_GREEN_2 19
#define LED_RED_2   27

// Buzzer
#define BUZZER_PIN 13

// ===== TIMING CONSTANTS =====
#define DATA_UPDATE_INTERVAL 15000  // هر 15 ثانیه
#define RGB1_CYCLE_INTERVAL 23000   // چرخش رنگ‌ها هر 23 ثانیه
#define RGB2_UPDATE_INTERVAL 5000   // بروزرسانی گرادیان هر 5 ثانیه
#define DISPLAY_UPDATE_INTERVAL 5000  // بروزرسانی نمایشگر هر 5 ثانیه
#define ALERT_DISPLAY_TIME 10000
#define WIFI_CONNECT_TIMEOUT 30000  // 30 ثانیه تایم‌اوت برای WiFi
#define PANEL_ACCESS_WINDOW 1000    // 1 ثانیه زمان دسترسی به پنل

// ===== TFT DISPLAY =====
TFT_eSPI tft = TFT_eSPI();

// رنگ‌های سفارشی
#define TFT_GRAY 0x7BEF
#define TFT_ORANGE 0xFDA0
#define TFT_CYAN 0x07FF
#define TFT_MAGENTA 0xF81F
#define TFT_YELLOW 0xFFE0
#define TFT_PURPLE 0x780F
#define TFT_PINK 0xFC18
#define TFT_LIGHT_BLUE 0x867D
#define TFT_DARK_GREEN 0x03E0

// ===== STRUCTS =====
struct AlertHistory {
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
    byte alertMode; // 0 = Entry Mode, 1 = Exit Mode
};

struct CryptoPosition {
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
};

struct PortfolioSummary {
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
};

struct SystemSettings {
    char ssid[32];
    char password[64];
    char server[128];
    char username[32];
    char userpass[64];
    char entryPortfolio[32];
    char exitPortfolio[32];
    
    float alertThreshold;
    float severeAlertThreshold;
    float portfolioAlertThreshold;
    float exitAlertPercent;
    
    int buzzerVolume;
    bool buzzerEnabled;
    bool ledEnabled;
    
    byte magicNumber;
    bool configured;
    unsigned long firstBoot;
    int bootCount;
    unsigned long totalUptime;
};

// ===== GLOBAL VARIABLES =====
SystemSettings settings;
CryptoPosition entryCryptoData[MAX_CRYPTO];
CryptoPosition exitCryptoData[MAX_CRYPTO];
PortfolioSummary entryPortfolio;
PortfolioSummary exitPortfolio;
AlertHistory alertHistory[MAX_ALERT_HISTORY];

int entryCryptoCount = 0;
int exitCryptoCount = 0;
int alertHistoryCount = 0;

bool isConnectedToWiFi = false;
bool apModeActive = false;
bool showingAlert = false;
bool panelAccessEnabled = false; // فعال بودن دسترسی به پنل
unsigned long wifiConnectStartTime = 0;
unsigned long panelAccessStartTime = 0;

WebServer server(80);
HTTPClient http;

unsigned long lastDataUpdate = 0;
unsigned long lastRGB1Cycle = 0;
unsigned long lastRGB2Update = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long alertStartTime = 0;
unsigned long lastPageChange = 0;

String currentDateTime = "";
String wifiIP = "";
String wifiSSID = "";

// برای نمایشگر TFT
int currentPage = 0; // 0: وضعیت کلی, 1: مد ۱, 2: مد ۲, 3: تاریخچه
const int TOTAL_PAGES = 4;
const int PAGE_CHANGE_INTERVAL = 10000; // تغییر صفحه هر 10 ثانیه

// ===== FUNCTION DECLARATIONS =====
void setupTFT();
void updateTFTDisplay();
void drawStatusPage();
void drawMode1Page();
void drawMode2Page();
void drawHistoryPage();
void drawAlert(String title, String message);
void drawWiFiInfo();
void drawHeader(String title, uint16_t color);

void setupLEDs();
void setupRGBLEDs();
void updateLEDs();
void updateRGB1Cycle();
void updateRGB2Gradient();
void setRGB1Color(int r, int g, int b);
void setRGB2Color(int r, int g, int b);
void turnOffAllLEDs();
void triggerAlertLED(int mode, bool isProfit, float pnlPercent);

void setupBuzzer();
void playTone(int frequency, int duration, int volumePercent);
void playAlertTone(bool isEntryMode, bool isLong, bool isSevere, bool isProfit);
void playSuccessTone();
void playErrorTone();

void initializeSettings();
bool loadSettings();
bool saveSettings();
bool connectToWiFi();
bool startAPMode();

void updateDateTime();
String formatPrice(float price);
String formatPercent(float percent);
String getShortSymbol(const char* symbol);

String getEntryModeData();
String getExitModeData();
void parseEntryModeData(String jsonData);
void parseExitModeData(String jsonData);
void clearCryptoData();
void checkAlerts();

void handleRoot();
void handleSetup();
void handleSaveWiFi();
void handleSaveAPI();
void handleSaveSettings();
void handleRefresh();
void handleTestAlert();
void handleResetAlerts();
bool isPanelAccessAllowed(); // بررسی مجوز دسترسی به پنل
void setupWebServer();

void addAlertToHistory(const char* symbol, float price, float pnlPercent, 
                      bool isLong, bool isSevere, bool isProfit, 
                      byte alertType, byte alertMode, const char* message);

// تابع کمکی برای min
int minInt(int a, int b) {
    return (a < b) ? a : b;
}

// ===== SETUP =====
void setup() {
    Serial.begin(115200);
    delay(3000);
    
    Serial.println("\n\n==========================================");
    Serial.println("   ESP32-WROVER Portfolio Monitor v4.0.2");
    Serial.println("   Display Rotation Fix");
    Serial.println("==========================================\n");
    
    // راه‌اندازی TFT
    setupTFT();
    
    // راه‌اندازی LEDها
    setupLEDs();
    setupRGBLEDs();
    
    // راه‌اندازی بوزر
    setupBuzzer();
    
    // بارگذاری تنظیمات
    if (!loadSettings()) {
        Serial.println("First boot or corrupted settings");
    }
    
    // تنظیمات زمان
    configTime(3 * 3600 + 1800, 0, "pool.ntp.org");
    
    // اتصال به WiFi
    wifiConnectStartTime = millis();
    if (strlen(settings.ssid) > 0) {
        Serial.println("Attempting to connect to WiFi: " + String(settings.ssid));
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.setTextSize(2);
        tft.setCursor(20, 80);
        tft.println("Connecting to");
        tft.setCursor(40, 110);
        tft.println(settings.ssid);
        
        if (connectToWiFi()) {
            Serial.println("WiFi connected successfully");
            wifiIP = WiFi.localIP().toString();
            wifiSSID = WiFi.SSID();
            
            // فعال کردن دسترسی به پنل برای 1 ثانیه
            panelAccessEnabled = true;
            panelAccessStartTime = millis();
            Serial.println("Panel access enabled for 1 second");
        } else {
            Serial.println("WiFi failed, starting AP");
            startAPMode();
        }
    } else {
        Serial.println("No WiFi credentials, starting AP");
        startAPMode();
    }
    
    // راه‌اندازی وب سرور
    setupWebServer();
    
    // نمایش وضعیت اولیه
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setTextSize(3);
    tft.setCursor(30, 100);
    tft.println("System Ready");
    
    Serial.println("Setup completed");
    
    // تست نمایش IP
    delay(2000);
    if (isConnectedToWiFi) {
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        tft.setTextSize(2);
        tft.setCursor(10, 50);
        tft.println("WiFi Connected!");
        tft.setTextSize(1);
        tft.setCursor(10, 90);
        tft.println("SSID: " + wifiSSID);
        tft.setCursor(10, 110);
        tft.println("IP: " + wifiIP);
        
        // نمایش زمان دسترسی به پنل
        tft.setCursor(10, 140);
        tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        tft.println("Panel access: 1 second");
        
        delay(1000);
    }
}

// ===== LOOP =====
void loop() {
    server.handleClient();
    
    // مدیریت زمان دسترسی به پنل
    if (panelAccessEnabled && millis() - panelAccessStartTime > PANEL_ACCESS_WINDOW) {
        panelAccessEnabled = false;
        Serial.println("Panel access disabled");
    }
    
    // بروزرسانی زمان
    static unsigned long lastTimeUpdate = 0;
    if (millis() - lastTimeUpdate > 60000) {
        lastTimeUpdate = millis();
        updateDateTime();
    }
    
    // بروزرسانی داده‌ها هر 15 ثانیه
    if (isConnectedToWiFi && millis() - lastDataUpdate > DATA_UPDATE_INTERVAL) {
        lastDataUpdate = millis();
        
        // دریافت داده‌های هر دو مد
        String entryData = getEntryModeData();
        String exitData = getExitModeData();
        
        if (entryData != "{}") parseEntryModeData(entryData);
        if (exitData != "{}") parseExitModeData(exitData);
        
        // بررسی الرت‌ها
        checkAlerts();
    }
    
    // چرخش رنگ RGB1 هر 23 ثانیه
    if (millis() - lastRGB1Cycle > RGB1_CYCLE_INTERVAL) {
        lastRGB1Cycle = millis();
        updateRGB1Cycle();
    }
    
    // بروزرسانی گرادیان RGB2 هر 5 ثانیه
    if (millis() - lastRGB2Update > RGB2_UPDATE_INTERVAL) {
        lastRGB2Update = millis();
        updateRGB2Gradient();
    }
    
    // تغییر خودکار صفحه هر 10 ثانیه
    if (millis() - lastPageChange > PAGE_CHANGE_INTERVAL) {
        lastPageChange = millis();
        currentPage = (currentPage + 1) % TOTAL_PAGES;
        lastDisplayUpdate = 0; // فوراً صفحه را بروزرسانی کن
    }
    
    // بروزرسانی نمایشگر هر 5 ثانیه یا پس از تغییر صفحه
    if (millis() - lastDisplayUpdate > DISPLAY_UPDATE_INTERVAL || lastDisplayUpdate == 0) {
        lastDisplayUpdate = millis();
        updateTFTDisplay();
    }
    
    // مدیریت نمایش الرت
    if (showingAlert) {
        if (millis() - alertStartTime > ALERT_DISPLAY_TIME) {
            showingAlert = false;
            lastDisplayUpdate = 0; // فوراً به نمایش عادی برگرد
        }
    }
    
    // پیشگیری از گیر کردن در مد ۱
    static unsigned long mode1SafetyTimer = 0;
    if (currentPage == 1 && millis() - mode1SafetyTimer > 30000) { // 30 ثانیه
        currentPage = (currentPage + 1) % TOTAL_PAGES;
        lastDisplayUpdate = 0;
        mode1SafetyTimer = millis();
    }
    
    delay(10);
}

// ===== TFT FUNCTIONS =====
void setupTFT() {
    tft.init();
    tft.setRotation(1); // 90 درجه در جهت عقربه‌های ساعت (حالت اصلی)
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    
    Serial.println("TFT display initialized (240x240, Rotation 90°)");
}

void drawHeader(String title, uint16_t color) {
    tft.fillRect(0, 0, 240, 25, TFT_BLACK);
    tft.setTextColor(color, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(5, 5);
    tft.println(title);
    
    // خط جداکننده
    tft.drawLine(0, 27, 240, 27, TFT_GRAY);
}

void updateTFTDisplay() {
    if (showingAlert) {
        // اگر الرت فعال است، نمایش الرت
        return;
    }
    
    tft.fillScreen(TFT_BLACK);
    
    switch(currentPage) {
        case 0:
            drawStatusPage();
            break;
        case 1:
            drawMode1Page();
            break;
        case 2:
            drawMode2Page();
            break;
        case 3:
            drawHistoryPage();
            break;
        default:
            currentPage = 0;
            drawStatusPage();
            break;
    }
    
    // نمایش شماره صفحه و وضعیت WiFi در پایین
    tft.setTextSize(1);
    tft.setTextColor(TFT_GRAY, TFT_BLACK);
    
    // نمایش شماره صفحه
    tft.setCursor(100, 220);
    tft.printf("Page %d/%d", currentPage + 1, TOTAL_PAGES);
    
    // نمایش وضعیت WiFi
    tft.setCursor(5, 220);
    if (isConnectedToWiFi) {
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.print("WiFi");
    } else if (apModeActive) {
        tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        tft.print("AP");
    } else {
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.print("No WiFi");
    }
    
    // نمایش زمان در گوشه راست بالا
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(140, 5);
    if (currentDateTime.length() > 10) {
        String timeOnly = currentDateTime.substring(11, 16); // فقط ساعت و دقیقه
        tft.print(timeOnly);
    }
}

void drawStatusPage() {
    drawHeader("SYSTEM STATUS", TFT_CYAN);
    
    tft.setTextSize(1);
    
    // وضعیت WiFi
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(10, 35);
    if (isConnectedToWiFi) {
        tft.print("Connected: ");
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.println(wifiSSID);
        
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.setCursor(10, 50);
        tft.print("IP: ");
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        tft.println(wifiIP);
    } else if (apModeActive) {
        tft.print("AP Mode: ");
        tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        tft.println("ESP32-Portfolio");
        
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.setCursor(10, 50);
        tft.print("IP: ");
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        tft.println("192.168.4.1");
    } else {
        tft.print("WiFi: ");
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.println("Disconnected");
    }
    
    // جداکننده
    tft.drawLine(10, 70, 230, 70, TFT_GRAY);
    
    // خلاصه مد ۱
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setCursor(10, 80);
    tft.print("MODE 1: ");
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.printf("%d positions", entryCryptoCount);
    
    if (entryCryptoCount > 0) {
        tft.setCursor(10, 95);
        tft.printf("P/L: ");
        if (entryPortfolio.totalPnlPercent >= 0) {
            tft.setTextColor(TFT_GREEN, TFT_BLACK);
        } else {
            tft.setTextColor(TFT_RED, TFT_BLACK);
        }
        tft.printf("%.1f%%", entryPortfolio.totalPnlPercent);
    }
    
    // خلاصه مد ۲
    tft.setTextColor(TFT_BLUE, TFT_BLACK);
    tft.setCursor(10, 120);
    tft.print("MODE 2: ");
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.printf("%d positions", exitCryptoCount);
    
    if (exitCryptoCount > 0) {
        tft.setCursor(10, 135);
        tft.printf("Threshold: ");
        tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        tft.printf("%.1f%%", settings.exitAlertPercent);
    }
    
    // تاریخچه الرت‌ها
    tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
    tft.setCursor(10, 160);
    tft.printf("Alerts: %d", alertHistoryCount);
    
    // زمان آخرین بروزرسانی
    tft.setTextColor(TFT_GRAY, TFT_BLACK);
    tft.setCursor(10, 180);
    tft.print("Last update: ");
    if (lastDataUpdate > 0) {
        unsigned long secondsAgo = (millis() - lastDataUpdate) / 1000;
        tft.printf("%ds ago", secondsAgo);
    } else {
        tft.print("Never");
    }
}

void drawMode1Page() {
    drawHeader("MODE 1 - ENTRY", TFT_GREEN);
    
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    
    if (entryCryptoCount == 0) {
        tft.setCursor(70, 100);
        tft.println("No data");
        return;
    }
    
    // نمایش ۶ موقعیت اول با فونت بزرگتر
    int y = 35;
    int displayCount = minInt(6, entryCryptoCount);
    
    for (int i = 0; i < displayCount; i++) {
        CryptoPosition* pos = &entryCryptoData[i];
        
        // نماد کوتاه شده
        String symbol = getShortSymbol(pos->symbol);
        
        tft.setCursor(10, y);
        tft.print(symbol);
        
        // موقعیت
        tft.setCursor(70, y);
        tft.print(pos->isLong ? "LONG" : "SHORT");
        
        // درصد تغییر با رنگ مناسب
        tft.setCursor(130, y);
        if (pos->changePercent >= 0) {
            tft.setTextColor(TFT_GREEN, TFT_BLACK);
        } else {
            tft.setTextColor(TFT_RED, TFT_BLACK);
        }
        tft.printf("%5.1f%%", pos->changePercent);
        
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        y += 20;
    }
    
    // خلاصه در پایین
    tft.drawLine(10, 160, 230, 160, TFT_GRAY);
    
    tft.setCursor(10, 170);
    tft.printf("Total: $%.0f", entryPortfolio.totalCurrentValue);
    
    tft.setCursor(120, 170);
    tft.printf("Pos: %d", entryCryptoCount);
    
    tft.setCursor(10, 190);
    tft.print("P/L: ");
    if (entryPortfolio.totalPnlPercent >= 0) {
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
    } else {
        tft.setTextColor(TFT_RED, TFT_BLACK);
    }
    tft.printf("%.1f%%", entryPortfolio.totalPnlPercent);
}

void drawMode2Page() {
    drawHeader("MODE 2 - EXIT", TFT_BLUE);
    
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    
    if (exitCryptoCount == 0) {
        tft.setCursor(70, 100);
        tft.println("No data");
        return;
    }
    
    // نمایش ۶ موقعیت اول
    int y = 35;
    int displayCount = minInt(6, exitCryptoCount);
    
    for (int i = 0; i < displayCount; i++) {
        CryptoPosition* pos = &exitCryptoData[i];
        
        // نماد کوتاه شده
        String symbol = getShortSymbol(pos->symbol);
        
        tft.setCursor(10, y);
        tft.print(symbol);
        
        // موقعیت
        tft.setCursor(70, y);
        tft.print(pos->isLong ? "LONG" : "SHORT");
        
        // محاسبه تغییر از آخرین الرت
        float changeFromLast = 0;
        if (pos->exitAlertLastPrice > 0 && pos->currentPrice > 0) {
            changeFromLast = ((pos->currentPrice - pos->exitAlertLastPrice) / 
                             pos->exitAlertLastPrice) * 100;
            
            // محدود کردن مقادیر
            changeFromLast = constrain(changeFromLast, -999.9, 999.9);
        } else {
            changeFromLast = 0;
            if (pos->currentPrice > 0) {
                pos->exitAlertLastPrice = pos->currentPrice;
            }
        }
        
        tft.setCursor(130, y);
        if (changeFromLast >= 0) {
            tft.setTextColor(TFT_GREEN, TFT_BLACK);
        } else {
            tft.setTextColor(TFT_RED, TFT_BLACK);
        }
        tft.printf("%5.1f%%", changeFromLast);
        
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        y += 20;
    }
    
    // آستانه‌ها و خلاصه
    tft.drawLine(10, 160, 230, 160, TFT_GRAY);
    
    tft.setCursor(10, 170);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.printf("T1: %.1f%%", settings.exitAlertPercent);
    
    tft.setCursor(80, 170);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.printf("T2: %.1f%%", settings.exitAlertPercent * 2);
    
    tft.setCursor(150, 170);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.printf("Active: %d", exitCryptoCount);
    
    tft.setCursor(10, 190);
    tft.print("Total P/L: ");
    if (exitPortfolio.totalPnlPercent >= 0) {
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
    } else {
        tft.setTextColor(TFT_RED, TFT_BLACK);
    }
    tft.printf("%.1f%%", exitPortfolio.totalPnlPercent);
}

void drawHistoryPage() {
    drawHeader("ALERT HISTORY", TFT_MAGENTA);
    
    tft.setTextSize(1);
    
    if (alertHistoryCount == 0) {
        tft.setCursor(70, 100);
        tft.println("No alerts");
        return;
    }
    
    // نمایش ۵ الرت آخر
    int y = 35;
    int startIdx = (alertHistoryCount - 5 > 0) ? alertHistoryCount - 5 : 0;
    
    for (int i = startIdx; i < alertHistoryCount; i++) {
        AlertHistory* alert = &alertHistory[i];
        
        // انتخاب رنگ بر اساس نوع الرت
        if (alert->alertMode == 0) { // مد ۱
            if (alert->isLong) {
                tft.setTextColor(TFT_GREEN, TFT_BLACK);
            } else {
                tft.setTextColor(TFT_RED, TFT_BLACK);
            }
        } else { // مد ۲
            if (alert->isProfit) {
                tft.setTextColor(TFT_CYAN, TFT_BLACK);
            } else {
                tft.setTextColor(TFT_ORANGE, TFT_BLACK);
            }
        }
        
        // نماد و زمان
        tft.setCursor(10, y);
        tft.print(alert->symbol);
        
        tft.setCursor(70, y);
        tft.print(alert->timeString);
        
        // درصد و نوع
        tft.setCursor(140, y);
        tft.printf("%.1f%%", alert->pnlPercent);
        
        tft.setCursor(190, y);
        if (alert->alertMode == 0) {
            tft.print(alert->isLong ? "L" : "S");
        } else {
            tft.print(alert->isProfit ? "P" : "L");
        }
        
        y += 25;
    }
    
    // خلاصه
    tft.drawLine(10, 165, 230, 165, TFT_GRAY);
    
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(10, 175);
    tft.printf("Total Alerts: %d", alertHistoryCount);
    
    tft.setCursor(10, 195);
    tft.print("M1: ");
    int mode1Count = 0;
    for (int i = 0; i < alertHistoryCount; i++) {
        if (alertHistory[i].alertMode == 0) mode1Count++;
    }
    tft.printf("%d | M2: %d", mode1Count, alertHistoryCount - mode1Count);
}

void drawAlert(String title, String message) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setCursor(20, 20);
    tft.println(title);
    
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    
    // شکستن متن به چند خط
    int y = 60;
    int lineLength = 28; // تعداد کاراکتر در هر خط
    for (int i = 0; i < message.length(); i += lineLength) {
        int endIdx = minInt(i + lineLength, message.length());
        String line = message.substring(i, endIdx);
        tft.setCursor(10, y);
        tft.println(line);
        y += 20;
    }
    
    // نمایش زمان در پایین
    tft.setTextColor(TFT_GRAY, TFT_BLACK);
    tft.setCursor(10, 200);
    if (currentDateTime.length() > 10) {
        tft.print(currentDateTime.substring(11, 19));
    }
}

String getShortSymbol(const char* symbol) {
    String s = String(symbol);
    if (s.endsWith("_USDT")) s = s.substring(0, s.length() - 5);
    if (s.endsWith("USDT")) s = s.substring(0, s.length() - 4);
    
    int maxLength = 6;
    if (s.length() > maxLength) {
        s = s.substring(0, maxLength);
    }
    return s;
}

// ===== LED FUNCTIONS =====
void setupLEDs() {
    pinMode(LED_GREEN_1, OUTPUT);
    pinMode(LED_RED_1, OUTPUT);
    pinMode(LED_GREEN_2, OUTPUT);
    pinMode(LED_RED_2, OUTPUT);
    
    digitalWrite(LED_GREEN_1, LOW);
    digitalWrite(LED_RED_1, LOW);
    digitalWrite(LED_GREEN_2, LOW);
    digitalWrite(LED_RED_2, LOW);
    
    Serial.println("Normal LEDs initialized");
}

void setupRGBLEDs() {
    pinMode(RGB1_RED, OUTPUT);
    pinMode(RGB1_GREEN, OUTPUT);
    pinMode(RGB1_BLUE, OUTPUT);
    
    pinMode(RGB2_RED, OUTPUT);
    pinMode(RGB2_GREEN, OUTPUT);
    pinMode(RGB2_BLUE, OUTPUT);
    
    analogWrite(RGB1_RED, 0);
    analogWrite(RGB1_GREEN, 0);
    analogWrite(RGB1_BLUE, 0);
    
    analogWrite(RGB2_RED, 0);
    analogWrite(RGB2_GREEN, 0);
    analogWrite(RGB2_BLUE, 0);
    
    Serial.println("RGB LEDs initialized");
}

void updateLEDs() {
    if (!settings.ledEnabled) {
        turnOffAllLEDs();
        return;
    }
    
    // بررسی مد ۱
    bool mode1Alert = false;
    bool mode1Long = false;
    
    for (int i = 0; i < entryCryptoCount; i++) {
        if (entryCryptoData[i].alerted) {
            mode1Alert = true;
            if (entryCryptoData[i].isLong) {
                mode1Long = true;
            } else {
                mode1Long = false;
            }
            break;
        }
    }
    
    if (mode1Alert) {
        digitalWrite(LED_GREEN_1, mode1Long ? HIGH : LOW);
        digitalWrite(LED_RED_1, mode1Long ? LOW : HIGH);
    } else {
        digitalWrite(LED_GREEN_1, LOW);
        digitalWrite(LED_RED_1, LOW);
    }
    
    // بررسی مد ۲
    bool mode2Alert = false;
    bool mode2Profit = false;
    
    for (int i = 0; i < exitCryptoCount; i++) {
        if (exitCryptoData[i].exitAlerted) {
            mode2Alert = true;
            // برای سادگی، فرض می‌کنیم افزایش قیمت = سود
            if (exitCryptoData[i].changePercent > 0) {
                mode2Profit = true;
            } else {
                mode2Profit = false;
            }
            break;
        }
    }
    
    if (mode2Alert) {
        digitalWrite(LED_GREEN_2, mode2Profit ? HIGH : LOW);
        digitalWrite(LED_RED_2, mode2Profit ? LOW : HIGH);
    } else {
        digitalWrite(LED_GREEN_2, LOW);
        digitalWrite(LED_RED_2, LOW);
    }
}

void updateRGB1Cycle() {
    // چرخش بین رنگ‌های الرت‌های تاریخچه
    static int colorIndex = 0;
    
    // اگر تاریخچه‌ای وجود ندارد، خاموش بماند
    if (alertHistoryCount == 0) {
        setRGB1Color(0, 0, 0);
        return;
    }
    
    // محاسبه رنگ بر اساس الرت‌های اخیر
    int recentCount = minInt(10, alertHistoryCount);
    int startIdx = alertHistoryCount - recentCount;
    if (startIdx < 0) startIdx = 0;
    
    // انتخاب الرت برای این چرخش
    AlertHistory* alert = &alertHistory[(startIdx + colorIndex) % alertHistoryCount];
    
    // تنظیم رنگ بر اساس نوع الرت
    if (alert->alertMode == 0) { // مد ۱
        if (alert->isLong) {
            // سبز برای LONG
            setRGB1Color(0, 255, 0);
        } else {
            // قرمز برای SHORT
            setRGB1Color(255, 0, 0);
        }
    } else { // مد ۲
        if (alert->isProfit) {
            // آبی برای PROFIT
            setRGB1Color(0, 0, 255);
        } else {
            // نارنجی برای LOSS
            setRGB1Color(255, 165, 0);
        }
    }
    
    colorIndex = (colorIndex + 1) % recentCount;
}

void updateRGB2Gradient() {
    if (exitCryptoCount == 0) {
        setRGB2Color(0, 0, 0);
        return;
    }
    
    // پیدا کردن بیشترین تغییر درصد
    float maxChange = 0;
    float currentChange = 0;
    
    for (int i = 0; i < exitCryptoCount; i++) {
        CryptoPosition* pos = &exitCryptoData[i];
        
        if (pos->exitAlertLastPrice > 0 && pos->currentPrice > 0) {
            float change = ((pos->currentPrice - pos->exitAlertLastPrice) / 
                           pos->exitAlertLastPrice) * 100;
            
            if (fabs(change) > fabs(maxChange)) {
                maxChange = change;
            }
            
            // برای تست، اولین موقعیت را استفاده می‌کنیم
            if (i == 0) {
                currentChange = change;
            }
        }
    }
    
    // محاسبه رنگ بر اساس گرادیان
    int r, g, b;
    
    if (currentChange > 0) {
        // تغییر مثبت
        if (currentChange >= settings.exitAlertPercent * 2) {
            // بالای آستانه دوم - آبی تیره
            r = 0;
            g = 0;
            b = minInt(255, (int)(200 + (currentChange * 2)));
        } else if (currentChange >= settings.exitAlertPercent) {
            // بین دو آستانه - گرادیان سبز به آبی
            float ratio = (currentChange - settings.exitAlertPercent) / settings.exitAlertPercent;
            ratio = constrain(ratio, 0.0, 1.0); // محدود کردن به بازه 0-1
            r = 0;
            g = max(0, (int)(255 * (1 - ratio)));
            b = minInt(255, (int)(255 * ratio));
        } else {
            // زیر آستانه اول - سبز
            float ratio = currentChange / settings.exitAlertPercent;
            ratio = constrain(ratio, 0.0, 1.0); // محدود کردن به بازه 0-1
            r = 0;
            g = minInt(255, (int)(200 + (ratio * 55)));
            b = 0;
        }
    } else {
        // تغییر منفی
        currentChange = fabs(currentChange);
        
        if (currentChange >= settings.exitAlertPercent * 2) {
            // بالای آستانه دوم - قرمز تیره
            r = minInt(255, (int)(200 + (currentChange * 2)));
            g = 0;
            b = 0;
        } else if (currentChange >= settings.exitAlertPercent) {
            // بین دو آستانه - گرادیان زرد به قرمز
            float ratio = (currentChange - settings.exitAlertPercent) / settings.exitAlertPercent;
            ratio = constrain(ratio, 0.0, 1.0); // محدود کردن به بازه 0-1
            r = minInt(255, (int)(255 * ratio));
            g = max(0, (int)(255 * (1 - ratio)));
            b = 0;
        } else {
            // زیر آستانه اول - زرد
            float ratio = currentChange / settings.exitAlertPercent;
            ratio = constrain(ratio, 0.0, 1.0); // محدود کردن به بازه 0-1
            r = minInt(255, (int)(200 * ratio));
            g = minInt(255, (int)(200 * ratio));
            b = 0;
        }
    }
    
    setRGB2Color(r, g, b);
}

void setRGB1Color(int r, int g, int b) {
    analogWrite(RGB1_RED, r);
    analogWrite(RGB1_GREEN, g);
    analogWrite(RGB1_BLUE, b);
}

void setRGB2Color(int r, int g, int b) {
    analogWrite(RGB2_RED, r);
    analogWrite(RGB2_GREEN, g);
    analogWrite(RGB2_BLUE, b);
}

void turnOffAllLEDs() {
    digitalWrite(LED_GREEN_1, LOW);
    digitalWrite(LED_RED_1, LOW);
    digitalWrite(LED_GREEN_2, LOW);
    digitalWrite(LED_RED_2, LOW);
    
    setRGB1Color(0, 0, 0);
    setRGB2Color(0, 0, 0);
}

void triggerAlertLED(int mode, bool isProfit, float pnlPercent) {
    if (!settings.ledEnabled) return;
    
    if (mode == 1) {
        // مد ۱: الگوی چشمک زن
        for (int i = 0; i < 5; i++) {
            if (isProfit) {
                digitalWrite(LED_GREEN_1, HIGH);
                delay(100);
                digitalWrite(LED_GREEN_1, LOW);
            } else {
                digitalWrite(LED_RED_1, HIGH);
                delay(100);
                digitalWrite(LED_RED_1, LOW);
            }
            delay(100);
        }
    } else {
        // مد ۲: الگوی چشمک زن
        for (int i = 0; i < 5; i++) {
            if (isProfit) {
                digitalWrite(LED_GREEN_2, HIGH);
                delay(100);
                digitalWrite(LED_GREEN_2, LOW);
            } else {
                digitalWrite(LED_RED_2, HIGH);
                delay(100);
                digitalWrite(LED_RED_2, LOW);
            }
            delay(100);
        }
    }
}

// ===== BUZZER FUNCTIONS =====
void setupBuzzer() {
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
    
    Serial.println("Buzzer initialized");
}

void playTone(int frequency, int duration, int volumePercent) {
    if (!settings.buzzerEnabled || settings.buzzerVolume == 0) {
        return;
    }
    
    int periodMicros = 1000000 / frequency;
    int halfPeriod = periodMicros / 2;
    
    long startTime = micros();
    long endTime = startTime + (duration * 1000L);
    
    while (micros() < endTime) {
        digitalWrite(BUZZER_PIN, HIGH);
        delayMicroseconds(halfPeriod);
        digitalWrite(BUZZER_PIN, LOW);
        delayMicroseconds(halfPeriod);
    }
}

void playAlertTone(bool isEntryMode, bool isLong, bool isSevere, bool isProfit) {
    if (!settings.buzzerEnabled) return;
    
    if (isEntryMode) {
        if (isLong) {
            // LONG: دو بوق
            playTone(400, 200, settings.buzzerVolume * 5);
            delay(150);
            playTone(400, 200, settings.buzzerVolume * 5);
        } else {
            // SHORT: یک بوق
            playTone(600, 300, settings.buzzerVolume * 5);
        }
    } else {
        if (isProfit) {
            // PROFIT: دو بوق
            playTone(500, 200, settings.buzzerVolume * 5);
            delay(150);
            playTone(500, 200, settings.buzzerVolume * 5);
        } else {
            // LOSS: یک بوق
            playTone(300, 300, settings.buzzerVolume * 5);
        }
    }
}

void playSuccessTone() {
    playTone(600, 150, settings.buzzerVolume * 5);
    delay(100);
    playTone(800, 200, settings.buzzerVolume * 5);
}

void playErrorTone() {
    playTone(300, 200, settings.buzzerVolume * 5);
    delay(100);
    playTone(200, 250, settings.buzzerVolume * 5);
}

// ===== SETTINGS FUNCTIONS =====
void initializeSettings() {
    memset(&settings, 0, sizeof(SystemSettings));
    
    settings.alertThreshold = -10.0;
    settings.severeAlertThreshold = -20.0;
    settings.portfolioAlertThreshold = -10.0;
    settings.exitAlertPercent = 5.0;
    
    settings.buzzerVolume = 10;
    settings.buzzerEnabled = true;
    settings.ledEnabled = true;
    
    strcpy(settings.entryPortfolio, "Main");
    strcpy(settings.exitPortfolio, "ActivePositions");
    
    settings.magicNumber = 123;
    settings.configured = false;
    settings.firstBoot = millis();
    settings.bootCount = 1;
    settings.totalUptime = 0;
}

bool loadSettings() {
    EEPROM.begin(EEPROM_SIZE);
    EEPROM.get(0, settings);
    EEPROM.end();
    
    if (settings.magicNumber != 123) {
        initializeSettings();
        saveSettings();
        return false;
    }
    
    return true;
}

bool saveSettings() {
    settings.magicNumber = 123;
    EEPROM.begin(EEPROM_SIZE);
    EEPROM.put(0, settings);
    bool success = EEPROM.commit();
    EEPROM.end();
    
    return success;
}

// ===== WIFI FUNCTIONS =====
bool connectToWiFi() {
    if (strlen(settings.ssid) == 0) {
        return false;
    }
    
    Serial.println("Connecting to WiFi: " + String(settings.ssid));
    
    WiFi.disconnect(true);
    delay(1000);
    WiFi.mode(WIFI_STA);
    
    // افزایش قدرت انتقال WiFi
    WiFi.setTxPower(WIFI_POWER_19_5dBm); // حداکثر قدرت
    
    WiFi.begin(settings.ssid, settings.password);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40) { // افزایش زمان انتظار
        delay(500);
        attempts++;
        Serial.print(".");
        
        // نمایش وضعیت روی TFT
        if (attempts % 10 == 0) {
            tft.fillRect(0, 140, 240, 20, TFT_BLACK);
            tft.setTextColor(TFT_YELLOW, TFT_BLACK);
            tft.setCursor(50, 140);
            tft.printf("Attempt %d", attempts/2);
        }
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        isConnectedToWiFi = true;
        apModeActive = false;
        wifiIP = WiFi.localIP().toString();
        wifiSSID = WiFi.SSID();
        
        Serial.println("\n✅ WiFi Connected!");
        Serial.println("SSID: " + wifiSSID);
        Serial.println("IP: " + wifiIP);
        Serial.println("RSSI: " + String(WiFi.RSSI()) + " dBm");
        
        return true;
    }
    
    Serial.println("\n❌ WiFi Connection Failed!");
    return false;
}

bool startAPMode() {
    WiFi.disconnect(true);
    delay(2000);
    WiFi.mode(WIFI_AP);
    
    if (WiFi.softAP("ESP32-Portfolio", "12345678")) {
        apModeActive = true;
        isConnectedToWiFi = false;
        wifiIP = WiFi.softAPIP().toString();
        wifiSSID = "ESP32-Portfolio";
        
        // فعال کردن دسترسی به پنل در AP Mode
        panelAccessEnabled = true;
        panelAccessStartTime = millis();
        
        Serial.println("✅ AP Mode Started");
        Serial.println("AP SSID: " + wifiSSID);
        Serial.println("AP IP: " + wifiIP);
        Serial.println("Panel access enabled for 1 second");
        return true;
    }
    
    return false;
}

// ===== TIME FUNCTIONS =====
void updateDateTime() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 100)) {
        currentDateTime = "No Time";
        return;
    }
    
    char timeString[25];
    strftime(timeString, sizeof(timeString), "%Y/%m/%d %H:%M:%S", &timeinfo);
    currentDateTime = String(timeString);
}

// ===== FORMATTING FUNCTIONS =====
String formatPrice(float price) {
    if (price <= 0) return "0.00";
    
    if (price >= 1000) return String(price, 2);
    else if (price >= 1) return String(price, 4);
    else if (price >= 0.01) return String(price, 6);
    else if (price >= 0.0001) return String(price, 8);
    else return String(price, 10);
}

String formatPercent(float percent) {
    return (percent >= 0 ? "+" : "") + String(percent, 1) + "%";
}

// ===== API FUNCTIONS =====
String getEntryModeData() {
    if (!isConnectedToWiFi || strlen(settings.server) == 0) {
        return "{}";
    }
    
    String url = String(settings.server) + "/api/device/portfolio/" + 
                String(settings.username) + "?portfolio_name=" + 
                String(settings.entryPortfolio);
    
    http.begin(url);
    
    String authString = String(settings.username) + ":" + String(settings.userpass);
    String encodedAuth = base64Encode(authString);
    http.addHeader("Authorization", "Basic " + encodedAuth);
    http.setTimeout(5000);
    
    int httpCode = http.GET();
    String result = "{}";
    
    if (httpCode == 200) {
        result = http.getString();
        Serial.println("Entry mode data received");
    } else {
        Serial.println("Entry mode HTTP error: " + String(httpCode));
    }
    
    http.end();
    return result;
}

String getExitModeData() {
    if (!isConnectedToWiFi || strlen(settings.server) == 0) {
        return "{}";
    }
    
    String url = String(settings.server) + "/api/device/portfolio/" + 
                String(settings.username) + "?portfolio_name=" + 
                String(settings.exitPortfolio);
    
    http.begin(url);
    
    String authString = String(settings.username) + ":" + String(settings.userpass);
    String encodedAuth = base64Encode(authString);
    http.addHeader("Authorization", "Basic " + encodedAuth);
    http.setTimeout(5000);
    
    int httpCode = http.GET();
    String result = "{}";
    
    if (httpCode == 200) {
        result = http.getString();
        Serial.println("Exit mode data received");
    } else {
        Serial.println("Exit mode HTTP error: " + String(httpCode));
    }
    
    http.end();
    return result;
}

String base64Encode(String data) {
    const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    String result = "";
    int i = 0;
    int len = data.length();
    
    while (i < len) {
        char b1 = data[i++];
        char b2 = i < len ? data[i++] : 0;
        char b3 = i < len ? data[i++] : 0;
        
        result += chars[b1 >> 2];
        result += chars[((b1 & 0x3) << 4) | (b2 >> 4)];
        result += chars[((b2 & 0xF) << 2) | (b3 >> 6)];
        result += chars[b3 & 0x3F];
    }
    
    if (len % 3 == 1) {
        result.setCharAt(result.length() - 2, '=');
        result.setCharAt(result.length() - 1, '=');
    } else if (len % 3 == 2) {
        result.setCharAt(result.length() - 1, '=');
    }
    
    return result;
}

void parseEntryModeData(String jsonData) {
    DynamicJsonDocument doc(JSON_BUFFER_SIZE);
    DeserializationError error = deserializeJson(doc, jsonData);
    
    if (error) {
        Serial.println("Entry mode JSON parse error: " + String(error.c_str()));
        return;
    }
    
    if (!doc.containsKey("portfolio")) {
        Serial.println("No portfolio in entry mode data");
        return;
    }
    
    JsonArray portfolioArray = doc["portfolio"];
    entryCryptoCount = minInt((int)portfolioArray.size(), MAX_CRYPTO);
    
    for (int i = 0; i < entryCryptoCount; i++) {
        JsonObject position = portfolioArray[i];
        
        const char* symbol = position["symbol"] | "UNKNOWN";
        strncpy(entryCryptoData[i].symbol, symbol, 15);
        entryCryptoData[i].symbol[15] = '\0';
        
        entryCryptoData[i].changePercent = position["pnl_percent"] | 0.0;
        entryCryptoData[i].currentPrice = position["current_price"] | 0.0;
        entryCryptoData[i].entryPrice = position["entry_price"] | 0.0;
        entryCryptoData[i].quantity = position["quantity"] | 0.0;
        
        // تشخیص موقعیت
        float quantity = position["quantity"] | 0.0;
        entryCryptoData[i].isLong = (quantity > 0);
        
        // نگهداری وضعیت الرت قبلی
        bool foundPrevious = false;
        for (int j = 0; j < i && j < MAX_CRYPTO; j++) {
            if (strcmp(entryCryptoData[j].symbol, symbol) == 0) {
                entryCryptoData[i].alerted = entryCryptoData[j].alerted;
                entryCryptoData[i].severeAlerted = entryCryptoData[j].severeAlerted;
                entryCryptoData[i].lastAlertTime = entryCryptoData[j].lastAlertTime;
                entryCryptoData[i].lastAlertPrice = entryCryptoData[j].lastAlertPrice;
                foundPrevious = true;
                break;
            }
        }
        
        if (!foundPrevious) {
            entryCryptoData[i].alerted = false;
            entryCryptoData[i].severeAlerted = false;
            entryCryptoData[i].lastAlertTime = 0;
            entryCryptoData[i].lastAlertPrice = 0;
        }
    }
    
    // محاسبه خلاصه
    if (doc.containsKey("summary")) {
        JsonObject summary = doc["summary"];
        
        entryPortfolio.totalInvestment = summary["total_investment"] | 0.0;
        entryPortfolio.totalCurrentValue = summary["total_current_value"] | 0.0;
        entryPortfolio.totalPnl = summary["total_pnl"] | 0.0;
        
        if (entryPortfolio.totalInvestment > 0) {
            entryPortfolio.totalPnlPercent = ((entryPortfolio.totalCurrentValue - entryPortfolio.totalInvestment) / 
                                            entryPortfolio.totalInvestment) * 100;
        } else {
            entryPortfolio.totalPnlPercent = 0.0;
        }
        
        entryPortfolio.totalPositions = entryCryptoCount;
    }
}

void parseExitModeData(String jsonData) {
    DynamicJsonDocument doc(JSON_BUFFER_SIZE);
    DeserializationError error = deserializeJson(doc, jsonData);
    
    if (error) {
        Serial.println("Exit mode JSON parse error: " + String(error.c_str()));
        return;
    }
    
    if (!doc.containsKey("portfolio")) {
        Serial.println("No portfolio in exit mode data");
        return;
    }
    
    JsonArray portfolioArray = doc["portfolio"];
    exitCryptoCount = minInt((int)portfolioArray.size(), MAX_CRYPTO);
    
    for (int i = 0; i < exitCryptoCount; i++) {
        JsonObject position = portfolioArray[i];
        
        const char* symbol = position["symbol"] | "UNKNOWN";
        strncpy(exitCryptoData[i].symbol, symbol, 15);
        exitCryptoData[i].symbol[15] = '\0';
        
        exitCryptoData[i].changePercent = position["pnl_percent"] | 0.0;
        exitCryptoData[i].currentPrice = position["current_price"] | 0.0;
        exitCryptoData[i].entryPrice = position["entry_price"] | 0.0;
        exitCryptoData[i].quantity = position["quantity"] | 0.0;
        
        // تشخیص موقعیت
        float quantity = position["quantity"] | 0.0;
        exitCryptoData[i].isLong = (quantity > 0);
        
        // نگهداری وضعیت الرت قبلی
        bool foundPrevious = false;
        for (int j = 0; j < i && j < MAX_CRYPTO; j++) {
            if (strcmp(exitCryptoData[j].symbol, symbol) == 0) {
                exitCryptoData[i].exitAlerted = exitCryptoData[j].exitAlerted;
                exitCryptoData[i].exitAlertLastPrice = exitCryptoData[j].exitAlertLastPrice;
                foundPrevious = true;
                break;
            }
        }
        
        if (!foundPrevious) {
            exitCryptoData[i].exitAlerted = false;
            exitCryptoData[i].exitAlertLastPrice = exitCryptoData[i].currentPrice;
        }
    }
    
    // محاسبه خلاصه
    if (doc.containsKey("summary")) {
        JsonObject summary = doc["summary"];
        
        exitPortfolio.totalInvestment = summary["total_investment"] | 0.0;
        exitPortfolio.totalCurrentValue = summary["total_current_value"] | 0.0;
        exitPortfolio.totalPnl = summary["total_pnl"] | 0.0;
        
        if (exitPortfolio.totalInvestment > 0) {
            exitPortfolio.totalPnlPercent = ((exitPortfolio.totalCurrentValue - exitPortfolio.totalInvestment) / 
                                           exitPortfolio.totalInvestment) * 100;
        } else {
            exitPortfolio.totalPnlPercent = 0.0;
        }
        
        exitPortfolio.totalPositions = exitCryptoCount;
    }
}

void clearCryptoData() {
    entryCryptoCount = 0;
    exitCryptoCount = 0;
    
    for (int i = 0; i < MAX_CRYPTO; i++) {
        entryCryptoData[i].alerted = false;
        exitCryptoData[i].exitAlerted = false;
    }
}

// ===== ALERT FUNCTIONS =====
void checkAlerts() {
    // بررسی الرت‌های مد ۱
    for (int i = 0; i < entryCryptoCount; i++) {
        CryptoPosition* crypto = &entryCryptoData[i];
        
        if (!crypto->alerted && crypto->changePercent <= settings.alertThreshold) {
            bool isSevere = crypto->changePercent <= settings.severeAlertThreshold;
            
            // پخش صدای الرت
            playAlertTone(true, crypto->isLong, isSevere, false);
            
            // فعال کردن LED
            triggerAlertLED(1, false, crypto->changePercent);
            
            // اضافه کردن به تاریخچه
            String message = String(crypto->symbol) + " " + 
                           (crypto->isLong ? "LONG" : "SHORT") + " " +
                           formatPercent(crypto->changePercent);
            
            addAlertToHistory(crypto->symbol, crypto->currentPrice, 
                            crypto->changePercent, crypto->isLong, 
                            isSevere, false, 1, 0, message.c_str());
            
            crypto->alerted = true;
            crypto->severeAlerted = isSevere;
            crypto->lastAlertTime = millis();
            crypto->lastAlertPrice = crypto->currentPrice;
            
            // نمایش الرت روی TFT
            showingAlert = true;
            alertStartTime = millis();
            drawAlert("MODE 1 ALERT", message);
        }
    }
    
    // بررسی الرت‌های مد ۲
    for (int i = 0; i < exitCryptoCount; i++) {
        CryptoPosition* crypto = &exitCryptoData[i];
        
        if (crypto->exitAlertLastPrice == 0) {
            crypto->exitAlertLastPrice = crypto->currentPrice;
            continue;
        }
        
        float priceChangePercent = fabs((crypto->currentPrice - crypto->exitAlertLastPrice) / 
                                       crypto->exitAlertLastPrice * 100);
        
        if (priceChangePercent >= settings.exitAlertPercent && !crypto->exitAlerted) {
            bool isProfit = (crypto->currentPrice > crypto->exitAlertLastPrice);
            
            // پخش صدای الرت
            playAlertTone(false, false, false, isProfit);
            
            // فعال کردن LED
            triggerAlertLED(2, isProfit, crypto->changePercent);
            
            // اضافه کردن به تاریخچه
            String message = String(crypto->symbol) + " " +
                           (isProfit ? "PROFIT" : "LOSS") + " " +
                           String(priceChangePercent, 1) + "%";
            
            addAlertToHistory(crypto->symbol, crypto->currentPrice, 
                            crypto->changePercent, crypto->isLong, 
                            false, isProfit, 2, 1, message.c_str());
            
            crypto->exitAlerted = true;
            crypto->exitAlertLastPrice = crypto->currentPrice;
            
            // نمایش الرت روی TFT
            showingAlert = true;
            alertStartTime = millis();
            drawAlert("MODE 2 ALERT", message);
        }
    }
}

void addAlertToHistory(const char* symbol, float price, float pnlPercent, 
                      bool isLong, bool isSevere, bool isProfit, 
                      byte alertType, byte alertMode, const char* message) {
    if (alertHistoryCount >= MAX_ALERT_HISTORY) {
        // جابجایی الرت‌های قدیمی
        for (int i = 0; i < MAX_ALERT_HISTORY - 1; i++) {
            alertHistory[i] = alertHistory[i + 1];
        }
        alertHistoryCount = MAX_ALERT_HISTORY - 1;
    }
    
    AlertHistory* alert = &alertHistory[alertHistoryCount];
    
    strncpy(alert->symbol, symbol, 15);
    alert->symbol[15] = '\0';
    
    alert->alertPrice = price;
    alert->pnlPercent = pnlPercent;
    alert->isLong = isLong;
    alert->isSevere = isSevere;
    alert->isProfit = isProfit;
    alert->alertType = alertType;
    alert->alertMode = alertMode;
    alert->acknowledged = false;
    
    strncpy(alert->message, message, 63);
    alert->message[63] = '\0';
    
    // زمان فعلی
    time_t now;
    time(&now);
    alert->alertTime = (unsigned long)now * 1000;
    
    // ساخت رشته زمان
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    strftime(alert->timeString, sizeof(alert->timeString), 
             "%m/%d %H:%M", &timeinfo);
    
    alertHistoryCount++;
    
    Serial.println("Alert added to history: " + String(symbol));
}

// ===== WEB SERVER FUNCTIONS =====
bool isPanelAccessAllowed() {
    // در AP Mode همیشه دسترسی مجاز است
    if (apModeActive) {
        return true;
    }
    
    // در WiFi Mode فقط در زمان مشخص شده
    return panelAccessEnabled;
}

void setupWebServer() {
    server.on("/", []() {
        if (!isPanelAccessAllowed()) {
            server.send(403, "text/html", 
                "<html><body style='text-align:center;padding:50px;'>"
                "<h1>Access Denied</h1>"
                "<p>Panel access is currently disabled for security.</p>"
                "<p>Access was only available for 1 second after WiFi connection.</p>"
                "<p>To re-enable access:</p>"
                "<p>1. Power cycle the device</p>"
                "<p>2. Connect within 1 second after WiFi connection</p>"
                "</body></html>");
            return;
        }
        handleRoot();
    });
    
    server.on("/setup", []() {
        if (!isPanelAccessAllowed()) {
            server.send(403, "text/html", "<html><body><h1>Access Denied</h1></body></html>");
            return;
        }
        handleSetup();
    });
    
    server.on("/savewifi", HTTP_POST, []() {
        if (!isPanelAccessAllowed()) {
            server.send(403, "text/html", "<html><body><h1>Access Denied</h1></body></html>");
            return;
        }
        handleSaveWiFi();
    });
    
    server.on("/saveapi", HTTP_POST, []() {
        if (!isPanelAccessAllowed()) {
            server.send(403, "text/html", "<html><body><h1>Access Denied</h1></body></html>");
            return;
        }
        handleSaveAPI();
    });
    
    server.on("/savesettings", HTTP_POST, []() {
        if (!isPanelAccessAllowed()) {
            server.send(403, "text/html", "<html><body><h1>Access Denied</h1></body></html>");
            return;
        }
        handleSaveSettings();
    });
    
    server.on("/refresh", []() {
        if (!isPanelAccessAllowed()) {
            server.send(403, "text/html", "<html><body><h1>Access Denied</h1></body></html>");
            return;
        }
        handleRefresh();
    });
    
    server.on("/testalert", []() {
        if (!isPanelAccessAllowed()) {
            server.send(403, "text/html", "<html><body><h1>Access Denied</h1></body></html>");
            return;
        }
        handleTestAlert();
    });
    
    server.on("/resetalerts", []() {
        if (!isPanelAccessAllowed()) {
            server.send(403, "text/html", "<html><body><h1>Access Denied</h1></body></html>");
            return;
        }
        handleResetAlerts();
    });
    
    server.begin();
    Serial.println("Web server started with access control");
}

void handleRoot() {
    String html = R"=====(
    <!DOCTYPE html>
    <html>
    <head>
        <meta charset="UTF-8">
        <meta name="viewport" content="width=device-width, initial-scale=1.0">
        <title>Dual Mode Portfolio Tracker</title>
        <style>
            body { font-family: Arial; margin: 20px; background: #f0f0f0; }
            .container { max-width: 1200px; margin: auto; background: white; padding: 20px; border-radius: 10px; }
            .header { text-align: center; margin-bottom: 30px; }
            .mode-container { display: flex; gap: 20px; margin-bottom: 30px; }
            .mode-box { flex: 1; padding: 20px; border-radius: 10px; }
            .mode1 { background: #e8f5e9; border: 2px solid #4caf50; }
            .mode2 { background: #e3f2fd; border: 2px solid #2196f3; }
            .history-container { display: flex; gap: 20px; }
            .history-column { flex: 1; }
            .alert-item { padding: 10px; margin: 5px 0; border-radius: 5px; }
            .alert-mode1 { background: #f1f8e9; border-left: 4px solid #4caf50; }
            .alert-mode2 { background: #e8eaf6; border-left: 4px solid #2196f3; }
            .controls { text-align: center; margin-top: 30px; }
            .btn { padding: 10px 20px; margin: 5px; border: none; border-radius: 5px; cursor: pointer; }
            .btn-primary { background: #2196f3; color: white; }
            .btn-success { background: #4caf50; color: white; }
            .btn-warning { background: #ff9800; color: white; }
            .status-bar { background: #f5f5f5; padding: 15px; border-radius: 5px; margin-bottom: 20px; }
            .access-warning { background: #fff3cd; border: 1px solid #ffc107; padding: 10px; margin-bottom: 20px; border-radius: 5px; }
        </style>
    </head>
    <body>
        <div class="container">
            )=====";
    
    // نمایش هشدار دسترسی
    if (panelAccessEnabled) {
        html += "<div class='access-warning'>";
        html += "<strong>⚠️ Warning:</strong> Panel access will be disabled in ";
        html += String((PANEL_ACCESS_WINDOW - (millis() - panelAccessStartTime)) / 1000);
        html += " seconds for security.";
        html += "</div>";
    }
    
    html += R"=====(
            <div class="header">
                <h1>📊 Dual Mode Portfolio Tracker</h1>
                <p>Simultaneous tracking of Entry & Exit modes</p>
            </div>
            
            <div class="status-bar">
                <p><strong>Status:</strong> )=====";
    
    if (isConnectedToWiFi) {
        html += "WiFi Connected | ";
        html += "Time: " + currentDateTime + " | ";
        html += "Mode 1: " + String(entryCryptoCount) + " positions | ";
        html += "Mode 2: " + String(exitCryptoCount) + " positions";
    } else if (apModeActive) {
        html += "AP Mode | Connect to configure";
    } else {
        html += "Disconnected";
    }
    
    html += R"=====(</p>
                <p><strong>Panel Access:</strong> )=====";
    html += panelAccessEnabled ? "Enabled" : "Disabled";
    html += R"=====(</p>
            </div>
            
            <div class="mode-container">
                <div class="mode-box mode1">
                    <h2>📈 Mode 1 - Entry Tracking</h2>
                    <p><strong>Portfolio:</strong> )=====";
    html += String(settings.entryPortfolio);
    html += R"=====(</p>
                    <p><strong>Positions:</strong> )=====";
    html += String(entryCryptoCount);
    html += R"=====(</p>
                    <p><strong>Total Value:</strong> $)=====";
    html += String(entryPortfolio.totalCurrentValue, 2);
    html += R"=====(</p>
                    <p><strong>P/L:</strong> )=====";
    html += formatPercent(entryPortfolio.totalPnlPercent);
    html += R"=====(</p>
                    <p><strong>Alert Threshold:</strong> )=====";
    html += String(settings.alertThreshold, 1) + "% / " + String(settings.severeAlertThreshold, 1) + "%";
    html += R"=====(</p>
                </div>
                
                <div class="mode-box mode2">
                    <h2>📉 Mode 2 - Exit Tracking</h2>
                    <p><strong>Portfolio:</strong> )=====";
    html += String(settings.exitPortfolio);
    html += R"=====(</p>
                    <p><strong>Positions:</strong> )=====";
    html += String(exitCryptoCount);
    html += R"=====(</p>
                    <p><strong>Total Value:</strong> $)=====";
    html += String(exitPortfolio.totalCurrentValue, 2);
    html += R"=====(</p>
                    <p><strong>P/L:</strong> )=====";
    html += formatPercent(exitPortfolio.totalPnlPercent);
    html += R"=====(</p>
                    <p><strong>Exit Threshold:</strong> )=====";
    html += String(settings.exitAlertPercent, 1) + "% price change";
    html += R"=====(</p>
                </div>
            </div>
            
            <div class="history-container">
                <div class="history-column">
                    <h3>Mode 1 Recent Alerts</h3>
                    )=====";
    
    // نمایش الرت‌های مد ۱
    int mode1Count = 0;
    for (int i = alertHistoryCount - 1; i >= 0 && mode1Count < 10; i--) {
        if (alertHistory[i].alertMode == 0) {
            html += "<div class='alert-item alert-mode1'>";
            html += "<strong>" + String(alertHistory[i].symbol) + "</strong> ";
            html += String(alertHistory[i].isLong ? "LONG" : "SHORT") + " ";
            html += formatPercent(alertHistory[i].pnlPercent) + " ";
            html += "<br><small>" + String(alertHistory[i].timeString) + "</small>";
            html += "</div>";
            mode1Count++;
        }
    }
    
    if (mode1Count == 0) {
        html += "<p>No alerts yet</p>";
    }
    
    html += R"=====(
                </div>
                
                <div class="history-column">
                    <h3>Mode 2 Recent Alerts</h3>
                    )=====";
    
    // نمایش الرت‌های مد ۲
    int mode2Count = 0;
    for (int i = alertHistoryCount - 1; i >= 0 && mode2Count < 10; i--) {
        if (alertHistory[i].alertMode == 1) {
            html += "<div class='alert-item alert-mode2'>";
            html += "<strong>" + String(alertHistory[i].symbol) + "</strong> ";
            html += String(alertHistory[i].isProfit ? "PROFIT" : "LOSS") + " ";
            html += formatPercent(alertHistory[i].pnlPercent) + " ";
            html += "<br><small>" + String(alertHistory[i].timeString) + "</small>";
            html += "</div>";
            mode2Count++;
        }
    }
    
    if (mode2Count == 0) {
        html += "<p>No alerts yet</p>";
    }
    
    html += R"=====(
                </div>
            </div>
            
            <div class="controls">
                <button class="btn btn-primary" onclick="location.href='/refresh'">🔄 Refresh Data</button>
                <button class="btn btn-success" onclick="location.href='/testalert'">🔊 Test Alert</button>
                <button class="btn btn-warning" onclick="location.href='/resetalerts'">♻️ Reset Alerts</button>
                <button class="btn" onclick="location.href='/setup'">⚙️ Settings</button>
            </div>
        </div>
    </body>
    </html>
    )=====";
    
    server.send(200, "text/html", html);
}

void handleSetup() {
    String html = R"=====(
    <!DOCTYPE html>
    <html>
    <head>
        <meta charset="UTF-8">
        <meta name="viewport" content="width=device-width, initial-scale=1.0">
        <title>Settings</title>
        <style>
            body { font-family: Arial; margin: 20px; }
            .container { max-width: 600px; margin: auto; }
            .form-group { margin-bottom: 15px; }
            label { display: block; margin-bottom: 5px; }
            input, select { width: 100%; padding: 8px; }
            .btn { padding: 10px 20px; margin: 5px; border: none; border-radius: 5px; cursor: pointer; }
            .btn-primary { background: #2196f3; color: white; }
            .btn-secondary { background: #757575; color: white; }
        </style>
    </head>
    <body>
        <div class="container">
            <h1>Settings</h1>
            
            <form action="/savewifi" method="post">
                <h2>WiFi Settings</h2>
                <div class="form-group">
                    <label>SSID:</label>
                    <input type="text" name="ssid" value=")=====";
    html += String(settings.ssid);
    html += R"=====(" required>
                </div>
                <div class="form-group">
                    <label>Password:</label>
                    <input type="password" name="password" value=")=====";
    html += String(settings.password);
    html += R"=====(" required>
                </div>
                <button type="submit" class="btn btn-primary">Save WiFi</button>
            </form>
            
            <hr>
            
            <form action="/saveapi" method="post">
                <h2>API Settings</h2>
                <div class="form-group">
                    <label>Server URL:</label>
                    <input type="text" name="server" value=")=====";
    html += String(settings.server);
    html += R"=====(" placeholder="http://your-server.com" required>
                </div>
                <div class="form-group">
                    <label>Username:</label>
                    <input type="text" name="username" value=")=====";
    html += String(settings.username);
    html += R"=====(" required>
                </div>
                <div class="form-group">
                    <label>Password:</label>
                    <input type="password" name="userpass" value=")=====";
    html += String(settings.userpass);
    html += R"=====(" required>
                </div>
                <div class="form-group">
                    <label>Entry Portfolio:</label>
                    <input type="text" name="entryportfolio" value=")=====";
    html += String(settings.entryPortfolio);
    html += R"=====(" required>
                </div>
                <div class="form-group">
                    <label>Exit Portfolio:</label>
                    <input type="text" name="exitportfolio" value=")=====";
    html += String(settings.exitPortfolio);
    html += R"=====(" required>
                </div>
                <button type="submit" class="btn btn-primary">Save API</button>
            </form>
            
            <hr>
            
            <form action="/savesettings" method="post">
                <h2>Alert Settings</h2>
                <div class="form-group">
                    <label>Mode 1 Alert Threshold (%):</label>
                    <input type="number" step="0.1" name="alertthreshold" value=")=====";
    html += String(settings.alertThreshold, 1);
    html += R"=====(" required>
                </div>
                <div class="form-group">
                    <label>Mode 1 Severe Threshold (%):</label>
                    <input type="number" step="0.1" name="severethreshold" value=")=====";
    html += String(settings.severeAlertThreshold, 1);
    html += R"=====(" required>
                </div>
                <div class="form-group">
                    <label>Mode 2 Exit Threshold (%):</label>
                    <input type="number" step="0.1" name="exitalertpercent" value=")=====";
    html += String(settings.exitAlertPercent, 1);
    html += R"=====(" required>
                </div>
                <div class="form-group">
                    <label>Buzzer Volume (0-20):</label>
                    <input type="number" min="0" max="20" name="volume" value=")=====";
    html += String(settings.buzzerVolume);
    html += R"=====(" required>
                </div>
                <div class="form-group">
                    <label>
                        <input type="checkbox" name="buzzerenabled" )=====";
    html += settings.buzzerEnabled ? "checked" : "";
    html += R"=====(> Enable Buzzer
                    </label>
                </div>
                <div class="form-group">
                    <label>
                        <input type="checkbox" name="ledenabled" )=====";
    html += settings.ledEnabled ? "checked" : "";
    html += R"=====(> Enable LEDs
                    </label>
                </div>
                <button type="submit" class="btn btn-primary">Save Settings</button>
            </form>
            
            <hr>
            
            <button class="btn btn-secondary" onclick="location.href='/'">Back to Dashboard</button>
        </div>
    </body>
    </html>
    )=====";
    
    server.send(200, "text/html", html);
}

void handleSaveWiFi() {
    if (server.hasArg("ssid") && server.hasArg("password")) {
        strncpy(settings.ssid, server.arg("ssid").c_str(), 31);
        strncpy(settings.password, server.arg("password").c_str(), 63);
        saveSettings();
        
        server.send(200, "text/html", 
            "<html><body style='text-align:center;padding:50px;'>"
            "<h1>WiFi Settings Saved!</h1>"
            "<p>Reconnecting...</p>"
            "<script>setTimeout(() => location.href='/', 2000);</script>"
            "</body></html>");
        
        connectToWiFi();
    }
}

void handleSaveAPI() {
    if (server.hasArg("server") && server.hasArg("username") && 
        server.hasArg("userpass") && server.hasArg("entryportfolio") && 
        server.hasArg("exitportfolio")) {
        
        strncpy(settings.server, server.arg("server").c_str(), 127);
        strncpy(settings.username, server.arg("username").c_str(), 31);
        strncpy(settings.userpass, server.arg("userpass").c_str(), 63);
        strncpy(settings.entryPortfolio, server.arg("entryportfolio").c_str(), 31);
        strncpy(settings.exitPortfolio, server.arg("exitportfolio").c_str(), 31);
        
        saveSettings();
        
        server.send(200, "text/html", 
            "<html><body style='text-align:center;padding:50px;'>"
            "<h1>API Settings Saved!</h1>"
            "<p>Entry Portfolio: " + String(settings.entryPortfolio) + "</p>"
            "<p>Exit Portfolio: " + String(settings.exitPortfolio) + "</p>"
            "<script>setTimeout(() => location.href='/', 2000);</script>"
            "</body></html>");
    }
}

void handleSaveSettings() {
    if (server.hasArg("alertthreshold")) {
        settings.alertThreshold = server.arg("alertthreshold").toFloat();
    }
    if (server.hasArg("severethreshold")) {
        settings.severeAlertThreshold = server.arg("severethreshold").toFloat();
    }
    if (server.hasArg("exitalertpercent")) {
        settings.exitAlertPercent = server.arg("exitalertpercent").toFloat();
    }
    if (server.hasArg("volume")) {
        settings.buzzerVolume = server.arg("volume").toInt();
    }
    
    settings.buzzerEnabled = server.hasArg("buzzerenabled");
    settings.ledEnabled = server.hasArg("ledenabled");
    
    saveSettings();
    
    server.send(200, "text/html", 
        "<html><body style='text-align:center;padding:50px;'>"
        "<h1>Settings Saved!</h1>"
        "<script>setTimeout(() => location.href='/', 2000);</script>"
        "</body></html>");
}

void handleRefresh() {
    if (isConnectedToWiFi && strlen(settings.server) > 0) {
        String entryData = getEntryModeData();
        String exitData = getExitModeData();
        
        parseEntryModeData(entryData);
        parseExitModeData(exitData);
    }
    
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
}

void handleTestAlert() {
    playAlertTone(true, true, false, false);
    delay(500);
    playAlertTone(false, false, false, true);
    
    server.send(200, "text/html", 
        "<html><body style='text-align:center;padding:50px;'>"
        "<h1>Test Alert Played!</h1>"
        "<p>Mode 1: LONG alert (2 beeps)</p>"
        "<p>Mode 2: PROFIT alert (2 beeps)</p>"
        "<a href='/'>Back</a>"
        "</body></html>");
}

void handleResetAlerts() {
    clearCryptoData();
    alertHistoryCount = 0;
    turnOffAllLEDs();
    
    server.send(200, "text/html", 
        "<html><body style='text-align:center;padding:50px;'>"
        "<h1>Alerts Reset!</h1>"
        "<p>All alerts have been cleared</p>"
        "<a href='/'>Back</a>"
        "</body></html>");
}