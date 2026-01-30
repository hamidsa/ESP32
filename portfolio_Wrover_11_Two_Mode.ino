/* ============================================================================
   PORTFOLIO MONITOR - ESP32-WROVER
   Complete Version with Long/Short Alerts & Exit Mode
   Version: 3.8.3 - Enhanced Time Display in Alert History
   Added Separate API Calls for Entry/Exit Modes
   Separate Portfolio Names for Entry/Exit Modes
   ============================================================================ */

#include <SPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <time.h>

// ===== نمایشگر ST7789 =====
#include <TFT_eSPI.h>
TFT_eSPI tft = TFT_eSPI();

// ===== DEFINES =====
#define MAX_ALERT_HISTORY 10
// ==================== HTTP ERROR CODES ====================
// این کدها از کتابخانه HTTPClient گرفته شده‌اند
#define HTTPC_ERROR_CONNECTION_REFUSED  -1
#define HTTPC_ERROR_SEND_HEADER_FAILED  -2
#define HTTPC_ERROR_SEND_PAYLOAD_FAILED -3
#define HTTPC_ERROR_NOT_CONNECTED       -4
#define HTTPC_ERROR_CONNECTION_LOST     -5
#define HTTPC_ERROR_NO_STREAM           -6
#define HTTPC_ERROR_NO_HTTP_SERVER      -7
#define HTTPC_ERROR_TOO_LESS_RAM        -8
#define HTTPC_ERROR_ENCODING            -9
#define HTTPC_ERROR_STREAM_WRITE        -10
#define HTTPC_ERROR_READ_TIMEOUT        -11
// =========================================================
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
    byte alertMode; // اضافه شده: 0 = Entry Mode, 1 = Exit Mode
};

// ===== GLOBAL VARIABLES =====
uint8_t activeAlertCount = 0;

// ===== NTP CONFIG =====
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 3 * 3600 + 1800;   // Iran
const int   daylightOffset_sec = 0;

// ==================== LED CONFIGURATION ====================
// ===== LEDهای RGB =====
#define RGB1_RED    32
#define RGB1_GREEN  33
#define RGB1_BLUE   25

#define RGB2_RED    26
#define RGB2_GREEN  14
#define RGB2_BLUE   12

// ===== LEDهای معمولی =====
#define LED_GREEN_1 22
#define LED_RED_1   21
#define LED_GREEN_2 19
#define LED_RED_2   27

#define DEFAULT_LED_BRIGHTNESS 100

// ==================== BUZZER CONFIGURATION ====================
#define BUZZER_PIN 13
#define DEFAULT_VOLUME 10
#define VOLUME_MIN 1
#define VOLUME_MAX 20
#define VOLUME_OFF 0

// ==================== SYSTEM CONSTANTS ====================
#define MAX_CRYPTO 60
#define DISPLAY_CRYPTO_COUNT 5
#define MAX_WIFI_NETWORKS 5
#define EEPROM_SIZE 4096
#define JSON_BUFFER_SIZE 8192
#define MAX_ALERT_HISTORY 50

// ==================== TIMING CONSTANTS ====================
#define DATA_UPDATE_INTERVAL 30000
#define CRYPTO_CHANGE_INTERVAL 4000
#define DISPLAY_UPDATE_INTERVAL 2000
#define ALERT_DISPLAY_TIME 7000
#define WIFI_CONNECT_TIMEOUT 15000
#define AP_MODE_TIMEOUT 300000 
#define RECONNECT_INTERVAL 60000
#define DEBOUNCE_DELAY 50
#define BUTTON_HOLD_TIME 5000

// ==================== ALERT THRESHOLDS ====================
#define DEFAULT_ALERT_THRESHOLD -10.0
#define DEFAULT_SEVERE_THRESHOLD -20.0
#define PORTFOLIO_ALERT_THRESHOLD -10.0

#define LONG_NORMAL_TONE 400
#define LONG_SEVERE_TONE 250
#define SHORT_NORMAL_TONE 600
#define SHORT_SEVERE_TONE 800
#define PORTFOLIO_ALERT_TONE 450

// ==================== TONE FREQUENCIES ====================
#define RESET_TONE_1 500
#define RESET_TONE_2 400
#define RESET_TONE_3 300

// ==================== RESET BUTTON ====================
#define RESET_BUTTON_PIN 0

// ==================== WIFI NETWORK STRUCTURE ====================
struct WiFiNetwork {
    char ssid[32];
    char password[64];
    bool configured;
    unsigned long lastConnected;
    int connectionAttempts;
    byte priority;
};

// ==================== CRYPTO POSITION STRUCTURE ====================
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

// ==================== PORTFOLIO SUMMARY STRUCTURE ====================
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

// ==================== ACTIVE ALERT FOR BLINKING ====================
struct ActiveAlert {
    String symbol;
    bool isLong;
    bool isProfit;
    float pnlPercent;
    unsigned long firstTriggerTime;
    int blinkCount;
    bool isBlinking;
};

// ==================== SYSTEM SETTINGS STRUCTURE ====================
struct SystemSettings {
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
    
    int tftBrightness;
    int displayTimeout;
    bool showDetails;
    
    float maxPositionSize;
    float maxLeverage;
    float riskPerTrade;
    bool autoHedge;
    
    bool emailAlerts;
    bool pushAlerts;
    bool soundAlerts;
    bool vibrationAlerts;
    
    bool isExitMode;
    float exitAlertPercent;
    bool exitAlertEnabled;
    bool exitAlertBlinkEnabled;
    
    int ledBrightness;
    bool ledEnabled;
    
    byte magicNumber;
    bool configured;
    unsigned long firstBoot;
    int bootCount;
    unsigned long totalUptime;
};

// ==================== GLOBAL VARIABLES ====================
SystemSettings settings;
CryptoPosition cryptoData[MAX_CRYPTO];
CryptoPosition sortedCryptoData[MAX_CRYPTO];
PortfolioSummary portfolio;
AlertHistory alertHistory[MAX_ALERT_HISTORY];
ActiveAlert activeAlerts[10];
int cryptoCount = 0;
int alertHistoryCount = 0;

int currentDisplayIndex = -1;
bool dataSorted = false;

bool isConnectedToWiFi = false;
bool apModeActive = false;
bool tftConnected = false;
bool showingAlert = false;
bool resetInProgress = false;

WebServer server(80);
HTTPClient http;

unsigned long lastDataUpdate = 0;
unsigned long lastCryptoChange = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long lastWiFiCheck = 0;
unsigned long lastAlertCheck = 0;
unsigned long alertStartTime = 0;
unsigned long lastResetButtonPress = 0;
unsigned long lastBlinkTime = 0;

String totalInvestmentStr = "0";
String totalValueStr = "0";
String totalPnlStr = "0";
String portfolioHeader = "";
String currentCryptoDisplay = "";
String currentDateTime = "";

String alertTitle = "";
String alertMessage1 = "";
String alertMessage2 = "";
String alertMessage3 = "";
float alertPrice = 0;

bool resetButtonActive = false;
bool newPortfolioDetected = false;
int newPortfolioConfirmCount = 0;
const int NEW_PORTFOLIO_CONFIRMATIONS = 2;

// LED Status
bool longAlertLEDActive = false;
bool shortAlertLEDActive = false;
bool profitLEDActive = false;
bool lossLEDActive = false;
String longAlertSymbol = "";
String shortAlertSymbol = "";
String profitAlertSymbol = "";
String lossAlertSymbol = "";
float longAlertPnlPercent = 0;
float shortAlertPnlPercent = 0;
float profitAlertPnlPercent = 0;
float lossAlertPnlPercent = 0;

// ==================== FUNCTION DECLARATIONS ====================
void setupTFT();
void updateTFTDisplay();
void manageWiFiConnection();
void showTFTMessage(String line1, String line2, String line3, String line4, String line5 = "");
void setupBuzzer();
void playTone(int frequency, int duration, int volumePercent);
void playLongPositionAlert(bool isSevere);
void playShortPositionAlert(bool isSevere);
void playPortfolioAlert();
void playTestAlertSequence();
void playResetAlertTone();
void playSuccessTone();
void playErrorTone();
void showAlert(String title, String msg1, String msg2, String msg3, bool isLong, bool isSevere, float price);
void initializeSettings();
bool loadSettings();
bool saveSettings();
bool addWiFiNetwork(const char* ssid, const char* password);
bool removeWiFiNetwork(const char* ssid);
bool connectToWiFi();
bool startAPMode();
String getShortSymbol(const char* symbol);
String formatPercent(float percent);
String formatNumber(float number);
String formatPrice(float price);
String getTimeString(unsigned long timestamp);
String getExactTimeString(unsigned long timestamp);
void updateCryptoDisplay();
void sortCryptosByLoss();
void checkCryptoAlerts();
void checkEntryModeAlerts();
void checkExitModeAlerts();
void resetAllAlerts();
void resetDisplayToFirstPosition();
bool detectNewPortfolio();
void parseCryptoData(String jsonData);
String getPortfolioData();
String base64Encode(String data);
void updateDateTime();
void handleRoot();
void handleSetup();
void handleSaveWiFi();
void handleRemoveWiFi();
void handleSaveAPI();
void handleSaveAlert();
void handleSaveMode();
void handleRefresh();
void handleTestAlert();
void handleResetAlerts();
void handleWiFiManager();
void handleConfig();
void handleLoginPost();
void handleLoginPage();
void handleTestConnection();
void setupWebServer();
void setupResetButton();
void checkResetButton();
void factoryReset();
void debugPositionData();
void debugServerArgs();
void debugHTTPResponse();
void debugExitModeStatus();

// LED Functions
void setupLEDs();
void turnOnGreenLED();
void turnOnRedLED();
void turnOffGreenLED();
void turnOffRedLED();
void turnOffAllLEDs();
void updateLEDs();
void triggerAlertLED(const char* symbol, bool isLong, bool isProfit, float pnlPercent);
void resetAlertLEDs();
void updateLEDBlinking();
void addActiveAlert(const char* symbol, bool isLong, bool isProfit, float pnlPercent);

// RGB LED Functions
void setRGB1Color(int r, int g, int b);
void setRGB2Color(int r, int g, int b);
void turnOffRGB1();
void turnOffRGB2();

// Exit Mode Functions
void showExitAlert(String title, String msg1, String msg2, String msg3, bool isProfit, float changePercent, float price);
void playExitAlertTone(bool isProfit);

// New API Functions for Different Modes
String getEntryModeData();
String getExitModeData();
void parseEntryModeData(String jsonData);
void parseExitModeData(String jsonData);
void clearCryptoData();

// Utility Functions
String urlDecode(String input);

// Function prototypes
unsigned long getCorrectTimestamp();
void printAlertHistoryStatus();

void debugHTTPResponse(int httpCode, String response) {
    Serial.println("\n=== HTTP Response Debug ===");
    Serial.println("HTTP Code: " + String(httpCode));
    Serial.println("Response length: " + String(response.length()));
    
    if (response.length() > 0) {
        // FIX: Cast response.length() to int
        Serial.println("First 200 chars: " + response.substring(0, min(200, (int)response.length())));
        
        if (response.length() > 500) {
            Serial.println("Response too large? Length: " + String(response.length()));
        }
        
        if (response.indexOf("{") == -1) {
            Serial.println("WARNING: No JSON opening brace found!");
        }
        
        if (response.indexOf("error") != -1) {
            Serial.println("ERROR detected in response!");
        }
    }
    Serial.println("=========================\n");
}

void debugServerArgs() {
    Serial.println("=== DEBUG Server Arguments ===");
    int args = server.args();
    Serial.println("Number of args: " + String(args));
    
    for (int i = 0; i < args; i++) {
        String argName = server.argName(i);
        String argValue = server.arg(i);
        Serial.println("Arg " + String(i) + ": " + argName + " = " + argValue);
    }
    Serial.println("=============================");
}

void debugPositionData() {
    Serial.println("\n=== DEBUG: Position Data ===");
    Serial.println("Index | Symbol | P/L% | Current Price | Exit Alerted | Last Exit Price");
    for (int i = 0; i < min(10, cryptoCount); i++) {
        Serial.print(i);
        Serial.print(" | ");
        Serial.print(cryptoData[i].symbol);
        Serial.print(" | ");
        Serial.print(cryptoData[i].changePercent, 2);
        Serial.print("% | ");
        Serial.print(formatPrice(cryptoData[i].currentPrice));
        Serial.print(" | ");
        Serial.print(cryptoData[i].exitAlerted ? "YES" : "NO");
        Serial.print(" | ");
        Serial.println(formatPrice(cryptoData[i].exitAlertLastPrice));
    }
    Serial.println("Mode: " + String(settings.isExitMode ? "EXIT (Position Tracking)" : "ENTRY (Portfolio Tracking)"));
    Serial.println("Exit Portfolio: " + String(settings.exitPortfolio));
    Serial.println("Entry Portfolio: " + String(settings.entryPortfolio));
    Serial.println("===========================\n");
}

// ==================== UTILITY FUNCTIONS ====================
String urlDecode(String input) {
    String output = "";
    char temp[] = "0x00";
    unsigned int len = input.length();
    unsigned int i = 0;
    
    while (i < len) {
        char ch = input.charAt(i);
        if (ch == '%') {
            if (i + 2 < len) {
                temp[2] = input.charAt(i + 1);
                temp[3] = input.charAt(i + 2);
                output += (char)strtol(temp, NULL, 16);
                i += 3;
            } else {
                break;
            }
        } else if (ch == '+') {
            output += ' ';
            i++;
        } else {
            output += ch;
            i++;
        }
    }
    return output;
}

// ==================== LED FUNCTIONS ====================
void setupLEDs() {
    Serial.println("Setting up LEDs...");
    
    // Setup RGB LEDs
    pinMode(RGB1_RED, OUTPUT);
    pinMode(RGB1_GREEN, OUTPUT);
    pinMode(RGB1_BLUE, OUTPUT);
    
    pinMode(RGB2_RED, OUTPUT);
    pinMode(RGB2_GREEN, OUTPUT);
    pinMode(RGB2_BLUE, OUTPUT);
    
    // Setup regular LEDs
    pinMode(LED_GREEN_1, OUTPUT);
    pinMode(LED_RED_1, OUTPUT);
    pinMode(LED_GREEN_2, OUTPUT);
    pinMode(LED_RED_2, OUTPUT);
    
    turnOffAllLEDs();
    
    // Test sequence
    setRGB1Color(255, 0, 0); // Red
    setRGB2Color(0, 255, 0); // Green
    delay(300);
    turnOffRGB1();
    turnOffRGB2();
    
    digitalWrite(LED_GREEN_1, HIGH);
    digitalWrite(LED_GREEN_2, HIGH);
    delay(300);
    digitalWrite(LED_GREEN_1, LOW);
    digitalWrite(LED_GREEN_2, LOW);
    
    digitalWrite(LED_RED_1, HIGH);
    digitalWrite(LED_RED_2, HIGH);
    delay(300);
    digitalWrite(LED_RED_1, LOW);
    digitalWrite(LED_RED_2, LOW);
    
    Serial.println("LEDs initialized");
    Serial.println("  RGB1: Red=GPIO32, Green=GPIO33, Blue=GPIO25");
    Serial.println("  RGB2: Red=GPIO26, Green=GPIO14, Blue=GPIO12");
    Serial.println("  LED_GREEN_1: GPIO22, LED_RED_1: GPIO21");
    Serial.println("  LED_GREEN_2: GPIO19, LED_RED_2: GPIO27");
}

void setRGB1Color(int r, int g, int b) {
    if (settings.ledEnabled) {
        int brightness = settings.ledBrightness;
        analogWrite(RGB1_RED, map(r, 0, 255, 0, brightness));
        analogWrite(RGB1_GREEN, map(g, 0, 255, 0, brightness));
        analogWrite(RGB1_BLUE, map(b, 0, 255, 0, brightness));
    }
}

void setRGB2Color(int r, int g, int b) {
    if (settings.ledEnabled) {
        int brightness = settings.ledBrightness;
        analogWrite(RGB2_RED, map(r, 0, 255, 0, brightness));
        analogWrite(RGB2_GREEN, map(g, 0, 255, 0, brightness));
        analogWrite(RGB2_BLUE, map(b, 0, 255, 0, brightness));
    }
}

void turnOffRGB1() {
    analogWrite(RGB1_RED, 0);
    analogWrite(RGB1_GREEN, 0);
    analogWrite(RGB1_BLUE, 0);
}

void turnOffRGB2() {
    analogWrite(RGB2_RED, 0);
    analogWrite(RGB2_GREEN, 0);
    analogWrite(RGB2_BLUE, 0);
}

void turnOnGreenLED() {
    if (settings.ledEnabled) {
        digitalWrite(LED_GREEN_1, HIGH);
        digitalWrite(LED_GREEN_2, HIGH);
    }
}

void turnOnRedLED() {
    if (settings.ledEnabled) {
        digitalWrite(LED_RED_1, HIGH);
        digitalWrite(LED_RED_2, HIGH);
    }
}

void turnOffGreenLED() {
    digitalWrite(LED_GREEN_1, LOW);
    digitalWrite(LED_GREEN_2, LOW);
}

void turnOffRedLED() {
    digitalWrite(LED_RED_1, LOW);
    digitalWrite(LED_RED_2, LOW);
}

void turnOffAllLEDs() {
    turnOffRGB1();
    turnOffRGB2();
    digitalWrite(LED_GREEN_1, LOW);
    digitalWrite(LED_RED_1, LOW);
    digitalWrite(LED_GREEN_2, LOW);
    digitalWrite(LED_RED_2, LOW);
}

void addActiveAlert(const char* symbol, bool isLong, bool isProfit, float pnlPercent) {
    for (int i = 0; i < activeAlertCount; i++) {
        if (activeAlerts[i].symbol == symbol && activeAlerts[i].isProfit == isProfit) {
            activeAlerts[i].firstTriggerTime = millis();
            activeAlerts[i].blinkCount = 0;
            activeAlerts[i].isBlinking = true;
            Serial.println("Updated blinking for existing alert: " + String(symbol));
            return;
        }
    }
    
    if (activeAlertCount < 10) {
        activeAlerts[activeAlertCount].symbol = symbol;
        activeAlerts[activeAlertCount].isLong = isLong;
        activeAlerts[activeAlertCount].isProfit = isProfit;
        activeAlerts[activeAlertCount].pnlPercent = pnlPercent;
        activeAlerts[activeAlertCount].firstTriggerTime = millis();
        activeAlerts[activeAlertCount].blinkCount = 0;
        activeAlerts[activeAlertCount].isBlinking = true;
        activeAlertCount++;
        Serial.println("New active alert added for blinking: " + String(symbol) + 
                      " (" + String(isProfit ? "PROFIT" : "LOSS") + ")");
    }
}

void updateLEDBlinking() {
    if (!settings.ledEnabled) return;
    
    static bool blinkState = false;
    
    if (millis() - lastBlinkTime < 300) return;
    
    lastBlinkTime = millis();
    blinkState = !blinkState;
    
    bool anyBlinking = false;
    
    for (int i = 0; i < activeAlertCount; i++) {
        if (activeAlerts[i].isBlinking) {
            anyBlinking = true;
            
            if (activeAlerts[i].blinkCount >= 10) {
                activeAlerts[i].isBlinking = false;
                Serial.println("Blinking completed for: " + activeAlerts[i].symbol);
                continue;
            }
            
            if (settings.isExitMode) {
                if (activeAlerts[i].isProfit) {
                    if (blinkState) {
                        setRGB1Color(0, 255, 0); // Green for profit
                        turnOnGreenLED();
                    } else {
                        turnOffRGB1();
                        turnOffGreenLED();
                    }
                } else {
                    if (blinkState) {
                        setRGB2Color(255, 0, 0); // Red for loss
                        turnOnRedLED();
                    } else {
                        turnOffRGB2();
                        turnOffRedLED();
                    }
                }
            } else {
                if (activeAlerts[i].isLong) {
                    if (blinkState) {
                        setRGB1Color(0, 255, 0); // Green for long
                        turnOnGreenLED();
                    } else {
                        turnOffRGB1();
                        turnOffGreenLED();
                    }
                } else {
                    if (blinkState) {
                        setRGB2Color(255, 0, 0); // Red for short
                        turnOnRedLED();
                    } else {
                        turnOffRGB2();
                        turnOffRedLED();
                    }
                }
            }
            
            activeAlerts[i].blinkCount++;
        }
    }
    
    if (!anyBlinking) {
        for (int i = 0; i < activeAlertCount; i++) {
            if (!activeAlerts[i].isBlinking) {
                for (int j = i; j < activeAlertCount - 1; j++) {
                    activeAlerts[j] = activeAlerts[j + 1];
                }
                activeAlertCount--;
                i--;
            }
        }
        updateLEDs();
    }
}

void updateLEDs() {
    if (!settings.ledEnabled) {
        turnOffAllLEDs();
        return;
    }
    
    if (settings.isExitMode) {
        bool foundProfitAlert = false;
        bool foundLossAlert = false;
        
        for (int i = 0; i < activeAlertCount; i++) {
            if (activeAlerts[i].isProfit) {
                foundProfitAlert = true;
                if (!profitLEDActive) {
                    profitLEDActive = true;
                    profitAlertSymbol = activeAlerts[i].symbol;
                    profitAlertPnlPercent = activeAlerts[i].pnlPercent;
                }
            } else {
                foundLossAlert = true;
                if (!lossLEDActive) {
                    lossLEDActive = true;
                    lossAlertSymbol = activeAlerts[i].symbol;
                    lossAlertPnlPercent = activeAlerts[i].pnlPercent;
                }
            }
        }
        
        if (foundProfitAlert) {
            setRGB1Color(0, 255, 0); // Green for profit
            turnOnGreenLED();
        } else {
            turnOffRGB1();
            turnOffGreenLED();
            profitLEDActive = false;
            profitAlertSymbol = "";
            profitAlertPnlPercent = 0;
        }
        
        if (foundLossAlert) {
            setRGB2Color(255, 0, 0); // Red for loss
            turnOnRedLED();
        } else {
            turnOffRGB2();
            turnOffRedLED();
            lossLEDActive = false;
            lossAlertSymbol = "";
            lossAlertPnlPercent = 0;
        }
    } else {
        bool foundLongAlert = false;
        bool foundShortAlert = false;
        
        for (int i = 0; i < cryptoCount; i++) {
            if (cryptoData[i].alerted) {
                if (cryptoData[i].isLong) {
                    foundLongAlert = true;
                    if (!longAlertLEDActive) {
                        longAlertLEDActive = true;
                        longAlertSymbol = String(cryptoData[i].symbol);
                        longAlertPnlPercent = cryptoData[i].changePercent;
                    }
                } else {
                    foundShortAlert = true;
                    if (!shortAlertLEDActive) {
                        shortAlertLEDActive = true;
                        shortAlertSymbol = String(cryptoData[i].symbol);
                        shortAlertPnlPercent = cryptoData[i].changePercent;
                    }
                }
            }
        }
        
        if (foundLongAlert) {
            setRGB1Color(0, 255, 0); // Green for long
            turnOnGreenLED();
        } else {
            turnOffRGB1();
            turnOffGreenLED();
            longAlertLEDActive = false;
            longAlertSymbol = "";
            longAlertPnlPercent = 0;
        }
        
        if (foundShortAlert) {
            setRGB2Color(255, 0, 0); // Red for short
            turnOnRedLED();
        } else {
            turnOffRGB2();
            turnOffRedLED();
            shortAlertLEDActive = false;
            shortAlertSymbol = "";
            shortAlertPnlPercent = 0;
        }
    }
}

void manageLEDStates() {
    if (!settings.ledEnabled) {
        turnOffAllLEDs();
        return;
    }
    
    bool shouldGreenBeOn = false;
    bool shouldRedBeOn = false;
    
    if (settings.isExitMode) {
        for (int i = 0; i < activeAlertCount; i++) {
            if (activeAlerts[i].isProfit) {
                shouldGreenBeOn = true;
            } else {
                shouldRedBeOn = true;
            }
        }
        
        if (profitLEDActive) shouldGreenBeOn = true;
        if (lossLEDActive) shouldRedBeOn = true;
    } else {
        for (int i = 0; i < cryptoCount; i++) {
            if (cryptoData[i].alerted) {
                if (cryptoData[i].isLong) {
                    shouldGreenBeOn = true;
                } else {
                    shouldRedBeOn = true;
                }
            }
        }
        
        if (longAlertLEDActive) shouldGreenBeOn = true;
        if (shortAlertLEDActive) shouldRedBeOn = true;
    }
    
    if (shouldGreenBeOn) {
        setRGB1Color(0, 255, 0);
        turnOnGreenLED();
    } else {
        turnOffRGB1();
        turnOffGreenLED();
    }
    
    if (shouldRedBeOn) {
        setRGB2Color(255, 0, 0);
        turnOnRedLED();
    } else {
        turnOffRGB2();
        turnOffRedLED();
    }
}

void triggerAlertLED(const char* symbol, bool isLong, bool isProfit, float pnlPercent) {
    Serial.println("\n🔔 TRIGGERING ALERT LED");
    Serial.println("  Symbol: " + String(symbol));
    Serial.println("  Type: " + String(isLong ? "LONG" : "SHORT"));
    Serial.println("  Profit/Loss: " + String(isProfit ? "PROFIT" : "LOSS"));
    Serial.println("  P/L%: " + String(pnlPercent, 1) + "%");
    Serial.println("  Brightness: " + String(settings.ledBrightness) + "/255");
    
    addActiveAlert(symbol, isLong, isProfit, pnlPercent);
    
    if (settings.isExitMode) {
        if (isProfit && settings.ledEnabled) {
            profitLEDActive = true;
            profitAlertSymbol = String(symbol);
            profitAlertPnlPercent = pnlPercent;
        } else if (!isProfit && settings.ledEnabled) {
            lossLEDActive = true;
            lossAlertSymbol = String(symbol);
            lossAlertPnlPercent = pnlPercent;
        }
    } else {
        if (isLong && settings.ledEnabled) {
            longAlertLEDActive = true;
            longAlertSymbol = String(symbol);
            longAlertPnlPercent = pnlPercent;
        } else if (!isLong && settings.ledEnabled) {
            shortAlertLEDActive = true;
            shortAlertSymbol = String(symbol);
            shortAlertPnlPercent = pnlPercent;
        }
    }
}

void resetAlertLEDs() {
    Serial.println("Resetting Alert LEDs...");
    turnOffAllLEDs();
    longAlertLEDActive = false;
    shortAlertLEDActive = false;
    profitLEDActive = false;
    lossLEDActive = false;
    longAlertSymbol = "";
    shortAlertSymbol = "";
    profitAlertSymbol = "";
    lossAlertSymbol = "";
    longAlertPnlPercent = 0;
    shortAlertPnlPercent = 0;
    profitAlertPnlPercent = 0;
    lossAlertPnlPercent = 0;
    
    activeAlertCount = 0;
    
    Serial.println("Alert LEDs reset");
}

// ==================== TFT FUNCTIONS ====================
void setupTFT() {
    Serial.println("Initializing TFT display...");
    
    tft.init();
    tft.setRotation(0); // 0 برای جهت عادی
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    
    tftConnected = true;
    
    // نمایش پیام راه‌اندازی با فونت بزرگ
    tft.setTextSize(3); // سایز بزرگ برای عنوان
    tft.setCursor(10, 10);
    tft.println("Portfolio");
    tft.setCursor(10, 50);
    tft.println("Monitor");
    
    tft.setTextSize(2); // سایز متوسط برای زیرعنوان
    tft.setCursor(10, 90);
    tft.println("ESP32-WROVER");
    tft.setCursor(10, 120);
    tft.println("v3.8.4");
    
    delay(2000);
    tft.fillScreen(TFT_BLACK);
    
    Serial.println("TFT display initialized successfully");
}

