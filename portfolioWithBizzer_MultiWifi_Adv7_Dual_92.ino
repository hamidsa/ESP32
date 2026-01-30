/* ============================================================================
   PORTFOLIO MONITOR - ESP32
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
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <time.h>

// ===== DEFINES =====
#define MAX_ALERT_HISTORY 10

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
};

// ===== GLOBAL VARIABLES =====

uint8_t activeAlertCount = 0;

// ===== NTP CONFIG =====
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 3 * 3600 + 1800;   // Iran
const int   daylightOffset_sec = 0;
// ==================== OLED CONFIGURATION ====================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_MOSI  23
#define OLED_CLK   18
#define OLED_DC    16
#define OLED_CS    5
#define OLED_RESET 17

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT,
  OLED_MOSI, OLED_CLK, OLED_DC, OLED_RESET, OLED_CS);

// ==================== LED ALERT SYSTEM ====================
#define LED_GREEN 22
#define LED_RED   21
#define DEFAULT_LED_BRIGHTNESS 100

// ==================== BUZZER CONFIGURATION ====================
#define BUZZER_PIN 19
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
#define AP_MODE_TIMEOUT 5000
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
    
    int oledBrightness;
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
bool oledConnected = false;
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
void setupOLED();
void updateOLEDDisplay();
void showOLEDMessage(String line1, String line2, String line3, String line4);
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
void setupWebServer();
void setupResetButton();
void checkResetButton();
void factoryReset();
void debugPositionData();
void debugServerArgs();
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
    Serial.println("Setting up Alert LEDs...");
    
    pinMode(LED_GREEN, OUTPUT);
    pinMode(LED_RED, OUTPUT);
    
    turnOffAllLEDs();
    
    turnOnGreenLED();
    delay(300);
    turnOffGreenLED();
    delay(100);
    turnOnRedLED();
    delay(300);
    turnOffRedLED();
    
    Serial.println("Alert LEDs initialized");
    Serial.println("  GREEN (GPIO 22) = LONG position alerts / PROFIT (Exit Mode)");
    Serial.println("  RED   (GPIO 21) = SHORT position alerts / LOSS (Exit Mode)");
    Serial.println("  Brightness level: " + String(settings.ledBrightness) + "/255");
}

void turnOnGreenLED() {
    if (settings.ledEnabled) {
        int brightness = map(settings.ledBrightness, 0, 255, 0, 255);
        analogWrite(LED_GREEN, brightness);
    }
}

void turnOnRedLED() {
    if (settings.ledEnabled) {
        int brightness = map(settings.ledBrightness, 0, 255, 0, 255);
        analogWrite(LED_RED, brightness);
    }
}

void turnOffGreenLED() {
    analogWrite(LED_GREEN, 0);
}

void turnOffRedLED() {
    analogWrite(LED_RED, 0);
}

void turnOffAllLEDs() {
    analogWrite(LED_GREEN, 0);
    analogWrite(LED_RED, 0);
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
                        turnOnGreenLED();
                    } else {
                        turnOffGreenLED();
                    }
                } else {
                    if (blinkState) {
                        turnOnRedLED();
                    } else {
                        turnOffRedLED();
                    }
                }
            } else {
                if (activeAlerts[i].isLong) {
                    if (blinkState) {
                        turnOnGreenLED();
                    } else {
                        turnOffGreenLED();
                    }
                } else {
                    if (blinkState) {
                        turnOnRedLED();
                    } else {
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
            turnOnGreenLED();
        } else {
            turnOffGreenLED();
            profitLEDActive = false;
            profitAlertSymbol = "";
            profitAlertPnlPercent = 0;
        }
        
        if (foundLossAlert) {
            turnOnRedLED();
        } else {
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
            turnOnGreenLED();
        } else {
            turnOffGreenLED();
            longAlertLEDActive = false;
            longAlertSymbol = "";
            longAlertPnlPercent = 0;
        }
        
        if (foundShortAlert) {
            turnOnRedLED();
        } else {
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
        turnOnGreenLED();
    } else {
        turnOffGreenLED();
    }
    
    if (shouldRedBeOn) {
        turnOnRedLED();
    } else {
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

// ==================== OLED FUNCTIONS ====================
void setupOLED() {
    Serial.println("Initializing OLED display...");
    
    pinMode(OLED_RESET, OUTPUT);
    digitalWrite(OLED_RESET, LOW);
    delay(100);
    digitalWrite(OLED_RESET, HIGH);
    delay(100);
    
    SPI.begin(OLED_CLK, -1, OLED_MOSI, OLED_CS);
    delay(100);
    
    if(!display.begin(SSD1306_SWITCHCAPVCC)) {
        Serial.println("SSD1306 allocation failed!");
        oledConnected = false;
        return;
    }
    
    oledConnected = true;
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0,0);
    display.println("Portfolio Monitor");
    display.println("Separate Portfolios");
    display.println("v3.8.3 Enhanced");
    display.display();
    delay(2000);
    
    Serial.println("OLED display initialized successfully");
}

void updateOLEDDisplay() {
    if (!oledConnected) return;
    
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0,0);
    
    if (currentDateTime.length() > 10) {
        display.println(currentDateTime);
    } else if (isConnectedToWiFi) {
        display.println("No time sync");
    } else {
        display.println("No WiFi");
    }
    
    if (showingAlert) {
        display.println(alertTitle);
        display.println("------------");
        display.println(alertMessage1);
        if (alertMessage2 != "") display.println(alertMessage2);
        if (alertMessage3 != "") display.println(alertMessage3);
        display.print("@");
        display.println(formatPrice(alertPrice));
    } else if (!isConnectedToWiFi || apModeActive) {
        if (currentDateTime.length() <= 10) {
            display.println("AP Mode Active");
            display.println("SSID: ESP32-Pfolio");
            display.println("IP: 192.168.4.1");
            display.println("Connect & Setup");
        }
    } else {
        display.println(portfolioHeader);
        display.println("-----------");
        
        display.print("I:");
        display.print(totalInvestmentStr);
        display.print(" V:");
        display.println(totalValueStr);
        
        display.print("P/L:");
        display.println(totalPnlStr);
        display.println("-----------");
        
        if (cryptoCount > 0) {
            display.println(currentCryptoDisplay);
            
            int displayCount = min(DISPLAY_CRYPTO_COUNT, cryptoCount);
            if (currentDisplayIndex >= 0 && currentDisplayIndex < displayCount) {
                CryptoPosition crypto = sortedCryptoData[currentDisplayIndex];
                if (crypto.alerted) {
                    if (crypto.isLong) {
                        display.println("LONG ALERT!");
                    } else {
                        display.println("SHORT ALERT!");
                    }
                } else if (crypto.changePercent <= settings.alertThreshold) {
                    display.print("ALRT ");
                    display.println(crypto.isLong ? "LONG Below" : "SHORT Above");
                }
            }
            
            if (newPortfolioDetected) {
                display.println("New Portfolio!");
            }
        } else {
            display.println("Loading data...");
        }
    }
    
    display.display();
}

void showOLEDMessage(String line1, String line2, String line3, String line4) {
    if (!oledConnected) return;
    
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0,0);
    display.println(line1);
    if (line2 != "") display.println(line2);
    if (line3 != "") display.println(line3);
    if (line4 != "") display.println(line4);
    display.display();
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
    
    if (settings.buzzerVolume <= 2) {
        digitalWrite(BUZZER_PIN, HIGH);
        delay(5);
        digitalWrite(BUZZER_PIN, LOW);
        delay(duration - 5);
        return;
    }
    
    int volume = settings.buzzerVolume;
    int dutyPercent = map(volume, VOLUME_MIN, VOLUME_MAX, 10, 90) * volumePercent / 100;
    
    int periodMicros = 1000000 / frequency;
    int onTime = periodMicros * dutyPercent / 100;
    int offTime = periodMicros - onTime;
    
    unsigned long startTime = micros();
    while (micros() - startTime < duration * 1000) {
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
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("📜 Recent Alert History");

    if (activeAlertCount == 0) {
        display.println("No alerts stored");
        display.display();
        return;
    }

    for (int i = activeAlertCount - 1; i >= 0; i--) {

        // اگر timeString خالی است، چیزی چاپ نکن
        if (strlen(alertHistory[i].timeString) == 0) {
            display.println("--/-- --:--");
        } else {
            display.println(alertHistory[i].timeString);
        }

        display.print(alertHistory[i].isLong ? "📈 " : "📉 ");
        display.print(alertHistory[i].symbol);
        display.println(alertHistory[i].isLong ? " LONG" : " SHORT");

        display.print("P/L: ");
        display.print(alertHistory[i].pnlPercent, 1);
        display.print("% $");
        display.print(alertHistory[i].alertPrice, 6);
        display.println(alertHistory[i].isLong ? " LONG" : " SHORT");

        display.println("----------------");
    }

    display.display();
}


void ensureAlertTimeString(AlertHistory &alert) {
    // اگر قبلاً پر شده، کاری نکن
    if (strlen(alert.timeString) > 5) return;

    // اگر timestamp معتبر داریم، از آن بساز
    if (alert.alertTime >= 1700000000UL) {
        time_t t = (time_t)(alert.alertTime / 1000); // تبدیل به ثانیه
        struct tm timeinfo;
        if (localtime_r(&t, &timeinfo)) {
            strftime(alert.timeString, sizeof(alert.timeString),
                     "%m/%d %H:%M", &timeinfo);
            return;
        }
    }

    // آخرین fallback
    strcpy(alert.timeString, "--/-- --:--");
}

void clearAlertHistory() {
    activeAlertCount = 0;
    Serial.println("🧹 Alert history cleared");
}



String getAlertTimeForDisplay(unsigned long timestampSec) {
    // ⛔ جلوگیری از 01/01 03:30
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
        // "2026/01/04 17:40:00" -> "01/04 17:40:00"
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
            alert->alertTime = (unsigned long)time_seconds * 1000; // به میلی‌ثانیه
        } else {
            alert->alertTime = 0; // اگر زمان سیستم در دسترس نیست
        }
        
        // ذخیره بقیه اطلاعات
        alert->pnlPercent = pnlPercent;
        alert->alertPrice = price;
        alert->isLong = isLong;
        alert->isSevere = isSevere;
        alert->isProfit = false;
        alert->alertType = isSevere ? 2 : 1;
        
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
            alert->alertTime = (unsigned long)time_seconds * 1000; // به میلی‌ثانیه
        } else {
            alert->alertTime = 0; // اگر زمان سیستم در دسترس نیست
        }
        
        alert->pnlPercent = pnlPercent;
        alert->alertPrice = price;
        alert->isLong = isLong;
        alert->isSevere = isSevere;
        alert->isProfit = false;
        alert->alertType = isSevere ? 2 : 1;
        
        String fullTitle = title;
        if (fullTitle.length() > 63) {
            fullTitle = fullTitle.substring(0, 60) + "...";
        }
        strncpy(alert->message, fullTitle.c_str(), 63);
        alert->message[63] = '\0';
        
        alert->acknowledged = false;
        
        Serial.println("✅ New alert saved (replaced oldest)");
        Serial.println("  Time: " + alertTimeString);
    }
    
    // نمایش روی OLED
    if (oledConnected) {
        display.clearDisplay();
        display.setTextSize(1);
        display.setCursor(0, 0);
        display.println(title);
        display.println("------------");
        display.println(msg1);
        display.println(msg2);
        if (msg3 != "") display.println(msg3);
        display.print("@");
        display.println(formatPrice(price));
        display.display();
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
    Serial.println("  Change%: " + String(changePercent, 1) + "%");
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
    
    // گرفتن زمان فعلی برای الرت (مشابه showAlert)
    String alertTimeString = "";
    
    // روش 1: از currentDateTime مستقیماً استفاده کن (مشابه showAlert)
    if (currentDateTime.length() >= 19) {
        // "2026/01/04 17:40:00" -> "01/04 17:40:00"
        String monthDay = currentDateTime.substring(5, 10);  // "01/04"
        String timePart = currentDateTime.substring(11, 19); // "17:40:00"
        alertTimeString = monthDay + " " + timePart;
    } else {
        // روش 2: از سیستم زمان بگیر (مشابه showAlert)
        struct tm timeinfo;
        if (getLocalTime(&timeinfo, 100)) {
            char timeString[20];
            strftime(timeString, sizeof(timeString), "%m/%d %H:%M:%S", &timeinfo);
            alertTimeString = String(timeString);
        } else {
            alertTimeString = "--/-- --:--";
        }
    }
    
    // استخراج symbol از msg1 (مشابه showAlert)
    String fullMsg = msg1;
    int spacePos = fullMsg.indexOf(' ');
    String symbol = "";
    if (spacePos > 0) {
        symbol = fullMsg.substring(0, spacePos);
    } else {
        symbol = fullMsg;
    }
    
    if (alertHistoryCount < MAX_ALERT_HISTORY) {
        AlertHistory* alert = &alertHistory[alertHistoryCount];
        
        // ذخیره symbol (مشابه showAlert)
        if (symbol.length() > 15) {
            symbol = symbol.substring(0, 15);
        }
        strncpy(alert->symbol, symbol.c_str(), 15);
        alert->symbol[15] = '\0';
        
        // ذخیره زمان الرت (مشابه showAlert)
        strncpy(alert->timeString, alertTimeString.c_str(), 19);
        alert->timeString[19] = '\0';
        
        // ذخیره timestamp از زمان سیستم
        struct tm timeinfo;
        if (getLocalTime(&timeinfo, 100)) {
            time_t time_seconds = mktime(&timeinfo);
            alert->alertTime = (unsigned long)time_seconds * 1000; // به میلی‌ثانیه
            Serial.println("System timestamp stored: " + String(alert->alertTime));
        } else {
            alert->alertTime = 0; // اگر زمان سیستم در دسترس نیست
            Serial.println("WARNING: Could not get system time for timestamp");
        }
        
        // ذخیره بقیه اطلاعات
        alert->pnlPercent = changePercent;
        alert->alertPrice = price;
        alert->isLong = (msg1.indexOf("LONG") != -1);
        alert->isSevere = (fabs(changePercent) >= 10.0);
        alert->isProfit = isProfit;
        alert->alertType = isProfit ? 3 : 4; // 3 = PROFIT, 4 = LOSS
        
        // ذخیره پیام (مشابه showAlert)
        String fullTitle = title;
        if (fullTitle.length() > 63) {
            fullTitle = fullTitle.substring(0, 60) + "...";
        }
        strncpy(alert->message, fullTitle.c_str(), 63);
        alert->message[63] = '\0';
        
        alert->acknowledged = false;
        
        alertHistoryCount++;
        
        Serial.println("✅ Exit Alert saved to history");
        Serial.println("  Time: " + alertTimeString);
        Serial.println("  Symbol: " + symbol);
        Serial.println("  P/L%: " + String(changePercent, 1) + "%");
        Serial.println("  Total alerts: " + String(alertHistoryCount));
        
    } else {
        // تاریخچه پر است - جایگزینی قدیمی‌ترین الرت (مشابه showAlert)
        Serial.println("⚠️ Alert history full, replacing oldest");
        
        // انتقال الرت‌ها (مشابه showAlert)
        for (int i = 0; i < MAX_ALERT_HISTORY - 1; i++) {
            alertHistory[i] = alertHistory[i + 1];
        }
        
        AlertHistory* alert = &alertHistory[MAX_ALERT_HISTORY - 1];
        
        // ذخیره symbol (مشابه showAlert)
        if (symbol.length() > 15) {
            symbol = symbol.substring(0, 15);
        }
        strncpy(alert->symbol, symbol.c_str(), 15);
        alert->symbol[15] = '\0';
        
        // ذخیره زمان (مشابه showAlert)
        strncpy(alert->timeString, alertTimeString.c_str(), 19);
        alert->timeString[19] = '\0';
        
        // ذخیره timestamp از زمان سیستم
        struct tm timeinfo;
        if (getLocalTime(&timeinfo, 100)) {
            time_t time_seconds = mktime(&timeinfo);
            alert->alertTime = (unsigned long)time_seconds * 1000;
            Serial.println("System timestamp stored: " + String(alert->alertTime));
        } else {
            alert->alertTime = 0;
            Serial.println("WARNING: Could not get system time for timestamp");
        }
        
        alert->pnlPercent = changePercent;
        alert->alertPrice = price;
        alert->isLong = (msg1.indexOf("LONG") != -1);
        alert->isSevere = (fabs(changePercent) >= 10.0);
        alert->isProfit = isProfit;
        alert->alertType = isProfit ? 3 : 4;
        
        String fullTitle = title;
        if (fullTitle.length() > 63) {
            fullTitle = fullTitle.substring(0, 60) + "...";
        }
        strncpy(alert->message, fullTitle.c_str(), 63);
        alert->message[63] = '\0';
        
        alert->acknowledged = false;
        
        Serial.println("✅ New exit alert saved (replaced oldest)");
        Serial.println("  Time: " + alertTimeString);
    }
    
    // نمایش روی OLED
    if (oledConnected) {
        display.clearDisplay();
        display.setTextSize(1);
        display.setCursor(0, 0);
        display.println(title);
        display.println("------------");
        display.println(msg1);
        display.println(msg2);
        if (msg3 != "") display.println(msg3);
        display.print("@");
        display.println(formatPrice(price));
        display.display();
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
    
    settings.oledBrightness = 255;
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
    
    return true;
}

bool saveSettings() {
    Serial.println("\n=== Saving Settings ===");
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
    
    settings.magicNumber = 123;
    EEPROM.begin(EEPROM_SIZE);
    EEPROM.put(0, settings);
    bool success = EEPROM.commit();
    EEPROM.end();
    
    if (success) {
        Serial.println("✅ Settings saved successfully to EEPROM");
        Serial.println("EEPROM commit successful");
        return true;
    } else {
        Serial.println("❌ Failed to save settings to EEPROM!");
        Serial.println("EEPROM commit failed");
        return false;
    }
}

bool addWiFiNetwork(const char* ssid, const char* password) {
    Serial.println("Adding WiFi network: " + String(ssid));
    
    String ssidStr = String(ssid);
    String passStr = String(password);
    ssidStr.trim();
    passStr.trim();
    
    for (int i = 0; i < settings.networkCount; i++) {
        if (strcmp(settings.networks[i].ssid, ssidStr.c_str()) == 0) {
            Serial.println("Updating existing network: " + String(ssid));
            strncpy(settings.networks[i].password, passStr.c_str(), 63);
            settings.networks[i].password[63] = '\0';
            settings.networks[i].configured = true;
            settings.networks[i].priority = 1;
            return saveSettings();
        }
    }
    
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
            
            Serial.println("All saved networks:");
            for (int i = 0; i < settings.networkCount; i++) {
                Serial.println("  " + String(i+1) + ". " + String(settings.networks[i].ssid));
            }
            
            return true;
        } else {
            Serial.println("Failed to save network!");
            settings.networkCount--;
            return false;
        }
    }
    
    Serial.println("Cannot add network - maximum limit reached");
    return false;
}

bool removeWiFiNetwork(const char* ssid) {
    Serial.println("Removing network: " + String(ssid));
    
    for (int i = 0; i < settings.networkCount; i++) {
        if (strcmp(settings.networks[i].ssid, ssid) == 0) {
            Serial.println("Found network at index: " + String(i));
            
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
    
    int startIndex = (settings.lastConnectedIndex != -1) ? settings.lastConnectedIndex : 0;
    
    for (int attempt = 0; attempt < settings.networkCount; attempt++) {
        int currentIndex = (startIndex + attempt) % settings.networkCount;
        
        if (!settings.networks[currentIndex].configured) {
            continue;
        }
        
        const char* ssid = settings.networks[currentIndex].ssid;
        const char* password = settings.networks[currentIndex].password;
        
        if (strlen(ssid) == 0) {
            Serial.println("Skipping empty SSID");
            continue;
        }
        
        Serial.println("Attempting to connect to: " + String(ssid));
        showOLEDMessage("Connecting to", ssid, "Attempt " + String(attempt + 1), "...");
        
        WiFi.disconnect(true);
        delay(1000);
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid, password);
        
        int wifiAttempts = 0;
        while (WiFi.status() != WL_CONNECTED && wifiAttempts < 30) {
            delay(500);
            wifiAttempts++;
            Serial.print(".");
            
            if (wifiAttempts % 10 == 0) {
                showOLEDMessage("Connecting", "Attempt " + String(wifiAttempts / 2), "seconds", "");
            }
        }
        
        if (WiFi.status() == WL_CONNECTED) {
            settings.lastConnectedIndex = currentIndex;
            settings.networks[currentIndex].lastConnected = millis();
            settings.networks[currentIndex].connectionAttempts++;
            saveSettings();
            
            isConnectedToWiFi = true;
            apModeActive = false;
            
            Serial.println("\n✅ WiFi Connected!");
            Serial.println("SSID: " + WiFi.SSID());
            Serial.println("IP: " + WiFi.localIP().toString());
            Serial.println("RSSI: " + String(WiFi.RSSI()) + " dBm");
            
            showOLEDMessage("✅ Connected", WiFi.SSID(), WiFi.localIP().toString(), "RSSI: " + String(WiFi.RSSI()) + "dBm");
            return true;
        }
        
        Serial.println("\n❌ Failed: " + String(ssid));
        settings.networks[currentIndex].connectionAttempts++;
    }
    
    Serial.println("Failed to connect to any saved network");
    showOLEDMessage("All networks", "failed!", "Starting AP...", "");
    return false;
}

bool startAPMode() {
    Serial.println("\n=== Starting AP Mode ===");
    
    WiFi.disconnect(true);
    delay(2000);
    WiFi.mode(WIFI_AP);
    delay(100);
    
    bool apStarted = false;
    int apAttempts = 0;
    
    while (!apStarted && apAttempts < 3) {
        Serial.println("AP Attempt " + String(apAttempts + 1));
        
        if (WiFi.softAP("ESP32-Portfolio", "12345678")) {
            apStarted = true;
            Serial.println("✅ AP Started!");
        } else {
            apAttempts++;
            Serial.println("AP failed, retrying...");
            delay(1000);
        }
    }
    
    if (!apStarted) {
        Serial.println("❌ Failed to start AP after 3 attempts!");
        showOLEDMessage("AP Failed!", "Restarting...", "", "");
        delay(3000);
        ESP.restart();
        return false;
    }
    
    apModeActive = true;
    isConnectedToWiFi = false;
    
    Serial.println("AP SSID: ESP32-Portfolio");
    Serial.println("AP Password: 12345678");
    Serial.println("AP IP: " + WiFi.softAPIP().toString());
    
    showOLEDMessage("📶 AP Mode", "SSID: ESP32-P", "Pass: 12345678", WiFi.softAPIP().toString());
    return true;
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
        // بررسی اعتبار تاریخ
        int year = timeinfo.tm_year + 1900;
        if (year < 2023 || year > 2030) {
            return "01/01 00:00:00";
        }
        
        char dateTimeStr[30];
        snprintf(dateTimeStr, sizeof(dateTimeStr), "%02d/%02d %02d:%02d:%02d", 
                 timeinfo.tm_mon + 1,  // ماه
                 timeinfo.tm_mday,     // روز
                 timeinfo.tm_hour,     // ساعت
                 timeinfo.tm_min,      // دقیقه
                 timeinfo.tm_sec);     // ثانیه
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
    static unsigned long lastSyncAttempt = 0;
    
    if (!isConnectedToWiFi) {
        currentDateTime = "No Time Sync";
        return;
    }
    
    if (!timeSynced || (millis() - lastSyncAttempt > 600000)) {
 
        timeSynced = true;
        lastSyncAttempt = millis();
    } else {
        struct tm timeinfo;
        if (getLocalTime(&timeinfo, 100)) {
            char timeString[25];
            strftime(timeString, sizeof(timeString), "%Y/%m/%d %H:%M:%S", &timeinfo);
            currentDateTime = String(timeString);
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
            
            // اصلاح: استفاده از متن کامل
            String positionType = crypto->isLong ? "Long position" : "Short position";
            
            // ترکیب symbol و positionType
            String alertMsg1 = symbol + " " + positionType;
            
            // بررسی طول متن
            if (alertMsg1.length() > 30) {
                alertMsg1 = symbol + " " + (crypto->isLong ? "LONG" : "SHORT");
            }
            
            showAlert(alertType,
                      alertMsg1,  // استفاده از متن اصلاح‌شده
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

// تابع دیباگ تاریخچه الرت‌ها
void debugAlertHistory() {
    Serial.println("\n=== DEBUG: Alert History ===");
    Serial.println("Total alerts: " + String(alertHistoryCount));
    
    if (alertHistoryCount == 0) {
        Serial.println("No alerts in history");
        Serial.println("=========================\n");
        return;
    }
    
    Serial.println("Index | Symbol | Timestamp | Time String | P/L% | Type");
    Serial.println("-------------------------------------------------------");
    
    for (int i = 0; i < alertHistoryCount; i++) {
        AlertHistory* alert = &alertHistory[i];
        
        Serial.print(i);
        Serial.print(" | ");
        Serial.print(alert->symbol);
        Serial.print(" | ");
        Serial.print(alert->alertTime);
        Serial.print(" | ");
        Serial.print(alert->timeString);
        Serial.print(" | ");
        Serial.print(alert->pnlPercent, 1);
        Serial.print("% | ");
        Serial.println(alert->isLong ? "LONG" : "SHORT");
        
    
    }
    
    Serial.println("=========================\n");
}

// تابع اصلاح timestamp‌های قدیمی
void fixOldTimestamps() {
    Serial.println("\n=== Fixing Old Alert Timestamps ===");
    
    int fixedCount = 0;
    unsigned long baseTimestamp = getCorrectTimestamp();
    
    for (int i = 0; i < alertHistoryCount; i++) {
        AlertHistory* alert = &alertHistory[i];
        
        // بررسی timestamp
        if (alert->alertTime < 1000000000000UL) {
            Serial.println("Fixing alert " + String(i) + ": " + String(alert->symbol));
            Serial.println("  Old timestamp: " + String(alert->alertTime));
            
            // تشخیص نوع مشکل
            if (alert->alertTime < 1000000000UL) {
                // احتمالاً timestamp در ثانیه است
                alert->alertTime = alert->alertTime * 1000;
                Serial.println("  Fixed (multiplied by 1000): " + String(alert->alertTime));
            } else {
                // ایجاد timestamp جدید بر اساس موقعیت
                unsigned long timeOffset = (alertHistoryCount - i - 1) * 300000; // 5 دقیقه فاصله
                alert->alertTime = baseTimestamp - timeOffset;
                Serial.println("  New timestamp: " + String(alert->alertTime));
            }
            
            // بروزرسانی timeString

            
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
                          ": " + formatPrice(crypto->currentPrice));
            continue;
        }
        
        float priceChangePercent = fabs((crypto->currentPrice - crypto->exitAlertLastPrice) / crypto->exitAlertLastPrice * 100);
        
        Serial.println("  Checking " + String(crypto->symbol) + 
                      ": Current=" + formatPrice(crypto->currentPrice) + 
                      ", Last Alert=" + formatPrice(crypto->exitAlertLastPrice) + 
                      ", Change=" + String(priceChangePercent, 2) + "%" +
                      ", Threshold=" + String(settings.exitAlertPercent) + "%");
        
        if (priceChangePercent >= settings.exitAlertPercent) {
            
            Serial.println("💰 Triggering EXIT alert for: " + String(crypto->symbol));
            Serial.println("    Change%: " + String(crypto->changePercent, 1) + "%");
            Serial.println("    Price: " + formatPrice(crypto->currentPrice));
            Serial.println("    Last Exit Alert Price: " + formatPrice(crypto->exitAlertLastPrice));
            Serial.println("    Price Change: " + String(priceChangePercent, 1) + "%");
            Serial.println("    Position Type: " + String(crypto->isLong ? "LONG" : "SHORT"));
            
            bool isProfit;
            
            if (crypto->isLong) {
                isProfit = (crypto->currentPrice > crypto->exitAlertLastPrice);
                Serial.println("    LONG position logic: Price " + 
                              String(crypto->currentPrice > crypto->exitAlertLastPrice ? "increased" : "decreased") + 
                              " = " + String(isProfit ? "PROFIT" : "LOSS"));
            } else {
                isProfit = (crypto->currentPrice < crypto->exitAlertLastPrice);
                Serial.println("    SHORT position logic: Price " + 
                              String(crypto->currentPrice < crypto->exitAlertLastPrice ? "decreased" : "increased") + 
                              " = " + String(isProfit ? "PROFIT" : "LOSS"));
            }
            
            float actualChangePercent = ((crypto->currentPrice - crypto->exitAlertLastPrice) / crypto->exitAlertLastPrice) * 100;
            
            String alertType = isProfit ? "PROFIT ALERT" : "LOSS ALERT";
            
            String symbol = getShortSymbol(crypto->symbol);
            String changeStr = formatPercent(actualChangePercent);
            
            showExitAlert(alertType,
                         symbol + " " + (crypto->isLong ? "LONG" : "SHORT") + " " + (isProfit ? "PROFIT" : "LOSS"),
                         changeStr + " Change",
                         "Price Change: " + String(priceChangePercent, 1) + "%",
                         isProfit,
                         actualChangePercent,
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
    
    showOLEDMessage("ALERTS RESET", "All position alerts", "have been cleared", "");
    
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
    bool isNewPortfolio = newPercentage >= 90.0;
    
    if (isNewPortfolio) {
        Serial.println("New portfolio detected: " + String(newPercentage, 1) + "% new positions");
    }
    
    return isNewPortfolio;
}

// ==================== NEW API FUNCTIONS ====================
String getEntryModeData() {
    if (!isConnectedToWiFi) {
        Serial.println("Cannot get data - WiFi not connected");
        return "{}";
    }
    
    if (strlen(settings.server) == 0 || strlen(settings.username) == 0) {
        Serial.println("API not configured");
        return "{}";
    }
    
    String url = String(settings.server) + "/api/device/portfolio/" + 
                String(settings.username) + "?portfolio_name=" + 
                String(settings.entryPortfolio);
    
    http.begin(url);
    
    String authString = String(settings.username) + ":" + String(settings.userpass);
    String encodedAuth = base64Encode(authString);
    http.addHeader("Authorization", "Basic " + encodedAuth);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(8000);
    http.setReuse(false);
    
    Serial.println("\n🔄 Fetching ENTRY MODE data...");
    Serial.println("URL: " + url);
    Serial.println("Portfolio: " + String(settings.entryPortfolio));
    Serial.println("Buffer size: " + String(JSON_BUFFER_SIZE) + " bytes");
    Serial.println("Timeout: 8000ms");
    
    int httpCode = http.GET();
    String result = "{}";
    
    if (httpCode == 200) {
        result = http.getString();
        Serial.println("✅ Entry Mode Data received successfully");
        Serial.println("Data length: " + String(result.length()) + " bytes");
        
        if (result.length() > JSON_BUFFER_SIZE - 500) {
            Serial.println("⚠️ WARNING: Data size接近 buffer limit!");
        }
    } else if (httpCode > 0) {
        Serial.println("❌ HTTP Error: " + String(httpCode));
        result = "{\"error\": \"HTTP " + String(httpCode) + "\"}";
    } else {
        Serial.println("❌ Connection failed or timeout");
        result = "{\"error\": \"Connection failed or timeout\"}";
    }
    
    http.end();
    return result;
}

String getExitModeData() {
    if (!isConnectedToWiFi) {
        Serial.println("Cannot get data - WiFi not connected");
        return "{}";
    }
    
    if (strlen(settings.server) == 0 || strlen(settings.username) == 0) {
        Serial.println("API not configured");
        return "{}";
    }
    
    String url = String(settings.server) + "/api/device/portfolio/" + 
                String(settings.username) + "?portfolio_name=" + 
                String(settings.exitPortfolio);
    
    http.begin(url);
    
    String authString = String(settings.username) + ":" + String(settings.userpass);
    String encodedAuth = base64Encode(authString);
    http.addHeader("Authorization", "Basic " + encodedAuth);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(8000);
    http.setReuse(false);
    
    Serial.println("\n🔄 Fetching EXIT MODE data...");
    Serial.println("URL: " + url);
    Serial.println("Portfolio: " + String(settings.exitPortfolio));
    Serial.println("Buffer size: " + String(JSON_BUFFER_SIZE) + " bytes");
    Serial.println("Timeout: 8000ms");
    
    int httpCode = http.GET();
    String result = "{}";
    
    if (httpCode == 200) {
        result = http.getString();
        Serial.println("✅ Exit Mode Data received successfully");
        Serial.println("Data length: " + String(result.length()) + " bytes");
        
        if (result.length() > JSON_BUFFER_SIZE - 500) {
            Serial.println("⚠️ WARNING: Data size接近 buffer limit!");
        }
    } else if (httpCode > 0) {
        Serial.println("❌ HTTP Error: " + String(httpCode));
        result = "{\"error\": \"HTTP " + String(httpCode) + "\"}";
    } else {
        Serial.println("❌ Connection failed or timeout");
        result = "{\"error\": \"Connection failed or timeout\"}";
    }
    
    http.end();
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
        else if (position.containsKey("side")) {
            const char* side = position["side"];
            isLongFromJSON = (strcasecmp(side, "LONG") == 0 || strcasecmp(side, "BUY") == 0);
        }
        else if (position.containsKey("quantity")) {
            float quantity = position["quantity"] | 0.0;
            isLongFromJSON = (quantity > 0);
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
        
        const char* positionSide = position["position_side"] | "LONG";
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
        
        Serial.print("  Position ");
        Serial.print(i + 1);
        Serial.print(": ");
        Serial.print(cryptoData[i].symbol);
        Serial.print(" - ");
        Serial.print(cryptoData[i].isLong ? "LONG" : "SHORT");
        Serial.print(" | P/L%: ");
        Serial.print(cryptoData[i].changePercent, 1);
        Serial.print("% | Price: ");
        Serial.print(formatPrice(cryptoData[i].currentPrice));
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
    if (settings.isExitMode) {
        return getExitModeData();
    } else {
        return getEntryModeData();
    }
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
.alert-history {margin:30px 0;padding:20px;background:#f8f9fa;border-radius:8px;}
.alert-history-item {padding:10px;border-bottom:1px solid #ddd;margin-bottom:10px;}
.alert-history-time {font-size:12px;color:#666;float:right;}
.alert-history-symbol {font-weight:bold;}
.alert-history-type {display:inline-block;padding:2px 8px;border-radius:4px;margin-left:10px;font-size:12px;}
.alert-long {background:#d4edda;color:#155724;}
.alert-short {background:#f8d7da;color:#721c24;}
.alert-severe {background:#f8d7da;color:#721c24;border:1px solid #f5c6cb;}
.alert-profit {background:#d4edda;color:#155724;}
.alert-loss {background:#f8d7da;color:#721c24;}
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
    
    String alertHistoryHTML = "";
        
    if (alertHistoryCount > 0) {
        alertHistoryHTML += "<div class='alert-history'>";
        alertHistoryHTML += "<h4>📜 Recent Alert History (Last " + String(min(alertHistoryCount, 15)) + " of " + String(alertHistoryCount) + ")</h4>";
        
        int displayCount = min(alertHistoryCount, 15);
        
        for (int i = alertHistoryCount - 1; i >= max(0, alertHistoryCount - 15); i--) {
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
            
            if (alert->alertType == 3 || alert->alertType == 4) {
                alertType = alert->isProfit ? "PROFIT" : "LOSS";
                alertColorClass = alert->isProfit ? "alert-profit" : "alert-loss";
                alertIcon = alert->isProfit ? "🟢" : "🔴";
            } else {
                alertType = alert->isLong ? "LONG" : "SHORT";
                if (alert->isSevere) {
                    alertType = "SEVERE " + alertType;
                    alertIcon = "⚠️";
                } else {
                    alertIcon = alert->isLong ? "📈" : "📉";
                }
                alertColorClass = alert->isSevere ? "alert-severe" : (alert->isLong ? "alert-long" : "alert-short");
            }
            
            alertHistoryHTML += "<div class='alert-history-item'>";
            alertHistoryHTML += "<div style='display:flex;justify-content:space-between;align-items:center;margin-bottom:3px;'>";
            alertHistoryHTML += "<div>";
            alertHistoryHTML += alertIcon + " <span class='alert-history-symbol' style='font-weight:bold;'>" + String(alert->symbol) + "</span>";
            alertHistoryHTML += " <span class='alert-history-type " + alertColorClass + "'>" + alertType + "</span>";
            alertHistoryHTML += "</div>";
            
            // نمایش زمان به صورت آبی + زمان سپری شده در پرانتز
            alertHistoryHTML += "<span class='alert-history-time' style='font-size:11px;font-family:monospace;color:#2196F3;font-weight:bold;'>";
            alertHistoryHTML += timeStr;
            if (elapsedTime != "") {
                alertHistoryHTML += " <span style='color:#666;font-weight:normal;'>" + elapsedTime + "</span>";
            }
            alertHistoryHTML += "</span>";
            
            alertHistoryHTML += "</div>";
            
            alertHistoryHTML += "<div style='font-size:11px;color:#555;'>";
            alertHistoryHTML += "P/L: <span style='color:" + String(alert->pnlPercent >= 0 ? "#28a745" : "#dc3545") + ";font-weight:bold;'>";
            alertHistoryHTML += String(alert->pnlPercent, 1) + "%</span>";
            alertHistoryHTML += " | Price: <span style='font-family:monospace;'>$" + formatPrice(alert->alertPrice) + "</span>";
            
            if (strlen(alert->message) > 0 && String(alert->message) != "LOSS ALERT" && String(alert->message) != "PROFIT ALERT") {
                alertHistoryHTML += " | " + String(alert->message);
            }
            
            alertHistoryHTML += "</div>";
            alertHistoryHTML += "</div>";
        }
        alertHistoryHTML += "</div>";
    } else {
        alertHistoryHTML = "<p style='text-align:center;color:#666;padding:20px;background:#f8f9fa;border-radius:8px;'>No alert history available</p>";
    }
    page.replace("%ALERT_HISTORY%", alertHistoryHTML);

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

void setupWebServer() {
    Serial.println("Setting up web server...");
    
    server.on("/", handleRoot);
    server.on("/config", handleConfig);
    server.on("/setup", handleSetup);
    server.on("/savewifi", HTTP_POST, handleSaveWiFi);
    server.on("/removewifi", handleRemoveWiFi);
    server.on("/saveapi", HTTP_POST, handleSaveAPI);
    server.on("/savealert", HTTP_POST, handleSaveAlert);
    server.on("/savemode", HTTP_POST, handleSaveMode);
    server.on("/refresh", handleRefresh);
    server.on("/testalert", handleTestAlert);
    server.on("/resetalerts", handleResetAlerts);
    server.on("/wifimanager", handleWiFiManager);
    
    server.onNotFound([]() {
        server.send(404, "text/html", 
            "<html><body style='text-align:center;padding:50px;'>"
            "<h1>404 - Page Not Found</h1>"
            "<p><a href='/'>Home</a></p>"
            "</body></html>");
    });
    
    server.begin();
    Serial.println("Web server started on port 80");
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
    showOLEDMessage("Factory Reset", "Erasing all", "settings...", "");
    
    initializeSettings();
    
    EEPROM.begin(EEPROM_SIZE);
    for (int i = 0; i < EEPROM_SIZE; i++) {
        EEPROM.write(i, 0);
    }
    EEPROM.commit();
    EEPROM.end();
    
    showOLEDMessage("Reset Complete", "Restarting", "device...", "");
    delay(2000);
    
    Serial.println("Factory reset complete, restarting...");
    ESP.restart();
}
// ==================== NEW TIME FUNCTIONS ====================
void setManualDateTime(int year, int month, int day, int hour, int minute, int second) {
    Serial.println("\n=== Setting Manual Date/Time ===");
    Serial.println("Year: " + String(year));
    Serial.println("Month: " + String(month));
    Serial.println("Day: " + String(day));
    Serial.println("Hour: " + String(hour));
    Serial.println("Minute: " + String(minute));
    Serial.println("Second: " + String(second));
    
    struct tm timeinfo;
    
    timeinfo.tm_year = year - 1900; // سال از 1900 شروع می‌شود
    timeinfo.tm_mon = month - 1; // ماه از 0 شروع می‌شود
    timeinfo.tm_mday = day;
    timeinfo.tm_hour = hour;
    timeinfo.tm_min = minute;
    timeinfo.tm_sec = second;
    timeinfo.tm_isdst = 0; // DST not in effect
    
    time_t new_time = mktime(&timeinfo);
    struct timeval tv = { .tv_sec = new_time };
    
    if (settimeofday(&tv, NULL) == 0) {
        Serial.println("✅ Manual date/time set successfully");
        
        // نمایش تاریخ تنظیم شده
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
    // ===== 1. دریافت زمان سیستم =====
    time_t now;
    time(&now);

    // اگر زمان هنوز با NTP سینک نشده، Alert نساز
    if (now < 1700000000UL) {
        Serial.println("❌ storeAlert skipped: time not synced");
        return;
    }

    // ===== 2. بررسی ظرفیت =====
    if (activeAlertCount >= MAX_ALERT_HISTORY) {
        Serial.println("❌ storeAlert skipped: history full");
        return;
    }

    // ===== 3. مرجع Alert =====
    AlertHistory &alert = alertHistory[activeAlertCount];

    // ===== 4. اطلاعات پایه =====
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

    // ===== 5. ذخیره زمان عددی (ثانیه Epoch) =====
    alert.alertTime = (unsigned long)now;

    // ===== 6. ساخت رشته‌ی زمان (تنها منبع مجاز) =====
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    strftime(
        alert.timeString,
        sizeof(alert.timeString),
        "%m/%d %H:%M",
        &timeinfo
    );

    // ===== 7. افزایش شمارنده =====
    activeAlertCount++;

    // ===== 8. لاگ برای دیباگ =====
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
        Serial.println("Month: " + String(timeinfo.tm_mon + 1) + " (0-based: " + String(timeinfo.tm_mon) + ")");
        Serial.println("Day: " + String(timeinfo.tm_mday));
        Serial.println("Hour: " + String(timeinfo.tm_hour) + " (24-hour format)");
        Serial.println("Minute: " + String(timeinfo.tm_min));
        Serial.println("Second: " + String(timeinfo.tm_sec));
        Serial.println("Day of week: " + String(timeinfo.tm_wday) + " (0=Sunday)");
        Serial.println("Is DST: " + String(timeinfo.tm_isdst));
        
        // نمایش timestamp فعلی
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

// ==================== NEW TIMESTAMP FUNCTIONS ====================

unsigned long getCorrectTimestamp() {
    struct tm timeinfo;
    
    // اول سعی کن از زمان سیستم بگیر
    if (getLocalTime(&timeinfo, 100)) {
        time_t time_seconds = mktime(&timeinfo);
        unsigned long timestamp = (unsigned long)time_seconds * 1000;
        return timestamp;
    }
    
    // Fallback: اگر زمان سیستم در دسترس نیست
    Serial.println("WARNING: Cannot get system time, using fallback timestamp");
    
    // استفاده از زمان ثابت 4 ژانویه 2026 + millis()
    static unsigned long baseTimestamp = 0;
    static bool initialized = false;
    
    if (!initialized) {
        // 4 ژانویه 2026 00:00:00
        struct tm jan4_2026 = {0};
        jan4_2026.tm_year = 126;  // 2026 - 1900
        jan4_2026.tm_mon = 0;     // January
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
    // این تابع دیگر استفاده نمی‌شود
    // از currentDateTime مستقیماً استفاده کنید
    
    if (currentDateTime.length() >= 19) {
        String monthDay = currentDateTime.substring(5, 10);  // "01/04"
        String timePart = currentDateTime.substring(11, 19); // "17:40:00"
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
    // از زمان فعلی سیستم استفاده می‌کند
    // فرمت: "01/04 17:40:00"
    
    if (currentDateTime.length() >= 19) {
        // "2026/01/04 17:40:00" -> "01/04 17:40:00"
        String monthDay = currentDateTime.substring(5, 10);  // "01/04"
        String timePart = currentDateTime.substring(11, 19); // "17:40:00"
        return monthDay + " " + timePart;
    }
    
    // Fallback ساده
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
        
        Serial.print("Alert " + String(i) + ": ");
        Serial.print(String(alert->symbol) + " - ");
        Serial.print("Time: " + String(alert->timeString) + " - ");
        Serial.println("Timestamp: " + String(alert->alertTime));
    }
    Serial.println("===========================\n");
}

// ==================== SETUP FUNCTION ====================
void setup() {
    Serial.begin(115200);
    delay(3000);
    
    Serial.println("\n\n==========================================");
    Serial.println("   ESP32 Portfolio Monitor v3.8.4");
    Serial.println("   Long/Short Alert System");
    Serial.println("   With EXIT Mode for Price Change Tracking");
    Serial.println("   Enhanced Time Display in Alert History");
    Serial.println("   Fixed Time Sync Issues");
    Serial.println("   Multi-WiFi Memory");
    Serial.println("   LED Blinking & Alert History");
    Serial.println("   Price & Time Tracking for Alerts");
    Serial.println("   High Precision Price Display (up to 10 decimals)");
    Serial.println("   SEPARATE PORTFOLIOS FOR ENTRY/EXIT MODES");
    Serial.println("==========================================\n");
    
    // راه‌اندازی اولیه OLED
    setupOLED();
    
    // راه‌اندازی بیزر
    setupBuzzer();
    
    // راه‌اندازی LED‌ها
    setupLEDs();
    
    // راه‌اندازی دکمه ریست
    setupResetButton();
    
    // بارگذاری تنظیمات از EEPROM
    if (!loadSettings()) {
        Serial.println("First boot or corrupted settings");
    }
    
    clearAlertHistory();

    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);


    // افزایش شماره بوت و محاسبه آپ‌تایم
    settings.bootCount++;
    settings.totalUptime += millis() - settings.firstBoot;
    saveSettings();
    
    Serial.println("\n=== System Initialization ===");
    Serial.println("Boot count: " + String(settings.bootCount));
    Serial.println("Trading Mode: " + String(settings.isExitMode ? "EXIT (Price Change Tracking)" : "ENTRY (Loss Tracking)"));
    Serial.println("Entry Portfolio: '" + String(settings.entryPortfolio) + "'");
    Serial.println("Exit Portfolio: '" + String(settings.exitPortfolio) + "'");
    Serial.println("Exit Alert Percent: " + String(settings.exitAlertPercent) + "%");
    Serial.println("Alert Threshold: " + String(settings.alertThreshold, 1) + "%");
    Serial.println("Severe Threshold: " + String(settings.severeAlertThreshold, 1) + "%");
    Serial.println("Buzzer Enabled: " + String(settings.buzzerEnabled ? "YES" : "NO"));
    Serial.println("Buzzer Volume: " + String(settings.buzzerVolume) + "/20");
    Serial.println("LED Enabled: " + String(settings.ledEnabled ? "YES" : "NO"));
    Serial.println("LED Brightness: " + String(settings.ledBrightness) + "/255");
    Serial.println("Max Crypto Positions: " + String(MAX_CRYPTO));
    Serial.println("============================\n");
    
    // اتصال به WiFi
    if (settings.networkCount > 0) {
        Serial.println("Found " + String(settings.networkCount) + " saved WiFi networks");
        showOLEDMessage("Found", String(settings.networkCount) + " networks", "Connecting...", "");
        
        if (connectToWiFi()) {
            Serial.println("Successfully connected to WiFi");
        } else {
            Serial.println("WiFi connection failed, starting AP mode");
            showOLEDMessage("WiFi Failed", "Starting AP...", "", "");
            startAPMode();
        }
    } else {
        Serial.println("No saved WiFi networks, starting AP mode");
        showOLEDMessage("No WiFi saved", "Starting AP...", "", "");
        startAPMode();
    }
    
    // راه‌اندازی وب سرور
    setupWebServer();
    
    // ========== راه‌اندازی زمان سیستم ==========
    if (isConnectedToWiFi) {
        Serial.println("\n=== Initial Time Setup ===");
        
   
        
        // بروزرسانی زمان جاری
        updateDateTime();
        
        // نمایش وضعیت زمان
        printTimeStatus();
        


        // بررسی و اصلاح timestamp‌های قدیمی در تاریخچه الرت‌ها
        if (alertHistoryCount > 0) {
            Serial.println("\n=== Checking Alert History Timestamps ===");
            Serial.println("Found " + String(alertHistoryCount) + " alerts in history");
            
            // دیباگ تاریخچه موجود
            debugAlertHistory();
            
            // اصلاح timestamp‌های اشتباه
            fixOldTimestamps();
            
            // دیباگ مجدد بعد از اصلاح
            debugAlertHistory();
        }
        
        Serial.println("=== Time Setup Complete ===\n");
    } else if (apModeActive) {
        Serial.println("AP Mode active - Time sync skipped");
    }
    
    // ========== بارگذاری اولیه داده‌ها ==========
    if (isConnectedToWiFi && strlen(settings.server) > 0) {
        Serial.println("\n=== Fetching Initial Data ===");
        Serial.println("Mode: " + String(settings.isExitMode ? "EXIT" : "ENTRY"));
        Serial.println("Active Portfolio: " + String(settings.isExitMode ? settings.exitPortfolio : settings.entryPortfolio));
        Serial.println("API Type: PORTFOLIO API");
        
        showOLEDMessage("Fetching", "portfolio data", "Please wait...", "");
        
        String data = getPortfolioData();
        parseCryptoData(data);
        resetDisplayToFirstPosition();
        
        Serial.println("Initial data loaded successfully");
        Serial.println("Positions count: " + String(cryptoCount));
        
        // نمایش خلاصه
        if (cryptoCount > 0) {
            Serial.println("Top 5 positions by loss:");
            for (int i = 0; i < min(5, cryptoCount); i++) {
                Serial.print("  ");
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
        Serial.println("===========================\n");
    } else {
        Serial.println("Skipping initial data fetch - No WiFi/AP or API not configured");
    }
    
    // ========== نمایش وضعیت نهایی سیستم ==========
    Serial.println("\n=== System Ready ===");
    
    if (isConnectedToWiFi) {
        Serial.println("Mode: WiFi Connected");
        Serial.println("SSID: " + WiFi.SSID());
        Serial.println("IP: " + WiFi.localIP().toString());
        Serial.println("RSSI: " + String(WiFi.RSSI()) + " dBm");
        Serial.println("Trading Mode: " + String(settings.isExitMode ? "EXIT (Price Change Tracking)" : "ENTRY (Loss Tracking)"));
        Serial.println("Entry Portfolio: '" + String(settings.entryPortfolio) + "'");
        Serial.println("Exit Portfolio: '" + String(settings.exitPortfolio) + "'");
        Serial.println("Active Portfolio: '" + String(settings.isExitMode ? settings.exitPortfolio : settings.entryPortfolio) + "'");
        Serial.println("API Type: PORTFOLIO API (Both modes)");
        
        if (settings.isExitMode) {
            Serial.println("Exit Alert Threshold: " + String(settings.exitAlertPercent) + "% price change");
            Serial.println("Price Display: Adaptive precision (2-10 decimal places)");
            Serial.println("LED Behavior: 🟢 GREEN = PROFIT, 🔴 RED = LOSS");
            Serial.println("Exit Mode Logic: Tracks price changes from last alert price");
        } else {
            Serial.println("Alert Threshold: " + String(settings.alertThreshold, 1) + "%");
            Serial.println("Severe Threshold: " + String(settings.severeAlertThreshold, 1) + "%");
            Serial.println("LED Behavior: 🟢 GREEN = LONG, 🔴 RED = SHORT");
        }
        
        Serial.println("LED Status: GREEN=" + String(settings.isExitMode ? (profitLEDActive ? "ON (PROFIT)" : "OFF") : (longAlertLEDActive ? "ON (LONG)" : "OFF")) + 
                      " RED=" + String(settings.isExitMode ? (lossLEDActive ? "ON (LOSS)" : "OFF") : (shortAlertLEDActive ? "ON (SHORT)" : "OFF")));
        Serial.println("LED Brightness: " + String(settings.ledBrightness) + "/255");
        Serial.println("Alert Checking: ALL " + String(cryptoCount) + " positions");
        Serial.println("Max Crypto Capacity: " + String(MAX_CRYPTO) + " positions");
        Serial.println("Price Format: High precision adaptive display");
        
        showOLEDMessage("✅ System Ready", 
                       "WiFi: " + WiFi.SSID(), 
                       settings.isExitMode ? "EXIT Mode Active" : "ENTRY Mode Active",
                       "Port: " + String(settings.isExitMode ? settings.exitPortfolio : settings.entryPortfolio));
        
    } else if (apModeActive) {
        Serial.println("Mode: AP Mode");
        Serial.println("AP SSID: ESP32-Portfolio");
        Serial.println("AP IP: " + WiFi.softAPIP().toString());
        Serial.println("Trading Mode: " + String(settings.isExitMode ? "EXIT" : "ENTRY"));
        Serial.println("Entry Portfolio: '" + String(settings.entryPortfolio) + "'");
        Serial.println("Exit Portfolio: '" + String(settings.exitPortfolio) + "'");
        Serial.println("API Type: PORTFOLIO API (Both modes)");
        Serial.println("LED Status: GREEN=" + String(settings.isExitMode ? (profitLEDActive ? "ON (PROFIT)" : "OFF") : (longAlertLEDActive ? "ON (LONG)" : "OFF")) + 
                      " RED=" + String(settings.isExitMode ? (lossLEDActive ? "ON (LOSS)" : "OFF") : (shortAlertLEDActive ? "ON (SHORT)" : "OFF")));
        Serial.println("LED Brightness: " + String(settings.ledBrightness) + "/255");
        Serial.println("Alert Checking: ALL positions");
        Serial.println("Max Crypto Capacity: " + String(MAX_CRYPTO) + " positions");
        Serial.println("Price Format: High precision adaptive display");
        
        showOLEDMessage("📶 AP Mode Active", 
                       "Connect to:", 
                       "ESP32-Portfolio", 
                       settings.isExitMode ? "EXIT Mode" : "ENTRY Mode");
    } else {
        Serial.println("Mode: Disconnected");
        showOLEDMessage("❌ Disconnected", "Check WiFi", "or enter AP", "192.168.4.1");
    }
    
    // نمایش اطلاعات تکمیلی
    Serial.println("\n=== Additional System Info ===");
    Serial.println("Buzzer Status: " + String(settings.buzzerEnabled ? "Enabled" : "Disabled"));
    Serial.println("Volume Level: " + String(settings.buzzerVolume) + "/20");
    Serial.println("LED Blinking: ENABLED");
    Serial.println("Alert History: ENABLED (Max " + String(MAX_ALERT_HISTORY) + " alerts)");
    Serial.println("Alert Tones: Long/Profit = 2 beeps, Short/Loss = 1 beep");
    Serial.println("Alert Price Tracking: ENABLED (shows price at alert time)");
    Serial.println("Alert Time Tracking: ENABLED (shows time for each alert)");
    Serial.println("Price Display Precision: Adaptive (2-10 decimal places)");
    Serial.println("Exit Mode Logic: Tracks price changes from last alert price");
    Serial.println("Alert Checking Mode: ALL positions checked");
    Serial.println("System Capacity: " + String(MAX_CRYPTO) + " crypto positions");
    Serial.println("Current Alert History Count: " + String(alertHistoryCount));
    Serial.println("Web Server: Ready on port 80");
    Serial.println("\nOpen browser and connect to device IP to configure");
    Serial.println("====================================================\n");
    
    // پخش صدای راه‌اندازی موفق
    if (settings.buzzerEnabled) {
        playSuccessTone();
    }
    
    // نمایش LED‌ها به صورت تست
    if (settings.ledEnabled) {
        turnOnGreenLED();
        delay(200);
        turnOffGreenLED();
        delay(100);
        turnOnRedLED();
        delay(200);
        turnOffRedLED();
        delay(100);
        turnOnGreenLED();
        turnOnRedLED();
        delay(300);
        turnOffAllLEDs();
    }
    
    Serial.println("Setup completed successfully!");
    Serial.println("System is now running...\n");
}

// ==================== LOOP FUNCTION ====================
void loop() {
    // ========== مدیریت وب سرور ==========
    server.handleClient();
    
    // ========== بررسی دکمه ریست ==========
    checkResetButton();
    if (resetInProgress) {
        return; // اگر ریست در حال انجام است، ادامه نده
    }
    
    // ========== بروزرسانی زمان ==========
    static unsigned long lastTimeUpdate = 0;
    if (millis() - lastTimeUpdate > 60000) { // هر 1 دقیقه
        lastTimeUpdate = millis();
        updateDateTime();
        
        // دیباگ زمان هر 5 دقیقه
        static unsigned long timeDebugCounter = 0;
        if (timeDebugCounter++ % 5 == 0) {
            Serial.println("\n=== Time Update ===");
            Serial.println("Current DateTime: " + currentDateTime);
            
            struct tm timeinfo;
            if (getLocalTime(&timeinfo, 100)) {
                char timeString[30];
                strftime(timeString, sizeof(timeString), "%Y/%m/%d %H:%M:%S", &timeinfo);
                Serial.println("System time: " + String(timeString));
            }
            Serial.println("==================\n");
        }
    }
    
    // ========== مدیریت اتصال WiFi ==========
    if (isConnectedToWiFi) {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("\n❌ WiFi connection lost!");
            isConnectedToWiFi = false;
            showOLEDMessage("WiFi Lost!", "Reconnecting...", "", "");
            lastWiFiCheck = millis();
            
            // خاموش کردن LED‌ها
            turnOffAllLEDs();
            longAlertLEDActive = false;
            shortAlertLEDActive = false;
            profitLEDActive = false;
            lossLEDActive = false;
        }
    } else if (apModeActive) {
        // در حالت AP کاری نکن
    } else {
        // تلاش برای reconnect هر 1 دقیقه
        if (millis() - lastWiFiCheck > RECONNECT_INTERVAL) {
            lastWiFiCheck = millis();
            Serial.println("Attempting WiFi reconnection...");
            showOLEDMessage("Reconnecting", "to WiFi...", "", "");
            
            if (connectToWiFi()) {
                Serial.println("✅ WiFi reconnected successfully!");
            } else if (!apModeActive) {
                Serial.println("Reconnect failed, starting AP mode");
                startAPMode();
            }
        }
    }
    
    // ========== بروزرسانی داده‌ها ==========
    if (isConnectedToWiFi && millis() - lastDataUpdate > DATA_UPDATE_INTERVAL) {
        lastDataUpdate = millis();
        
        Serial.println("\n=== Data Update Cycle ===");
        Serial.println("Mode: " + String(settings.isExitMode ? "EXIT (Price Change)" : "ENTRY (Loss Tracking)"));
        Serial.println("Active Portfolio: " + String(settings.isExitMode ? settings.exitPortfolio : settings.entryPortfolio));
        
        String data = getPortfolioData();
        parseCryptoData(data);
        
        // بررسی الرت‌ها بعد از بروزرسانی داده
        checkCryptoAlerts();
        
        Serial.println("Data update completed");
        Serial.println("Positions: " + String(cryptoCount));
        Serial.println("Total P/L: " + String(portfolio.totalPnlPercent, 1) + "%");
        Serial.println("=======================\n");
    }
    
    // ========== تغییر نمایش موقعیت‌ها ==========
    if (millis() - lastCryptoChange > CRYPTO_CHANGE_INTERVAL && !showingAlert) {
        lastCryptoChange = millis();
        updateCryptoDisplay();
    }
    
    // ========== مدیریت نمایش الرت ==========
    if (showingAlert) {
        if (millis() - alertStartTime > ALERT_DISPLAY_TIME) {
            showingAlert = false;
            Serial.println("Alert display timeout, returning to normal view");
        }
    }
    
    // ========== بررسی دوره‌ای الرت‌ها ==========
    if (millis() - lastAlertCheck > 5000 && isConnectedToWiFi && cryptoCount > 0) {
        lastAlertCheck = millis();
        
        static int alertCheckCounter = 0;
        alertCheckCounter++;
        
        if (alertCheckCounter % 3 == 0) { // هر 15 ثانیه دیباگ کن
            Serial.println("\n=== Periodic Alert Check ===");
            Serial.println("Checking " + String(cryptoCount) + " positions");
            Serial.println("Mode: " + String(settings.isExitMode ? "EXIT" : "ENTRY"));
            
            if (settings.isExitMode) {
                Serial.println("Exit Alert Threshold: " + String(settings.exitAlertPercent) + "%");
            } else {
                Serial.println("Alert Threshold: " + String(settings.alertThreshold, 1) + "%");
                Serial.println("Severe Threshold: " + String(settings.severeAlertThreshold, 1) + "%");
            }
        }
        
        checkCryptoAlerts();
    }
    
    // ========== بروزرسانی نمایش OLED ==========
    if (millis() - lastDisplayUpdate > DISPLAY_UPDATE_INTERVAL) {
        lastDisplayUpdate = millis();
        updateOLEDDisplay();
    }
    
    // ========== مدیریت LED‌ها ==========
    if (settings.ledEnabled) {
        updateLEDBlinking();
        manageLEDStates();
    } else {
        turnOffAllLEDs();
    }
    
    // ========== دیباگ سیستم هر 30 ثانیه ==========
    static unsigned long lastSystemDebug = 0;
    if (millis() - lastSystemDebug > 30000) {
        lastSystemDebug = millis();
        
        Serial.println("\n=== System Status Debug ===");
        Serial.println("Connection: " + String(isConnectedToWiFi ? "WiFi Connected" : (apModeActive ? "AP Mode" : "Disconnected")));
        
        if (isConnectedToWiFi) {
            Serial.println("SSID: " + WiFi.SSID());
            Serial.println("IP: " + WiFi.localIP().toString());
            Serial.println("RSSI: " + String(WiFi.RSSI()) + " dBm");
        } else if (apModeActive) {
            Serial.println("AP IP: " + WiFi.softAPIP().toString());
        }
        
        Serial.println("Current Time: " + currentDateTime);
        Serial.println("Trading Mode: " + String(settings.isExitMode ? "EXIT" : "ENTRY"));
        Serial.println("Active Portfolio: " + String(settings.isExitMode ? settings.exitPortfolio : settings.entryPortfolio));
        Serial.println("Positions Loaded: " + String(cryptoCount));
        Serial.println("Active Alerts: " + String(activeAlertCount));
        
        // وضعیت LED‌ها
        if (settings.isExitMode) {
            Serial.println("LED Status: 🟢 " + String(profitLEDActive ? "ON (PROFIT)" : "OFF") + 
                          " | 🔴 " + String(lossLEDActive ? "ON (LOSS)" : "OFF"));
        } else {
            Serial.println("LED Status: 🟢 " + String(longAlertLEDActive ? "ON (LONG)" : "OFF") + 
                          " | 🔴 " + String(shortAlertLEDActive ? "ON (SHORT)" : "OFF"));
        }
        
        // وضعیت تاریخچه الرت‌ها
        Serial.println("Alert History: " + String(alertHistoryCount) + " alerts stored");
        
        if (alertHistoryCount > 0) {
            AlertHistory* latest = &alertHistory[alertHistoryCount - 1];
            Serial.println("Latest Alert: " + String(latest->symbol) + 
                          " at " + String(latest->timeString) + 
                          " (" + String(latest->pnlPercent, 1) + "%)");
        }
        
        Serial.println("Last Data Update: " + String((millis() - lastDataUpdate) / 1000) + "s ago");
        Serial.println("Next Update In: " + String((DATA_UPDATE_INTERVAL - (millis() - lastDataUpdate)) / 1000) + "s");
        Serial.println("=============================\n");
    }
    
    // ========== دیباگ تخصصی زمان هر 2 دقیقه ==========

    static unsigned long lastTimeDebug = 0;
    if (millis() - lastTimeDebug > 120000) {
        lastTimeDebug = millis();
        
        Serial.println("\n=== Detailed Time Debug ===");
        printTimeStatus();

        // بررسی timestamp در صورت وجود الرت
        if (alertHistoryCount > 0) {
            Serial.println("\nChecking alert timestamps:");
            for (int i = max(0, alertHistoryCount - 3); i < alertHistoryCount; i++) {
                AlertHistory* alert = &alertHistory[i];
                Serial.print("Alert " + String(i) + " (" + String(alert->symbol) + "): ");
                Serial.print("Timestamp=" + String(alert->alertTime));
                Serial.print(", Stored='" + String(alert->timeString) + "'");

                // فقط بررسی عددی timestamp (بدون تبدیل)
                if (alert->alertTime < 1700000000UL) {
                    Serial.print(" ⚠️ INVALID (too small)");
                }
                Serial.println();
            }
        }
        Serial.println("===========================\n");
    }

    
    // ========== دیباگ وضعیت EXIT MODE هر 1 دقیقه ==========
    static unsigned long lastExitModeDebug = 0;
    if (settings.isExitMode && millis() - lastExitModeDebug > 60000) {
        lastExitModeDebug = millis();
        
        Serial.println("\n=== EXIT Mode Status ===");
        Serial.println("Exit Alert Threshold: " + String(settings.exitAlertPercent) + "%");
        Serial.println("Exit Alerts Enabled: " + String(settings.exitAlertEnabled ? "YES" : "NO"));
        Serial.println("Exit Blink Enabled: " + String(settings.exitAlertBlinkEnabled ? "YES" : "NO"));
        Serial.println("Active Portfolio: " + String(settings.exitPortfolio));
        
        // نمایش چند موقعیت اول
        int displayCount = min(5, cryptoCount);
        if (displayCount > 0) {
            Serial.println("Top " + String(displayCount) + " positions for exit tracking:");
            for (int i = 0; i < displayCount; i++) {
                CryptoPosition* crypto = &cryptoData[i];
                float priceChangePercent = 0;
                if (crypto->exitAlertLastPrice > 0) {
                    priceChangePercent = fabs((crypto->currentPrice - crypto->exitAlertLastPrice) / 
                                            crypto->exitAlertLastPrice * 100);
                }
                
                Serial.print("  ");
                Serial.print(crypto->symbol);
                Serial.print(": ");
                Serial.print(formatPrice(crypto->currentPrice));
                Serial.print(" (Last Alert: ");
                Serial.print(formatPrice(crypto->exitAlertLastPrice));
                Serial.print(", Change: ");
                Serial.print(priceChangePercent, 2);
                Serial.print("%, Alerted: ");
                Serial.print(crypto->exitAlerted ? "YES" : "NO");
                Serial.println(")");
            }
        }
        Serial.println("=======================\n");
    }
    
    // ========== دیباگ وضعیت ENTRY MODE هر 1 دقیقه ==========
    static unsigned long lastEntryModeDebug = 0;
    if (!settings.isExitMode && millis() - lastEntryModeDebug > 60000) {
        lastEntryModeDebug = millis();
        
        Serial.println("\n=== ENTRY Mode Status ===");
        Serial.println("Alert Threshold: " + String(settings.alertThreshold, 1) + "%");
        Serial.println("Severe Threshold: " + String(settings.severeAlertThreshold, 1) + "%");
        Serial.println("Portfolio Threshold: " + String(settings.portfolioAlertThreshold, 1) + "%");
        Serial.println("Active Portfolio: " + String(settings.entryPortfolio));
        Serial.println("Portfolio P/L: " + String(portfolio.totalPnlPercent, 1) + "%");
        
        // نمایش چند موقعیت اول
        int displayCount = min(5, cryptoCount);
        if (displayCount > 0) {
            Serial.println("Top " + String(displayCount) + " losses:");
            for (int i = 0; i < displayCount; i++) {
                CryptoPosition* crypto = &sortedCryptoData[i];
                Serial.print("  ");
                Serial.print(i + 1);
                Serial.print(". ");
                Serial.print(crypto->symbol);
                Serial.print(": ");
                Serial.print(crypto->changePercent, 1);
                Serial.print("% (");
                Serial.print(formatPrice(crypto->currentPrice));
                Serial.print(", ");
                Serial.print(crypto->isLong ? "LONG" : "SHORT");
                Serial.print(", Alerted: ");
                Serial.print(crypto->alerted ? "YES" : "NO");
                Serial.println(")");
            }
        }
        Serial.println("========================\n");
    }
    
    // ========== دیباگ حافظه هر 5 دقیقه ==========
    static unsigned long lastMemoryDebug = 0;
    if (millis() - lastMemoryDebug > 300000) {
        lastMemoryDebug = millis();
        
        Serial.println("\n=== Memory Status ===");
        Serial.println("Alert History: " + String(alertHistoryCount) + "/" + String(MAX_ALERT_HISTORY));
        Serial.println("Active Alerts: " + String(activeAlertCount) + "/10");
        Serial.println("Crypto Positions: " + String(cryptoCount) + "/" + String(MAX_CRYPTO));
        Serial.println("WiFi Networks: " + String(settings.networkCount) + "/" + String(MAX_WIFI_NETWORKS));
        
        // بررسی وضعیت حافظه
        if (alertHistoryCount >= MAX_ALERT_HISTORY - 5) {
            Serial.println("⚠️ Warning: Alert history nearly full!");
        }
        if (cryptoCount >= MAX_CRYPTO - 5) {
            Serial.println("⚠️ Warning: Crypto positions nearly at maximum!");
        }
        Serial.println("====================\n");
    }
    
    // ========== بررسی سلامت سیستم ==========
    static unsigned long lastHealthCheck = 0;
    if (millis() - lastHealthCheck > 180000) { // هر 3 دقیقه
        lastHealthCheck = millis();
        
        Serial.println("\n=== System Health Check ===");
        
        // بررسی اتصال WiFi
        if (isConnectedToWiFi && WiFi.status() != WL_CONNECTED) {
            Serial.println("❌ WiFi connection issue detected");
        }
        
        // بررسی داده‌ها
        if (cryptoCount == 0 && isConnectedToWiFi && strlen(settings.server) > 0) {
            Serial.println("⚠️ Warning: No crypto data loaded despite connectivity");
        }
        
        // بررسی زمان
        struct tm timeinfo;
        if (!getLocalTime(&timeinfo, 100)) {
            Serial.println("⚠️ Warning: Cannot get system time");
        } else if (timeinfo.tm_year + 1900 < 2023) {
            Serial.println("⚠️ Warning: System time appears incorrect (year: " + String(timeinfo.tm_year + 1900) + ")");
        }
        
        // بررسی وضعیت الرت‌ها
        int activeAlertsCount = 0;
        for (int i = 0; i < cryptoCount; i++) {
            if (cryptoData[i].alerted || cryptoData[i].exitAlerted) {
                activeAlertsCount++;
            }
        }
        Serial.println("Active Position Alerts: " + String(activeAlertsCount));
        
        Serial.println("Health check completed");
        Serial.println("======================\n");
    }
    
    // ========== تاخیر کوچک برای جلوگیری از overload ==========
    delay(10);
}