void updateTFTDisplay() {
    if (!tftConnected) return;
    
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(0, 0);
    
    if (showingAlert) {
        // ========== نمایش الرت با فونت بزرگ ==========
        tft.setTextSize(3); // بسیار بزرگ برای عنوان الرت
        tft.setCursor(2, 5);
        tft.println(alertTitle);
        
        tft.setTextSize(1); // کوچک برای جداکننده
        tft.setCursor(2, 40);
        tft.println("------------");
        
        tft.setTextSize(2); // بزرگ برای پیام‌ها
        tft.setCursor(2, 55);
        tft.println(alertMessage1);
        
        if (alertMessage2 != "") {
            tft.setCursor(2, tft.getCursorY() + 25);
            tft.println(alertMessage2);
        }
        
        if (alertMessage3 != "") {
            tft.setCursor(2, tft.getCursorY() + 25);
            tft.println(alertMessage3);
        }
        
        tft.setTextSize(1); // متوسط برای قیمت
        tft.setCursor(2, tft.getCursorY() + 20);
        tft.print("@ ");
        tft.println(formatPrice(alertPrice));
        
    } else if (apModeActive) {
        // ========== حالت AP با فونت بزرگ ==========
        tft.setTextSize(4); // بسیار بزرگ برای عنوان
        tft.setCursor(2, 5);
        tft.println("AP MODE");
        
        tft.setTextSize(2); // بزرگ برای SSID
        tft.setCursor(2, 50);
        tft.println("SSID:");
        
        tft.setTextSize(3); // بزرگ‌تر برای نام SSID
        tft.setCursor(2, 75);
        tft.println("ESP32-Pfolio");
        
        tft.setTextSize(2); // بزرگ برای رمز
        tft.setCursor(2, 115);
        tft.println("Pass: 12345678");
        
        tft.setTextSize(2); // بزرگ برای آدرس IP
        tft.setCursor(2, 145);
        tft.println("IP: 192.168.4.1");
        
        // نمایش تعداد کلاینت‌های متصل
        int clientCount = WiFi.softAPgetStationNum();
        tft.setTextSize(1); // کوچک برای وضعیت
        tft.setCursor(2, 190);
        tft.print("Clients: ");
        tft.print(clientCount);
        
        if (clientCount > 0) {
            tft.print(" connected");
        } else {
            tft.print(" waiting...");
        }
        
    } else if (!isConnectedToWiFi) {
        // ========== حالت بدون اتصال WiFi ==========
        tft.setTextSize(4); // بسیار بزرگ
        tft.setCursor(5, 10);
        tft.println("NO WIFI");
        
        tft.setTextSize(2); // بزرگ
        tft.setCursor(5, 70);
        tft.println("Check connection");
        
        tft.setTextSize(2); // بزرگ
        tft.setCursor(5, 105);
        tft.println("or setup via AP");
        
        tft.setTextSize(1); // کوچک
        tft.setCursor(5, 150);
        tft.println("Press reset button");
        tft.setCursor(5, 170);
        tft.println("for 5 seconds");
        tft.setCursor(5, 190);
        tft.println("to start AP mode");
        
    } else {
        // ========== حالت عادی (متصل به WiFi) ==========
        
        // خط 1: تاریخ و زمان با فونت متوسط
        tft.setTextSize(2);
        if (currentDateTime.length() > 10) {
            String shortTime = currentDateTime.substring(5, 16); // MM/DD HH:MM
            tft.setCursor(2, 5);
            tft.println(shortTime);
        } else {
            tft.setCursor(2, 5);
            tft.println("No time sync");
        }
        
        // جداکننده
        tft.setTextSize(1);
        tft.setCursor(2, 35);
        tft.println("-----------");
        
        // عنوان پورتفولیو با فونت بزرگ
        tft.setTextSize(3);
        tft.setCursor(2, 45);
        
        // محدود کردن طول عنوان اگر خیلی طولانی است
        String displayHeader = portfolioHeader;
        if (displayHeader.length() > 15) {
            displayHeader = displayHeader.substring(0, 15) + "...";
        }
        tft.println(displayHeader);
        
        tft.setTextSize(1);
        tft.setCursor(2, 80);
        tft.println("-----------");
        
        // اطلاعات پورتفولیو با فونت متوسط
        tft.setTextSize(2);
        
        // سرمایه‌گذاری
        tft.setCursor(2, 95);
        tft.print("Inv: $");
        tft.println(totalInvestmentStr);
        
        // ارزش فعلی
        tft.setCursor(2, 120);
        tft.print("Val: $");
        tft.println(totalValueStr);
        
        // سود/زیان
        tft.setCursor(2, 145);
        tft.print("P/L: $");
        tft.println(totalPnlStr);
        
        // درصد سود/زیان
        tft.setCursor(2, 170);
        tft.print("P/L%: ");
        tft.print(portfolio.totalPnlPercent >= 0 ? "+" : "");
        tft.print(String(portfolio.totalPnlPercent, 1));
        tft.println("%");
        
        tft.setTextSize(1);
        tft.setCursor(2, 195);
        tft.println("-----------");
        
        // نمایش رمزارز جاری (اگر داده وجود دارد)
        if (cryptoCount > 0) {
            int displayCount = min(DISPLAY_CRYPTO_COUNT, cryptoCount);
            if (currentDisplayIndex >= 0 && currentDisplayIndex < displayCount) {
                CryptoPosition crypto = sortedCryptoData[currentDisplayIndex];
                
                // نمایش رمزارز با فونت بزرگ
                tft.setTextSize(2);
                tft.setCursor(2, 210);
                
                String symbol = getShortSymbol(crypto.symbol);
                String change = formatPercent(crypto.changePercent);
                
                // محدود کردن طول نمایش
                String displayStr = symbol + " " + change;
                if (displayStr.length() > 15) {
                    displayStr = displayStr.substring(0, 15) + "...";
                }
                tft.print(displayStr);
                
                // نمایش نوع پوزیشن و وضعیت الرت
                tft.setTextSize(1);
                tft.setCursor(150, 215);
                if (crypto.isLong) {
                    tft.print("L");
                } else {
                    tft.print("S");
                }
                
                if (crypto.alerted) {
                    tft.setTextColor(TFT_RED, TFT_BLACK);
                    tft.print(" ALRT");
                    tft.setTextColor(TFT_WHITE, TFT_BLACK);
                }
            }
        } else {
            // اگر داده‌ای وجود ندارد
            tft.setTextSize(2);
            tft.setCursor(2, 210);
            tft.println("No data");
        }
        
        // اگر پورتفولیوی جدید تشخیص داده شده
        if (newPortfolioDetected) {
            tft.setTextSize(1);
            tft.setTextColor(TFT_YELLOW, TFT_BLACK);
            tft.setCursor(2, 230);
            tft.println("New Portfolio!");
            tft.setTextColor(TFT_WHITE, TFT_BLACK);
        }
    }
}

void showTFTMessage(String line1, String line2, String line3, String line4, String line5) {
    if (!tftConnected) return;
    
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    
    int yPos = 0;
    
    // Line 1 - بسیار بزرگ (سایز 4)
    if (line1 != "") {
        tft.setTextSize(4);
        tft.setCursor(2, yPos);
        tft.println(line1);
        yPos += 45;
    }
    
    // Line 2 - بزرگ (سایز 3)
    if (line2 != "") {
        tft.setTextSize(3);
        tft.setCursor(2, yPos);
        tft.println(line2);
        yPos += 35;
    }
    
    // Line 3 - متوسط (سایز 2)
    if (line3 != "") {
        tft.setTextSize(2);
        tft.setCursor(2, yPos);
        tft.println(line3);
        yPos += 25;
    }
    
    // Line 4 - متوسط (سایز 2)
    if (line4 != "") {
        tft.setTextSize(2);
        tft.setCursor(2, yPos);
        tft.println(line4);
        yPos += 25;
    }
    
    // Line 5 - کوچک (سایز 1)
    if (line5 != "") {
        tft.setTextSize(1);
        tft.setCursor(2, yPos);
        tft.println(line5);
    }
}

// ==================== BUZZER & ALERT FUNCTIONS ====================
void setupBuzzer() {
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
    
    if (settings.buzzerVolume < VOLUME_MIN || settings.buzzerVolume > VOLUME_MAX) {
        settings.buzzerVolume = DEFAULT_VOLUME;
    }
    
    Serial.println("Buzzer initialized");
    Serial.println("Volume: " + String(settings.buzzerVolume) + "/20");
    Serial.println("Enabled: " + String(settings.buzzerEnabled ? "YES" : "NO"));
}

void playTone(int frequency, int duration, int volumePercent) {
    if (!settings.buzzerEnabled || settings.buzzerVolume == VOLUME_OFF) {
        digitalWrite(BUZZER_PIN, LOW);
        return;
    }
    
    // روش بازر بر اساس فایل مرجع
    int volume = settings.buzzerVolume;
    
    if (volume <= 2) {
        // حجم کم: یک پالس کوتاه
        digitalWrite(BUZZER_PIN, HIGH);
        delay(5);
        digitalWrite(BUZZER_PIN, LOW);
        delay(duration - 5);
        return;
    }
    
    // محاسبه زمان‌ها بر اساس حجم
    int periodMicros = 1000000 / frequency;
    int onTime = periodMicros * volume / 20; // Adjust based on volume
    int offTime = periodMicros - onTime;
    
    unsigned long startTime = micros();
    unsigned long targetTime = startTime + duration * 1000;
    
    while (micros() < targetTime) {
        digitalWrite(BUZZER_PIN, HIGH);
        delayMicroseconds(onTime);
        digitalWrite(BUZZER_PIN, LOW);
        delayMicroseconds(offTime);
    }
    
    digitalWrite(BUZZER_PIN, LOW);
}

void playLongPositionAlert(bool isSevere) {
    if (!settings.buzzerEnabled) return;
    
    int frequency = isSevere ? LONG_SEVERE_TONE : LONG_NORMAL_TONE;
    int beepCount = isSevere ? 3 : 2;
    int duration = isSevere ? 200 : 300;
    int pause = isSevere ? 150 : 200;
    
    Serial.println("Playing LONG position alert" + String(isSevere ? " (SEVERE)" : ""));
    
    for (int i = 0; i < beepCount; i++) {
        playTone(frequency, duration, 80);
        if (i < beepCount - 1) delay(pause);
    }
}

void playShortPositionAlert(bool isSevere) {
    if (!settings.buzzerEnabled) return;
    
    int frequency = isSevere ? SHORT_SEVERE_TONE : SHORT_NORMAL_TONE;
    int beepCount = isSevere ? 3 : 1;
    int duration = isSevere ? 100 : 250;
    int pause = isSevere ? 80 : 0;
    
    Serial.println("Playing SHORT position alert" + String(isSevere ? " (SEVERE)" : ""));
    
    for (int i = 0; i < beepCount; i++) {
        playTone(frequency, duration, 80);
        if (i < beepCount - 1) delay(pause);
    }
}

void playExitAlertTone(bool isProfit) {
    if (!settings.buzzerEnabled) return;
    
    Serial.println("Playing EXIT alert tone for " + String(isProfit ? "PROFIT" : "LOSS"));
    
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

void playResetAlertTone() {
    if (!settings.buzzerEnabled) return;
    
    Serial.println("Playing reset tone");
    
    int melody[] = {RESET_TONE_1, RESET_TONE_2, RESET_TONE_3};
    int durations[] = {80, 60, 100};
    
    for (int i = 0; i < 3; i++) {
        playTone(melody[i], durations[i], 50);
        delay(50);
    }
}

void playSuccessTone() {
    if (!settings.buzzerEnabled) return;
    
    Serial.println("Playing success tone");
    
    playTone(600, 150, 60);
    delay(100);
    playTone(800, 200, 60);
}

void playErrorTone() {
    if (!settings.buzzerEnabled) return;
    
    Serial.println("Playing error tone");
    
    playTone(300, 200, 60);
    delay(100);
    playTone(200, 250, 60);
}

void playTestAlertSequence() {
    if (!settings.buzzerEnabled) {
        Serial.println("Buzzer is disabled, skipping test");
        return;
    }
    
    Serial.println("Playing test alert sequence...");
    
    playLongPositionAlert(false);
    delay(300);
    
    playShortPositionAlert(false);
    delay(300);
    
    playSuccessTone();
    
    Serial.println("Test sequence completed");
}

void displayAlertHistory() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(0, 0);
    tft.println("Recent Alert History");

    if (activeAlertCount == 0) {
        tft.println("No alerts stored");
        return;
    }

    for (int i = activeAlertCount - 1; i >= 0; i--) {
        if (strlen(alertHistory[i].timeString) == 0) {
            tft.println("--/-- --:--");
        } else {
            tft.println(alertHistory[i].timeString);
        }

        tft.print(alertHistory[i].isLong ? "L " : "S ");
        tft.print(alertHistory[i].symbol);
        tft.println(alertHistory[i].isLong ? " LONG" : " SHORT");

        tft.print("P/L: ");
        tft.print(alertHistory[i].pnlPercent, 1);
        tft.print("% $");
        tft.print(alertHistory[i].alertPrice, 6);
        tft.println(alertHistory[i].isLong ? " LONG" : " SHORT");

        tft.println("----------------");
    }
}

void ensureAlertTimeString(AlertHistory &alert) {
    if (strlen(alert.timeString) > 5) return;

    if (alert.alertTime >= 1700000000UL) {
        time_t t = (time_t)(alert.alertTime / 1000);
        struct tm timeinfo;
        if (localtime_r(&t, &timeinfo)) {
            strftime(alert.timeString, sizeof(alert.timeString),
                     "%m/%d %H:%M", &timeinfo);
            return;
        }
    }

    strcpy(alert.timeString, "--/-- --:--");
}

void clearAlertHistory() {
    activeAlertCount = 0;
    Serial.println("Alert history cleared");
}

String getAlertTimeForDisplay(unsigned long timestampSec) {
    if (timestampSec < 1700000000UL) {
        return "--/-- --:--";
    }

    struct tm timeinfo;
    time_t t = (time_t)timestampSec;

    if (!localtime_r(&t, &timeinfo)) {
        return "--/-- --:--";
    }

    char buffer[20];
    strftime(buffer, sizeof(buffer), "%m/%d %H:%M", &timeinfo);
    return String(buffer);
}

void showAlert(String title, String msg1, String msg2, String msg3, bool isLong, bool isSevere, float price) {
    alertTitle = title;
    alertMessage1 = msg1;
    alertMessage2 = msg2;
    alertMessage3 = msg3;
    alertPrice = price;
    showingAlert = true;
    alertStartTime = millis();
    
    Serial.println("\n=== ALERT TRIGGERED ===");
    Serial.println("Title: " + title);
    Serial.println("Message: " + msg1 + " | " + msg2 + " | " + msg3);
    Serial.println("Type: " + String(isLong ? "LONG" : "SHORT"));
    Serial.println("Severe: " + String(isSevere ? "YES" : "NO"));
    Serial.println("Price: " + formatPrice(price));
    Serial.println("Current time: " + currentDateTime);
    Serial.println("Mode: ENTRY MODE");
    Serial.println("========================\n");
    
    // پخش صدای الرت
    if (settings.buzzerEnabled) {
        if (isLong) {
            playLongPositionAlert(isSevere);
        } else {
            playShortPositionAlert(isSevere);
        }
    }
    
    // استخراج symbol از msg1
    String fullMsg = msg1;
    int spacePos = fullMsg.indexOf(' ');
    String symbol = "";
    if (spacePos > 0) {
        symbol = fullMsg.substring(0, spacePos);
    } else {
        symbol = fullMsg;
    }
    
    // استخراج درصد P/L از msg2
    String pnlStr = msg2;
    pnlStr.replace("%", "");
    pnlStr.replace(" P/L", "");
    pnlStr.trim();
    float pnlPercent = pnlStr.toFloat();
    
    // فعال کردن LED
    triggerAlertLED(symbol.c_str(), isLong, false, pnlPercent);
    
    // ========== ذخیره در تاریخچه الرت ==========
    
    // گرفتن زمان فعلی برای الرت
    String alertTimeString = "";
    
    // روش 1: از currentDateTime مستقیماً استفاده کن
    if (currentDateTime.length() >= 19) {
        String monthDay = currentDateTime.substring(5, 10);  // "01/04"
        String timePart = currentDateTime.substring(11, 19); // "17:40:00"
        alertTimeString = monthDay + " " + timePart;
    } else {
        // روش 2: از سیستم زمان بگیر
        struct tm timeinfo;
        if (getLocalTime(&timeinfo, 100)) {
            char timeString[20];
            strftime(timeString, sizeof(timeString), "%m/%d %H:%M:%S", &timeinfo);
            alertTimeString = String(timeString);
        } else {
            alertTimeString = "N/A";
        }
    }
    
    if (alertHistoryCount < MAX_ALERT_HISTORY) {
        AlertHistory* alert = &alertHistory[alertHistoryCount];
        
        // ذخیره symbol
        if (symbol.length() > 15) {
            symbol = symbol.substring(0, 15);
        }
        strncpy(alert->symbol, symbol.c_str(), 15);
        alert->symbol[15] = '\0';
        
        // ذخیره زمان الرت
        strncpy(alert->timeString, alertTimeString.c_str(), 19);
        alert->timeString[19] = '\0';
        
        // ذخیره timestamp فعلی
        struct tm timeinfo;
        if (getLocalTime(&timeinfo, 100)) {
            time_t time_seconds = mktime(&timeinfo);
            alert->alertTime = (unsigned long)time_seconds * 1000;
        } else {
            alert->alertTime = 0;
        }
        
        // ذخیره بقیه اطلاعات
        alert->pnlPercent = pnlPercent;
        alert->alertPrice = price;
        alert->isLong = isLong;
        alert->isSevere = isSevere;
        alert->isProfit = false;
        alert->alertType = isSevere ? 2 : 1;
        
        // اضافه کردن این خط: مشخص کردن مود
        alert->alertMode = 0; // 0 = Entry Mode
        
        // ذخیره پیام
        String fullTitle = title;
        if (fullTitle.length() > 63) {
            fullTitle = fullTitle.substring(0, 60) + "...";
        }
        strncpy(alert->message, fullTitle.c_str(), 63);
        alert->message[63] = '\0';
        
        alert->acknowledged = false;
        
        alertHistoryCount++;
        
        Serial.println("✅ Alert saved to history");
        Serial.println("  Mode: Entry Mode");
        Serial.println("  Position: " + String(isLong ? "LONG" : "SHORT"));
        Serial.println("  Time: " + alertTimeString);
        Serial.println("  Symbol: " + symbol);
        Serial.println("  P/L%: " + String(pnlPercent, 1) + "%");
        Serial.println("  Total alerts: " + String(alertHistoryCount));
        
    } else {
        // تاریخچه پر است - جایگزینی قدیمی‌ترین الرت
        Serial.println("⚠️ Alert history full, replacing oldest");
        
        // انتقال الرت‌ها
        for (int i = 0; i < MAX_ALERT_HISTORY - 1; i++) {
            alertHistory[i] = alertHistory[i + 1];
        }
        
        AlertHistory* alert = &alertHistory[MAX_ALERT_HISTORY - 1];
        
        // ذخیره symbol
        if (symbol.length() > 15) {
            symbol = symbol.substring(0, 15);
        }
        strncpy(alert->symbol, symbol.c_str(), 15);
        alert->symbol[15] = '\0';
        
        // ذخیره زمان
        strncpy(alert->timeString, alertTimeString.c_str(), 19);
        alert->timeString[19] = '\0';
        
        // ذخیره timestamp از زمان سیستم
        struct tm timeinfo;
        if (getLocalTime(&timeinfo, 100)) {
            time_t time_seconds = mktime(&timeinfo);
            alert->alertTime = (unsigned long)time_seconds * 1000;
        } else {
            alert->alertTime = 0;
        }
        
        alert->pnlPercent = pnlPercent;
        alert->alertPrice = price;
        alert->isLong = isLong;
        alert->isSevere = isSevere;
        alert->isProfit = false;
        alert->alertType = isSevere ? 2 : 1;
        
        // مشخص کردن مود
        alert->alertMode = 0; // 0 = Entry Mode
        
        String fullTitle = title;
        if (fullTitle.length() > 63) {
            fullTitle = fullTitle.substring(0, 60) + "...";
        }
        strncpy(alert->message, fullTitle.c_str(), 63);
        alert->message[63] = '\0';
        
        alert->acknowledged = false;
        
        Serial.println("✅ New alert saved (replaced oldest)");
        Serial.println("  Mode: Entry Mode");
        Serial.println("  Position: " + String(isLong ? "LONG" : "SHORT"));
        Serial.println("  Time: " + alertTimeString);
    }
    
    // نمایش روی TFT
    if (tftConnected) {
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.setTextSize(2);
        tft.setCursor(0, 0);
        tft.println(title);
        tft.setTextSize(1);
        tft.println("------------");
        tft.println(msg1);
        tft.println(msg2);
        if (msg3 != "") tft.println(msg3);
        tft.print("@");
        tft.println(formatPrice(price));
    }
}

void showExitAlert(String title, String msg1, String msg2, String msg3, bool isProfit, float changePercent, float price) {
    alertTitle = title;
    alertMessage1 = msg1;
    alertMessage2 = msg2;
    alertMessage3 = msg3;
    alertPrice = price;
    showingAlert = true;
    alertStartTime = millis();
    
    Serial.println("\n=== DEBUG: Exit Alert Time Info ===");
    Serial.println("Exit Alert Displayed:");
    Serial.println("  Title: " + title);
    Serial.println("  Msg1: " + msg1);
    Serial.println("  Msg2: " + msg2);
    Serial.println("  Msg3: " + msg3);
    Serial.println("  Profit/Loss: " + String(isProfit ? "PROFIT" : "LOSS"));
    Serial.println("  Price Change%: " + String(changePercent, 1) + "%");
    Serial.println("  Price: " + formatPrice(price));
    Serial.println("  Current DateTime: " + currentDateTime);
    
    if (settings.buzzerEnabled) {
        playExitAlertTone(isProfit);
    } else {
        Serial.println("Buzzer is disabled, no sound played");
    }
    
    if (settings.exitAlertBlinkEnabled) {
        String symbol = msg1.substring(0, msg1.indexOf(' '));
        triggerAlertLED(symbol.c_str(), true, isProfit, changePercent);
    }
    
    // ========== ذخیره در تاریخچه الرت ==========
    
    String alertTimeString = "";
    
    if (currentDateTime.length() >= 19) {
        String monthDay = currentDateTime.substring(5, 10);
        String timePart = currentDateTime.substring(11, 19);
        alertTimeString = monthDay + " " + timePart;
    } else {
        struct tm timeinfo;
        if (getLocalTime(&timeinfo, 100)) {
            char timeString[20];
            strftime(timeString, sizeof(timeString), "%m/%d %H:%M:%S", &timeinfo);
            alertTimeString = String(timeString);
        } else {
            alertTimeString = "--/-- --:--";
        }
    }
    
    String fullMsg = msg1;
    int spacePos = fullMsg.indexOf(' ');
    String symbol = "";
    if (spacePos > 0) {
        symbol = fullMsg.substring(0, spacePos);
    } else {
        symbol = fullMsg;
    }
    
    bool positionIsLong = true;
    float entryPrice = 0.0;
    float totalPnlPercent = 0.0;
    float lastAlertedPrice = 0.0;
    bool foundPosition = false;
    String foundSymbol = "";
    
    Serial.println("🔍 Looking for position in cryptoData: " + symbol);
    
    for (int i = 0; i < cryptoCount; i++) {
        if (strcmp(cryptoData[i].symbol, symbol.c_str()) == 0) {
            positionIsLong = cryptoData[i].isLong;
            entryPrice = cryptoData[i].entryPrice;
            lastAlertedPrice = cryptoData[i].exitAlertLastPrice;
            foundSymbol = String(cryptoData[i].symbol);
            foundPosition = true;
            Serial.println("  ✓ Found exact match: " + foundSymbol);
            break;
        }
    }
    
    if (foundPosition) {
        Serial.println("  ✓ Position type: " + String(positionIsLong ? "LONG" : "SHORT"));
        Serial.println("  ✓ Entry price: " + formatPrice(entryPrice));
        Serial.println("  ✓ Last alerted price: " + formatPrice(lastAlertedPrice));
        Serial.println("  ✓ Current price: " + formatPrice(price));
        
        if (entryPrice > 0) {
            if (positionIsLong) {
                totalPnlPercent = ((price - entryPrice) / entryPrice) * 100;
            } else {
                totalPnlPercent = ((entryPrice - price) / entryPrice) * 100;
            }
            Serial.println("  ✓ Total P/L% from entry: " + String(totalPnlPercent, 1) + "%");
        } else {
            Serial.println("  ⚠️ Entry price is 0, using provided changePercent");
            totalPnlPercent = changePercent;
        }
    } else {
        Serial.println("  ✗ WARNING: Position not found in cryptoData!");
        Serial.println("  Using default LONG position");
        positionIsLong = true;
        totalPnlPercent = changePercent;
    }
    
    bool isProfitFromLastAlert = isProfit;
    
    if (alertHistoryCount < MAX_ALERT_HISTORY) {
        AlertHistory* alert = &alertHistory[alertHistoryCount];
        
        String symbolToStore = foundPosition ? foundSymbol : symbol;
        if (symbolToStore.length() > 15) {
            symbolToStore = symbolToStore.substring(0, 15);
        }
        strncpy(alert->symbol, symbolToStore.c_str(), 15);
        alert->symbol[15] = '\0';
        
        strncpy(alert->timeString, alertTimeString.c_str(), 19);
        alert->timeString[19] = '\0';
        
        struct tm timeinfo;
        if (getLocalTime(&timeinfo, 100)) {
            time_t time_seconds = mktime(&timeinfo);
            alert->alertTime = (unsigned long)time_seconds * 1000;
            Serial.println("System timestamp stored: " + String(alert->alertTime));
        } else {
            alert->alertTime = 0;
            Serial.println("WARNING: Could not get system time for timestamp");
        }
        
        alert->pnlPercent = totalPnlPercent;
        alert->alertPrice = price;
        
        alert->isLong = positionIsLong;
        alert->isSevere = (fabs(totalPnlPercent) >= 10.0);
        alert->isProfit = isProfitFromLastAlert;
        alert->alertType = isProfitFromLastAlert ? 3 : 4;
        
        alert->alertMode = 1; // 1 = Exit Mode
        
        String positionType = positionIsLong ? "LONG" : "SHORT";
        String profitLossType = isProfitFromLastAlert ? "PROFIT" : "LOSS";
        
        String alertMessage = positionType + " " + profitLossType + " ALERT";
        alertMessage += " | Current: " + formatPrice(price);
        
        if (lastAlertedPrice > 0 && lastAlertedPrice != price) {
            alertMessage += " | Last Alert: " + formatPrice(lastAlertedPrice);
            
            float changeFromLastAlert = 0;
            if (positionIsLong) {
                changeFromLastAlert = ((price - lastAlertedPrice) / lastAlertedPrice) * 100;
            } else {
                changeFromLastAlert = ((lastAlertedPrice - price) / lastAlertedPrice) * 100;
            }
            
            alertMessage += " (" + String(fabs(changeFromLastAlert), 1) + "%)";
        }
        
        if (entryPrice > 0) {
            alertMessage += " | Entry: " + formatPrice(entryPrice);
            
            float pnlFromEntry = 0;
            if (positionIsLong) {
                pnlFromEntry = ((price - entryPrice) / entryPrice) * 100;
            } else {
                pnlFromEntry = ((entryPrice - price) / entryPrice) * 100;
            }
            
            alertMessage += " (" + String(pnlFromEntry, 1) + "%)";
        }
        
        alertMessage += " | Total P/L: " + String(totalPnlPercent, 1) + "%";
        
        if (alertMessage.length() > 63) {
            alertMessage = alertMessage.substring(0, 60) + "...";
        }
        strncpy(alert->message, alertMessage.c_str(), 63);
        alert->message[63] = '\0';
        
        alert->acknowledged = false;
        
        alertHistoryCount++;
        
        Serial.println("✅ Exit Alert saved to history");
        Serial.println("  Mode: Exit Mode");
        Serial.println("  Position: " + String(positionIsLong ? "LONG" : "SHORT"));
        Serial.println("  Symbol: " + symbolToStore);
        Serial.println("  Alert Message: " + alertMessage);
        Serial.println("  Entry Price: " + formatPrice(entryPrice));
        Serial.println("  Total P/L from entry: " + String(totalPnlPercent, 1) + "%");
        Serial.println("  Last Alerted Price: " + formatPrice(lastAlertedPrice));
        Serial.println("  Current Price: " + formatPrice(price));
        Serial.println("  Time: " + alertTimeString);
        Serial.println("  Total alerts: " + String(alertHistoryCount));
        
    } else {
        Serial.println("⚠️ Alert history full, replacing oldest");
        
        for (int i = 0; i < MAX_ALERT_HISTORY - 1; i++) {
            alertHistory[i] = alertHistory[i + 1];
        }
        
        AlertHistory* alert = &alertHistory[MAX_ALERT_HISTORY - 1];
        
        String symbolToStore = foundPosition ? foundSymbol : symbol;
        if (symbolToStore.length() > 15) {
            symbolToStore = symbolToStore.substring(0, 15);
        }
        strncpy(alert->symbol, symbolToStore.c_str(), 15);
        alert->symbol[15] = '\0';
        
        strncpy(alert->timeString, alertTimeString.c_str(), 19);
        alert->timeString[19] = '\0';
        
        struct tm timeinfo;
        if (getLocalTime(&timeinfo, 100)) {
            time_t time_seconds = mktime(&timeinfo);
            alert->alertTime = (unsigned long)time_seconds * 1000;
        } else {
            alert->alertTime = 0;
        }
        
        alert->pnlPercent = totalPnlPercent;
        alert->alertPrice = price;
        alert->isLong = positionIsLong;
        alert->isSevere = (fabs(totalPnlPercent) >= 10.0);
        alert->isProfit = isProfitFromLastAlert;
        alert->alertType = isProfitFromLastAlert ? 3 : 4;
        alert->alertMode = 1;
        
        String positionType = positionIsLong ? "LONG" : "SHORT";
        String profitLossType = isProfitFromLastAlert ? "PROFIT" : "LOSS";
        String alertMessage = positionType + " " + profitLossType + " ALERT";
        alertMessage += " | Current: " + formatPrice(price);
        
        if (alertMessage.length() > 63) {
            alertMessage = alertMessage.substring(0, 60) + "...";
        }
        strncpy(alert->message, alertMessage.c_str(), 63);
        alert->message[63] = '\0';
        
        alert->acknowledged = false;
        
        Serial.println("✅ New exit alert saved (replaced oldest)");
    }
    
    // نمایش روی TFT
    if (tftConnected) {
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.setTextSize(2);
        tft.setCursor(0, 0);
        
        String positionType = positionIsLong ? "LONG" : "SHORT";
        String displayTitle = positionType + " " + title;
        tft.println(displayTitle);
        tft.setTextSize(1);
        tft.println("------------");
        
        String displayMsg1 = symbol + " " + positionType + " " + (isProfitFromLastAlert ? "PROFIT" : "LOSS");
        tft.println(displayMsg1);
        
        String totalPnlMsg = "P/L from entry: " + String(totalPnlPercent, 1) + "%";
        tft.println(totalPnlMsg);
        
        if (lastAlertedPrice > 0) {
            String lastAlertMsg = "Last Alert: " + formatPrice(lastAlertedPrice);
            tft.println(lastAlertMsg);
        }
        
        String changeMsg = "Change: " + String(changePercent, 1) + "%";
        tft.println(changeMsg);
        
        tft.print("@");
        tft.println(formatPrice(price));
    }
}

// ==================== DISPLAY CONTROL FUNCTIONS ====================
void resetDisplayToFirstPosition() {
    currentDisplayIndex = -1;
    lastCryptoChange = millis();
    Serial.println("Display reset to first position");
}

// ==================== WIFI MANAGEMENT FUNCTIONS ====================
void initializeSettings() {
    Serial.println("Initializing default settings...");
    memset(&settings, 0, sizeof(SystemSettings));
    
    settings.networkCount = 0;
    settings.lastConnectedIndex = -1;
    settings.buzzerVolume = DEFAULT_VOLUME;
    settings.buzzerEnabled = true;
    strcpy(settings.entryPortfolio, "Main");
    strcpy(settings.exitPortfolio, "ActivePositions");
    settings.magicNumber = 123;
    settings.configured = false;
    settings.firstBoot = millis();
    settings.bootCount = 1;
    settings.totalUptime = 0;
    
    settings.alertThreshold = DEFAULT_ALERT_THRESHOLD;
    settings.severeAlertThreshold = DEFAULT_SEVERE_THRESHOLD;
    settings.portfolioAlertThreshold = PORTFOLIO_ALERT_THRESHOLD;
    settings.separateLongShortAlerts = true;
    settings.autoResetAlerts = true;
    settings.alertCooldown = 300000;
    
    settings.tftBrightness = 255;
    settings.displayTimeout = 30000;
    settings.showDetails = true;
    
    settings.maxPositionSize = 10000.0;
    settings.maxLeverage = 10.0;
    settings.riskPerTrade = 0.02;
    settings.autoHedge = false;
    
    settings.emailAlerts = false;
    settings.pushAlerts = false;
    settings.soundAlerts = true;
    settings.vibrationAlerts = false;
    
    settings.isExitMode = false;
    settings.exitAlertPercent = 5.0;
    settings.exitAlertEnabled = true;
    settings.exitAlertBlinkEnabled = true;
    
    settings.ledBrightness = DEFAULT_LED_BRIGHTNESS;
    settings.ledEnabled = true;
    
    Serial.println("Default settings initialized");
    Serial.println("Entry Portfolio: " + String(settings.entryPortfolio));
    Serial.println("Exit Portfolio: " + String(settings.exitPortfolio));
    Serial.println("Default Mode: " + String(settings.isExitMode ? "EXIT" : "ENTRY"));
    Serial.println("Exit Alert Percent: " + String(settings.exitAlertPercent) + "%");
    Serial.println("LED Brightness: " + String(settings.ledBrightness) + "/255");
    Serial.println("Max Crypto Positions: " + String(MAX_CRYPTO));
}

bool loadSettings() {
    Serial.println("Loading settings from EEPROM...");
    
    EEPROM.begin(EEPROM_SIZE);
    EEPROM.get(0, settings);
    EEPROM.end();
    
    if (settings.magicNumber != 123) {
        Serial.println("No valid settings found, initializing defaults");
        initializeSettings();
        saveSettings();
        return false;
    }
    
    if (settings.networkCount < 0 || settings.networkCount > MAX_WIFI_NETWORKS) {
        settings.networkCount = 0;
    }
    
    if (settings.buzzerVolume < VOLUME_MIN || settings.buzzerVolume > VOLUME_MAX) {
        settings.buzzerVolume = DEFAULT_VOLUME;
    }
    
    if (settings.ledBrightness < 0 || settings.ledBrightness > 255) {
        settings.ledBrightness = DEFAULT_LED_BRIGHTNESS;
    }
    
    if (settings.exitAlertPercent < 1 || settings.exitAlertPercent > 20) {
        settings.exitAlertPercent = 5.0;
    }
    
    if (strlen(settings.entryPortfolio) == 0) {
        strcpy(settings.entryPortfolio, "Main");
    }
    if (strlen(settings.exitPortfolio) == 0) {
        strcpy(settings.exitPortfolio, "ActivePositions");
    }
    
    Serial.println("Settings loaded successfully");
    Serial.println("Network count: " + String(settings.networkCount));
    Serial.println("Boot count: " + String(settings.bootCount));
    Serial.println("Trading Mode: " + String(settings.isExitMode ? "EXIT (Position Tracking)" : "ENTRY (Portfolio Tracking)"));
    Serial.println("Entry Portfolio: '" + String(settings.entryPortfolio) + "'");
    Serial.println("Exit Portfolio: '" + String(settings.exitPortfolio) + "'");
    Serial.println("Exit Alert Percent: " + String(settings.exitAlertPercent) + "%");
    Serial.println("Buzzer enabled: " + String(settings.buzzerEnabled ? "YES" : "NO"));
    Serial.println("LED enabled: " + String(settings.ledEnabled ? "YES" : "NO"));
    Serial.println("LED Brightness: " + String(settings.ledBrightness) + "/255");
    Serial.println("Max Crypto Capacity: " + String(MAX_CRYPTO) + " positions");
    
    // Log WiFi networks
    Serial.println("Saved WiFi networks:");
    if (settings.networkCount == 0) {
        Serial.println("  No networks saved");
    } else {
        for (int i = 0; i < settings.networkCount; i++) {
            Serial.print("  [" + String(i) + "] " + String(settings.networks[i].ssid));
            if (i == settings.lastConnectedIndex) {
                Serial.print(" (Last Connected)");
            }
            Serial.println();
        }
    }
    
    return true;
}

bool saveSettings() {
    Serial.println("\n=== Saving Settings to EEPROM ===");
    Serial.println("Trading Mode: " + String(settings.isExitMode ? "EXIT (Position Tracking)" : "ENTRY (Portfolio Tracking)"));
    Serial.println("Entry Portfolio: '" + String(settings.entryPortfolio) + "'");
    Serial.println("Exit Portfolio: '" + String(settings.exitPortfolio) + "'");
    Serial.println("Exit Alert Percent: " + String(settings.exitAlertPercent) + "%");
    Serial.println("Alert Threshold: " + String(settings.alertThreshold));
    Serial.println("Severe Threshold: " + String(settings.severeAlertThreshold));
    Serial.println("Portfolio Threshold: " + String(settings.portfolioAlertThreshold));
    Serial.println("Buzzer Volume: " + String(settings.buzzerVolume));
    Serial.println("Buzzer Enabled: " + String(settings.buzzerEnabled ? "YES" : "NO"));
    Serial.println("LED Enabled: " + String(settings.ledEnabled ? "YES" : "NO"));
    Serial.println("LED Brightness: " + String(settings.ledBrightness) + "/255");
    Serial.println("Max Crypto Capacity: " + String(MAX_CRYPTO) + " positions");
    
    Serial.println("WiFi Networks saved: " + String(settings.networkCount));
    for (int i = 0; i < settings.networkCount; i++) {
        Serial.println("  [" + String(i) + "] " + String(settings.networks[i].ssid));
    }
    
    settings.magicNumber = 123;
    settings.configured = true;
    settings.bootCount++;
    
    EEPROM.begin(EEPROM_SIZE);
    EEPROM.put(0, settings);
    bool success = EEPROM.commit();
    EEPROM.end();
    
    if (success) {
        Serial.println("✅ Settings saved successfully to EEPROM");
        Serial.println("Total size: " + String(sizeof(SystemSettings)) + " bytes");
        return true;
    } else {
        Serial.println("❌ Failed to save settings to EEPROM!");
        return false;
    }
}

bool addWiFiNetwork(const char* ssid, const char* password) {
    Serial.println("Adding WiFi network: " + String(ssid));
    
    String ssidStr = String(ssid);
    String passStr = String(password);
    ssidStr.trim();
    passStr.trim();
    
    // بررسی آیا شبکه قبلاً ذخیره شده
    for (int i = 0; i < settings.networkCount; i++) {
        if (strcmp(settings.networks[i].ssid, ssidStr.c_str()) == 0) {
            Serial.println("Updating existing network: " + String(ssid));
            strncpy(settings.networks[i].password, passStr.c_str(), 63);
            settings.networks[i].password[63] = '\0';
            settings.networks[i].configured = true;
            settings.networks[i].priority = 1;
            settings.networks[i].lastConnected = 0;
            settings.networks[i].connectionAttempts = 0;
            return saveSettings();
        }
    }
    
    // اگر شبکه جدید است
    if (settings.networkCount < MAX_WIFI_NETWORKS) {
        Serial.println("Adding new network: " + ssidStr);
        
        memset(&settings.networks[settings.networkCount], 0, sizeof(WiFiNetwork));
        
        strncpy(settings.networks[settings.networkCount].ssid, ssidStr.c_str(), 31);
        settings.networks[settings.networkCount].ssid[31] = '\0';
        
        strncpy(settings.networks[settings.networkCount].password, passStr.c_str(), 63);
        settings.networks[settings.networkCount].password[63] = '\0';
        
        settings.networks[settings.networkCount].configured = true;
        settings.networks[settings.networkCount].lastConnected = 0;
        settings.networks[settings.networkCount].connectionAttempts = 0;
        settings.networks[settings.networkCount].priority = 1;
        
        settings.networkCount++;
        
        if (saveSettings()) {
            Serial.println("Network added successfully. Total: " + String(settings.networkCount));
            return true;
        } else {
            Serial.println("Failed to save network!");
            settings.networkCount--;
            return false;
        }
    } else {
        Serial.println("Cannot add network - maximum limit reached (" + String(MAX_WIFI_NETWORKS) + ")");
        return false;
    }
}

bool removeWiFiNetwork(const char* ssid) {
    Serial.println("Removing network: " + String(ssid));
    
    for (int i = 0; i < settings.networkCount; i++) {
        if (strcmp(settings.networks[i].ssid, ssid) == 0) {
            Serial.println("Found network at index: " + String(i));
            
            // اگر شبکه حذف شده آخرین شبکه متصل بود، اندیس را تنظیم کن
            if (settings.lastConnectedIndex == i) {
                settings.lastConnectedIndex = -1;
            } else if (settings.lastConnectedIndex > i) {
                settings.lastConnectedIndex--;
            }
            
            // حذف شبکه
            for (int j = i; j < settings.networkCount - 1; j++) {
                settings.networks[j] = settings.networks[j + 1];
            }
            
            memset(&settings.networks[settings.networkCount - 1], 0, sizeof(WiFiNetwork));
            settings.networkCount--;
            
            if (saveSettings()) {
                Serial.println("Network removed. Total: " + String(settings.networkCount));
                return true;
            } else {
                Serial.println("Failed to save after removal!");
                return false;
            }
        }
    }
    
    Serial.println("Network not found: " + String(ssid));
    return false;
}

bool connectToWiFi() {
    if (settings.networkCount == 0) {
        Serial.println("No WiFi networks configured");
        return false;
    }
    
    Serial.println("\n=== WiFi Connection Attempt ===");
    
    // اول AP Mode را خاموش کن (اگر فعال است)
    if (apModeActive) {
        Serial.println("Disabling AP Mode for WiFi connection...");
        WiFi.softAPdisconnect(true);
        apModeActive = false;
        delay(1000);
    }
    
    WiFi.disconnect(true);
    delay(1000);
    
    // تنظیم فقط به حالت STA
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);
    delay(500);
    
    int startIndex = (settings.lastConnectedIndex != -1) ? settings.lastConnectedIndex : 0;
    
    for (int attempt = 0; attempt < settings.networkCount; attempt++) {
        int currentIndex = (startIndex + attempt) % settings.networkCount;
        
        if (!settings.networks[currentIndex].configured) continue;
        
        const char* ssid = settings.networks[currentIndex].ssid;
        const char* password = settings.networks[currentIndex].password;
        
        if (strlen(ssid) == 0) continue;
        
        Serial.println("\nAttempt " + String(attempt + 1) + "/" + String(settings.networkCount) + 
                      ": Connecting to: " + String(ssid));
        
        WiFi.begin(ssid, password);
        
        int attempts = 0;
        bool connected = false;
        
        while (attempts < 30) { // 15 ثانیه
            delay(500);
            attempts++;
            
            if (WiFi.status() == WL_CONNECTED) {
                connected = true;
                break;
            }
        }
        
        if (connected) {
            // === اصلاح مهم: حتماً متغیرها را تنظیم کن ===
            isConnectedToWiFi = true;
            apModeActive = false; // AP Mode را غیرفعال کن
            
            settings.lastConnectedIndex = currentIndex;
            settings.networks[currentIndex].lastConnected = millis();
            saveSettings();
            
            Serial.println("\n✅ WiFi Connected Successfully!");
            Serial.println("SSID: " + WiFi.SSID());
            Serial.println("IP: " + WiFi.localIP().toString());
            Serial.println("isConnectedToWiFi: " + String(isConnectedToWiFi));
            Serial.println("apModeActive: " + String(apModeActive));
            
            // راه‌اندازی NTP
            configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
            delay(2000);
            
            return true;
        } else {
            Serial.println("❌ Failed to connect to: " + String(ssid));
            WiFi.disconnect(true);
            delay(1000);
        }
    }
    
    // اگر همه شکست خوردند
    Serial.println("❌ All WiFi attempts failed");
    isConnectedToWiFi = false;
    
    return false;
}

bool startAPMode() {
    Serial.println("\n=== Starting AP Mode ===");
    
    // اول WiFi STA را قطع کن
    WiFi.disconnect(true);
    delay(1000);
    
    // به حالت AP برو
    WiFi.mode(WIFI_AP);
    delay(1000);
    
    // تنظیمات AP
    IPAddress local_IP(192, 168, 4, 1);
    IPAddress gateway(192, 168, 4, 1);
    IPAddress subnet(255, 255, 255, 0);
    
    if (!WiFi.softAPConfig(local_IP, gateway, subnet)) {
        Serial.println("AP Config failed!");
        return false;
    }
    
    bool apStarted = WiFi.softAP("ESP32-Pfolio", "12345678", 1, 0, 4);
    
    if (!apStarted) {
        Serial.println("AP failed to start!");
        return false;
    }
    
    delay(2000);
    
    // === متغیرهای حالت را تنظیم کن ===
    apModeActive = true;
    isConnectedToWiFi = false; // در AP Mode به WiFi وصل نیستیم
    
    Serial.println("✅ AP Mode started");
    Serial.println("SSID: ESP32-Pfolio");
    Serial.println("IP: 192.168.4.1");
    Serial.println("apModeActive: " + String(apModeActive));
    Serial.println("isConnectedToWiFi: " + String(isConnectedToWiFi));
    
    return true;
}


void scanWiFiNetworks() {
    Serial.println("\n=== SCANNING FOR WiFi NETWORKS ===");
    
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);
    
    int n = WiFi.scanNetworks();
    Serial.println("Scan completed");
    
    if (n == 0) {
        Serial.println("No networks found");
    } else {
        Serial.print(n);
        Serial.println(" networks found:");
        
        for (int i = 0; i < n; ++i) {
            Serial.print(i + 1);
            Serial.print(": ");
            Serial.print(WiFi.SSID(i));
            Serial.print(" (");
            Serial.print(WiFi.RSSI(i));
            Serial.print(" dBm) Ch:");
            Serial.print(WiFi.channel(i));
            Serial.print(" ");
            Serial.println((WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "OPEN" : "SECURED");
        }
    }
    
    // برگرد به حالت AP
    WiFi.mode(WIFI_AP);
    delay(100);
}

// ==================== DATA PROCESSING FUNCTIONS ====================
String getShortSymbol(const char* symbol) {
    String s = String(symbol);
    if (s.endsWith("_USDT")) s = s.substring(0, s.length() - 5);
    if (s.endsWith("USDT")) s = s.substring(0, s.length() - 4);
    
    int maxLength = 8;
    int strLength = s.length();
    int substrLength = (maxLength < strLength) ? maxLength : strLength;
    
    return s.substring(0, substrLength);
}

String formatPercent(float percent) {
    return (percent >= 0 ? "+" : "") + String(percent, 1) + "%";
}

String formatNumber(float number) {
    if (fabs(number) >= 1000000) {
        return String(number / 1000000, 1) + "M";
    } else if (fabs(number) >= 1000) {
        return String(number / 1000, 1) + "K";
    } else if (fabs(number) >= 1) {
        return String(number, 1);
    } else {
        return String(number, 2);
    }
}

String formatPrice(float price) {
    if (price <= 0) {
        return "0.00";
    }
    
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

// ==================== TIME FUNCTIONS ====================
String getFullDateTimeString(unsigned long timestamp) {
    if (timestamp == 0) {
        return "01/01 00:00:00";
    }
    
    struct tm timeinfo;
    time_t time_seconds = timestamp / 1000;
    
    if (localtime_r(&time_seconds, &timeinfo)) {
        int year = timeinfo.tm_year + 1900;
        if (year < 2023 || year > 2030) {
            return "01/01 00:00:00";
        }
        
        char dateTimeStr[30];
        snprintf(dateTimeStr, sizeof(dateTimeStr), "%02d/%02d %02d:%02d:%02d", 
                 timeinfo.tm_mon + 1,
                 timeinfo.tm_mday,
                 timeinfo.tm_hour,
                 timeinfo.tm_min,
                 timeinfo.tm_sec);
        return String(dateTimeStr);
    }
    
    return "01/01 00:00:00";
}

String getSimpleTimeString(unsigned long timestamp) {
    if (timestamp == 0) return "00:00:00";
    
    unsigned long totalSeconds = timestamp / 1000;
    unsigned long hours = (totalSeconds / 3600) % 24;
    unsigned long minutes = (totalSeconds % 3600) / 60;
    unsigned long seconds = totalSeconds % 60;
    
    char timeStr[20];
    snprintf(timeStr, sizeof(timeStr), "%02lu:%02lu:%02lu", hours, minutes, seconds);
    return String(timeStr);
}

void updateDateTime() {
    static bool timeSynced = false;
    
    if (!isConnectedToWiFi) {
        currentDateTime = "No WiFi";
        return;
    }
    
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 100)) {
        char timeString[25];
        strftime(timeString, sizeof(timeString), "%Y/%m/%d %H:%M:%S", &timeinfo);
        currentDateTime = String(timeString);
        timeSynced = true;
    } else {
        currentDateTime = "Time Sync Failed";
        
        // اگر زمان sync نشده، یک بار تلاش کن
        if (!timeSynced) {
            configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
            delay(100);
        }
    }
}

void printCurrentTime24Hour() {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 100)) {
        char timeString[25];
        strftime(timeString, sizeof(timeString), "%H:%M:%S", &timeinfo);
        Serial.println("Current time (24h): " + String(timeString));
    }
}

String getTimeString(unsigned long timestamp) {
    if (timestamp == 0) {
        return "Never";
    }
    
    unsigned long secondsAgo = (millis() - timestamp) / 1000;
    
    if (secondsAgo < 60) {
        return String(secondsAgo) + "s ago";
    } else if (secondsAgo < 3600) {
        unsigned long minutes = secondsAgo / 60;
        unsigned long seconds = secondsAgo % 60;
        return String(minutes) + "m " + String(seconds) + "s ago";
    } else if (secondsAgo < 86400) {
        unsigned long hours = secondsAgo / 3600;
        unsigned long minutes = (secondsAgo % 3600) / 60;
        return String(hours) + "h " + String(minutes) + "m ago";
    } else {
        unsigned long days = secondsAgo / 86400;
        unsigned long hours = (secondsAgo % 86400) / 3600;
        return String(days) + "d " + String(hours) + "h ago";
    }
}

String getExactTimeString(unsigned long timestamp) {
    if (timestamp == 0) {
        return "00:00";
    }
    
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 5000)) {
        unsigned long seconds = timestamp / 1000;
        unsigned long minutes = seconds / 60;
        unsigned long hours = minutes / 60;
        
        minutes = minutes % 60;
        hours = hours % 24;
        
        char timeStr[20];
        snprintf(timeStr, sizeof(timeStr), "%02lu:%02lu", hours, minutes);
        return String(timeStr);
    }
    
    time_t time_seconds = timestamp / 1000;
    localtime_r(&time_seconds, &timeinfo);
    
    char timeStr[20];
    strftime(timeStr, sizeof(timeStr), "%H:%M", &timeinfo);
    return String(timeStr);
}

void updateCryptoDisplay() {
    if (cryptoCount == 0) {
        currentCryptoDisplay = "No data";
        return;
    }
    
    if (!dataSorted && cryptoCount > 0) {
        sortCryptosByLoss();
    }
    
    int displayCount = min(DISPLAY_CRYPTO_COUNT, cryptoCount);
    if (displayCount == 0) {
        currentCryptoDisplay = "No data";
        return;
    }
    
    if (currentDisplayIndex == -1) {
        currentDisplayIndex = 0;
    } else {
        currentDisplayIndex = (currentDisplayIndex + 1) % displayCount;
    }
    
    CryptoPosition crypto = sortedCryptoData[currentDisplayIndex];
    String symbol = getShortSymbol(crypto.symbol);
    String change = formatPercent(crypto.changePercent);
    
    int rank = currentDisplayIndex + 1;
    String displayStr = String(rank) + "." + symbol + " " + change;
    
    if (crypto.isLong) {
        displayStr += " L";
    } else {
        displayStr += " S";
    }
    
    if (crypto.alerted) {
        displayStr += " ALRT";
    }
    
    currentCryptoDisplay = displayStr;
    
    static int lastDisplayedIndex = -1;
    if (currentDisplayIndex != lastDisplayedIndex) {
        Serial.print("Displaying #");
        Serial.print(rank);
        Serial.print(": ");
        Serial.print(symbol);
        Serial.print(" ");
        Serial.print(change);
        Serial.print(" ");
        Serial.print(crypto.isLong ? "LONG" : "SHORT");
        Serial.print(" | Price: ");
        Serial.print(formatPrice(crypto.currentPrice));
        Serial.print(" | Alerted: ");
        Serial.println(crypto.alerted ? "YES" : "NO");
        lastDisplayedIndex = currentDisplayIndex;
    }
}

void sortCryptosByLoss() {
    if (cryptoCount == 0) {
        Serial.println("No cryptos to sort!");
        return;
    }
    
    for (int i = 0; i < cryptoCount; i++) {
        sortedCryptoData[i] = cryptoData[i];
    }
    
    Serial.println("Sorting " + String(cryptoCount) + " cryptos by loss...");
    
    bool swapped;
    for (int i = 0; i < cryptoCount - 1; i++) {
        swapped = false;
        for (int j = 0; j < cryptoCount - i - 1; j++) {
            if (sortedCryptoData[j].changePercent > sortedCryptoData[j+1].changePercent) {
                CryptoPosition temp = sortedCryptoData[j];
                sortedCryptoData[j] = sortedCryptoData[j+1];
                sortedCryptoData[j+1] = temp;
                swapped = true;
            }
        }
        
        if (!swapped) {
            break;
        }
    }
    
    dataSorted = true;
    Serial.println("Sorting completed for " + String(cryptoCount) + " cryptos");
}

void checkCryptoAlerts() {
    if (cryptoCount == 0 || !isConnectedToWiFi) {
        Serial.println("Cannot check alerts: No data or no WiFi");
        return;
    }
    
    if (settings.isExitMode) {
        checkExitModeAlerts();
    } else {
        checkEntryModeAlerts();
    }
}

void checkEntryModeAlerts() {
    Serial.println("\n🔔 Checking ENTRY MODE alerts for all " + String(cryptoCount) + " positions");
    
    bool newAlertTriggered = false;
    
    for (int i = 0; i < cryptoCount && !newAlertTriggered; i++) {
        CryptoPosition* crypto = &sortedCryptoData[i];
        
        if (!crypto->alerted && crypto->changePercent <= settings.alertThreshold) {
            bool isSevere = crypto->changePercent <= settings.severeAlertThreshold;
            
            Serial.println("⚠️ Triggering alert for: " + String(crypto->symbol));
            Serial.println("    P/L%: " + String(crypto->changePercent, 1) + "%");
            Serial.println("    Type: " + String(crypto->isLong ? "LONG" : "SHORT"));
            Serial.println("    Severity: " + String(isSevere ? "SEVERE" : "NORMAL"));
            Serial.println("    Price: " + formatPrice(crypto->currentPrice));
            
            String alertType = crypto->isLong ? "LONG ALERT" : "SHORT ALERT";
            if (isSevere) alertType = "SEVERE " + alertType;
            
            String symbol = getShortSymbol(crypto->symbol);
            String changeStr = formatPercent(crypto->changePercent);
            
            String severityMsg = isSevere ? "CRITICAL LOSS!" : "Below threshold";
            
            String positionType = crypto->isLong ? "Long position" : "Short position";
            String alertMsg1 = symbol + " " + positionType;
            
            if (alertMsg1.length() > 30) {
                alertMsg1 = symbol + " " + (crypto->isLong ? "LONG" : "SHORT");
            }
            
            showAlert(alertType,
                      alertMsg1,
                      changeStr + " P/L",
                      severityMsg,
                      crypto->isLong,
                      isSevere,
                      crypto->currentPrice);
            
            crypto->alerted = true;
            crypto->severeAlerted = isSevere;
            crypto->lastAlertTime = millis();
            crypto->lastAlertPrice = crypto->currentPrice;
            
            for (int j = 0; j < cryptoCount; j++) {
                if (strcmp(cryptoData[j].symbol, crypto->symbol) == 0) {
                    cryptoData[j].alerted = true;
                    cryptoData[j].severeAlerted = isSevere;
                    cryptoData[j].lastAlertTime = millis();
                    cryptoData[j].lastAlertPrice = crypto->currentPrice;
                    break;
                }
            }
            
            newAlertTriggered = true;
        }
    }
    
    if (portfolio.totalPnlPercent <= settings.portfolioAlertThreshold) {
        bool isPortfolioSevere = portfolio.totalPnlPercent <= (settings.portfolioAlertThreshold * 1.5);
        
        Serial.println("📉 PORTFOLIO ALERT!");
        Serial.println("    Total P/L%: " + String(portfolio.totalPnlPercent, 1) + "%");
        Serial.println("    Threshold: " + String(settings.portfolioAlertThreshold, 1) + "%");
        
        showAlert("PORTFOLIO ALERT",
                  "Total P/L: " + formatPercent(portfolio.totalPnlPercent),
                  "Value: $" + formatNumber(portfolio.totalCurrentValue),
                  "Check all positions",
                  true,
                  isPortfolioSevere,
                  portfolio.totalCurrentValue);
        
        playPortfolioAlert();
    }
    
    if (!newAlertTriggered) {
        Serial.println("✅ No new alerts triggered");
    }
}

void debugAlertHistory() {
    Serial.println("\n=== DEBUG: Alert History ===");
    Serial.println("Total alerts: " + String(alertHistoryCount));
    
    if (alertHistoryCount == 0) {
        Serial.println("No alerts in history");
        Serial.println("=========================\n");
        return;
    }
    
    Serial.println("Index | Symbol | Mode | Position | Timestamp | Time String | P/L% | Alert Type");
    Serial.println("------------------------------------------------------------------------------");
    
    for (int i = 0; i < alertHistoryCount; i++) {
        AlertHistory* alert = &alertHistory[i];
        
        String modeStr = (alert->alertMode == 0) ? "ENTRY" : "EXIT";
        String positionStr = alert->isLong ? "LONG" : "SHORT";
        String alertTypeStr = "";
        
        if (alert->alertType == 1) alertTypeStr = "NORMAL";
        else if (alert->alertType == 2) alertTypeStr = "SEVERE";
        else if (alert->alertType == 3) alertTypeStr = "PROFIT";
        else if (alert->alertType == 4) alertTypeStr = "LOSS";
        else alertTypeStr = "UNKNOWN";
        
        Serial.print(i);
        Serial.print(" | ");
        Serial.print(alert->symbol);
        Serial.print(" | ");
        Serial.print(modeStr);
        Serial.print(" | ");
        Serial.print(positionStr);
        Serial.print(" | ");
        Serial.print(alert->alertTime);
        Serial.print(" | ");
        Serial.print(alert->timeString);
        Serial.print(" | ");
        Serial.print(alert->pnlPercent, 1);
        Serial.print("% | ");
        Serial.println(alertTypeStr);
    }
    
    Serial.println("=========================\n");
}

void fixOldTimestamps() {
    Serial.println("\n=== Fixing Old Alert Timestamps ===");
    
    int fixedCount = 0;
    unsigned long baseTimestamp = getCorrectTimestamp();
    
    for (int i = 0; i < alertHistoryCount; i++) {
        AlertHistory* alert = &alertHistory[i];
        
        if (alert->alertTime < 1000000000000UL) {
            Serial.println("Fixing alert " + String(i) + ": " + String(alert->symbol));
            Serial.println("  Old timestamp: " + String(alert->alertTime));
            
            if (alert->alertTime < 1000000000UL) {
                alert->alertTime = alert->alertTime * 1000;
                Serial.println("  Fixed (multiplied by 1000): " + String(alert->alertTime));
            } else {
                unsigned long timeOffset = (alertHistoryCount - i - 1) * 300000;
                alert->alertTime = baseTimestamp - timeOffset;
                Serial.println("  New timestamp: " + String(alert->alertTime));
            }
            
            fixedCount++;
        }
    }
    
    if (fixedCount > 0) {
        Serial.println("Fixed " + String(fixedCount) + " alert timestamps");
    } else {
        Serial.println("No timestamps needed fixing");
    }
    
    Serial.println("==================================\n");
}

void checkExitModeAlerts() {
    if (cryptoCount == 0 || !isConnectedToWiFi || !settings.exitAlertEnabled) {
        return;
    }
    
    Serial.println("\n💰 Checking EXIT MODE alerts for all " + String(cryptoCount) + " positions");
    Serial.println("Exit Alert Threshold: " + String(settings.exitAlertPercent) + "% price change");
    
    bool newAlertTriggered = false;
    
    for (int i = 0; i < cryptoCount && !newAlertTriggered; i++) {
        CryptoPosition* crypto = &sortedCryptoData[i];
        
        if (crypto->exitAlertLastPrice == 0) {
            crypto->exitAlertLastPrice = crypto->currentPrice;
            Serial.println("  Initializing exit alert price for " + String(crypto->symbol) + 
                          " (" + String(crypto->isLong ? "LONG" : "SHORT") + "): " + 
                          formatPrice(crypto->currentPrice));
            continue;
        }
        
        float priceChangePercent = fabs((crypto->currentPrice - crypto->exitAlertLastPrice) / crypto->exitAlertLastPrice * 100);
        
        Serial.println("  Checking " + String(crypto->symbol) + 
                      " (" + String(crypto->isLong ? "LONG" : "SHORT") + "): " +
                      "Current=" + formatPrice(crypto->currentPrice) + 
                      ", Last Alert=" + formatPrice(crypto->exitAlertLastPrice) + 
                      ", Change=" + String(priceChangePercent, 2) + "%" +
                      ", Threshold=" + String(settings.exitAlertPercent) + "%");
        
        if (priceChangePercent >= settings.exitAlertPercent) {
            
            Serial.println("💰 Triggering EXIT alert for: " + String(crypto->symbol));
            Serial.println("    Position: " + String(crypto->isLong ? "LONG" : "SHORT"));
            Serial.println("    Entry Price: " + formatPrice(crypto->entryPrice));
            Serial.println("    Current Price: " + formatPrice(crypto->currentPrice));
            Serial.println("    Last Exit Alert Price: " + formatPrice(crypto->exitAlertLastPrice));
            Serial.println("    Price Change from Last Alert: " + String(priceChangePercent, 1) + "%");
            
            float totalPnlPercent = 0.0;
            if (crypto->entryPrice > 0) {
                if (crypto->isLong) {
                    totalPnlPercent = ((crypto->currentPrice - crypto->entryPrice) / crypto->entryPrice) * 100;
                } else {
                    totalPnlPercent = ((crypto->entryPrice - crypto->currentPrice) / crypto->entryPrice) * 100;
                }
                Serial.println("    TOTAL P/L% from entry: " + String(totalPnlPercent, 1) + "%");
            } else {
                Serial.println("    ⚠️ Entry price is 0, cannot calculate total P/L");
                totalPnlPercent = crypto->changePercent;
            }
            
            bool isProfitFromLastAlert;
            
            if (crypto->isLong) {
                isProfitFromLastAlert = (crypto->currentPrice > crypto->exitAlertLastPrice);
                Serial.println("    LONG position logic: Price " + 
                              String(crypto->currentPrice > crypto->exitAlertLastPrice ? "increased" : "decreased") + 
                              " = " + String(isProfitFromLastAlert ? "PROFIT" : "LOSS"));
            } else {
                isProfitFromLastAlert = (crypto->currentPrice < crypto->exitAlertLastPrice);
                Serial.println("    SHORT position logic: Price " + 
                              String(crypto->currentPrice < crypto->exitAlertLastPrice ? "decreased" : "increased") + 
                              " = " + String(isProfitFromLastAlert ? "PROFIT" : "LOSS"));
            }
            
            String alertType = isProfitFromLastAlert ? "PROFIT ALERT" : "LOSS ALERT";
            
            String symbol = getShortSymbol(crypto->symbol);
            
            showExitAlert(alertType,
                         symbol + " " + (crypto->isLong ? "LONG" : "SHORT") + " " + (isProfitFromLastAlert ? "PROFIT" : "LOSS"),
                         "Total P/L: " + String(totalPnlPercent, 1) + "% from entry",
                         "Change: " + String(priceChangePercent, 1) + "% from last alert",
                         isProfitFromLastAlert,
                         totalPnlPercent,
                         crypto->currentPrice);
            
            crypto->lastAlertTime = millis();
            crypto->exitAlertLastPrice = crypto->currentPrice;
            crypto->exitAlerted = true;
            
            for (int j = 0; j < cryptoCount; j++) {
                if (strcmp(cryptoData[j].symbol, crypto->symbol) == 0) {
                    cryptoData[j].lastAlertTime = millis();
                    cryptoData[j].exitAlertLastPrice = crypto->currentPrice;
                    cryptoData[j].exitAlerted = true;
                    break;
                }
            }
            
            Serial.println("  ✅ Updated exit alert price for " + String(crypto->symbol) + 
                          " to: " + formatPrice(crypto->currentPrice));
            
            newAlertTriggered = true;
        }
    }
    
    if (!newAlertTriggered) {
        Serial.println("✅ No exit alerts triggered");
    }
}

void debugRawJSON(String jsonData) {
    Serial.println("\n=== DEBUG: Raw JSON Data ===");
    
    int displayLength = min(1000, (int)jsonData.length());
    String partialData = jsonData.substring(0, displayLength);
    
    Serial.println("First " + String(displayLength) + " characters of JSON:");
    Serial.println(partialData);
    
    if (jsonData.length() > displayLength) {
        Serial.println("... (truncated)");
    }
    
    DynamicJsonDocument doc(3000);
    DeserializationError error = deserializeJson(doc, jsonData);
    
    if (!error) {
        Serial.println("\nJSON Structure Analysis:");
        
        if (doc.containsKey("portfolio")) {
            JsonArray portfolio = doc["portfolio"];
            Serial.println("✓ Portfolio array found");
            Serial.println("  Size: " + String(portfolio.size()) + " items");
        } else {
            Serial.println("✗ 'portfolio' field NOT FOUND");
        }
    } else {
        Serial.println("JSON Parse Error: " + String(error.c_str()));
    }
    
    Serial.println("===========================\n");
}

void resetAllAlerts() {
    Serial.println("Resetting all alerts...");
    
    for (int i = 0; i < cryptoCount; i++) {
        cryptoData[i].alerted = false;
        cryptoData[i].severeAlerted = false;
        cryptoData[i].lastAlertTime = 0;
        cryptoData[i].lastAlertPrice = 0;
        cryptoData[i].exitAlerted = false;
        cryptoData[i].exitAlertLastPrice = cryptoData[i].currentPrice;
        
        sortedCryptoData[i].alerted = false;
        sortedCryptoData[i].severeAlerted = false;
        sortedCryptoData[i].lastAlertTime = 0;
        sortedCryptoData[i].lastAlertPrice = 0;
        sortedCryptoData[i].exitAlerted = false;
        sortedCryptoData[i].exitAlertLastPrice = sortedCryptoData[i].currentPrice;
    }
    
    resetDisplayToFirstPosition();
    resetAlertLEDs();
    
    showTFTMessage("ALERTS RESET", "All position alerts", "have been cleared", "");
    
    if (settings.buzzerEnabled) {
        playResetAlertTone();
    }
    
    Serial.println("All alerts have been reset");
    Serial.println("Exit Mode: Reset all exit alert prices to current prices");
}

bool detectNewPortfolio() {
    if (cryptoCount < 5) return false;
    
    int newPositions = 0;
    for (int i = 0; i < cryptoCount; i++) {
        if (fabs(cryptoData[i].changePercent) < 1.0) {
            newPositions++;
        }
    }
    
    float newPercentage = (float)newPositions / cryptoCount * 100;
    bool isNewPortfolio = newPercentage >= 80.0;
    
    if (isNewPortfolio) {
        Serial.println("New portfolio detected: " + String(newPercentage, 1) + "% new positions");
    }
    
    return isNewPortfolio;
}

// ==================== NEW API FUNCTIONS ====================
String getEntryModeData() {
    if (!isConnectedToWiFi) {
        Serial.println("❌ Cannot get data - WiFi not connected");
        return "{\"error\":\"no_wifi\"}";
    }
    
    if (strlen(settings.server) == 0 || strlen(settings.username) == 0) {
        Serial.println("❌ API not configured");
        return "{\"error\":\"no_api_config\"}";
    }
    
    String url = String(settings.server) + "/api/device/portfolio/" + 
                String(settings.username) + "?portfolio_name=" + 
                String(settings.entryPortfolio);
    
    Serial.println("\n=== Fetching ENTRY MODE Data ===");
    Serial.println("URL: " + url);
    
    // پاک کردن اتصال قبلی
    http.end();
    delay(100);
    
    // شروع اتصال جدید
    http.begin(url);
    
    // تنظیم timeout کوتاه
    http.setTimeout(3000);
    http.setConnectTimeout(2000);
    http.setReuse(false);
    
    // افزودن هدرها
    String authString = String(settings.username) + ":" + String(settings.userpass);
    String encodedAuth = base64Encode(authString);
    http.addHeader("Authorization", "Basic " + encodedAuth);
    http.addHeader("Accept", "application/json");
    
    Serial.println("Sending GET request with 3-second timeout...");
    
    int httpCode = 0;
    String response = "{}";
    
    unsigned long startTime = millis();
    
    try {
        httpCode = http.GET();
        
        if (httpCode > 0) {
            response = http.getString();
            Serial.println("HTTP Code: " + String(httpCode));
            
            if (httpCode == 200) {
                Serial.println("✅ Success! Response length: " + String(response.length()));
            } else {
                Serial.println("❌ HTTP Error: " + String(httpCode));
                response = "{\"error\":\"http_" + String(httpCode) + "\"}";
            }
        } else {
            Serial.println("❌ Connection failed: " + String(http.errorToString(httpCode)));
            response = "{\"error\":\"conn_failed\",\"msg\":\"" + String(http.errorToString(httpCode)) + "\"}";
        }
    } catch (...) {
        Serial.println("❌ Exception in HTTP request!");
        response = "{\"error\":\"exception\"}";
    }
    
    unsigned long elapsedTime = millis() - startTime;
    Serial.println("Request completed in: " + String(elapsedTime) + "ms");
    
    http.end();
    
    return response;
}

enum SystemState {
    STATE_IDLE,
    STATE_FETCHING,
    STATE_PARSING,
    STATE_ALERT_CHECKING,
    STATE_ERROR
};

SystemState currentState = STATE_IDLE;

void handleStateMachine() {
    static SystemState currentState = STATE_IDLE;
    static unsigned long lastStateChange = 0;
    static int fetchFailCount = 0;
    
    switch(currentState) {
        case STATE_IDLE:
            if (isConnectedToWiFi && (millis() - lastDataUpdate > DATA_UPDATE_INTERVAL)) {
                currentState = STATE_FETCHING;
                lastStateChange = millis();
                Serial.println("State: IDLE -> FETCHING");
            } else if (!isConnectedToWiFi && apModeActive) {
                // در حالت AP، فقط وب سرور را مدیریت کن
                server.handleClient();
            }
            break;
            
        case STATE_FETCHING:
            if (millis() - lastStateChange > 5000) {  // حداکثر 5 ثانیه
                Serial.println("⚠️ Fetching timeout");
                fetchFailCount++;
                currentState = STATE_IDLE;
                lastDataUpdate = millis();
                
                // اگر چند بار خطا خورد، به AP mode برو
                if (fetchFailCount >= 3 && isConnectedToWiFi) {
                    Serial.println("Too many fetch failures, switching to AP mode...");
                    fetchFailCount = 0;
                    startAPMode();
                }
                break;
            }
            
            String data = getPortfolioData();
            
            if (data.indexOf("error") == -1) {
                // موفق
                fetchFailCount = 0;
                currentState = STATE_PARSING;
                parseCryptoData(data);
                currentState = STATE_ALERT_CHECKING;
                checkCryptoAlerts();
                currentState = STATE_IDLE;
                lastDataUpdate = millis();
                Serial.println("✅ Data fetch successful");
            } else {
                // خطا
                fetchFailCount++;
                Serial.println("❌ Data fetch failed (count: " + String(fetchFailCount) + ")");
                
                // اگر در حالت WiFi هستیم و چند بار خطا خوردیم، به AP برویم
                if (fetchFailCount >= 3 && isConnectedToWiFi && !apModeActive) {
                    Serial.println("Switching to AP mode due to repeated failures...");
                    showTFTMessage("API Failed", "Switching to", "AP Mode...", "", "");
                    startAPMode();
                }
                
                currentState = STATE_IDLE;
                lastDataUpdate = millis() - (DATA_UPDATE_INTERVAL - 30000); // 30 ثانیه بعد دوباره تلاش کن
            }
            break;
    }
}

String getExitModeData() {
    if (!isConnectedToWiFi) {
        return "{}";
    }
    
    if (strlen(settings.server) == 0 || strlen(settings.username) == 0) {
        return "{}";
    }
    
    String url = String(settings.server) + "/api/device/portfolio/" + 
                String(settings.username) + "?portfolio_name=" + 
                String(settings.exitPortfolio);
    
    Serial.println("\n=== Fetching EXIT MODE Data ===");
    Serial.println("URL: " + url);
    Serial.println("Portfolio: " + String(settings.exitPortfolio));
    
    showTFTMessage("Fetching", "Exit Mode", "Portfolio:", String(settings.exitPortfolio), "");
    
    http.begin(url);
    
    String authString = String(settings.username) + ":" + String(settings.userpass);
    String encodedAuth = base64Encode(authString);
    http.addHeader("Authorization", "Basic " + encodedAuth);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("User-Agent", "ESP32-Portfolio-Monitor");
    
    // افزایش timeout ها
    http.setTimeout(120000); // 120 ثانیه
    http.setConnectTimeout(30000); // 30 ثانیه
    http.setReuse(false);
    
    int httpCode = http.GET();
    String result = "{}";
    
    Serial.println("HTTP Code: " + String(httpCode));
    
    if (httpCode == 200) {
        result = http.getString();
        Serial.println("✅ Exit data received, length: " + String(result.length()));
    } else if (httpCode > 0) {
        Serial.println("❌ HTTP Error: " + String(httpCode));
        showTFTMessage("Exit Mode", "HTTP Error", String(httpCode), "", "");
        result = "{\"error\": \"HTTP " + String(httpCode) + "\"}";
    } else {
        Serial.println("❌ Connection failed: " + String(http.errorToString(httpCode)));
        showTFTMessage("Exit Mode", "Connection", "Failed", String(http.errorToString(httpCode)), "");
        result = "{\"error\": \"Connection failed\", \"code\": " + String(httpCode) + "}";
    }
    
    http.end();
    delay(1000);
    
    return result;
}

void parseExitModeData(String jsonData) {
    DynamicJsonDocument doc(JSON_BUFFER_SIZE);
    DeserializationError error = deserializeJson(doc, jsonData);
    
    if (error) {
        Serial.println("JSON Parse Error: " + String(error.c_str()));
        Serial.println("Buffer size: " + String(JSON_BUFFER_SIZE));
        return;
    }
    
    if (!doc.containsKey("portfolio")) {
        Serial.println("No 'portfolio' field in response (Exit Mode)");
        return;
    }
    
    JsonArray portfolioArray = doc["portfolio"];
    int receivedCount = portfolioArray.size();
    cryptoCount = min(receivedCount, MAX_CRYPTO);
    
    Serial.println("Received " + String(receivedCount) + " positions for exit tracking, using " + String(cryptoCount) + " (Max: " + String(MAX_CRYPTO) + ")");
    Serial.println("Portfolio: " + String(settings.exitPortfolio));
    
    CryptoPosition previousData[MAX_CRYPTO];
    for (int i = 0; i < min(cryptoCount, MAX_CRYPTO); i++) {
        previousData[i] = cryptoData[i];
    }
    
    clearCryptoData();
    
    int longCount = 0;
    int shortCount = 0;
    
    for (int i = 0; i < cryptoCount; i++) {
        JsonObject position = portfolioArray[i];
        
        const char* symbol = position["symbol"] | "UNKNOWN";
        strncpy(cryptoData[i].symbol, symbol, 15);
        cryptoData[i].symbol[15] = '\0';
        
        cryptoData[i].changePercent = position["pnl_percent"] | 0.0;
        cryptoData[i].currentPrice = position["current_price"] | 0.0;
        
        bool isLongFromJSON = true;
        bool positionFound = false;
        
        if (position.containsKey("position")) {
            const char* positionType = position["position"];
            String posTypeStr = String(positionType);
            posTypeStr.toUpperCase();
            posTypeStr.trim();
            
            if (posTypeStr == "SHORT" || posTypeStr == "SELL") {
                isLongFromJSON = false;
            } else if (posTypeStr == "LONG" || posTypeStr == "BUY") {
                isLongFromJSON = true;
            }
            positionFound = true;
        }
        
        if (!positionFound && position.containsKey("position_side")) {
            const char* positionSide = position["position_side"];
            String sideStr = String(positionSide);
            sideStr.toUpperCase();
            sideStr.trim();
            
            if (sideStr == "SHORT") {
                isLongFromJSON = false;
            } else if (sideStr == "LONG") {
                isLongFromJSON = true;
            }
            positionFound = true;
        }
        
        if (!positionFound && position.containsKey("side")) {
            const char* side = position["side"];
            String sideStr = String(side);
            sideStr.toUpperCase();
            sideStr.trim();
            
            if (sideStr == "SELL" || sideStr == "SHORT") {
                isLongFromJSON = false;
            } else if (sideStr == "BUY" || sideStr == "LONG") {
                isLongFromJSON = true;
            }
            positionFound = true;
        }
        
        if (!positionFound && position.containsKey("quantity")) {
            float quantity = position["quantity"] | 0.0;
            if (quantity < 0) {
                isLongFromJSON = false;
            } else if (quantity > 0) {
                isLongFromJSON = true;
            }
            positionFound = true;
        }
        
        if (!positionFound) {
            isLongFromJSON = true;
        }
        
        cryptoData[i].isLong = isLongFromJSON;
        
        if (cryptoData[i].isLong) {
            longCount++;
        } else {
            shortCount++;
        }
        
        cryptoData[i].pnlValue = position["pnl"] | 0.0;
        cryptoData[i].quantity = position["quantity"] | 0.0;
        cryptoData[i].entryPrice = position["entry_price"] | 0.0;
        
        const char* positionSide = position["position_side"] | (isLongFromJSON ? "LONG" : "SHORT");
        strncpy(cryptoData[i].positionSide, positionSide, 11);
        cryptoData[i].positionSide[11] = '\0';
        
        const char* marginType = position["margin_type"] | "ISOLATED";
        strncpy(cryptoData[i].marginType, marginType, 11);
        cryptoData[i].marginType[11] = '\0';
        
        bool foundInPrevious = false;
        for (int j = 0; j < min(cryptoCount, MAX_CRYPTO); j++) {
            if (strcmp(previousData[j].symbol, cryptoData[i].symbol) == 0) {
                foundInPrevious = true;
                cryptoData[i].exitAlerted = previousData[j].exitAlerted;
                cryptoData[i].exitAlertLastPrice = previousData[j].exitAlertLastPrice;
                cryptoData[i].lastAlertTime = previousData[j].lastAlertTime;
                break;
            }
        }
        
        if (!foundInPrevious) {
            cryptoData[i].exitAlerted = false;
            cryptoData[i].exitAlertLastPrice = cryptoData[i].currentPrice;
            cryptoData[i].lastAlertTime = 0;
        }
        
        Serial.print("  [");
        Serial.print(i + 1);
        Serial.print("] ");
        Serial.print(cryptoData[i].symbol);
        Serial.print(" - ");
        Serial.print(cryptoData[i].isLong ? "LONG" : "SHORT");
        Serial.print(" | Entry: ");
        Serial.print(formatPrice(cryptoData[i].entryPrice));
        Serial.print(" | Current: ");
        Serial.print(formatPrice(cryptoData[i].currentPrice));
        Serial.print(" | P/L%: ");
        Serial.print(cryptoData[i].changePercent, 1);
        Serial.print("%");
        if (cryptoData[i].exitAlerted) {
            Serial.print(" | Last Alert: ");
            Serial.print(formatPrice(cryptoData[i].exitAlertLastPrice));
        }
        Serial.println();
    }
    
    if (doc.containsKey("summary")) {
        JsonObject summary = doc["summary"];
        
        portfolio.totalInvestment = summary["total_investment"] | 0.0;
        portfolio.totalCurrentValue = summary["total_current_value"] | 0.0;
        portfolio.totalPnl = summary["total_pnl"] | 0.0;
        
        if (portfolio.totalInvestment > 0) {
            portfolio.totalPnlPercent = ((portfolio.totalCurrentValue - portfolio.totalInvestment) / portfolio.totalInvestment) * 100;
        } else {
            portfolio.totalPnlPercent = 0.0;
        }
        
        portfolio.totalPositions = summary["total_positions"] | cryptoCount;
        portfolio.longPositions = summary["long_positions"] | longCount;
        portfolio.shortPositions = summary["short_positions"] | shortCount;
        portfolio.winningPositions = summary["winning_positions"] | 0;
        portfolio.losingPositions = summary["losing_positions"] | 0;
        
        totalInvestmentStr = formatNumber(portfolio.totalInvestment);
        totalValueStr = formatNumber(portfolio.totalCurrentValue);
        totalPnlStr = formatNumber(portfolio.totalPnl);
    } else {
        portfolio.totalInvestment = 0;
        portfolio.totalCurrentValue = 0;
        portfolio.totalPnl = 0;
        portfolio.totalPnlPercent = 0;
        portfolio.totalPositions = cryptoCount;
        portfolio.longPositions = longCount;
        portfolio.shortPositions = shortCount;
        
        totalInvestmentStr = "0";
        totalValueStr = "0";
        totalPnlStr = "0";
    }
    
    portfolioHeader = String(settings.exitPortfolio) + " " + 
                     String(longCount) + "L/" + String(shortCount) + "S";
    
    Serial.println("\n=== Position Summary ===");
    Serial.println("Total: " + String(cryptoCount) + " positions");
    Serial.println("Long: " + String(longCount) + " positions");
    Serial.println("Short: " + String(shortCount) + " positions");
    Serial.println("========================");
    
    sortCryptosByLoss();
    resetDisplayToFirstPosition();
    
    debugExitModeStatus();
    
    Serial.println("Exit Mode data parsing complete for " + String(cryptoCount) + " positions");
    Serial.println("Long positions: " + String(longCount) + ", Short positions: " + String(shortCount));
    Serial.println("Portfolio: " + String(settings.exitPortfolio));
    Serial.println("Tracking price changes for exit alerts");
}

void parseEntryModeData(String jsonData) {
    DynamicJsonDocument doc(JSON_BUFFER_SIZE);
    DeserializationError error = deserializeJson(doc, jsonData);
    
    if (error) {
        Serial.println("JSON Parse Error: " + String(error.c_str()));
        Serial.println("Buffer size: " + String(JSON_BUFFER_SIZE));
        return;
    }
    
    if (!doc.containsKey("portfolio")) {
        Serial.println("No 'portfolio' field in response (Entry Mode)");
        return;
    }
    
    JsonArray portfolioArray = doc["portfolio"];
    int receivedCount = portfolioArray.size();
    cryptoCount = min(receivedCount, MAX_CRYPTO);
    
    Serial.println("Received " + String(receivedCount) + " portfolio positions, using " + String(cryptoCount) + " (Max: " + String(MAX_CRYPTO) + ")");
    Serial.println("Portfolio Name: " + String(settings.entryPortfolio));
    
    CryptoPosition previousData[MAX_CRYPTO];
    for (int i = 0; i < min(cryptoCount, MAX_CRYPTO); i++) {
        previousData[i] = cryptoData[i];
    }
    
    clearCryptoData();
    
    for (int i = 0; i < cryptoCount; i++) {
        JsonObject position = portfolioArray[i];
        
        const char* symbol = position["symbol"] | "UNKNOWN";
        strncpy(cryptoData[i].symbol, symbol, 15);
        cryptoData[i].symbol[15] = '\0';
        
        cryptoData[i].changePercent = position["pnl_percent"] | 0.0;
        cryptoData[i].pnlValue = position["pnl"] | 0.0;
        cryptoData[i].quantity = position["quantity"] | 0.0;
        cryptoData[i].entryPrice = position["entry_price"] | 0.0;
        cryptoData[i].currentPrice = position["current_price"] | 0.0;
        
        bool isLongFromJSON = true;
        
        if (position.containsKey("position")) {
            const char* positionType = position["position"];
            if (strcasecmp(positionType, "short") == 0) {
                isLongFromJSON = false;
            } else if (strcasecmp(positionType, "long") == 0) {
                isLongFromJSON = true;
            }
        }
        else if (position.containsKey("position_side")) {
            const char* positionSide = position["position_side"];
            isLongFromJSON = (strcasecmp(positionSide, "LONG") == 0);
        }
        else {
            isLongFromJSON = (cryptoData[i].quantity > 0);
        }
        
        cryptoData[i].isLong = isLongFromJSON;
        
        const char* positionSide = position["position_side"] | "LONG";
        strncpy(cryptoData[i].positionSide, positionSide, 11);
        cryptoData[i].positionSide[11] = '\0';
        
        const char* marginType = position["margin_type"] | "ISOLATED";
        strncpy(cryptoData[i].marginType, marginType, 11);
        cryptoData[i].marginType[11] = '\0';
        
        cryptoData[i].alertThreshold = settings.alertThreshold;
        cryptoData[i].severeThreshold = settings.severeAlertThreshold;
        
        bool foundInPrevious = false;
        for (int j = 0; j < min(cryptoCount, MAX_CRYPTO); j++) {
            if (strcmp(previousData[j].symbol, cryptoData[i].symbol) == 0) {
                foundInPrevious = true;
                cryptoData[i].alerted = previousData[j].alerted;
                cryptoData[i].severeAlerted = previousData[j].severeAlerted;
                cryptoData[i].lastAlertTime = previousData[j].lastAlertTime;
                cryptoData[i].lastAlertPrice = previousData[j].lastAlertPrice;
                cryptoData[i].exitAlerted = false;
                cryptoData[i].exitAlertLastPrice = 0;
                break;
            }
        }
        
        if (!foundInPrevious) {
            cryptoData[i].alerted = false;
            cryptoData[i].severeAlerted = false;
            cryptoData[i].lastAlertTime = 0;
            cryptoData[i].lastAlertPrice = 0;
            cryptoData[i].exitAlerted = false;
            cryptoData[i].exitAlertLastPrice = 0;
        }
    }
    
    if (doc.containsKey("summary")) {
        JsonObject summary = doc["summary"];
        
        portfolio.totalInvestment = summary["total_investment"] | 0.0;
        portfolio.totalCurrentValue = summary["total_current_value"] | 0.0;
        portfolio.totalPnl = summary["total_pnl"] | 0.0;
        
        if (portfolio.totalInvestment > 0) {
            portfolio.totalPnlPercent = ((portfolio.totalCurrentValue - portfolio.totalInvestment) / portfolio.totalInvestment) * 100;
        } else {
            portfolio.totalPnlPercent = 0.0;
        }
        
        portfolio.totalPositions = summary["total_positions"] | cryptoCount;
        portfolio.longPositions = summary["long_positions"] | 0;
        portfolio.shortPositions = summary["short_positions"] | 0;
        portfolio.winningPositions = summary["winning_positions"] | 0;
        portfolio.losingPositions = summary["losing_positions"] | 0;
        portfolio.maxDrawdown = summary["max_drawdown"] | 0.0;
        portfolio.sharpeRatio = summary["sharpe_ratio"] | 0.0;
        
        totalInvestmentStr = formatNumber(portfolio.totalInvestment);
        totalValueStr = formatNumber(portfolio.totalCurrentValue);
        totalPnlStr = formatNumber(portfolio.totalPnl);
        
        portfolioHeader = String(settings.entryPortfolio) + " " + 
                         (portfolio.totalPnlPercent >= 0 ? "+" : "") + 
                         String(portfolio.totalPnlPercent, 1) + "%";
    }
    
    if (cryptoCount >= 5) {
        if (detectNewPortfolio()) {
            newPortfolioConfirmCount++;
            Serial.println("New portfolio detected! Count: " + String(newPortfolioConfirmCount));
            
            if (newPortfolioConfirmCount >= NEW_PORTFOLIO_CONFIRMATIONS && !newPortfolioDetected) {
                Serial.println("NEW PORTFOLIO CONFIRMED!");
                newPortfolioDetected = true;
                
                showAlert("NEW PORTFOLIO", 
                         "Detected!", 
                         "Resetting alerts", 
                         "for new positions",
                         true,
                         false,
                         0);
                
                resetAllAlerts();
            }
        } else {
            newPortfolioConfirmCount = 0;
            newPortfolioDetected = false;
        }
    }
    
    sortCryptosByLoss();
    resetDisplayToFirstPosition();
    
    Serial.println("Entry Mode data parsing complete for " + String(cryptoCount) + " positions");
    Serial.println("Portfolio: " + String(settings.entryPortfolio));
    Serial.println("Top " + String(min(DISPLAY_CRYPTO_COUNT, cryptoCount)) + " losses:");
    for (int i = 0; i < min(DISPLAY_CRYPTO_COUNT, cryptoCount); i++) {
        Serial.print("   ");
        Serial.print(i + 1);
        Serial.print(". ");
        Serial.print(sortedCryptoData[i].symbol);
        Serial.print(": ");
        Serial.print(sortedCryptoData[i].changePercent, 1);
        Serial.print("% ");
        Serial.print("Price: ");
        Serial.print(formatPrice(sortedCryptoData[i].currentPrice));
        Serial.print(" ");
        Serial.println(sortedCryptoData[i].isLong ? "LONG" : "SHORT");
    }
}

void clearCryptoData() {
    for (int i = 0; i < MAX_CRYPTO; i++) {
        cryptoData[i].alerted = false;
        cryptoData[i].severeAlerted = false;
    }
    dataSorted = false;
}

String getPortfolioData() {
    // بررسی اولیه شرایط
    if (!isConnectedToWiFi) {
        Serial.println("❌ getPortfolioData: Not connected to WiFi");
        return "{\"error\":\"no_wifi\",\"message\":\"WiFi not connected\"}";
    }
    
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("❌ getPortfolioData: WiFi status is not connected");
        isConnectedToWiFi = false;
        return "{\"error\":\"wifi_disconnected\",\"message\":\"WiFi connection lost\"}";
    }
    
    if (strlen(settings.server) == 0) {
        Serial.println("❌ getPortfolioData: API server not configured");
        return "{\"error\":\"no_server\",\"message\":\"API server not set\"}";
    }
    
    if (strlen(settings.username) == 0) {
        Serial.println("❌ getPortfolioData: Username not configured");
        return "{\"error\":\"no_username\",\"message\":\"Username not set\"}";
    }
    
    // انتخاب پورتفولیو بر اساس مود
    const char* portfolioName = settings.isExitMode ? settings.exitPortfolio : settings.entryPortfolio;
    String modeStr = settings.isExitMode ? "EXIT" : "ENTRY";
    
    // ساخت URL
    String url = String(settings.server);
    
    // اطمینان از اینکه URL با / تمام می‌شود
    if (!url.endsWith("/")) {
        url += "/";
    }
    
    // ساخت URL کامل
    url += "api/device/portfolio/" + String(settings.username);
    url += "?portfolio_name=" + String(portfolioName);
    
    // افزودن timestamp برای جلوگیری از کش
    url += "&t=" + String(millis());
    
    Serial.println("\n=== Fetching Portfolio Data (" + modeStr + " Mode) ===");
    Serial.println("URL: " + url);
    Serial.println("Portfolio: " + String(portfolioName));
    Serial.println("Mode: " + modeStr + " Mode");
    Serial.println("Server: " + String(settings.server));
    Serial.println("Username: " + String(settings.username));
    
    // نمایش روی TFT
    showTFTMessage("Fetching", modeStr + " Data", "Portfolio:", String(portfolioName), "Please wait...");
    
    // پاک کردن اتصال قبلی
    http.end();
    delay(100);
    
    // شروع اتصال جدید
    HTTPClient httpLocal;
    httpLocal.setReuse(false);
    httpLocal.begin(url);
    
    // تنظیم timeout ها
    httpLocal.setTimeout(15000);
    httpLocal.setConnectTimeout(10000);
    
    // ساخت authentication header
    String authString = String(settings.username) + ":" + String(settings.userpass);
    String encodedAuth = base64Encode(authString);
    
    // تنظیم هدرها
    httpLocal.addHeader("Authorization", "Basic " + encodedAuth);
    httpLocal.addHeader("Content-Type", "application/json");
    httpLocal.addHeader("Accept", "application/json");
    httpLocal.addHeader("User-Agent", "ESP32-Portfolio-Monitor/3.8.4");
    httpLocal.addHeader("Cache-Control", "no-cache");
    
    Serial.println("Sending GET request with 15s timeout...");
    Serial.println("Auth: Basic " + encodedAuth.substring(0, 10) + "...");
    
    int httpCode = 0;
    String response = "{}";
    unsigned long requestStartTime = millis();
    
    try {
        // ارسال درخواست
        httpCode = httpLocal.GET();
        unsigned long requestTime = millis() - requestStartTime;
        
        Serial.println("Request completed in: " + String(requestTime) + "ms");
        Serial.println("HTTP Response Code: " + String(httpCode));
        
        // بررسی پاسخ HTTP
        if (httpCode > 0) {
            if (httpCode == HTTP_CODE_OK) {
                response = httpLocal.getString();
                
                Serial.println("✅ Success! Response length: " + String(response.length()) + " bytes");
                
                // لاگ بخشی از پاسخ برای دیباگ
                if (response.length() > 0) {
                    Serial.println("First 200 chars of response:");
                    Serial.println(response.substring(0, min(200, (int)response.length())));
                    
                    // بررسی ساختار JSON
                    if (response.indexOf("{") == -1) {
                        Serial.println("⚠️ Warning: Response doesn't contain JSON!");
                        Serial.println("Response starts with: " + response.substring(0, 50));
                    }
                    
                    // نمایش اطلاعات مفید
                    DynamicJsonDocument doc(500);
                    DeserializationError error = deserializeJson(doc, response);
                    
                    if (!error) {
                        if (doc.containsKey("portfolio")) {
                            JsonArray portfolio = doc["portfolio"];
                            Serial.println("✓ JSON parsed successfully");
                            Serial.println("✓ Portfolio array found with " + String(portfolio.size()) + " items");
                        } else if (doc.containsKey("error")) {
                            Serial.println("✗ API returned error: " + String(doc["error"].as<String>()));
                        }
                    } else {
                        Serial.println("✗ JSON parse error: " + String(error.c_str()));
                    }
                } else {
                    Serial.println("⚠️ Warning: Empty response received");
                    response = "{\"error\":\"empty_response\",\"message\":\"Server returned empty response\"}";
                }
                
            } else if (httpCode == HTTP_CODE_UNAUTHORIZED || httpCode == HTTP_CODE_FORBIDDEN) {
                Serial.println("❌ Authentication failed (HTTP " + String(httpCode) + ")");
                showTFTMessage("API Error", "Auth Failed", "HTTP " + String(httpCode), "Check credentials", "");
                response = "{\"error\":\"auth_failed\",\"http_code\":" + String(httpCode) + "}";
                
            } else if (httpCode == HTTP_CODE_NOT_FOUND) {
                Serial.println("❌ Endpoint not found (HTTP 404)");
                Serial.println("Check API URL and portfolio name");
                showTFTMessage("API Error", "Not Found", "HTTP 404", "Check URL/Portfolio", "");
                response = "{\"error\":\"not_found\",\"http_code\":404}";
                
            } else if (httpCode == HTTP_CODE_BAD_GATEWAY || httpCode == HTTP_CODE_SERVICE_UNAVAILABLE) {
                Serial.println("❌ Server error (HTTP " + String(httpCode) + ")");
                showTFTMessage("Server Error", "HTTP " + String(httpCode), "Try again later", "", "");
                response = "{\"error\":\"server_error\",\"http_code\":" + String(httpCode) + "}";
                
            } else {
                Serial.println("❌ HTTP Error: " + String(httpCode));
                String errorBody = httpLocal.getString();
                Serial.println("Error response: " + errorBody.substring(0, min(200, (int)errorBody.length())));
                
                showTFTMessage("HTTP Error", "Code: " + String(httpCode), "", "", "");
                response = "{\"error\":\"http_error\",\"code\":" + String(httpCode) + "}";
            }
        } else {
            // خطای اتصال - استفاده از کدهای خطای استاندارد
            String errorMsg = httpLocal.errorToString(httpCode);
            Serial.println("❌ Connection failed: " + errorMsg + " (" + String(httpCode) + ")");
            
            // بررسی نوع خطا با استفاده از کدهای عددی
            if (httpCode == -1) { // HTTPC_ERROR_CONNECTION_REFUSED
                Serial.println("Connection refused - Check if server is running");
                showTFTMessage("Connection", "Refused", "Check server", "", "");
            } else if (httpCode == -2) { // HTTPC_ERROR_SEND_HEADER_FAILED
                Serial.println("Send header failed - Network issue");
                showTFTMessage("Network", "Header Failed", "Check connection", "", "");
            } else if (httpCode == -3) { // HTTPC_ERROR_SEND_PAYLOAD_FAILED
                Serial.println("Send payload failed");
                showTFTMessage("Network", "Data Failed", "Check connection", "", "");
            } else if (httpCode == -4) { // HTTPC_ERROR_NOT_CONNECTED
                Serial.println("Not connected");
                showTFTMessage("Not Connected", "Check WiFi", "", "", "");
            } else if (httpCode == -5) { // HTTPC_ERROR_CONNECTION_LOST
                Serial.println("Connection lost");
                showTFTMessage("Connection", "Lost", "Reconnecting...", "", "");
            } else if (httpCode == -6) { // HTTPC_ERROR_NO_STREAM
                Serial.println("No stream - Server didn't send data");
                showTFTMessage("No Data", "Server empty", "", "", "");
            } else if (httpCode == -7) { // HTTPC_ERROR_NO_HTTP_SERVER
                Serial.println("No HTTP server");
                showTFTMessage("No Server", "Check URL", "", "", "");
            } else if (httpCode == -8) { // HTTPC_ERROR_TOO_LESS_RAM
                Serial.println("Too little RAM");
                showTFTMessage("Low RAM", "Restarting...", "", "", "");
            } else if (httpCode == -9) { // HTTPC_ERROR_ENCODING
                Serial.println("Encoding error");
                showTFTMessage("Encoding", "Error", "", "", "");
            } else if (httpCode == -10) { // HTTPC_ERROR_STREAM_WRITE
                Serial.println("Stream write error");
                showTFTMessage("Stream", "Write Error", "", "", "");
            } else if (httpCode == -11) { // HTTPC_ERROR_READ_TIMEOUT
                Serial.println("Read timeout - Server too slow");
                showTFTMessage("Timeout", "Server too slow", "", "", "");
            }
            
            response = "{\"error\":\"connection_failed\",\"msg\":\"" + errorMsg + "\",\"code\":" + String(httpCode) + "}";
        }
        
    } catch (const std::exception& e) {
        Serial.println("❌ Exception in HTTP request: " + String(e.what()));
        response = "{\"error\":\"exception\",\"message\":\"HTTP request exception\"}";
    } catch (...) {
        Serial.println("❌ Unknown exception in HTTP request");
        response = "{\"error\":\"unknown_exception\",\"message\":\"Unknown HTTP error\"}";
    }
    
    // بستن اتصال
    httpLocal.end();
    
    // تاخیر کوتاه قبل از درخواست بعدی
    delay(100);
    
    // نمایش نتیجه نهایی
    if (response.indexOf("error") == -1 && response.length() > 10) {
        Serial.println("✅ Data fetch successful for " + String(portfolioName));
        showTFTMessage("✅ Data Fetched", String(portfolioName), "Success", "", "");
    } else {
        Serial.println("❌ Data fetch failed for " + String(portfolioName));
        
        // نمایش خطا روی TFT فقط اگر بار اول نباشد
        static int errorCount = 0;
        errorCount++;
        
        if (errorCount % 3 == 0) {
            showTFTMessage("❌ Fetch Failed", String(portfolioName), "Check API/Network", "", "");
            delay(2000);
        }
    }
    
    Serial.println("=== End of getPortfolioData ===");
    
    return response;
}

void parseCryptoData(String jsonData) {
    if (settings.isExitMode) {
        parseExitModeData(jsonData);
    } else {
        parseEntryModeData(jsonData);
    }
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

// ==================== WEB SERVER FUNCTIONS ====================
const char* dashboardPage = R"=====(
<!DOCTYPE html>
<html><head>
<meta charset="UTF-8">
<meta http-equiv="Content-Type" content="text/html; charset=UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Portfolio Dashboard</title>
<style>
body{font-family:Arial;background:#f0f0f0;margin:0;padding:20px;}
.container{max-width:800px;margin:auto;background:white;padding:30px;border-radius:10px;box-shadow:0 2px 10px rgba(0,0,0,0.1);}
h1{text-align:center;color:#333;}
.stats-grid{display:grid;grid-template-columns:repeat(2,1fr);gap:15px;margin:20px 0;}
.stat-box{background:#f8f9fa;padding:20px;border-radius:8px;text-align:center;}
.stat-value{font-size:24px;font-weight:bold;margin:10px 0;}
.positive{color:#28a745;}
.negative{color:#dc3545;}
.position-list{margin:30px 0;max-height:400px;overflow-y:auto;}
.position-item{padding:12px;border-bottom:1px solid #ddd;display:flex;justify-content:space-between;align-items:center;}
.position-long{background:#ffe6e6;}
.position-short{background:#fff0e6;}
.alert-indicator{color:#dc3545;font-weight:bold;}
.controls{text-align:center;margin-top:30px;}
.btn{background:#007bff;color:white;border:none;padding:14px 28px;border-radius:5px;cursor:pointer;margin:8px;text-decoration:none;display:inline-block;font-size:16px;min-width:140px;}
.btn:hover{background:#0056b3;transform:translateY(-2px);box-shadow:0 4px 8px rgba(0,0,0,0.1);}
.btn-alert{background:#dc3545;}
.btn-wifi{background:#17a2b8;}
.btn-settings{background:#6c757d;}
.btn-dashboard{background:#28a745;}
.led-status{margin:20px 0;padding:15px;border-radius:8px;}
.led-green{background:#d4edda;border:1px solid #c3e6cb;}
.led-red{background:#f8d7da;border:1px solid #f5c6cb;}
.brightness-control{margin:15px 0;}
.slider{width:100%;}
/* ===== تاریخچه الرت‌ها با اسکرول ===== */
.alert-history {
    margin:30px 0;
    padding:20px;
    background:#f8f9fa;
    border-radius:8px;
    /* اضافه کردن خاصیت‌های جدید برای اسکرول */
    max-height: 400px;
    overflow-y: auto;
    border: 1px solid #ddd;
    position: relative;
    scroll-behavior: smooth;
    transition: all 0.3s ease;
}

/* نوار اسکرول سفارشی */
.alert-history::-webkit-scrollbar {
    width: 8px;
}

.alert-history::-webkit-scrollbar-track {
    background: #f1f1f1;
    border-radius: 4px;
}

.alert-history::-webkit-scrollbar-thumb {
    background: #888;
    border-radius: 4px;
}

.alert-history::-webkit-scrollbar-thumb:hover {
    background: #555;
}

/* برای Firefox */
.alert-history {
    scrollbar-width: thin;
    scrollbar-color: #888 #f1f1f1;
}

.alert-history-item {
    padding:12px 15px;
    border-bottom:1px solid #ddd;
    margin-bottom:10px;
    background: white;
    border-radius: 6px;
    transition: all 0.2s ease;
    position: relative;
    animation: fadeIn 0.3s ease-out;
}

.alert-history-item:last-child {
    border-bottom: none;
    margin-bottom: 0;
}

/* بهبود نمایش زمان */
.alert-history-time {
    font-size: 12px;
    color: #2196F3;
    font-weight: bold;
    font-family: monospace;
    white-space: nowrap;
    text-align: right;
}

/* استایل برای اطلاعات قیمت */
.price-info-container {
    display: flex;
    justify-content: space-between;
    align-items: center;
    font-size: 12px;
    color: #555;
    margin-top: 10px;
    padding-top: 10px;
    border-top: 1px solid #eee;
}

.price-info-item {
    margin-right: 20px;
}

.last-alert-info {
    font-size: 11px;
    color: #666;
    background: #f0f8ff;
    padding: 4px 10px;
    border-radius: 4px;
    border: 1px solid #d1e7ff;
    font-family: monospace;
}

/* برای موبایل responsiveness */
@media (max-width: 768px) {
    .alert-history-item > div:first-child {
        flex-direction: column;
    }
    
    .alert-history-time {
        text-align: left;
        margin-top: 5px;
    }
    
    .price-info-container {
        flex-direction: column;
        align-items: flex-start;
    }
    
    .price-info-item {
        margin-right: 0;
        margin-bottom: 5px;
    }
}

.alert-history-symbol {
    font-weight:bold;
}

.alert-history-type {
    display:inline-block;
    padding:2px 8px;
    border-radius:4px;
    margin-left:10px;
    font-size:12px;
}

.alert-history-mode {
    font-size: 10px;
    background: #6c757d;
    color: white;
    padding: 1px 5px;
    border-radius: 3px;
    margin-left: 5px;
    font-weight: normal;
}

/* رنگ‌بندی بر اساس نوع الرت */
.alert-long {
    border-left: 4px solid #28a745;
}

.alert-short {
    border-left: 4px solid #dc3545;
}

.alert-profit {
    border-left: 4px solid #28a745;
}

.alert-loss {
    border-left: 4px solid #dc3545;
}

.alert-severe {
    border-left: 4px solid #ffc107;
    background: linear-gradient(to right, rgba(255,193,7,0.05), white);
}

/* نشانگر اسکرول */
.scroll-indicator {
    position: absolute;
    top: 10px;
    right: 15px;
    font-size: 12px;
    color: #666;
    background: rgba(255,255,255,0.8);
    padding: 2px 8px;
    border-radius: 10px;
    display: flex;
    align-items: center;
    gap: 5px;
    z-index: 10;
}

.scroll-indicator i {
    animation: bounce 2s infinite;
}

/* افکت hover */
.alert-history-item:hover {
    background: #f8f9fa;
    box-shadow: 0 2px 5px rgba(0,0,0,0.1);
    transform: translateX(5px);
}

.alert-history:hover {
    border-color: #2196F3;
}
/* بهبود نمایش زمان */
.alert-history-time {
    font-size: 12px;
    color: #2196F3;
    font-weight: bold;
    font-family: monospace;
    white-space: nowrap;
    text-align: right;
}

/* بهبود flex layout برای آیتم‌ها */
.alert-history-item > div:first-child {
    display: flex;
    justify-content: space-between;
    align-items: flex-start;
    margin-bottom: 10px;
}

/* جداکننده بین بخش‌ها */
.alert-history-item-content {
    flex: 1;
    margin-right: 15px;
}

/* استایل برای اطلاعات قیمت */
.price-info-container {
    display: flex;
    justify-content: space-between;
    align-items: center;
    font-size: 12px;
    color: #555;
    margin-top: 10px;
    padding-top: 10px;
    border-top: 1px solid #eee;
}

.price-info-item {
    margin-right: 20px;
}

.last-alert-info {
    font-size: 11px;
    color: #666;
    background: #f0f8ff;
    padding: 4px 10px;
    border-radius: 4px;
    border: 1px solid #d1e7ff;
    font-family: monospace;
}

/* برای موبایل responsiveness */
@media (max-width: 768px) {
    .alert-history-item > div:first-child {
        flex-direction: column;
    }
    
    .alert-history-time {
        text-align: left;
        margin-top: 5px;
    }
    
    .price-info-container {
        flex-direction: column;
        align-items: flex-start;
    }
    
    .price-info-item {
        margin-right: 0;
        margin-bottom: 5px;
    }
}
/* انیمیشن‌ها */
@keyframes bounce {
    0%, 20%, 50%, 80%, 100% {
        transform: translateY(0);
    }
    40% {
        transform: translateY(-3px);
    }
    60% {
        transform: translateY(-1px);
    }
}

@keyframes fadeIn {
    from {
        opacity: 0;
        transform: translateY(10px);
    }
    to {
        opacity: 1;
        transform: translateY(0);
    }
}
/* ===== پایان تاریخچه الرت‌ها ===== */

.mode-indicator {
    padding: 10px;
    border-radius: 5px;
    text-align: center;
    margin-bottom: 20px;
    font-weight: bold;
}
.mode-entry {
    background: #e6f3ff;
    border: 2px solid #007bff;
    color: #007bff;
}
.mode-exit {
    background: #fff0e6;
    border: 2px solid #ff6b00;
    color: #ff6b00;
}
.alert-price {
    font-size: 11px;
    color: #666;
    margin-top: 2px;
    font-family: monospace;
}
.price-cell {
    font-family: monospace;
    font-size: 11px;
}
.api-info {
    background: #f8f9fa;
    padding: 15px;
    border-radius: 8px;
    margin: 10px 0;
    border-left: 4px solid #6c757d;
}
.api-info h4 {
    margin-top: 0;
}
.portfolio-info {
    background: #e8f4fd;
    padding: 15px;
    border-radius: 8px;
    margin: 15px 0;
    border: 1px solid #2196F3;
}
.portfolio-info h4 {
    margin-top: 0;
    color: #2196F3;
}
.controls-row {
    display: flex;
    justify-content: center;
    flex-wrap: wrap;
    gap: 10px;
    margin: 20px 0;
}
</style>
</head>
<body>
<div class="container">
<h1>📊 Portfolio Dashboard v3.8.3</h1>
<h2 style="text-align:center;color:#666;">Enhanced Time Display in Alert History</h2>

<div class="portfolio-info">
    <h4>📁 Portfolio Information</h4>
    <p><strong>Current Mode:</strong> %TRADING_MODE%</p>
    <p><strong>Active Portfolio:</strong> <span style="color:#2196F3;font-weight:bold;">%ACTIVE_PORTFOLIO%</span></p>
    <p><strong>Entry Mode Portfolio:</strong> %ENTRY_PORTFOLIO%</p>
    <p><strong>Exit Mode Portfolio:</strong> %EXIT_PORTFOLIO%</p>
</div>

<div class="mode-indicator %MODE_CLASS%">
    %MODE_ICON% Trading Mode: %TRADING_MODE%
    %EXIT_MODE_INFO%
</div>

<div class="api-info">
    <h4>🔌 API Connection Information</h4>
    <p><strong>Current API Type:</strong> %API_TYPE%</p>
    <p><strong>API Endpoint:</strong> %API_ENDPOINT%</p>
    <p><strong>Last Update:</strong> %LAST_UPDATE%</p>
</div>

<div class="stats-grid">
<div class="stat-box">
<h4>Total Investment</h4>
<div class="stat-value">$%TOTAL_INVESTMENT%</div>
</div>
<div class="stat-box">
<h4>Current Value</h4>
<div class="stat-value">$%TOTAL_VALUE%</div>
</div>
<div class="stat-box">
<h4>Profit/Loss</h4>
<div class="stat-value %PNL_CLASS%">$%TOTAL_PNL%</div>
</div>
<div class="stat-box">
<h4>Portfolio Change</h4>
<div class="stat-value %CHANGE_CLASS%">%TOTAL_CHANGE%%</div>
</div>
<div class="stat-box">
<h4>Total Positions</h4>
<div class="stat-value">%TOTAL_POSITIONS%</div>
</div>
<div class="stat-box">
<h4>Active Alerts</h4>
<div class="stat-value">%ALERT_COUNT%</div>
</div>
</div>

%LED_STATUS%

%ALERT_HISTORY%

<div class="position-list">
<h3>All Positions (%POSITION_COUNT% total) - Sorted by Loss</h3>
%POSITION_LIST%
</div>

<div class="controls-row">
<a href="/" class="btn btn-dashboard">📊 Dashboard</a>
<a href="/refresh" class="btn">🔄 Refresh</a>
<a href="/config" class="btn btn-settings">⚙️ Settings</a>
</div>

<div class="controls-row">
<a href="/testalert" class="btn">🔊 Test Alert</a>
<a href="/resetalerts" class="btn btn-alert">♻️ Reset Alerts</a>
<a href="/wifimanager" class="btn btn-wifi">📶 WiFi Manager</a>
</div>

<div style="margin-top:30px;padding:20px;background:#f8f9fa;border-radius:8px;">
<h4>🎯 Alert System Status</h4>
<p><strong>Trading Mode:</strong> %TRADING_MODE%</p>
<p><strong>Active Portfolio:</strong> %ACTIVE_PORTFOLIO%</p>
<p><strong>Entry Mode Portfolio:</strong> %ENTRY_PORTFOLIO%</p>
<p><strong>Exit Mode Portfolio:</strong> %EXIT_PORTFOLIO%</p>
<p><strong>API Type:</strong> %API_TYPE%</p>
%EXIT_MODE_DETAILS%
<p><strong>Alert Thresholds:</strong> Normal: %ALERT_THRESHOLD%% | Severe: %SEVERE_THRESHOLD%% | Portfolio: %PORTFOLIO_THRESHOLD%%</p>
<p><strong>Exit Alert Percent:</strong> %EXIT_ALERT_PERCENT%% price change</p>
<p><strong>Price Display Format:</strong> Adaptive decimal places (2-10 digits)</p>
<p><strong>Volume Level:</strong> %VOLUME%/20</p>
<p><strong>Buzzer Status:</strong> %BUZZER_STATUS%</p>
%LED_DETAILS%
<p><strong>System Capacity:</strong> Supports up to 100 crypto positions</p>
<p><strong>Alert Checking:</strong> ALL positions</p>
<p><strong>Alert Tones:</strong> Long/Profit: 2 beeps, Short/Loss: 1 beep</p>
<p><strong>Price Precision:</strong> High precision (up to 10 decimal places)</p>
<p><strong>Exit Mode Logic:</strong> Alerts on %EXIT_ALERT_PERCENT%% price change from last alert</p>
<p><strong>Last Update:</strong> %LAST_UPDATE%</p>
</div>
</div>

<script>
// اسکرول نرم برای تاریخچه الرت‌ها
document.addEventListener('DOMContentLoaded', function() {
    const alertHistory = document.querySelector('.alert-history');
    
    if (alertHistory) {
        // اضافه کردن افکت hover بر روی آیتم‌ها
        const items = alertHistory.querySelectorAll('.alert-history-item');
        items.forEach(item => {
            item.addEventListener('mouseenter', function() {
                this.style.transform = 'translateX(5px)';
                this.style.boxShadow = '0 3px 8px rgba(0,0,0,0.15)';
            });
            
            item.addEventListener('mouseleave', function() {
                this.style.transform = 'translateX(0)';
                this.style.boxShadow = '0 2px 5px rgba(0,0,0,0.1)';
            });
        });
        
        // نشان دادن/پنهان کردن نشانگر اسکرول
        alertHistory.addEventListener('scroll', function() {
            const scrollIndicator = this.querySelector('.scroll-indicator');
            if (scrollIndicator) {
                if (this.scrollTop > 50) {
                    scrollIndicator.style.opacity = '0.5';
                } else {
                    scrollIndicator.style.opacity = '1';
                }
            }
        });
        
        // افکت برای نشانگر اسکرول
        const scrollIndicator = alertHistory.querySelector('.scroll-indicator');
        if (scrollIndicator) {
            setInterval(() => {
                if (alertHistory.scrollTop === 0) {
                    scrollIndicator.style.animation = 'bounce 2s infinite';
                } else {
                    scrollIndicator.style.animation = 'none';
                }
            }, 1000);
        }
    }
    
    // دکمه برگشت به بالا
    window.addEventListener('scroll', function() {
        const backToTopBtn = document.getElementById('backToTopBtn');
        if (backToTopBtn) {
            if (window.pageYOffset > 300) {
                backToTopBtn.style.display = 'block';
            } else {
                backToTopBtn.style.display = 'none';
            }
        }
    });
});

// تابع ساده برای برگشت به بالا
function scrollToTop() {
    window.scrollTo({top: 0, behavior: 'smooth'});
}
</script>

<button id="backToTopBtn" onclick="scrollToTop()" style="display:none;position:fixed;bottom:20px;right:20px;background:#2196F3;color:white;border:none;padding:10px 15px;border-radius:50%;cursor:pointer;z-index:1000;box-shadow:0 2px 10px rgba(0,0,0,0.2);font-size:20px;">
    ↑
</button>

</body>
</html>
)=====";

const char* setupPage = R"=====(
<!DOCTYPE html>
<html><head>
<meta charset="UTF-8">
<meta http-equiv="Content-Type" content="text/html; charset=UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Portfolio Setup</title>
<style>
body{font-family:Arial;background:#f0f0f0;margin:0;padding:20px;}
.container{max-width:600px;margin:auto;background:white;padding:30px;border-radius:10px;box-shadow:0 2px 10px rgba(0,0,0,0.1);}
h1{text-align:center;color:#333;}
.status-box{background:#e8f5e9;padding:15px;border-radius:5px;margin:20px 0;}
.wifi-list{background:#f8f9fa;padding:15px;border-radius:5px;margin:20px 0;}
.form-group{margin:15px 0;}
input,select{width:100%;padding:12px;border:1px solid #ddd;border-radius:5px;}
.btn{background:#4CAF50;color:white;border:none;padding:14px 28px;border-radius:5px;cursor:pointer;margin:8px;text-decoration:none;display:inline-block;font-size:16px;min-width:140px;}
.btn:hover{background:#45a049;transform:translateY(-2px);box-shadow:0 4px 8px rgba(0,0,0,0.1);}
.btn-test{background:#007bff;}
.btn-alert{background:#dc3545;}
.btn-wifi{background:#17a2b8;}
.btn-settings{background:#6c757d;}
.btn-dashboard{background:#28a745;}
.controls{text-align:center;margin-top:20px;}
.slider{width:100%;}
.checkbox-group{margin:10px 0;}
.checkbox-label{display:flex;align-items:center;}
.checkbox-label input{margin-right:10px;}
.form-container {
    background: #f8f9fa;
    padding: 20px;
    border-radius: 10px;
    margin: 20px 0;
    border: 2px solid #007bff;
}
.slider-container {
    background: white;
    padding: 15px;
    border-radius: 8px;
    margin: 10px 0;
    border: 1px solid #ddd;
}
.slider-value {
    display: inline-block;
    min-width: 50px;
    text-align: center;
    font-weight: bold;
    margin-left: 10px;
}
.wifi-network-item {
    padding: 10px;
    border-bottom: 1px solid #eee;
    display: flex;
    justify-content: space-between;
    align-items: center;
}
.wifi-network-item:hover {
    background: #f5f5f5;
}
.delete-btn {
    color: #dc3545;
    text-decoration: none;
    font-weight: bold;
}
.delete-btn:hover {
    color: #c82333;
}
.mode-container {
    background: #e8f4fd;
    padding: 20px;
    border-radius: 10px;
    margin: 20px 0;
    border: 2px solid #2196F3;
}
.mode-option {
    padding: 15px;
    border-radius: 8px;
    margin: 10px 0;
    cursor: pointer;
    border: 2px solid transparent;
}
.mode-option:hover {
    background: #f0f8ff;
}
.mode-selected {
    background: #d4edda;
    border-color: #28a745;
}
.mode-desc {
    font-size: 14px;
    color: #666;
    margin-top: 5px;
}
.led-explanation {
    font-size: 12px;
    color: #666;
    margin-top: 5px;
    padding: 10px;
    background: #f8f9fa;
    border-radius: 5px;
}
.price-precision-info {
    font-size: 12px;
    color: #666;
    padding: 10px;
    background: #f0f8ff;
    border-radius: 5px;
    margin-top: 10px;
    border-left: 4px solid #2196F3;
}
.api-info-box {
    background: #fff3cd;
    padding: 15px;
    border-radius: 8px;
    margin: 15px 0;
    border: 1px solid #ffeaa7;
}
.portfolio-container {
    background: #e8f4fd;
    padding: 20px;
    border-radius: 10px;
    margin: 20px 0;
    border: 2px solid #2196F3;
}
.portfolio-group {
    margin: 15px 0;
    padding: 15px;
    background: white;
    border-radius: 8px;
    border: 1px solid #ddd;
}
.controls-row {
    display: flex;
    justify-content: center;
    flex-wrap: wrap;
    gap: 10px;
    margin: 20px 0;
}
</style>
</head>
<body>
<div class="container">
<h1>📱 Portfolio Monitor Setup v3.8.3</h1>
<h2 style="text-align:center;color:#666;">Enhanced Time Display in Alert History</h2>
<h3 style="text-align:center;color:#2196F3;">Separate Portfolios for Entry/Exit Modes</h3>

<div class="status-box">
<h3>System Status</h3>
<p><strong>Mode:</strong> %MODE%</p>
<p><strong>IP:</strong> %IP%</p>
<p><strong>Trading Mode:</strong> <span style="color:%MODE_COLOR%;">%TRADING_MODE%</span></p>
<p><strong>Active Portfolio:</strong> <span style="color:#2196F3;font-weight:bold;">%ACTIVE_PORTFOLIO%</span></p>
<p><strong>Entry Portfolio:</strong> %ENTRY_PORTFOLIO%</p>
<p><strong>Exit Portfolio:</strong> %EXIT_PORTFOLIO%</p>
<p><strong>Exit Alert Threshold:</strong> %EXIT_ALERT_PERCENT%% price change</p>
<p><strong>Saved Networks:</strong> %NETWORK_COUNT%</p>
<p><strong>Positions:</strong> %POSITION_COUNT%</p>
<p><strong>Alerts Active:</strong> %ALERT_COUNT%</p>
%LED_STATUS_DETAILS%
<p><strong>LED Brightness:</strong> %LED_BRIGHTNESS%/255 (%LED_BRIGHTNESS_PERCENT%%%)</p>
<p><strong>LED Enabled:</strong> %LED_ENABLED%</p>
<p><strong>Buzzer Status:</strong> %BUZZER_STATUS%</p>
<p><strong>Current Thresholds:</strong> Normal: %ALERT_THRESHOLD%% | Severe: %SEVERE_THRESHOLD%% | Portfolio: %PORTFOLIO_THRESHOLD%%</p>
<p><strong>Price Display Format:</strong> Adaptive (2-10 decimal places)</p>
<p><strong>System Capacity:</strong> Supports up to 100 crypto positions</p>
<p><strong>Alert Checking:</strong> ALL positions (not just top 5)</p>
<p><strong>Exit Mode Logic:</strong> Tracks price changes from last alert</p>
</div>

<div class="api-info-box">
    <h4>🔗 API Endpoints Information</h4>
    <p><strong>Entry Mode (Portfolio Tracking):</strong><br>
    Endpoint: <code>/api/device/portfolio/{username}?portfolio_name={entry_portfolio}</code><br>
    Description: Fetches active portfolio positions with P/L data</p>
    
    <p><strong>Exit Mode (Position Tracking):</strong><br>
    Endpoint: <code>/api/device/portfolio/{username}?portfolio_name={exit_portfolio}</code><br>
    Description: Fetches positions for price change tracking</p>
    
    <p><strong>Note:</strong> Both modes use the same endpoint with different portfolio names.</p>
</div>

<div class="portfolio-container">
    <h3>📊 Portfolio Names Configuration</h3>
    
    <form action="/saveapi" method="post">
        <div class="portfolio-group">
            <h4>📈 ENTRY Mode Portfolio</h4>
            <p style="font-size:12px;color:#666;">Used for portfolio tracking in ENTRY mode</p>
            <input type="text" name="entryportfolio" value="%ENTRY_PORTFOLIO%" 
                   placeholder="Portfolio name for ENTRY mode" style="width:100%;padding:10px;" required>
        </div>
        
        <div class="portfolio-group">
            <h4>📉 EXIT Mode Portfolio</h4>
            <p style="font-size:12px;color:#666;">Used for position tracking in EXIT mode</p>
            <input type="text" name="exitportfolio" value="%EXIT_PORTFOLIO%" 
                   placeholder="Portfolio name for EXIT mode" style="width:100%;padding:10px;" required>
        </div>
        
        <div class="form-group">
            <label>API Server:</label>
            <input type="text" name="server" value="%SERVER%" placeholder="http://your-server.com" required>
        </div>
        <div class="form-group">
            <label>Username:</label>
            <input type="text" name="username" value="%USERNAME%" required>
        </div>
        <div class="form-group">
            <label>Password:</label>
            <input type="password" name="userpass" value="%USERPASS%" required>
        </div>
        
        <div style="text-align:center; margin-top:20px;">
            <button type="submit" class="btn" style="background:#4CAF50;padding:15px 40px;font-size:16px;">
                💾 Save API & Portfolios
            </button>
        </div>
    </form>
</div>

<div class="mode-container">
    <h3>🔄 Trading Mode Control</h3>
    
    <form action="/savemode" method="post">
        <div class="mode-option %ENTRY_MODE_SELECTED%" onclick="document.getElementById('entryMode').checked = true; updateModeSettings()">
            <input type="radio" id="entryMode" name="tradingmode" value="entry" %ENTRY_MODE_CHECKED% style="display:none;">
            <label for="entryMode" style="cursor:pointer;display:block;">
                <h4 style="margin:0;">📈 ENTRY Mode (Portfolio Tracking)</h4>
                <p class="mode-desc">• Uses Portfolio: <strong>%ENTRY_PORTFOLIO%</strong><br>
                   • API: /api/device/portfolio/<br>
                   • Alerts on drawdown thresholds<br>
                   • Long: 2 beeps, Short: 1 beep<br>
                   • LED: Green=LONG, Red=SHORT<br>
                   • Shows alert time & price</p>
            </label>
        </div>
        
        <div class="mode-option %EXIT_MODE_SELECTED%" onclick="document.getElementById('exitMode').checked = true; updateModeSettings()">
            <input type="radio" id="exitMode" name="tradingmode" value="exit" %EXIT_MODE_CHECKED% style="display:none;">
            <label for="exitMode" style="cursor:pointer;display:block;">
                <h4 style="margin:0;">📉 EXIT Mode (Price Change Tracking)</h4>
                <p class="mode-desc">• Uses Portfolio: <strong>%EXIT_PORTFOLIO%</strong><br>
                   • API: /api/device/portfolio/<br>
                   • Alerts on % price change (default: 5%%)<br>
                   • Same beep patterns: Profit=2 beeps, Loss=1 beep<br>
                   • LED: Green=PROFIT, Red=LOSS (regardless of position type)<br>
                   • Alerts once per %EXIT_ALERT_PERCENT%% price change<br>
                   • High precision price display (up to 10 decimals)<br>
                   • Shows alert time & price</p>
            </label>
        </div>
        
        <div id="exitModeSettings" style="%EXIT_MODE_VISIBILITY%">
            <div class="slider-container">
                <label>Price Change Alert Threshold: 
                    <span id="exitAlertValue" class="slider-value">%EXIT_ALERT_PERCENT%</span>%
                </label>
                <input type="range" name="exitalertpercent" min="1" max="20" step="0.5" 
                       value="%EXIT_ALERT_PERCENT%" class="slider" 
                       oninput="document.getElementById('exitAlertValue').textContent = this.value">
                <div style="font-size:12px;color:#666;margin-top:5px;">Alerts when price changes by this percent from last alert price</div>
            </div>
            
            <div class="checkbox-group">
                <label class="checkbox-label">
                    <input type="checkbox" name="exitalertenabled" id="exitalertenabled" %EXIT_ALERT_ENABLED_CHECKED%>
                    Enable Exit Alerts
                </label>
            </div>
            
            <div class="checkbox-group">
                <label class="checkbox-label">
                    <input type="checkbox" name="exitalertblink" id="exitalertblink" %EXIT_ALERT_BLINK_CHECKED%>
                    Enable LED Blinking for Exit Alerts
                </label>
            </div>
            
            <div class="led-explanation">
                <strong>💡 LED Behavior in EXIT Mode:</strong><br>
                • 🟢 GREEN LED: Activated for PROFIT (any position type)<br>
                • 🔴 RED LED: Activated for LOSS (any position type)<br>
                • LEDs blink 5 times then stay on<br>
                • Each %EXIT_ALERT_PERCENT%% price change triggers new alert
            </div>
            
            <div class="price-precision-info">
                <strong>💰 Price Display Precision:</strong><br>
                • ≥1000: 2 decimal places (e.g., 1543.27)<br>
                • ≥1: 4 decimal places (e.g., 0.1234)<br>
                • ≥0.01: 6 decimal places (e.g., 0.001234)<br>
                • ≥0.0001: 8 decimal places (e.g., 0.00001234)<br>
                • &lt;0.0001: 10 decimal places (e.g., 0.0000001234)
            </div>
        </div>
        
        <div style="text-align:center; margin-top:20px;">
            <button type="submit" class="btn" style="background:#2196F3;padding:15px 40px;font-size:16px;">
                💾 Save Mode Settings
            </button>
        </div>
    </form>
</div>

<script>
function updateModeSettings() {
    var exitModeSettings = document.getElementById('exitModeSettings');
    var exitMode = document.getElementById('exitMode');
    
    if (exitMode.checked) {
        exitModeSettings.style.display = 'block';
    } else {
        exitModeSettings.style.display = 'none';
    }
}

document.addEventListener('DOMContentLoaded', function() {
    updateModeSettings();
    
    document.querySelectorAll('.mode-option').forEach(function(option) {
        option.addEventListener('click', function() {
            document.querySelectorAll('.mode-option').forEach(function(opt) {
                opt.classList.remove('mode-selected');
            });
            this.classList.add('mode-selected');
        });
    });
});
</script>

<div class="wifi-list">
<h3>WiFi Networks (%NETWORK_COUNT%)</h3>
%NETWORK_LIST%
</div>

<form action="/savewifi" method="post">
<h3>Add WiFi Network</h3>
<div class="form-group">
<label>WiFi SSID:</label>
<input type="text" name="ssid" placeholder="WiFi name" required>
</div>
<div class="form-group">
<label>Password:</label>
<input type="password" name="password" placeholder="WiFi password" required>
</div>
<button type="submit" class="btn btn-wifi">➕ Add Network</button>
</form>

<div class="form-container">
    <h3>🎯 Alert & LED Control Settings</h3>
    
    <form id="alertForm" method="POST" action="/savealert">
        <div class="slider-container">
            <label>LED Brightness: <span id="ledBrightnessValue" class="slider-value">%LED_BRIGHTNESS%</span>/255</label>
            <input type="range" name="ledbrightness" min="0" max="255" value="%LED_BRIGHTNESS%" 
                   class="slider" oninput="document.getElementById('ledBrightnessValue').textContent = this.value; 
                                          document.getElementById('ledBrightnessPercent').textContent = Math.round(this.value/255*100) + '%'">
            <div style="font-size:12px;color:#666;margin-top:5px;">
                <span id="ledBrightnessPercent">%LED_BRIGHTNESS_PERCENT%</span> - Lower values = dimmer LEDs
            </div>
        </div>
        
        <div class="slider-container">
            <label>Normal Alert Threshold (%): <span id="thresholdValue" class="slider-value">%ALERT_THRESHOLD%</span></label>
            <input type="range" name="threshold" min="-50" max="0" step="0.5" value="%ALERT_THRESHOLD_SLIDER%" 
                   class="slider" oninput="document.getElementById('thresholdValue').textContent = this.value">
            <div style="font-size:12px;color:#666;margin-top:5px;">For ENTRY Mode only - alerts when loss exceeds this value</div>
        </div>
        
        <div class="slider-container">
            <label>Severe Alert Threshold (%): <span id="severeValue" class="slider-value">%SEVERE_THRESHOLD%</span></label>
            <input type="range" name="severethreshold" min="-50" max="0" step="0.5" value="%SEVERE_THRESHOLD_SLIDER%" 
                   class="slider" oninput="document.getElementById('severeValue').textContent = this.value">
            <div style="font-size:12px;color:#666;margin-top:5px;">For ENTRY Mode only - severe alerts when loss exceeds this value</div>
        </div>
        
        <div class="slider-container">
            <label>Portfolio Alert Threshold (%): <span id="portfolioValue" class="slider-value">%PORTFOLIO_THRESHOLD%</span></label>
            <input type="range" name="portfoliothreshold" min="-50" max="0" step="0.5" value="%PORTFOLIO_THRESHOLD_SLIDER%" 
                   class="slider" oninput="document.getElementById('portfolioValue').textContent = this.value">
            <div style="font-size:12px;color:#666;margin-top:5px;">Alerts when total portfolio loss exceeds this value</div>
        </div>
        
        <div class="slider-container">
            <label>Volume: <span id="volumeValue" class="slider-value">%VOLUME%</span>/20</label>
            <input type="range" name="volume" min="0" max="20" value="%VOLUME%" 
                   class="slider" oninput="document.getElementById('volumeValue').textContent = this.value">
            <div style="font-size:12px;color:#666;margin-top:5px;">0 = Mute, 1-20 = Volume levels<br>
            Same volume for both Entry and Exit modes</div>
        </div>
        
        <div class="checkbox-group">
            <label class="checkbox-label">
                <input type="checkbox" name="buzzerEnabled" id="buzzerEnabled" %BUZZER_ENABLED_CHECKED% 
                       onchange="document.querySelector('input[name=\"volume\"]').disabled = !this.checked">
                Enable Buzzer
            </label>
        </div>
        
        <div class="checkbox-group">
            <label class="checkbox-label">
                <input type="checkbox" name="ledEnabled" id="ledEnabled" %LED_ENABLED_CHECKED%>
                Enable LED Alert System
            </label>
            <p style="font-size:12px;color:#666;">🟢 GREEN (GPIO 22) = LONG alerts / PROFIT (Exit Mode)<br>
            🔴 RED (GPIO 21) = SHORT alerts / LOSS (Exit Mode)</p>
        </div>
        
        <div style="text-align:center; margin-top:20px;">
            <button type="submit" class="btn" style="background:#4CAF50;padding:15px 40px;font-size:16px;">
                💾 Save All Settings
            </button>
        </div>
    </form>
</div>

<script>
    document.addEventListener('DOMContentLoaded', function() {
        var buzzerCheckbox = document.getElementById('buzzerEnabled');
        var volumeSlider = document.querySelector('input[name="volume"]');
        
        volumeSlider.disabled = !buzzerCheckbox.checked;
        
        buzzerCheckbox.addEventListener('change', function() {
            volumeSlider.disabled = !this.checked;
            if (!this.checked) {
                document.getElementById('volumeValue').textContent = '0';
                volumeSlider.value = 0;
            }
        });
        
        var ledSlider = document.querySelector('input[name="ledbrightness"]');
        var ledPercent = Math.round(ledSlider.value/255*100);
        document.getElementById('ledBrightnessPercent').textContent = ledPercent + '%';
    });
</script>

<div class="controls-row">
    <a href="/" class="btn btn-dashboard">📊 Dashboard</a>
    <a href="/testalert" class="btn btn-test">🔊 Test Alerts</a>
    <a href="/resetalerts" class="btn btn-alert">♻️ Reset Alerts</a>
    <a href="/refresh" class="btn">🔄 Refresh Data</a>
</div>
</div>
</body>
</html>
)=====";


void handleRoot() {
    if (!isConnectedToWiFi || apModeActive) {
        handleSetup();
        return;
    }
    
    int alertCount = 0;
    for (int i = 0; i < cryptoCount; i++) {
        if (cryptoData[i].alerted || cryptoData[i].exitAlerted) alertCount++;
    }
    
    String page = String(dashboardPage);
    
    page.replace("%TOTAL_INVESTMENT%", totalInvestmentStr);
    page.replace("%TOTAL_VALUE%", totalValueStr);
    page.replace("%TOTAL_PNL%", totalPnlStr);
    page.replace("%TOTAL_CHANGE%", String(portfolio.totalPnlPercent, 1));
    page.replace("%TOTAL_POSITIONS%", String(cryptoCount));
    page.replace("%POSITION_COUNT%", String(cryptoCount));
    page.replace("%ALERT_COUNT%", String(alertCount));
    page.replace("%ALERT_THRESHOLD%", String(settings.alertThreshold, 1));
    page.replace("%SEVERE_THRESHOLD%", String(settings.severeAlertThreshold, 1));
    page.replace("%PORTFOLIO_THRESHOLD%", String(settings.portfolioAlertThreshold, 1));
    page.replace("%VOLUME%", String(settings.buzzerVolume));
    page.replace("%BUZZER_STATUS%", String(settings.buzzerEnabled ? "Enabled" : "Disabled"));
    page.replace("%LED_ENABLED%", String(settings.ledEnabled ? "Enabled" : "Disabled"));
    page.replace("%LAST_UPDATE%", currentDateTime);
    page.replace("%LED_BRIGHTNESS%", String(settings.ledBrightness));
    page.replace("%LED_BRIGHTNESS_PERCENT%", String(round(settings.ledBrightness * 100.0 / 255.0)));
    page.replace("%EXIT_ALERT_PERCENT%", String(settings.exitAlertPercent));
    
    page.replace("%PNL_CLASS%", portfolio.totalPnl >= 0 ? "positive" : "negative");
    page.replace("%CHANGE_CLASS%", portfolio.totalPnlPercent >= 0 ? "positive" : "negative");
    
    String activePortfolio = settings.isExitMode ? String(settings.exitPortfolio) : String(settings.entryPortfolio);
    String tradingMode = settings.isExitMode ? "EXIT (Price Change Tracking)" : "ENTRY (Loss Tracking)";
    String modeIcon = settings.isExitMode ? "📉" : "📈";
    String modeClass = settings.isExitMode ? "mode-exit" : "mode-entry";
    String exitModeInfo = settings.isExitMode ? 
        "<br>Alert Threshold: " + String(settings.exitAlertPercent) + "% price change" : "";
    String exitModeDetails = settings.isExitMode ?
        "<p><strong>Exit Alert Threshold:</strong> " + String(settings.exitAlertPercent) + "% price change</p>" : "";
    
    String apiType = settings.isExitMode ? "PORTFOLIO API (Exit Mode)" : "PORTFOLIO API (Entry Mode)";
    String apiEndpoint = settings.isExitMode ? 
        String(settings.server) + "/api/device/portfolio/" + String(settings.username) + "?portfolio_name=" + String(settings.exitPortfolio) :
        String(settings.server) + "/api/device/portfolio/" + String(settings.username) + "?portfolio_name=" + String(settings.entryPortfolio);
    
    page.replace("%ACTIVE_PORTFOLIO%", activePortfolio);
    page.replace("%ENTRY_PORTFOLIO%", String(settings.entryPortfolio));
    page.replace("%EXIT_PORTFOLIO%", String(settings.exitPortfolio));
    page.replace("%TRADING_MODE%", tradingMode);
    page.replace("%MODE_ICON%", modeIcon);
    page.replace("%MODE_CLASS%", modeClass);
    page.replace("%EXIT_MODE_INFO%", exitModeInfo);
    page.replace("%EXIT_MODE_DETAILS%", exitModeDetails);
    page.replace("%API_TYPE%", apiType);
    page.replace("%API_ENDPOINT%", apiEndpoint);
    
    String ledStatusHTML = "";
    if (settings.isExitMode) {
        ledStatusHTML += "<div class='led-status " + String(profitLEDActive ? "led-green" : "") + "'>";
        ledStatusHTML += "<h4>🟢 LED Status (PROFIT Alerts)</h4>";
        ledStatusHTML += "<p>Status: <strong>" + String(profitLEDActive ? "🟢 ON (PROFIT Alert)" : "⚫ OFF") + "</strong></p>";
        ledStatusHTML += "<p>Symbol: " + String(profitLEDActive ? profitAlertSymbol : "None") + "</p>";
        ledStatusHTML += "<p>P/L: " + String(profitLEDActive ? String(profitAlertPnlPercent, 1) : "0.0") + "%</p>";
        ledStatusHTML += "</div>";
        
        ledStatusHTML += "<div class='led-status " + String(lossLEDActive ? "led-red" : "") + "'>";
        ledStatusHTML += "<h4>🔴 LED Status (LOSS Alerts)</h4>";
        ledStatusHTML += "<p>Status: <strong>" + String(lossLEDActive ? "🔴 ON (LOSS Alert)" : "⚫ OFF") + "</strong></p>";
        ledStatusHTML += "<p>Symbol: " + String(lossLEDActive ? lossAlertSymbol : "None") + "</p>";
        ledStatusHTML += "<p>P/L: " + String(lossLEDActive ? String(lossAlertPnlPercent, 1) : "0.0") + "%</p>";
        ledStatusHTML += "</div>";
    } else {
        ledStatusHTML += "<div class='led-status " + String(longAlertLEDActive ? "led-green" : "") + "'>";
        ledStatusHTML += "<h4>🟢 LED Status (LONG Alerts)</h4>";
        ledStatusHTML += "<p>Status: <strong>" + String(longAlertLEDActive ? "🟢 ON (LONG Alert)" : "⚫ OFF") + "</strong></p>";
        ledStatusHTML += "<p>Symbol: " + String(longAlertLEDActive ? longAlertSymbol : "None") + "</p>";
        ledStatusHTML += "<p>P/L: " + String(longAlertLEDActive ? String(longAlertPnlPercent, 1) : "0.0") + "%</p>";
        ledStatusHTML += "</div>";
        
        ledStatusHTML += "<div class='led-status " + String(shortAlertLEDActive ? "led-red" : "") + "'>";
        ledStatusHTML += "<h4>🔴 LED Status (SHORT Alerts)</h4>";
        ledStatusHTML += "<p>Status: <strong>" + String(shortAlertLEDActive ? "🔴 ON (SHORT Alert)" : "⚫ OFF") + "</strong></p>";
        ledStatusHTML += "<p>Symbol: " + String(shortAlertLEDActive ? shortAlertSymbol : "None") + "</p>";
        ledStatusHTML += "<p>P/L: " + String(shortAlertLEDActive ? String(shortAlertPnlPercent, 1) : "0.0") + "%</p>";
        ledStatusHTML += "</div>";
    }
    page.replace("%LED_STATUS%", ledStatusHTML);
    
    String ledDetails = "<p><strong>LED Status:</strong> ";
    if (settings.isExitMode) {
        ledDetails += "🟢 " + String(profitLEDActive ? "ON (PROFIT)" : "OFF") + 
                     " | 🔴 " + String(lossLEDActive ? "ON (LOSS)" : "OFF");
    } else {
        ledDetails += "🟢 " + String(longAlertLEDActive ? "ON (LONG)" : "OFF") + 
                     " | 🔴 " + String(shortAlertLEDActive ? "ON (SHORT)" : "OFF");
    }
    ledDetails += "</p>";
    page.replace("%LED_DETAILS%", ledDetails);
    
    // پیدا کردن بخش نمایش تاریخچه در تابع handleRoot (حدود خط ۲۱۵۰)
    // و جایگزینی کامل کد زیر:

 // ========== بخش تاریخچه الرت‌ها ==========

String alertHistoryHTML = "";
        
if (alertHistoryCount > 0) {
    alertHistoryHTML += "<div class='alert-history'>";
    
    // نشانگر اسکرول اگر الرت‌ها زیاد باشند
    if (alertHistoryCount > 5) {
        alertHistoryHTML += "<div class='scroll-indicator'>";
        alertHistoryHTML += "<i>↓</i> Scroll for more alerts";
        alertHistoryHTML += "</div>";
    }
    
    alertHistoryHTML += "<h4 style='margin-top:0;margin-bottom:15px;color:#333;'>📜 Recent Alert History (Last " + String(min(alertHistoryCount, 20)) + " of " + String(alertHistoryCount) + ")</h4>";
    
    int displayCount = min(alertHistoryCount, 20);
    
    for (int i = alertHistoryCount - 1; i >= max(0, alertHistoryCount - 20); i--) {
        AlertHistory* alert = &alertHistory[i];
        
        String timeStr = String(alert->timeString);
        if (timeStr == "01/01 00:00:00" || timeStr == "--/-- --:--") {
            timeStr = "Just now";
        }
        
        // محاسبه زمان سپری شده
        unsigned long currentTimestamp = 0;
        struct tm timeinfo;
        if (getLocalTime(&timeinfo, 100)) {
            time_t time_seconds = mktime(&timeinfo);
            currentTimestamp = (unsigned long)time_seconds * 1000;
        }
        
        String elapsedTime = "";
        if (alert->alertTime > 0 && currentTimestamp > alert->alertTime) {
            unsigned long secondsAgo = (currentTimestamp - alert->alertTime) / 1000;
            
            if (secondsAgo < 60) {
                elapsedTime = "(" + String(secondsAgo) + "s ago)";
            } else if (secondsAgo < 3600) {
                unsigned long minutes = secondsAgo / 60;
                elapsedTime = "(" + String(minutes) + "m ago)";
            } else if (secondsAgo < 86400) {
                unsigned long hours = secondsAgo / 3600;
                elapsedTime = "(" + String(hours) + "h ago)";
            } else {
                unsigned long days = secondsAgo / 86400;
                elapsedTime = "(" + String(days) + "d ago)";
            }
        }
        
        String alertColorClass = "";
        String alertType = "";
        String alertIcon = "";
        String alertMode = "";
        String positionType = "";
        String displayType = "";
        
        if (alert->alertType == 3 || alert->alertType == 4) {
            // برای Exit Mode
            alertMode = "EXIT";
            positionType = alert->isLong ? "LONG" : "SHORT";
            String profitLossType = alert->isProfit ? "PROFIT" : "LOSS";
            alertColorClass = alert->isProfit ? "alert-profit" : "alert-loss";
            alertIcon = alert->isProfit ? "🟢" : "🔴";
            displayType = positionType + " " + profitLossType;
            
        } else {
            // برای Entry Mode
            alertMode = "ENTRY";
            positionType = alert->isLong ? "LONG" : "SHORT";
            String severityPrefix = alert->isSevere ? "SEVERE " : "";
            displayType = severityPrefix + positionType;
            
            if (alert->isSevere) {
                alertIcon = "⚠️";
            } else {
                alertIcon = alert->isLong ? "📈" : "📉";
            }
            alertColorClass = alert->isSevere ? "alert-severe" : (alert->isLong ? "alert-long" : "alert-short");
        }
        
        // ========== HTML جدید برای هر آیتم ==========
        alertHistoryHTML += "<div class='alert-history-item " + alertColorClass + "'>";
        
        // ردیف اول: اطلاعات الرت و زمان
        alertHistoryHTML += "<div style='display:flex;justify-content:space-between;align-items:flex-start;margin-bottom:10px;'>";
        
        // بخش چپ: اطلاعات الرت
        alertHistoryHTML += "<div style='flex:1;'>";
        alertHistoryHTML += "<div style='display:flex;align-items:center;gap:8px;margin-bottom:5px;flex-wrap:wrap;'>";
        alertHistoryHTML += "<span style='font-size:16px;'>" + alertIcon + "</span>";
        alertHistoryHTML += "<span class='alert-history-symbol' style='font-weight:bold;font-size:14px;'>" + String(alert->symbol) + "</span>";
        alertHistoryHTML += " <span class='alert-history-type' style='font-size:12px;padding:2px 8px;border-radius:4px;'>" + displayType + "</span>";
        alertHistoryHTML += " <span class='alert-history-mode' style='font-size:10px;background:#6c757d;color:white;padding:2px 6px;border-radius:3px;'>";
        alertHistoryHTML += alertMode + " MODE</span>";
        alertHistoryHTML += "</div>";
        alertHistoryHTML += "</div>";
        
        // بخش راست: زمان
        alertHistoryHTML += "<div style='text-align:right;min-width:150px;'>";
        alertHistoryHTML += "<div style='font-size:11px;font-family:monospace;color:#2196F3;font-weight:bold;white-space:nowrap;'>";
        alertHistoryHTML += timeStr;
        alertHistoryHTML += "</div>";
        if (elapsedTime != "") {
            alertHistoryHTML += "<div style='font-size:10px;color:#888;margin-top:2px;'>" + elapsedTime + "</div>";
        }
        alertHistoryHTML += "</div>";
        
        alertHistoryHTML += "</div>"; // پایان ردیف اول
        
        // ردیف دوم: اطلاعات قیمت
        alertHistoryHTML += "<div style='display:flex;justify-content:space-between;align-items:center;font-size:12px;color:#555;margin-top:8px;padding-top:8px;border-top:1px solid #eee;'>";
        alertHistoryHTML += "<div>";
        
        // P/L
        alertHistoryHTML += "<span style='margin-right:15px;'>";
        alertHistoryHTML += "P/L: <span style='color:" + String(alert->pnlPercent >= 0 ? "#28a745" : "#dc3545") + ";font-weight:bold;'>";
        alertHistoryHTML += String(alert->pnlPercent, 1) + "%</span>";
        alertHistoryHTML += "</span>";
        
        // قیمت فعلی
        alertHistoryHTML += "<span style='margin-right:15px;'>";
        alertHistoryHTML += "Price: <span style='font-family:monospace;font-weight:bold;'>$" + formatPrice(alert->alertPrice) + "</span>";
        alertHistoryHTML += "</span>";
        
        alertHistoryHTML += "</div>";
        
        // نمایش Last Alert Price
        if (strlen(alert->message) > 0) {
            String messageStr = String(alert->message);
            
            if (messageStr.indexOf("Last Alert:") != -1) {
                int lastAlertStart = messageStr.indexOf("Last Alert:");
                String lastAlertPart = messageStr.substring(lastAlertStart);
                
                // حذف اضافات
                if (lastAlertPart.indexOf(" | Entry:") != -1) {
                    lastAlertPart = lastAlertPart.substring(0, lastAlertPart.indexOf(" | Entry:"));
                }
                if (lastAlertPart.indexOf(" | LONG") != -1) {
                    lastAlertPart = lastAlertPart.substring(0, lastAlertPart.indexOf(" | LONG"));
                }
                if (lastAlertPart.indexOf(" | SHORT") != -1) {
                    lastAlertPart = lastAlertPart.substring(0, lastAlertPart.indexOf(" | SHORT"));
                }
                
                lastAlertPart.trim();
                
                alertHistoryHTML += "<div style='font-size:11px;color:#666;background:#f0f8ff;padding:4px 8px;border-radius:4px;border:1px solid #d1e7ff;font-family:monospace;'>";
                alertHistoryHTML += lastAlertPart;
                alertHistoryHTML += "</div>";
            }
        }
        
        alertHistoryHTML += "</div>"; // پایان ردیف دوم
        alertHistoryHTML += "</div>"; // پایان alert-history-item
        // ========== پایان HTML جدید ==========
    }
    
    // پیام پایانی
    if (alertHistoryCount > 5) {
        alertHistoryHTML += "<div style='text-align:center;padding:10px;color:#666;font-size:12px;background:#f8f9fa;border-radius:5px;margin-top:10px;'>";
        alertHistoryHTML += "↑ " + String(displayCount) + " alerts shown - Scroll up to see more";
        alertHistoryHTML += "</div>";
    }
    
    alertHistoryHTML += "</div>";
} else {
    alertHistoryHTML = "<div style='text-align:center;padding:30px;background:#f8f9fa;border-radius:8px;border:2px dashed #ddd;'>";
    alertHistoryHTML += "<div style='font-size:48px;color:#ccc;margin-bottom:15px;'>📜</div>";
    alertHistoryHTML += "<h4 style='color:#666;margin-bottom:10px;'>No Alert History</h4>";
    alertHistoryHTML += "<p style='color:#888;font-size:14px;'>Alerts will appear here when triggered</p>";
    alertHistoryHTML += "</div>";
}

page.replace("%ALERT_HISTORY%", alertHistoryHTML);

// ========== پایان بخش تاریخچه الرت‌ها ==========


    String positionList = "";
    int displayCount = cryptoCount;
    
    for (int i = 0; i < displayCount; i++) {
        CryptoPosition crypto = sortedCryptoData[i];
        String symbol = getShortSymbol(crypto.symbol);
        String change = formatPercent(crypto.changePercent);
        String cssClass = crypto.isLong ? "position-long" : "position-short";
        String alertIcon = (crypto.alerted || crypto.exitAlerted) ? " ALRT" : "";
        String positionType = crypto.isLong ? "LONG" : "SHORT";
        
        positionList += "<div class='position-item " + cssClass + "'>";
        positionList += "<div>";
        positionList += "<strong>" + String(i+1) + ". " + symbol + "</strong>";
        
        String colorStr;
        if (crypto.isLong) {
            colorStr = "#dc3545";
        } else {
            colorStr = "#ff6b00";
        }
        
        positionList += " <span style='color:" + colorStr + ";'>";
        positionList += "(" + positionType + ")</span>" + alertIcon;
        positionList += "<div class='alert-price'>Price: <span class='price-cell'>$" + formatPrice(crypto.currentPrice) + "</span></div>";
        positionList += "</div>";
        
        String colorStr2;
        if (crypto.changePercent >= 0) {
            colorStr2 = "#28a745";
        } else {
            colorStr2 = "#dc3545";
        }
        
        positionList += "<div style='color:" + colorStr2 + ";'>";
        positionList += "<strong>" + change + "</strong>";
        positionList += "</div>";
        positionList += "</div>";
    }
    
    if (positionList == "") {
        positionList = "<p style='text-align:center;padding:20px;'>No position data available</p>";
    }
    
    page.replace("%POSITION_LIST%", positionList);
    
    server.send(200, "text/html", page);
}

void handleSetup() {
    String page = String(setupPage);
    
    String mode, ip;
    if (isConnectedToWiFi) {
        mode = "✅ WiFi Connected";
        ip = WiFi.localIP().toString();
    } else if (apModeActive) {
        mode = "📶 AP Mode";
        ip = WiFi.softAPIP().toString();
    } else {
        mode = "❌ Disconnected";
        ip = "N/A";
    }
    
    page.replace("%MODE%", mode);
    page.replace("%IP%", ip);
    page.replace("%NETWORK_COUNT%", String(settings.networkCount));
    page.replace("%POSITION_COUNT%", String(cryptoCount));
    
    int alertCount = 0;
    for (int i = 0; i < cryptoCount; i++) {
        if (cryptoData[i].alerted || cryptoData[i].exitAlerted) alertCount++;
    }
    page.replace("%ALERT_COUNT%", String(alertCount));
    
    String activePortfolio = settings.isExitMode ? String(settings.exitPortfolio) : String(settings.entryPortfolio);
    String tradingMode = settings.isExitMode ? "EXIT (Price Change Tracking)" : "ENTRY (Loss Tracking)";
    String modeColor = settings.isExitMode ? "#ff6b00" : "#007bff";
    String apiType = settings.isExitMode ? "PORTFOLIO API (Exit Mode)" : "PORTFOLIO API (Entry Mode)";
    
    page.replace("%ACTIVE_PORTFOLIO%", activePortfolio);
    page.replace("%ENTRY_PORTFOLIO%", String(settings.entryPortfolio));
    page.replace("%EXIT_PORTFOLIO%", String(settings.exitPortfolio));
    page.replace("%TRADING_MODE%", tradingMode);
    page.replace("%MODE_COLOR%", modeColor);
    page.replace("%API_TYPE%", apiType);
    page.replace("%EXIT_ALERT_PERCENT%", String(settings.exitAlertPercent));
    
    String ledStatusDetails = "";
    if (settings.isExitMode) {
        ledStatusDetails = "<p><strong>LED Status:</strong> 🟢 " + String(profitLEDActive ? "ON (PROFIT)" : "OFF") + 
                          " | 🔴 " + String(lossLEDActive ? "ON (LOSS)" : "OFF") + "</p>";
    } else {
        ledStatusDetails = "<p><strong>LED Status:</strong> 🟢 " + String(longAlertLEDActive ? "ON (LONG)" : "OFF") + 
                          " | 🔴 " + String(shortAlertLEDActive ? "ON (SHORT)" : "OFF") + "</p>";
    }
    page.replace("%LED_STATUS_DETAILS%", ledStatusDetails);
    
    page.replace("%ENTRY_MODE_SELECTED%", settings.isExitMode ? "" : "mode-selected");
    page.replace("%EXIT_MODE_SELECTED%", settings.isExitMode ? "mode-selected" : "");
    page.replace("%ENTRY_MODE_CHECKED%", settings.isExitMode ? "" : "checked");
    page.replace("%EXIT_MODE_CHECKED%", settings.isExitMode ? "checked" : "");
    page.replace("%EXIT_MODE_VISIBILITY%", settings.isExitMode ? "display:block;" : "display:none;");
    page.replace("%EXIT_ALERT_ENABLED_CHECKED%", settings.exitAlertEnabled ? "checked" : "");
    page.replace("%EXIT_ALERT_BLINK_CHECKED%", settings.exitAlertBlinkEnabled ? "checked" : "");
    
    page.replace("%LED_GREEN_STATUS%", longAlertLEDActive ? "ON 🟢" : "OFF");
    page.replace("%LED_RED_STATUS%", shortAlertLEDActive ? "ON 🔴" : "OFF");
    page.replace("%LED_BRIGHTNESS%", String(settings.ledBrightness));
    page.replace("%LED_BRIGHTNESS_PERCENT%", String(round(settings.ledBrightness * 100.0 / 255.0)));
    page.replace("%LED_ENABLED%", String(settings.ledEnabled ? "Enabled" : "Disabled"));
    
    String buzzerStatus = settings.buzzerEnabled ? "Enabled" : "Disabled";
    page.replace("%BUZZER_STATUS%", buzzerStatus);
    
    String networkList = "";
    if (settings.networkCount == 0) {
        networkList = "<p>No networks saved</p>";
    } else {
        for (int i = 0; i < settings.networkCount; i++) {
            networkList += "<div class='wifi-network-item'>";
            networkList += "<div>";
            networkList += "<strong>" + String(settings.networks[i].ssid) + "</strong>";
            if (i == settings.lastConnectedIndex) {
                networkList += " <span style='color:green;font-size:12px;'>(Last Connected)</span>";
            }
            networkList += "</div>";
            networkList += "<a href='/removewifi?ssid=" + String(settings.networks[i].ssid) + "' class='delete-btn' onclick='return confirm(\"Delete " + String(settings.networks[i].ssid) + "?\")'>✗ Delete</a>";
            networkList += "</div>";
        }
    }
    page.replace("%NETWORK_LIST%", networkList);
    
    page.replace("%SERVER%", String(settings.server));
    page.replace("%USERNAME%", String(settings.username));
    page.replace("%USERPASS%", String(settings.userpass));
    
    page.replace("%ALERT_THRESHOLD%", String(settings.alertThreshold, 1));
    page.replace("%SEVERE_THRESHOLD%", String(settings.severeAlertThreshold, 1));
    page.replace("%PORTFOLIO_THRESHOLD%", String(settings.portfolioAlertThreshold, 1));
    page.replace("%ALERT_THRESHOLD_SLIDER%", String(settings.alertThreshold));
    page.replace("%SEVERE_THRESHOLD_SLIDER%", String(settings.severeAlertThreshold));
    page.replace("%PORTFOLIO_THRESHOLD_SLIDER%", String(settings.portfolioAlertThreshold));
    page.replace("%VOLUME%", String(settings.buzzerVolume));
    
    String buzzerEnabledChecked = settings.buzzerEnabled ? "checked" : "";
    page.replace("%BUZZER_ENABLED_CHECKED%", buzzerEnabledChecked);
    
    String ledEnabledChecked = settings.ledEnabled ? "checked" : "";
    page.replace("%LED_ENABLED_CHECKED%", ledEnabledChecked);
    
    server.send(200, "text/html", page);
}

void handleSaveWiFi() {
    if (server.hasArg("ssid") && server.hasArg("password")) {
        String ssid = server.arg("ssid");
        String password = server.arg("password");
        
        Serial.println("Received WiFi credentials:");
        Serial.println("SSID: " + ssid);
        Serial.println("Password length: " + String(password.length()));
        
        if (addWiFiNetwork(ssid.c_str(), password.c_str())) {
            String response = "<html><body style='text-align:center;padding:50px;'>";
            response += "<h1 style='color:green;'>✅ WiFi Saved!</h1>";
            response += "<p>Network: " + ssid + "</p>";
            response += "<p>Total networks: " + String(settings.networkCount) + "</p>";
            response += "<p>Attempting to connect...</p>";
            response += "<script>setTimeout(() => location.href='/', 3000);</script>";
            response += "</body></html>";
            
            server.send(200, "text/html", response);
            
            delay(2000);
            connectToWiFi();
        } else {
            server.send(200, "text/html", 
                "<html><body style='text-align:center;padding:50px;'>"
                "<h1 style='color:red;'>❌ Failed to save WiFi!</h1>"
                "<p>Network: " + ssid + "</p>"
                "<p>Error: Maximum networks reached or save failed</p>"
                "<a href='/config'>Back to Setup</a>"
                "</body></html>");
        }
    } else {
        server.send(400, "text/html", 
            "<html><body style='text-align:center;padding:50px;'>"
            "<h1 style='color:red;'>Missing Parameters!</h1>"
            "<p>Please provide both SSID and Password</p>"
            "<a href='/config'>Back to Setup</a>"
            "</body></html>");
    }
}

void handleRemoveWiFi() {
    if (server.hasArg("ssid")) {
        String ssid = server.arg("ssid");
        ssid.replace("+", " ");
        ssid = urlDecode(ssid);
        Serial.println("Removing network: " + ssid);
        removeWiFiNetwork(ssid.c_str());
    }
    
    server.sendHeader("Location", "/config", true);
    server.send(302, "text/plain", "");
}

void handleSaveAPI() {
    Serial.println("\n=== Handling Save API Request ===");
    debugServerArgs();
    
    bool portfoliosChanged = false;
    
    if (server.hasArg("entryportfolio")) {
        String entryPortfolio = server.arg("entryportfolio");
        entryPortfolio.trim();
        if (entryPortfolio != "" && strcmp(settings.entryPortfolio, entryPortfolio.c_str()) != 0) {
            strncpy(settings.entryPortfolio, entryPortfolio.c_str(), 31);
            settings.entryPortfolio[31] = '\0';
            portfoliosChanged = true;
            Serial.println("Entry Portfolio set to: '" + String(settings.entryPortfolio) + "'");
        }
    }
    
    if (server.hasArg("exitportfolio")) {
        String exitPortfolio = server.arg("exitportfolio");
        exitPortfolio.trim();
        if (exitPortfolio != "" && strcmp(settings.exitPortfolio, exitPortfolio.c_str()) != 0) {
            strncpy(settings.exitPortfolio, exitPortfolio.c_str(), 31);
            settings.exitPortfolio[31] = '\0';
            portfoliosChanged = true;
            Serial.println("Exit Portfolio set to: '" + String(settings.exitPortfolio) + "'");
        }
    }
    
    if (server.hasArg("server")) {
        String serverUrl = server.arg("server");
        serverUrl.trim();
        strncpy(settings.server, serverUrl.c_str(), 127);
        settings.server[127] = '\0';
        Serial.println("Server set to: " + String(settings.server));
    }
    
    if (server.hasArg("username")) {
        String username = server.arg("username");
        username.trim();
        strncpy(settings.username, username.c_str(), 31);
        settings.username[31] = '\0';
        Serial.println("Username set to: " + String(settings.username));
    }
    
    if (server.hasArg("userpass")) {
        String userpass = server.arg("userpass");
        userpass.trim();
        strncpy(settings.userpass, userpass.c_str(), 63);
        settings.userpass[63] = '\0';
        Serial.println("Password set (length): " + String(strlen(settings.userpass)));
    }
    
    bool hasError = false;
    String errorMsg = "";
    
    if (strlen(settings.entryPortfolio) == 0) {
        hasError = true;
        errorMsg = "Entry Portfolio name cannot be empty";
    }
    
    if (strlen(settings.exitPortfolio) == 0) {
        hasError = true;
        errorMsg = "Exit Portfolio name cannot be empty";
    }
    
    if (!hasError) {
        if (saveSettings()) {
            String response = "<html><body style='text-align:center;padding:50px;'>";
            response += "<h1 style='color:green;'>✅ API & Portfolio Settings Saved!</h1>";
            response += "<p><strong>Server:</strong> " + String(settings.server) + "</p>";
            response += "<p><strong>Username:</strong> " + String(settings.username) + "</p>";
            response += "<p><strong>Entry Portfolio:</strong> <span style='color:blue;'>" + String(settings.entryPortfolio) + "</span></p>";
            response += "<p><strong>Exit Portfolio:</strong> <span style='color:orange;'>" + String(settings.exitPortfolio) + "</span></p>";
            
            if (portfoliosChanged) {
                response += "<p style='color:#ff6b00;'>⚠️ Portfolio names changed - Clearing data...</p>";
                cryptoCount = 0;
                clearCryptoData();
            }
            
            response += "<script>setTimeout(() => location.href='/config', 2000);</script>";
            response += "</body></html>";
            
            server.send(200, "text/html", response);
        } else {
            hasError = true;
            errorMsg = "Failed to save to EEPROM";
        }
    }
    
    if (hasError) {
        server.send(400, "text/html", 
            "<html><body style='text-align:center;padding:50px;'>"
            "<h1 style='color:red;'>❌ Error Saving Settings</h1>"
            "<p><strong>Error:</strong> " + errorMsg + "</p>"
            "<a href='/config'>Back to Setup</a>"
            "</body></html>");
    }
}

void handleSaveAlert() {
    Serial.println("\n=== Handling Save Alert Request ===");
    debugServerArgs();
    
    bool hasError = false;
    String errorMsg = "";
    
    if (server.hasArg("ledbrightness")) {
        int ledBrightness = server.arg("ledbrightness").toInt();
        if (ledBrightness >= 0 && ledBrightness <= 255) {
            settings.ledBrightness = ledBrightness;
            Serial.println("LED brightness set to: " + String(ledBrightness) + "/255");
        } else {
            hasError = true;
            errorMsg = "Invalid LED brightness value: " + String(ledBrightness);
        }
    }
    
    if (server.hasArg("threshold")) {
        float threshold = server.arg("threshold").toFloat();
        if (threshold >= -50 && threshold <= 0) {
            settings.alertThreshold = threshold;
            Serial.println("Alert threshold set to: " + String(threshold));
        } else {
            hasError = true;
            errorMsg = "Invalid threshold value: " + String(threshold);
        }
    }
    
    if (server.hasArg("severethreshold")) {
        float severeThreshold = server.arg("severethreshold").toFloat();
        if (severeThreshold >= -50 && severeThreshold <= 0) {
            settings.severeAlertThreshold = severeThreshold;
            Serial.println("Severe threshold set to: " + String(severeThreshold));
        } else {
            hasError = true;
            errorMsg = "Invalid severe threshold value: " + String(severeThreshold);
        }
    }
    
    if (server.hasArg("portfoliothreshold")) {
        float portfolioThreshold = server.arg("portfoliothreshold").toFloat();
        if (portfolioThreshold >= -50 && portfolioThreshold <= 0) {
            settings.portfolioAlertThreshold = portfolioThreshold;
            Serial.println("Portfolio threshold set to: " + String(portfolioThreshold));
        } else {
            hasError = true;
            errorMsg = "Invalid portfolio threshold value: " + String(portfolioThreshold);
        }
    }
    
    if (server.hasArg("volume")) {
        int volume = server.arg("volume").toInt();
        if (volume >= 0 && volume <= 20) {
            settings.buzzerVolume = volume;
            Serial.println("Volume set to: " + String(volume));
        } else {
            hasError = true;
            errorMsg = "Invalid volume value: " + String(volume);
        }
    }
    
    if (server.hasArg("buzzerEnabled")) {
        settings.buzzerEnabled = true;
        Serial.println("Buzzer enabled: YES");
    } else {
        settings.buzzerEnabled = false;
        Serial.println("Buzzer enabled: NO");
    }
    
    if (server.hasArg("ledEnabled")) {
        settings.ledEnabled = true;
        Serial.println("LED enabled: YES");
    } else {
        settings.ledEnabled = false;
        Serial.println("LED enabled: NO");
    }
    
    if (!hasError) {
        if (saveSettings()) {
            String response = "<html><body style='text-align:center;padding:50px;'>";
            response += "<h1 style='color:green;'>✅ Settings Saved!</h1>";
            response += "<p>LED Brightness: " + String(settings.ledBrightness) + "/255</p>";
            response += "<p>Alert Threshold: " + String(settings.alertThreshold, 1) + "%</p>";
            response += "<p>Severe Threshold: " + String(settings.severeAlertThreshold, 1) + "%</p>";
            response += "<p>Portfolio Threshold: " + String(settings.portfolioAlertThreshold, 1) + "%</p>";
            response += "<p>Volume: " + String(settings.buzzerVolume) + "/20</p>";
            response += "<script>setTimeout(() => location.href='/config', 2000);</script>";
            response += "</body></html>";
            
            server.send(200, "text/html", response);
        } else {
            hasError = true;
            errorMsg = "Failed to save settings to EEPROM!";
        }
    }
    
    if (hasError) {
        server.send(400, "text/html", 
            "<html><body style='text-align:center;padding:50px;'>"
            "<h1 style='color:red;'>❌ Error Saving Settings</h1>"
            "<p><strong>Error:</strong> " + errorMsg + "</p>"
            "<p>Please check the values and try again.</p>"
            "<a href='/config'>Back to Settings</a>"
            "</body></html>");
    }
    
    Serial.println("=== Save Alert Request Complete ===");
}

void handleSaveMode() {
    Serial.println("\n=== Handling Save Mode Request ===");
    debugServerArgs();
    
    bool modeChanged = false;
    
    if (server.hasArg("tradingmode")) {
        String mode = server.arg("tradingmode");
        bool newExitMode = (mode == "exit");
        
        if (newExitMode != settings.isExitMode) {
            modeChanged = true;
            settings.isExitMode = newExitMode;
            Serial.println("Trading mode changed to: " + String(settings.isExitMode ? "EXIT" : "ENTRY"));
            
            resetAllAlerts();
        } else {
            Serial.println("Trading mode remains: " + String(settings.isExitMode ? "EXIT" : "ENTRY"));
        }
    }
    
    if (server.hasArg("exitalertpercent")) {
        float percent = server.arg("exitalertpercent").toFloat();
        if (percent >= 1 && percent <= 20) {
            settings.exitAlertPercent = percent;
            Serial.println("Exit alert percent set to: " + String(percent) + "%");
        }
    }
    
    if (server.hasArg("exitalertenabled")) {
        settings.exitAlertEnabled = true;
        Serial.println("Exit alerts: ENABLED");
    } else {
        settings.exitAlertEnabled = false;
        Serial.println("Exit alerts: DISABLED");
    }
    
    if (server.hasArg("exitalertblink")) {
        settings.exitAlertBlinkEnabled = true;
        Serial.println("Exit alert blinking: ENABLED");
    } else {
        settings.exitAlertBlinkEnabled = false;
        Serial.println("Exit alert blinking: DISABLED");
    }
    
    if (saveSettings()) {
        String response = "<html><body style='text-align:center;padding:50px;'>";
        response += "<h1 style='color:green;'>✅ Mode Settings Saved!</h1>";
        response += "<p>Trading Mode: <strong>" + String(settings.isExitMode ? "EXIT (Price Change Tracking)" : "ENTRY (Loss Tracking)") + "</strong></p>";
        response += "<p>Active Portfolio: <strong>" + String(settings.isExitMode ? settings.exitPortfolio : settings.entryPortfolio) + "</strong></p>";
        
        if (modeChanged) {
            response += "<p style='color:#ff6b00;'>⚠️ Mode changed - All alerts have been reset</p>";
        }
        
        if (settings.isExitMode) {
            response += "<p>Exit Alert Percent: " + String(settings.exitAlertPercent) + "% price change</p>";
            response += "<p>Exit Alerts: " + String(settings.exitAlertEnabled ? "ENABLED" : "DISABLED") + "</p>";
            response += "<p>LED Blinking: " + String(settings.exitAlertBlinkEnabled ? "ENABLED" : "DISABLED") + "</p>";
            response += "<p>💡 LED Behavior: Green=PROFIT, Red=LOSS (regardless of position type)</p>";
            response += "<p>🔗 API Endpoint: /api/device/portfolio/</p>";
            response += "<p>📁 Portfolio: " + String(settings.exitPortfolio) + "</p>";
        } else {
            response += "<p>🔗 API Endpoint: /api/device/portfolio/</p>";
            response += "<p>📁 Portfolio: " + String(settings.entryPortfolio) + "</p>";
        }
        
        response += "<script>setTimeout(() => location.href='/config', 3000);</script>";
        response += "</body></html>";
        
        server.send(200, "text/html", response);
    } else {
        server.send(500, "text/html", 
            "<html><body style='text-align:center;padding:50px;'>"
            "<h1 style='color:red;'>❌ Failed to save mode settings!</h1>"
            "<a href='/config'>Back to Setup</a>"
            "</body></html>");
    }
}

void handleRefresh() {
    if (isConnectedToWiFi) {
        String data = getPortfolioData();
        parseCryptoData(data);
    }
    
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
}

void handleTestAlert() {
    playTestAlertSequence();
    
    turnOnGreenLED();
    delay(300);
    turnOffGreenLED();
    delay(100);
    turnOnRedLED();
    delay(300);
    turnOffRedLED();
    
    String buzzerStatus = settings.buzzerEnabled ? "Enabled" : "Disabled";
    String ledStatus = settings.ledEnabled ? "Enabled" : "Disabled";
    
    server.send(200, "text/html", 
        "<html><body style='text-align:center;padding:50px;'>"
        "<h1>Test Alert Sequence</h1>"
        "<p><strong>Buzzer Status:</strong> " + buzzerStatus + "</p>"
        "<p><strong>Volume Level:</strong> " + String(settings.buzzerVolume) + "/20</p>"
        "<p><strong>LED Status:</strong> " + ledStatus + "</p>"
        "<p><strong>LED Brightness:</strong> " + String(settings.ledBrightness) + "/255</p>"
        "<p><strong>Test Sequence:</strong> Long (2 beeps), Short (1 beep), Success tone</p>"
        "<a href='/'>Back</a>"
        "</body></html>");
}

void handleResetAlerts() {
    resetAllAlerts();
    
    server.send(200, "text/html", 
        "<html><body style='text-align:center;padding:50px;'>"
        "<h1>Alerts Reset!</h1>"
        "<p>All position alerts have been reset</p>"
        "<p>LEDs have been turned off</p>"
        "<a href='/'>Back</a>"
        "</body></html>");
}

void handleWiFiManager() {
    handleSetup();
}

void handleConfig() {
    handleSetup();
}

void handleTestConnection() {
    String result = "";
    
    if (!isConnectedToWiFi) {
        result = "<h3 style='color:red;'>❌ Not connected to WiFi</h3>";
    } else if (strlen(settings.server) == 0) {
        result = "<h3 style='color:red;'>❌ API Server not configured</h3>";
    } else {
        String testUrl = String(settings.server) + "/api/device/portfolio/" + 
                        String(settings.username) + "?portfolio_name=" + 
                        String(settings.isExitMode ? settings.exitPortfolio : settings.entryPortfolio);
        
        result = "<h3>Testing API Connection</h3>";
        result += "<p><strong>URL:</strong> " + testUrl + "</p>";
        result += "<p><strong>Username:</strong> " + String(settings.username) + "</p>";
        result += "<p><strong>Portfolio:</strong> " + String(settings.isExitMode ? settings.exitPortfolio : settings.entryPortfolio) + "</p>";
        
        // تست اتصال
        http.begin(testUrl);
        String authString = String(settings.username) + ":" + String(settings.userpass);
        String encodedAuth = base64Encode(authString);
        http.addHeader("Authorization", "Basic " + encodedAuth);
        
        int httpCode = http.GET();
        result += "<p><strong>HTTP Response:</strong> " + String(httpCode) + "</p>";
        
        if (httpCode == 200) {
            String response = http.getString();
            result += "<p><strong>Response length:</strong> " + String(response.length()) + " bytes</p>";
            result += "<div style='background:#f0f0f0;padding:10px;border-radius:5px;max-height:200px;overflow:auto;'>";
            result += "<pre>" + response.substring(0, 500) + "...</pre>";
            result += "</div>";
        } else {
            result += "<p style='color:red;'>❌ Connection failed: " + String(http.errorToString(httpCode)) + "</p>";
        }
        
        http.end();
    }
    
    String page = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Test Connection</title>";
    page += "<style>body{font-family:Arial;padding:20px;}</style></head><body>";
    page += "<h1>API Connection Test</h1>";
    page += result;
    page += "<p><a href='/config'>Back to Setup</a></p>";
    page += "</body></html>";
    
    server.send(200, "text/html", page);
}

void handleLoginPage() {
    String html = R"=====(
<!DOCTYPE html>
<html><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Portfolio Monitor - Login</title>
<style>
body{font-family:Arial;background:#f0f0f0;margin:0;padding:0;display:flex;justify-content:center;align-items:center;min-height:100vh;}
.login-container{background:white;padding:40px;border-radius:10px;box-shadow:0 5px 20px rgba(0,0,0,0.1);width:100%;max-width:400px;}
h1{text-align:center;color:#333;margin-bottom:30px;}
.form-group{margin-bottom:20px;}
label{display:block;margin-bottom:5px;color:#555;}
input[type="text"], input[type="password"]{width:100%;padding:12px;border:1px solid #ddd;border-radius:5px;font-size:16px;}
.btn{width:100%;padding:14px;background:#4CAF50;color:white;border:none;border-radius:5px;cursor:pointer;font-size:16px;font-weight:bold;}
.btn:hover{background:#45a049;}
.error{color:red;text-align:center;margin-top:15px;}
.info{text-align:center;color:#666;margin-top:20px;font-size:14px;}
</style>
</head>
<body>
<div class="login-container">
<h1>🔒 Portfolio Monitor</h1>
<p class="info">Session timeout: 5 minutes</p>
<form action="/login" method="post">
<div class="form-group">
<label>Username:</label>
<input type="text" name="username" required>
</div>
<div class="form-group">
<label>Password:</label>
<input type="password" name="password" required>
</div>
<button type="submit" class="btn">Login</button>
</form>
</div>
</body>
</html>
)=====";
    
    server.send(200, "text/html", html);
}



void handleLoginPost() {
    // بررسی credentials
    // اگر صحیح بود:
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
}

void setupWebServer() {
    Serial.println("Setting up web server...");
    
    // صفحات اصلی
    server.on("/", handleRoot);
    server.on("/config", handleConfig);
    server.on("/setup", handleSetup);
    
    // سیستم لاگین
    server.on("/login", HTTP_GET, handleLoginPage);
    server.on("/login", HTTP_POST, handleLoginPost);
    
    // مدیریت WiFi
    server.on("/savewifi", HTTP_POST, handleSaveWiFi);
    server.on("/removewifi", handleRemoveWiFi);
    server.on("/wifimanager", handleWiFiManager);
    
    // مدیریت API
    server.on("/saveapi", HTTP_POST, handleSaveAPI);
    server.on("/testconnection", handleTestConnection);  // <-- اینجا اضافه شد
    
    // مدیریت آلرت‌ها و تنظیمات
    server.on("/savealert", HTTP_POST, handleSaveAlert);
    server.on("/savemode", HTTP_POST, handleSaveMode);
    server.on("/refresh", handleRefresh);
    server.on("/testalert", handleTestAlert);
    server.on("/resetalerts", handleResetAlerts);
    
    // صفحه 404
    server.onNotFound([]() {
        server.send(404, "text/html", 
            "<html><body style='text-align:center;padding:50px;'>"
            "<h1>404 - Page Not Found</h1>"
            "<p><a href='/'>Home</a></p>"
            "</body></html>");
    });
    
    server.begin();
    Serial.println("Web server started on port 80");
    Serial.println("Available pages:");
    Serial.println("  /               - Dashboard");
    Serial.println("  /config         - Setup page");
    Serial.println("  /testconnection - Test API connection");  // <-- این هم اضافه کنید
    Serial.println("  /testalert      - Test alert sounds");
    Serial.println("  /resetalerts    - Reset all alerts");
    Serial.println("  /refresh        - Refresh data");
}

void setupResetButton() {
    pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);
    Serial.println("Reset button initialized");
}

void checkResetButton() {
    int buttonState = digitalRead(RESET_BUTTON_PIN);
    
    if (buttonState == LOW) {
        if (!resetButtonActive) {
            resetButtonActive = true;
            lastResetButtonPress = millis();
            Serial.println("Reset button pressed");
        } else {
            unsigned long holdTime = millis() - lastResetButtonPress;
            
            if (holdTime >= BUTTON_HOLD_TIME && !resetInProgress) {
                resetInProgress = true;
                factoryReset();
            }
        }
    } else {
        resetButtonActive = false;
    }
}

void factoryReset() {
    Serial.println("Performing factory reset...");
    showTFTMessage("Factory Reset", "Erasing all", "settings...", "");
    
    initializeSettings();
    
    EEPROM.begin(EEPROM_SIZE);
    for (int i = 0; i < EEPROM_SIZE; i++) {
        EEPROM.write(i, 0);
    }
    EEPROM.commit();
    EEPROM.end();
    
    showTFTMessage("Reset Complete", "Restarting", "device...", "");
    delay(2000);
    
    Serial.println("Factory reset complete, restarting...");
    ESP.restart();
}

void setManualDateTime(int year, int month, int day, int hour, int minute, int second) {
    Serial.println("\n=== Setting Manual Date/Time ===");
    Serial.println("Year: " + String(year));
    Serial.println("Month: " + String(month));
    Serial.println("Day: " + String(day));
    Serial.println("Hour: " + String(hour));
    Serial.println("Minute: " + String(minute));
    Serial.println("Second: " + String(second));
    
    struct tm timeinfo;
    
    timeinfo.tm_year = year - 1900;
    timeinfo.tm_mon = month - 1;
    timeinfo.tm_mday = day;
    timeinfo.tm_hour = hour;
    timeinfo.tm_min = minute;
    timeinfo.tm_sec = second;
    timeinfo.tm_isdst = 0;
    
    time_t new_time = mktime(&timeinfo);
    struct timeval tv = { .tv_sec = new_time };
    
    if (settimeofday(&tv, NULL) == 0) {
        Serial.println("✅ Manual date/time set successfully");
        
        getLocalTime(&timeinfo, 100);
        char timeString[25];
        strftime(timeString, sizeof(timeString), "%Y/%m/%d %H:%M:%S", &timeinfo);
        currentDateTime = String(timeString);
        Serial.println("Date set to: " + currentDateTime);
    } else {
        Serial.println("❌ Failed to set manual date/time");
    }
}

void storeAlert(
    const char* symbol,
    float alertPrice,
    float pnlPercent,
    bool isLong,
    bool isSevere,
    bool isProfit,
    byte alertType,
    const char* message
) {
    time_t now;
    time(&now);

    if (now < 1700000000UL) {
        Serial.println("❌ storeAlert skipped: time not synced");
        return;
    }

    if (activeAlertCount >= MAX_ALERT_HISTORY) {
        Serial.println("❌ storeAlert skipped: history full");
        return;
    }

    AlertHistory &alert = alertHistory[activeAlertCount];

    strncpy(alert.symbol, symbol, sizeof(alert.symbol) - 1);
    alert.symbol[sizeof(alert.symbol) - 1] = '\0';

    alert.alertPrice  = alertPrice;
    alert.pnlPercent  = pnlPercent;
    alert.isLong      = isLong;
    alert.isSevere    = isSevere;
    alert.isProfit    = isProfit;
    alert.alertType   = alertType;
    alert.acknowledged = false;

    strncpy(alert.message, message, sizeof(alert.message) - 1);
    alert.message[sizeof(alert.message) - 1] = '\0';

    alert.alertTime = (unsigned long)now;

    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    strftime(
        alert.timeString,
        sizeof(alert.timeString),
        "%m/%d %H:%M",
        &timeinfo
    );

    activeAlertCount++;

    Serial.print("✅ Alert stored: ");
    Serial.print(alert.symbol);
    Serial.print(" at ");
    Serial.println(alert.timeString);
}

void printTimeStatus() {
    struct tm timeinfo;
    
    Serial.println("\n=== Current Time Status ===");
    
    if (getLocalTime(&timeinfo, 100)) {
        char timeString[30];
        strftime(timeString, sizeof(timeString), "%Y/%m/%d %H:%M:%S", &timeinfo);
        
        Serial.println("Formatted time: " + String(timeString));
        Serial.println("Year: " + String(timeinfo.tm_year + 1900));
        Serial.println("Month: " + String(timeinfo.tm_mon + 1));
        Serial.println("Day: " + String(timeinfo.tm_mday));
        Serial.println("Hour: " + String(timeinfo.tm_hour));
        Serial.println("Minute: " + String(timeinfo.tm_min));
        Serial.println("Second: " + String(timeinfo.tm_sec));
        
        time_t time_seconds = mktime(&timeinfo);
        unsigned long timestamp = (unsigned long)time_seconds * 1000;
        Serial.println("Current timestamp: " + String(timestamp));
    } else {
        Serial.println("❌ Cannot get local time");
    }
    
    Serial.println("Current DateTime variable: " + currentDateTime);
    Serial.println("==========================\n");
}

// ==================== DEBUG FUNCTIONS ====================
void debugExitModeStatus() {
    Serial.println("\n=== DEBUG: Exit Mode Status ===");
    Serial.println("Index | Symbol | Current Price | Last Exit Price | Change% | Alerted");
    
    int displayCount = min(10, cryptoCount);
    for (int i = 0; i < displayCount; i++) {
        float change = 0;
        if (cryptoData[i].exitAlertLastPrice > 0) {
            change = fabs((cryptoData[i].currentPrice - cryptoData[i].exitAlertLastPrice) / 
                         cryptoData[i].exitAlertLastPrice * 100);
        }
        
        Serial.print(i);
        Serial.print(" | ");
        Serial.print(cryptoData[i].symbol);
        Serial.print(" | ");
        Serial.print(formatPrice(cryptoData[i].currentPrice));
        Serial.print(" | ");
        Serial.print(formatPrice(cryptoData[i].exitAlertLastPrice));
        Serial.print(" | ");
        Serial.print(change, 2);
        Serial.print("% | ");
        Serial.println(cryptoData[i].exitAlerted ? "YES" : "NO");
    }
    
    Serial.println("Threshold: " + String(settings.exitAlertPercent) + "%");
    Serial.println("Mode: " + String(settings.isExitMode ? "EXIT" : "ENTRY"));
    Serial.println("API Type: " + String(settings.isExitMode ? "PORTFOLIO (Exit)" : "PORTFOLIO (Entry)"));
    Serial.println("Exit Portfolio: " + String(settings.exitPortfolio));
    Serial.println("Entry Portfolio: " + String(settings.entryPortfolio));
    Serial.println("==============================\n");
}

unsigned long getCorrectTimestamp() {
    struct tm timeinfo;
    
    if (getLocalTime(&timeinfo, 100)) {
        time_t time_seconds = mktime(&timeinfo);
        unsigned long timestamp = (unsigned long)time_seconds * 1000;
        return timestamp;
    }
    
    Serial.println("WARNING: Cannot get system time, using fallback timestamp");
    
    static unsigned long baseTimestamp = 0;
    static bool initialized = false;
    
    if (!initialized) {
        struct tm jan4_2026 = {0};
        jan4_2026.tm_year = 126;
        jan4_2026.tm_mon = 0;
        jan4_2026.tm_mday = 4;
        jan4_2026.tm_hour = 0;
        jan4_2026.tm_min = 0;
        jan4_2026.tm_sec = 0;
        jan4_2026.tm_isdst = -1;
        
        time_t base_seconds = mktime(&jan4_2026);
        baseTimestamp = (unsigned long)base_seconds * 1000;
        initialized = true;
        
        Serial.println("Fallback base timestamp: " + String(baseTimestamp));
    }
    
    return baseTimestamp + millis();
}

String getTimeStringForAlert() {
    if (currentDateTime.length() >= 19) {
        String monthDay = currentDateTime.substring(5, 10);
        String timePart = currentDateTime.substring(11, 19);
        return monthDay + " " + timePart;
    }
    
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 100)) {
        char timeString[20];
        strftime(timeString, sizeof(timeString), "%m/%d %H:%M:%S", &timeinfo);
        return String(timeString);
    }
    
    return "N/A";
}

String getCurrentTimeForDisplay() {
    if (currentDateTime.length() >= 19) {
        String monthDay = currentDateTime.substring(5, 10);
        String timePart = currentDateTime.substring(11, 19);
        return monthDay + " " + timePart;
    }
    
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 100)) {
        char timeString[20];
        strftime(timeString, sizeof(timeString), "%m/%d %H:%M:%S", &timeinfo);
        return String(timeString);
    }
    
    return "N/A";
}

void validateAlertTimestamps() {
    Serial.println("\n=== Validating Alert Timestamps ===");
    Serial.println("Total alerts in history: " + String(alertHistoryCount));
    
    if (alertHistoryCount == 0) {
        Serial.println("No alerts to validate");
        return;
    }
    
    int fixedCount = 0;
    
    for (int i = 0; i < alertHistoryCount; i++) {
        AlertHistory* alert = &alertHistory[i];
        
        if (alert->alertTime < 1000000000000UL) {
            Serial.println("Alert " + String(i) + " (" + String(alert->symbol) + "): Fixing invalid timestamp");
            
            unsigned long currentTime = getCorrectTimestamp();
            unsigned long timeOffset = (alertHistoryCount - i - 1) * 60000;
            alert->alertTime = currentTime - timeOffset;
            
            String newTimeStr = getFullDateTimeString(alert->alertTime);
            newTimeStr.toCharArray(alert->timeString, 20);
            
            fixedCount++;
        }
    }
    
    Serial.println("Fixed " + String(fixedCount) + " alert timestamps");
    Serial.println("===========================\n");
}

void printAlertHistoryStatus() {
    Serial.println("\n=== Alert History Status ===");
    Serial.println("Total alerts: " + String(alertHistoryCount));
    
    for (int i = 0; i < min(5, alertHistoryCount); i++) {
        AlertHistory* alert = &alertHistory[i];
        
        String modeStr = (alert->alertMode == 0) ? "ENTRY" : "EXIT";
        String positionStr = alert->isLong ? "LONG" : "SHORT";
        String alertTypeStr = "";
        
        if (alert->alertType == 1) alertTypeStr = "NORMAL";
        else if (alert->alertType == 2) alertTypeStr = "SEVERE";
        else if (alert->alertType == 3) alertTypeStr = "PROFIT";
        else if (alert->alertType == 4) alertTypeStr = "LOSS";
        
        Serial.print("Alert " + String(i) + " (" + modeStr + " " + positionStr + " " + alertTypeStr + "): ");
        Serial.print(String(alert->symbol) + " - ");
        Serial.print("Time: " + String(alert->timeString) + " - ");
        Serial.println("P/L: " + String(alert->pnlPercent, 1) + "%");
    }
    Serial.println("===========================\n");
}

// ==================== SETUP FUNCTION ====================
// ==================== SETUP FUNCTION ====================
void setup() {
    Serial.begin(115200);
    delay(3000);
    
    Serial.println("\n\n==========================================");
    Serial.println("   ESP32-WROVER Portfolio Monitor v3.8.4");
    Serial.println("==========================================\n");
    
    // راه‌اندازی اولیه سخت‌افزار
    setupTFT();
    setupBuzzer();
    setupLEDs();
    setupResetButton();
    
    showTFTMessage("System", "Booting...", "v3.8.4", "ESP32-WROVER", "");
    delay(1500);
    
    // بارگذاری تنظیمات
    if (!loadSettings()) {
        Serial.println("Using default settings");
    }
    
    Serial.println("\n=== System Initialization ===");
    Serial.println("Boot Count: " + String(settings.bootCount));
    Serial.println("Trading Mode: " + String(settings.isExitMode ? "EXIT" : "ENTRY"));
    Serial.println("Entry Portfolio: '" + String(settings.entryPortfolio) + "'");
    Serial.println("Exit Portfolio: '" + String(settings.exitPortfolio) + "'");
    Serial.println("Saved WiFi Networks: " + String(settings.networkCount));
    
    // === ابتدا WiFi را امتحان کن (اگر شبکه‌ای ذخیره شده) ===
    bool wifiConnected = false;
    
    if (settings.networkCount > 0) {
        Serial.println("\n=== Attempting WiFi Connection ===");
        showTFTMessage("Connecting", "to WiFi...", "", "", "");
        
        wifiConnected = connectToWiFi();
        
        if (wifiConnected) {
            Serial.println("✅ WiFi connection successful!");
            
            // راه‌اندازی NTP بلافاصله پس از اتصال
            configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
            delay(2000);
            
            struct tm timeinfo;
            if (getLocalTime(&timeinfo, 5000)) {
                char timeString[25];
                strftime(timeString, sizeof(timeString), "%Y/%m/%d %H:%M:%S", &timeinfo);
                currentDateTime = String(timeString);
                Serial.println("Time synchronized: " + currentDateTime);
            }
            
            // اولین دریافت داده را انجام بده
            Serial.println("\n=== Fetching Initial Data ===");
            String data = getPortfolioData();
            
            if (data.indexOf("error") == -1 && data.length() > 10) {
                parseCryptoData(data);
                Serial.println("✅ Initial data fetched successfully");
                Serial.println("Positions loaded: " + String(cryptoCount));
                
                showTFTMessage("✅ Connected", "Data Loaded", String(cryptoCount) + " positions", "", "");
            } else {
                Serial.println("❌ Failed to fetch initial data");
                Serial.println("Error: " + data);
                
                showTFTMessage("Connected", "API Error", "Check settings", "", "");
            }
            
            delay(2000);
            
            // نمایش اطلاعات اتصال
            showTFTMessage("✅ WiFi Connected", 
                          "SSID: " + WiFi.SSID(), 
                          "IP: " + WiFi.localIP().toString(),
                          "Mode: " + String(settings.isExitMode ? "EXIT" : "ENTRY"),
                          "");
                          
        } else {
            Serial.println("❌ WiFi connection failed");
            showTFTMessage("WiFi Failed", "Starting AP", "Mode...", "", "");
        }
    } else {
        Serial.println("No saved WiFi networks");
        showTFTMessage("No WiFi", "Configured", "Starting AP", "Mode...", "");
    }
    
    // === اگر WiFi وصل نشد یا شبکه‌ای ذخیره نشده، AP Mode را شروع کن ===
    if (!wifiConnected) {
        delay(2000); // تأخیر قبل از شروع AP Mode
        
        Serial.println("\n=== Starting AP Mode ===");
        
        if (startAPMode()) {
            Serial.println("✅ AP Mode started successfully");
            showTFTMessage("AP Mode Active", 
                          "SSID: ESP32-Pfolio", 
                          "Pass: 12345678", 
                          "IP: 192.168.4.1",
                          "Connect to configure");
        } else {
            Serial.println("❌ AP Mode failed!");
            showTFTMessage("AP Failed", "Restarting...", "in 5 seconds", "", "");
            delay(5000);
            ESP.restart();
            return;
        }
    }
    
    // === راه‌اندازی وب سرور ===
    Serial.println("\n=== Starting Web Server ===");
    setupWebServer();
    Serial.println("✅ Web server ready on port 80");
    
    // === اگر WiFi وصل است، NTP را دوباره راه‌اندازی کن (برای اطمینان) ===
    if (isConnectedToWiFi) {
        Serial.println("Re-initializing NTP...");
        configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
        delay(1000);
        updateDateTime();
    }
    
    delay(2000);
    tft.fillScreen(TFT_BLACK);
    
    // === نمایش اطلاعات نهایی ===
    Serial.println("\n==========================================");
    Serial.println("✅ System Setup Completed!");
    Serial.println("==========================================");
    
    if (isConnectedToWiFi) {
        Serial.println("🔗 WiFi Mode: ACTIVE");
        Serial.println("   SSID: " + WiFi.SSID());
        Serial.println("   IP: " + WiFi.localIP().toString());
        Serial.println("   Signal: " + String(WiFi.RSSI()) + " dBm");
        Serial.println("   Access URL: http://" + WiFi.localIP().toString());
    }
    
    if (apModeActive) {
        Serial.println("📶 AP Mode: ACTIVE");
        Serial.println("   SSID: ESP32-Pfolio");
        Serial.println("   Password: 12345678");
        Serial.println("   IP: 192.168.4.1");
        Serial.println("   Access URL: http://192.168.4.1");
    }
    
    Serial.println("📊 Trading Mode: " + String(settings.isExitMode ? "EXIT" : "ENTRY"));
    Serial.println("📁 Entry Portfolio: " + String(settings.entryPortfolio));
    Serial.println("📁 Exit Portfolio: " + String(settings.exitPortfolio));
    
    if (isConnectedToWiFi) {
        Serial.println("🔄 API Endpoint: " + String(settings.server) + "/api/device/portfolio/");
    }
    
    Serial.println("💾 Positions in memory: " + String(cryptoCount));
    Serial.println("🔔 Alerts configured: " + String(settings.alertThreshold, 1) + "% / " + 
                   String(settings.severeAlertThreshold, 1) + "%");
    
    if (settings.isExitMode) {
        Serial.println("💰 Exit Alert Threshold: " + String(settings.exitAlertPercent) + "%");
    }
    
    Serial.println("==========================================\n");
    
    // تست کوتاه LED و Buzzer
    if (settings.buzzerEnabled) {
        playSuccessTone();
    }
    
    if (settings.ledEnabled) {
        // تست LED سبز
        setRGB1Color(0, 255, 0);
        turnOnGreenLED();
        delay(300);
        turnOffRGB1();
        turnOffGreenLED();
        
        // تست LED قرمز
        setRGB2Color(255, 0, 0);
        turnOnRedLED();
        delay(300);
        turnOffRGB2();
        turnOffRedLED();
    }
}


// ==================== LOOP FUNCTION ====================
void loop() {
    server.handleClient();
    checkResetButton();
    
    if (resetInProgress) return;
    
    // نمایش وضعیت
    static unsigned long lastStatusUpdate = 0;
    if (millis() - lastStatusUpdate > 15000) {
        Serial.println("\n=== System Status ===");
        Serial.println("AP Mode: " + String(apModeActive ? "Active" : "Inactive"));
        Serial.println("WiFi Connected: " + String(isConnectedToWiFi ? "Yes" : "No"));
        Serial.println("WiFi Status: " + String(WiFi.status()));
        if (isConnectedToWiFi) {
            Serial.println("SSID: " + WiFi.SSID());
            Serial.println("IP: " + WiFi.localIP().toString());
        }
        if (apModeActive) {
            Serial.println("AP IP: 192.168.4.1");
        }
        Serial.println("====================");
        lastStatusUpdate = millis();
    }
    
    // اگر به WiFi وصلیم و AP Mode فعال نیست
    if (isConnectedToWiFi && !apModeActive) {
        if (millis() - lastDataUpdate > DATA_UPDATE_INTERVAL) {
            String data = getPortfolioData();
            
            if (data.indexOf("error") == -1 && data.length() > 10) {
                parseCryptoData(data);
                checkCryptoAlerts();
                lastDataUpdate = millis();
            } else {
                Serial.println("Failed to fetch data or empty response");
                // اگر چند بار خطا خورد، ریست کن
                static int fetchErrorCount = 0;
                fetchErrorCount++;
                if (fetchErrorCount > 3) {
                    Serial.println("Too many fetch errors, reconnecting WiFi...");
                    fetchErrorCount = 0;
                    connectToWiFi();
                }
            }
        }
        
        updateDateTime();
    }
    // اگر به WiFi وصل نیستیم و AP Mode هم فعال نیست
    else if (!isConnectedToWiFi && !apModeActive) {
        // بعد از 30 ثانیه AP Mode را شروع کن
        static unsigned long lastNoConnection = 0;
        if (lastNoConnection == 0) lastNoConnection = millis();
        
        if (millis() - lastNoConnection > 30000) {
            Serial.println("No connection for 30s, starting AP mode...");
            startAPMode();
            lastNoConnection = 0;
        }
    }
    
    // به‌روزرسانی نمایشگر
    updateTFTDisplay();
    
    if (settings.ledEnabled) {
        updateLEDBlinking();
        manageLEDStates();
    }
    
    delay(10);
}

void manageWiFiConnection() {
    static unsigned long lastCheck = 0;
    
    if (millis() - lastCheck > 30000) { // هر 30 ثانیه
        lastCheck = millis();
        
        // فقط اگر در حالت WiFi هستیم وضعیت را چک کنیم
        if (isConnectedToWiFi && !apModeActive) {
            if (WiFi.status() != WL_CONNECTED) {
                Serial.println("⚠️ WiFi lost connection!");
                isConnectedToWiFi = false;
                
                // اگر شبکه‌ای ذخیره شده داریم، دوباره تلاش کن
                if (settings.networkCount > 0) {
                    Serial.println("Attempting to reconnect...");
                    connectToWiFi();
                } else {
                    // اگر شبکه‌ای نداریم، AP Mode را شروع کن
                    Serial.println("No saved networks, starting AP...");
                    startAPMode();
                }
            }
        }
    }
}
