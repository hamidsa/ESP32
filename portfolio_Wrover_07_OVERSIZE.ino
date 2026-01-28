/* ============================================================================
   PORTFOLIO MONITOR - ESP32-WROVER (Dual Mode)
   Version: 4.0.0 - Enhanced for ESP32-WROVER with ST7789 Display
   Hardware Configuration Based on GitHub File
   Features:
   - Simultaneous Entry & Exit Mode Monitoring
   - ST7789 240x240 IPS Display
   - RGB LED Color Spectrum System
   - Bluetooth BLE Notifications
   - Dual LED Sets (2 RGB + 4 Single Color)
   - API Check every 15 seconds
   ============================================================================ */

#include <SPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <time.h>
#include <BluetoothSerial.h>

// ===== DEFINES =====
#define MAX_ALERT_HISTORY 10
#define MAX_CRYPTO 60
#define DISPLAY_CRYPTO_COUNT 5
#define MAX_WIFI_NETWORKS 5
#define EEPROM_SIZE 4096
#define JSON_BUFFER_SIZE 8192

// ===== DISPLAY CONFIGURATION =====
TFT_eSPI tft = TFT_eSPI();

// ===== PIN DEFINITIONS BASED ON GITHUB FILE =====
// Display Backlight
#define TFT_BL_PIN 5

// Bluetooth
BluetoothSerial SerialBT;

// RGB LEDs
#define RGB1_RED    32
#define RGB1_GREEN  33
#define RGB1_BLUE   25

#define RGB2_RED    26
#define RGB2_GREEN  14
#define RGB2_BLUE   12

// Single Color LEDs
#define LED_GREEN_1 22
#define LED_RED_1   21
#define LED_GREEN_2 19
#define LED_RED_2   27

// Buzzer
#define BUZZER_PIN 13

// Reset Button
#define RESET_BUTTON_PIN 0

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

struct WiFiNetwork {
    char ssid[32];
    char password[64];
    bool configured;
    unsigned long lastConnected;
    int connectionAttempts;
    byte priority;
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
    
    // For simultaneous monitoring
    byte monitoringMode; // 0=Entry, 1=Exit, 2=Both
    bool entryAlerted;
    bool exitPriceAlerted;
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
};

struct ActiveAlert {
    String symbol;
    bool isLong;
    bool isProfit;
    float pnlPercent;
    unsigned long firstTriggerTime;
    int blinkCount;
    bool isBlinking;
    byte alertMode;
};

struct SystemSettings {
    // WiFi
    WiFiNetwork networks[MAX_WIFI_NETWORKS];
    int networkCount;
    int lastConnectedIndex;
    
    // API
    char server[128];
    char username[32];
    char userpass[64];
    char entryPortfolio[32];
    char exitPortfolio[32];
    
    // Alerts
    float alertThreshold;
    float severeAlertThreshold;
    float portfolioAlertThreshold;
    int buzzerVolume;
    bool buzzerEnabled;
    
    // Display
    int displayBrightness;
    bool displayEnabled;
    
    // Modes
    bool simultaneousMode; // Monitor both modes simultaneously
    float exitAlertPercent;
    bool exitAlertEnabled;
    
    // LED Settings
    int ledBrightness;
    bool ledEnabled;
    bool rgbEnabled;
    int rgbSpeed; // 1-10 (1=slow, 10=fast)
    
    // Bluetooth
    bool bluetoothEnabled;
    char bluetoothName[32];
    
    // System
    byte magicNumber;
    bool configured;
    unsigned long firstBoot;
    int bootCount;
    unsigned long totalUptime;
    
    // Timings
    int apiCheckInterval; // in seconds
    int displayRefreshInterval;
    int rgbAnimationSpeed;
};

// ===== GLOBAL VARIABLES =====
SystemSettings settings;
CryptoPosition cryptoData[MAX_CRYPTO];
CryptoPosition sortedEntryData[MAX_CRYPTO];
CryptoPosition sortedExitData[MAX_CRYPTO];
PortfolioSummary entryPortfolio;
PortfolioSummary exitPortfolio;
AlertHistory alertHistory[MAX_ALERT_HISTORY];
ActiveAlert activeAlerts[20];

int cryptoCount = 0;
int alertHistoryCount = 0;
int activeAlertCount = 0;

bool isConnectedToWiFi = false;
bool apModeActive = false;
bool bluetoothConnected = false;
bool showingAlert = false;
bool resetInProgress = false;

WebServer server(80);
HTTPClient http;

// Timers
unsigned long lastDataUpdate = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long lastWiFiCheck = 0;
unsigned long lastAlertCheck = 0;
unsigned long alertStartTime = 0;
unsigned long lastResetButtonPress = 0;
unsigned long lastBlinkTime = 0;
unsigned long lastRGBTimer = 0;
unsigned long lastBluetoothBroadcast = 0;

// Display buffers
String currentDateTime = "";
String alertTitle = "";
String alertMessage = "";

// Mode tracking
int currentDisplayMode = 0; // 0=Entry, 1=Exit, 2=Both
bool displayBothModes = true;

// RGB Animation
int rgbMode1Position = 0;
int rgbMode2Position = 0;
int rgbMode1ColorIndex = 0;
int rgbMode2ColorIndex = 0;
byte rgbMode1Colors[10][3]; // Store recent alert colors for mode 1
byte rgbMode2Color[3]; // Current color for mode 2

// Button state
bool resetButtonActive = false;

// ===== FUNCTION DECLARATIONS =====
void setupDisplay();
void updateDisplay();
void displayEntryMode();
void displayExitMode();
void displayBothModesView();
void displayAlert(String title, String message);
void setupRGBLEDs();
void setRGBColor(int mode, byte r, byte g, byte b);
void updateRGBAnimation();
void calculateMode2RGBColor(float percentChange, bool isLong);
void setupSingleLEDs();
void updateSingleLEDs();
void setupBuzzer();
void playTone(int frequency, int duration);
void setupBluetooth();
void sendBluetoothNotification(String message);
void initializeSettings();
bool loadSettings();
bool saveSettings();
bool connectToWiFi();
bool startAPMode();
void setupWebServer();
void handleRoot();
void handleSetup();
void handleSaveSettings();
void checkCryptoAlerts();
void checkEntryModeAlerts();
void checkExitModeAlerts();
void processAPIResponse(String jsonData, bool isExitMode);
String getPortfolioData(String portfolioName);
String getTimeString();
void addToAlertHistory(String symbol, float price, float pnl, bool isLong, bool isSevere, bool isProfit, byte mode);
void updateAlertDisplay();
void factoryReset();
void setupResetButton();
void checkResetButton();
void sortPositionsByLoss();
void resetAllAlerts();

// ===== SETUP FUNCTION =====
void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n\n==========================================");
    Serial.println("   ESP32-WROVER Portfolio Monitor v4.0");
    Serial.println("   Based on GitHub Hardware Configuration");
    Serial.println("   Dual Mode Simultaneous Monitoring");
    Serial.println("   ST7789 240x240 Display");
    Serial.println("   RGB LED Color Spectrum System");
    Serial.println("   Bluetooth Serial Notifications");
    Serial.println("==========================================\n");
    
    // Initialize display
    setupDisplay();
    
    // Initialize LEDs
    setupRGBLEDs();
    setupSingleLEDs();
    
    // Initialize buzzer
    setupBuzzer();
    
    // Initialize reset button
    setupResetButton();
    
    // Load settings
    if (!loadSettings()) {
        Serial.println("First boot - initializing default settings");
        initializeSettings();
    }
    
    // Initialize WiFi
    if (settings.networkCount > 0) {
        if (connectToWiFi()) {
            Serial.println("✅ WiFi connected successfully");
        } else {
            Serial.println("❌ WiFi connection failed, starting AP mode");
            startAPMode();
        }
    } else {
        Serial.println("No WiFi networks saved, starting AP mode");
        startAPMode();
    }
    
    // Initialize Bluetooth if enabled
    if (settings.bluetoothEnabled) {
        setupBluetooth();
    }
    
    // Initialize web server
    setupWebServer();
    
    // Configure time
    configTime(3.5 * 3600, 0, "pool.ntp.org", "time.nist.gov");
    
    // Initial display
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    tft.setCursor(20, 100);
    tft.println("System Ready");
    
    Serial.println("\n✅ System initialization complete");
    Serial.println("📱 Connect to WiFi or Bluetooth to configure");
}

// ===== LOOP FUNCTION =====
void loop() {
    // Handle web server
    server.handleClient();
    
    // Check reset button
    checkResetButton();
    
    // Update WiFi connection
    if (isConnectedToWiFi && WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi connection lost");
        isConnectedToWiFi = false;
    }
    
    // Update data every 15 seconds
    if (isConnectedToWiFi && millis() - lastDataUpdate > (settings.apiCheckInterval * 1000)) {
        lastDataUpdate = millis();
        
        // Fetch data for both portfolios
        String entryData = getPortfolioData(settings.entryPortfolio);
        String exitData = getPortfolioData(settings.exitPortfolio);
        
        // Process both responses
        processAPIResponse(entryData, false);
        processAPIResponse(exitData, true);
        
        // Sort positions
        sortPositionsByLoss();
        
        // Check alerts
        checkCryptoAlerts();
    }
    
    // Update display
    if (millis() - lastDisplayUpdate > (settings.displayRefreshInterval * 1000)) {
        lastDisplayUpdate = millis();
        updateDisplay();
    }
    
    // Update LEDs
    updateRGBAnimation();
    updateSingleLEDs();
    
    // Handle Bluetooth
    if (settings.bluetoothEnabled && SerialBT.hasClient()) {
        // Send periodic status updates
        if (millis() - lastBluetoothBroadcast > 10000) {
            lastBluetoothBroadcast = millis();
            String status = "Active: " + String(cryptoCount) + " positions";
            SerialBT.println(status);
        }
    }
    
    // Handle alert timeout
    if (showingAlert && millis() - alertStartTime > 7000) {
        showingAlert = false;
    }
    
    // Small delay
    delay(10);
}

// ===== DISPLAY FUNCTIONS =====
void setupDisplay() {
    tft.init();
    tft.setRotation(1); // Landscape mode
    
    // Set backlight
    pinMode(TFT_BL_PIN, OUTPUT);
    digitalWrite(TFT_BL_PIN, HIGH);
    
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    
    Serial.println("✅ Display initialized (ST7789 240x240)");
}

void updateDisplay() {
    tft.fillScreen(TFT_BLACK);
    
    if (showingAlert) {
        displayAlert(alertTitle, alertMessage);
    } else {
        if (displayBothModes) {
            displayBothModesView();
        } else {
            if (currentDisplayMode == 0) {
                displayEntryMode();
            } else {
                displayExitMode();
            }
        }
    }
}

void displayEntryMode() {
    tft.setTextColor(TFT_CYAN);
    tft.setTextSize(2);
    tft.setCursor(10, 10);
    tft.println("ENTRY MODE");
    
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(1);
    tft.setCursor(10, 40);
    tft.printf("Portfolio: %s", settings.entryPortfolio);
    
    tft.setCursor(10, 60);
    tft.printf("Positions: %d", entryPortfolio.totalPositions);
    
    tft.setCursor(10, 80);
    tft.printf("P/L: %.2f%%", entryPortfolio.totalPnlPercent);
    
    // Display top 5 positions
    tft.setTextColor(TFT_YELLOW);
    tft.setCursor(10, 110);
    tft.println("Top Positions:");
    
    tft.setTextColor(TFT_WHITE);
    int yPos = 130;
    for (int i = 0; i < min(5, cryptoCount); i++) {
        CryptoPosition* pos = &sortedEntryData[i];
        String symbol = String(pos->symbol);
        if (symbol.length() > 6) symbol = symbol.substring(0, 6);
        
        tft.setCursor(10, yPos);
        tft.printf("%s: %.1f%%", symbol.c_str(), pos->changePercent);
        yPos += 15;
        if (yPos > 220) break;
    }
}

void displayExitMode() {
    tft.setTextColor(TFT_MAGENTA);
    tft.setTextSize(2);
    tft.setCursor(10, 10);
    tft.println("EXIT MODE");
    
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(1);
    tft.setCursor(10, 40);
    tft.printf("Portfolio: %s", settings.exitPortfolio);
    
    tft.setCursor(10, 60);
    tft.printf("Alert %%: %.1f%%", settings.exitAlertPercent);
    
    tft.setCursor(10, 80);
    tft.printf("Positions: %d", exitPortfolio.totalPositions);
    
    // Display positions with price changes
    tft.setTextColor(TFT_YELLOW);
    tft.setCursor(10, 110);
    tft.println("Recent Changes:");
    
    tft.setTextColor(TFT_WHITE);
    int yPos = 130;
    int count = 0;
    for (int i = 0; i < cryptoCount && count < 5; i++) {
        CryptoPosition* pos = &cryptoData[i];
        if (pos->exitAlerted) {
            float change = fabs((pos->currentPrice - pos->exitAlertLastPrice) / pos->exitAlertLastPrice * 100);
            String symbol = String(pos->symbol);
            if (symbol.length() > 6) symbol = symbol.substring(0, 6);
            
            tft.setCursor(10, yPos);
            tft.printf("%s: %.1f%%", symbol.c_str(), change);
            yPos += 15;
            count++;
            if (yPos > 220) break;
        }
    }
}

void displayBothModesView() {
    // Split screen vertically
    tft.drawLine(0, 120, 240, 120, TFT_GRAY);
    
    // Top half - Entry Mode
    tft.setTextColor(TFT_CYAN);
    tft.setTextSize(1);
    tft.setCursor(10, 10);
    tft.println("ENTRY MODE");
    
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(10, 30);
    tft.printf("Portfolio: %s", settings.entryPortfolio);
    
    tft.setCursor(10, 50);
    tft.printf("P/L: %.1f%%", entryPortfolio.totalPnlPercent);
    
    tft.setCursor(10, 70);
    tft.printf("Pos: %d", entryPortfolio.totalPositions);
    
    // Show top 2 positions for Entry
    tft.setTextColor(TFT_YELLOW);
    tft.setCursor(10, 90);
    tft.println("Top 2:");
    tft.setTextColor(TFT_WHITE);
    int yPos = 100;
    for (int i = 0; i < min(2, cryptoCount); i++) {
        CryptoPosition* pos = &sortedEntryData[i];
        String symbol = String(pos->symbol);
        if (symbol.length() > 6) symbol = symbol.substring(0, 6);
        
        tft.setCursor(10, yPos);
        tft.printf("%s: %.1f%%", symbol.c_str(), pos->changePercent);
        yPos += 15;
    }
    
    // Bottom half - Exit Mode
    tft.setTextColor(TFT_MAGENTA);
    tft.setCursor(10, 130);
    tft.println("EXIT MODE");
    
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(10, 150);
    tft.printf("Portfolio: %s", settings.exitPortfolio);
    
    tft.setCursor(10, 170);
    tft.printf("Alert: %.1f%%", settings.exitAlertPercent);
    
    tft.setCursor(10, 190);
    tft.printf("Pos: %d", exitPortfolio.totalPositions);
    
    // Current time
    tft.setTextColor(TFT_GREEN);
    tft.setCursor(10, 220);
    tft.print(getTimeString());
}

void displayAlert(String title, String message) {
    tft.fillScreen(TFT_RED);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    tft.setCursor(20, 50);
    tft.println(title);
    
    tft.setTextSize(1);
    tft.setCursor(20, 100);
    tft.println(message);
    
    tft.setCursor(20, 150);
    tft.println("Press any key to continue...");
}

// ===== LED FUNCTIONS =====
void setupRGBLEDs() {
    pinMode(RGB1_RED, OUTPUT);
    pinMode(RGB1_GREEN, OUTPUT);
    pinMode(RGB1_BLUE, OUTPUT);
    pinMode(RGB2_RED, OUTPUT);
    pinMode(RGB2_GREEN, OUTPUT);
    pinMode(RGB2_BLUE, OUTPUT);
    
    // Initialize RGB arrays
    for (int i = 0; i < 10; i++) {
        rgbMode1Colors[i][0] = 0;
        rgbMode1Colors[i][1] = 0;
        rgbMode1Colors[i][2] = 0;
    }
    
    rgbMode2Color[0] = 0;
    rgbMode2Color[1] = 0;
    rgbMode2Color[2] = 0;
    
    // Turn off LEDs initially
    digitalWrite(RGB1_RED, LOW);
    digitalWrite(RGB1_GREEN, LOW);
    digitalWrite(RGB1_BLUE, LOW);
    digitalWrite(RGB2_RED, LOW);
    digitalWrite(RGB2_GREEN, LOW);
    digitalWrite(RGB2_BLUE, LOW);
    
    Serial.println("✅ RGB LEDs initialized");
}

void setRGBColor(int mode, byte r, byte g, byte b) {
    if (!settings.rgbEnabled) {
        digitalWrite(RGB1_RED, LOW);
        digitalWrite(RGB1_GREEN, LOW);
        digitalWrite(RGB1_BLUE, LOW);
        digitalWrite(RGB2_RED, LOW);
        digitalWrite(RGB2_GREEN, LOW);
        digitalWrite(RGB2_BLUE, LOW);
        return;
    }
    
    // Scale brightness
    float scale = settings.ledBrightness / 255.0;
    r = (byte)(r * scale);
    g = (byte)(g * scale);
    b = (byte)(b * scale);
    
    if (mode == 1) {
        analogWrite(RGB1_RED, r);
        analogWrite(RGB1_GREEN, g);
        analogWrite(RGB1_BLUE, b);
    } else {
        analogWrite(RGB2_RED, r);
        analogWrite(RGB2_GREEN, g);
        analogWrite(RGB2_BLUE, b);
    }
}

void updateRGBAnimation() {
    if (!settings.rgbEnabled) return;
    
    unsigned long currentTime = millis();
    int animationDelay = 23000 / settings.rgbSpeed; // 23 seconds total divided by speed
    
    // Mode 1: Show alert history colors
    if (currentTime - lastRGBTimer > animationDelay) {
        lastRGBTimer = currentTime;
        
        // Rotate through stored colors
        rgbMode1Position = (rgbMode1Position + 1) % 10;
        
        byte r = rgbMode1Colors[rgbMode1Position][0];
        byte g = rgbMode1Colors[rgbMode1Position][1];
        byte b = rgbMode1Colors[rgbMode1Position][2];
        
        setRGBColor(1, r, g, b);
        
        // Debug output
        Serial.print("RGB Mode 1 - Color ");
        Serial.print(rgbMode1Position);
        Serial.print(": R=");
        Serial.print(r);
        Serial.print(" G=");
        Serial.print(g);
        Serial.print(" B=");
        Serial.println(b);
    }
    
    // Mode 2: Show current status with color spectrum
    // This updates continuously based on current positions
    bool updated = false;
    for (int i = 0; i < cryptoCount; i++) {
        CryptoPosition* pos = &cryptoData[i];
        if (pos->exitAlerted) {
            calculateMode2RGBColor(pos->changePercent, pos->isLong);
            updated = true;
            break; // Use first alerted position
        }
    }
    
    if (!updated) {
        // Default color when no alerts
        setRGBColor(2, 0, 0, 0); // Turn off
    }
}

void calculateMode2RGBColor(float percentChange, bool isLong) {
    if (!settings.rgbEnabled) return;
    
    byte r, g, b;
    
    float absChange = fabs(percentChange);
    bool isPositive = (isLong && percentChange > 0) || (!isLong && percentChange < 0);
    
    if (isPositive) {
        // Positive changes (profit for LONG, profit for SHORT when price drops)
        if (absChange >= settings.exitAlertPercent * 2) {
            // Beyond second threshold - Blue
            float ratio = min(1.0, (absChange - settings.exitAlertPercent * 2) / (settings.exitAlertPercent * 2));
            r = 0;
            g = (byte)(100 * (1.0 - ratio));
            b = (byte)(255 * ratio + 155 * (1.0 - ratio));
        } else if (absChange >= settings.exitAlertPercent) {
            // Between thresholds - Green to Blue gradient
            float ratio = (absChange - settings.exitAlertPercent) / settings.exitAlertPercent;
            r = 0;
            g = (byte)(255 * (1.0 - ratio));
            b = (byte)(255 * ratio);
        } else {
            // Below first threshold - Green
            float ratio = absChange / settings.exitAlertPercent;
            r = 0;
            g = (byte)(255 * ratio + 100 * (1.0 - ratio));
            b = 0;
        }
    } else {
        // Negative changes (loss for LONG, loss for SHORT when price rises)
        if (absChange >= settings.exitAlertPercent * 2) {
            // Beyond second threshold - Dark Red
            float ratio = min(1.0, (absChange - settings.exitAlertPercent * 2) / (settings.exitAlertPercent * 2));
            r = (byte)(255 * ratio + 200 * (1.0 - ratio));
            g = 0;
            b = 0;
        } else if (absChange >= settings.exitAlertPercent) {
            // Between thresholds - Yellow to Red gradient
            float ratio = (absChange - settings.exitAlertPercent) / settings.exitAlertPercent;
            r = (byte)(255 * ratio + 255 * (1.0 - ratio));
            g = (byte)(255 * (1.0 - ratio));
            b = 0;
        } else {
            // Below first threshold - Yellow
            float ratio = absChange / settings.exitAlertPercent;
            r = (byte)(255 * ratio);
            g = (byte)(255 * ratio);
            b = 0;
        }
    }
    
    rgbMode2Color[0] = r;
    rgbMode2Color[1] = g;
    rgbMode2Color[2] = b;
    setRGBColor(2, r, g, b);
    
    // Debug output
    Serial.print("RGB Mode 2 - Change: ");
    Serial.print(percentChange, 1);
    Serial.print("%, Color: R=");
    Serial.print(r);
    Serial.print(" G=");
    Serial.print(g);
    Serial.print(" B=");
    Serial.println(b);
}

void setupSingleLEDs() {
    pinMode(LED_GREEN_1, OUTPUT);
    pinMode(LED_RED_1, OUTPUT);
    pinMode(LED_GREEN_2, OUTPUT);
    pinMode(LED_RED_2, OUTPUT);
    
    digitalWrite(LED_GREEN_1, LOW);
    digitalWrite(LED_RED_1, LOW);
    digitalWrite(LED_GREEN_2, LOW);
    digitalWrite(LED_RED_2, LOW);
    
    Serial.println("✅ Single-color LEDs initialized");
}

void updateSingleLEDs() {
    if (!settings.ledEnabled) {
        digitalWrite(LED_GREEN_1, LOW);
        digitalWrite(LED_RED_1, LOW);
        digitalWrite(LED_GREEN_2, LOW);
        digitalWrite(LED_RED_2, LOW);
        return;
    }
    
    // Mode 1 LEDs (Entry Mode)
    bool entryAlert = false;
    bool entrySevere = false;
    for (int i = 0; i < cryptoCount; i++) {
        if (cryptoData[i].entryAlerted) {
            entryAlert = true;
            if (cryptoData[i].severeAlerted) entrySevere = true;
        }
    }
    
    if (entryAlert) {
        if (entrySevere) {
            // Blink red for severe alerts
            if ((millis() / 500) % 2 == 0) {
                digitalWrite(LED_RED_1, HIGH);
                digitalWrite(LED_GREEN_1, LOW);
            } else {
                digitalWrite(LED_RED_1, LOW);
            }
        } else {
            // Solid red for normal alerts
            digitalWrite(LED_RED_1, HIGH);
            digitalWrite(LED_GREEN_1, LOW);
        }
    } else {
        // Green if no alerts
        digitalWrite(LED_GREEN_1, HIGH);
        digitalWrite(LED_RED_1, LOW);
    }
    
    // Mode 2 LEDs (Exit Mode)
    bool exitAlert = false;
    bool exitProfit = false;
    for (int i = 0; i < cryptoCount; i++) {
        if (cryptoData[i].exitPriceAlerted) {
            exitAlert = true;
            // Determine if profit or loss based on position type
            if (cryptoData[i].isLong) {
                exitProfit = (cryptoData[i].changePercent > 0);
            } else {
                exitProfit = (cryptoData[i].changePercent < 0);
            }
            break;
        }
    }
    
    if (exitAlert) {
        if (exitProfit) {
            digitalWrite(LED_GREEN_2, HIGH);
            digitalWrite(LED_RED_2, LOW);
        } else {
            digitalWrite(LED_RED_2, HIGH);
            digitalWrite(LED_GREEN_2, LOW);
        }
    } else {
        // Both off if no alerts
        digitalWrite(LED_GREEN_2, LOW);
        digitalWrite(LED_RED_2, LOW);
    }
}

// ===== BUZZER FUNCTIONS =====
void setupBuzzer() {
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
    Serial.println("✅ Buzzer initialized");
}

void playTone(int frequency, int duration) {
    if (!settings.buzzerEnabled) return;
    
    tone(BUZZER_PIN, frequency, duration);
    delay(duration);
    noTone(BUZZER_PIN);
}

// ===== BLUETOOTH FUNCTIONS =====
void setupBluetooth() {
    SerialBT.begin(settings.bluetoothName);
    Serial.println("✅ Bluetooth Serial initialized");
    Serial.println("📱 Device name: " + String(settings.bluetoothName));
    Serial.println("📱 Connect to this device from your phone");
}

void sendBluetoothNotification(String message) {
    if (settings.bluetoothEnabled && SerialBT.hasClient()) {
        SerialBT.println(message);
        Serial.println("📱 Bluetooth notification: " + message);
    }
}

// ===== SETTINGS FUNCTIONS =====
void initializeSettings() {
    memset(&settings, 0, sizeof(SystemSettings));
    
    settings.networkCount = 0;
    settings.buzzerVolume = 10;
    settings.buzzerEnabled = true;
    
    strcpy(settings.server, "http://your-server.com");
    strcpy(settings.username, "your-username");
    strcpy(settings.userpass, "your-password");
    strcpy(settings.entryPortfolio, "Main");
    strcpy(settings.exitPortfolio, "ActivePositions");
    strcpy(settings.bluetoothName, "ESP32-Portfolio");
    
    settings.alertThreshold = -10.0;
    settings.severeAlertThreshold = -20.0;
    settings.portfolioAlertThreshold = -10.0;
    settings.exitAlertPercent = 5.0;
    
    settings.displayBrightness = 100;
    settings.displayEnabled = true;
    
    settings.simultaneousMode = true;
    settings.exitAlertEnabled = true;
    
    settings.ledBrightness = 100;
    settings.ledEnabled = true;
    settings.rgbEnabled = true;
    settings.rgbSpeed = 5;
    
    settings.bluetoothEnabled = true;
    
    settings.apiCheckInterval = 15; // Every 15 seconds
    settings.displayRefreshInterval = 2;
    settings.rgbAnimationSpeed = 5;
    
    settings.magicNumber = 123;
    settings.configured = false;
    settings.firstBoot = millis();
    settings.bootCount = 1;
    
    saveSettings();
}

bool loadSettings() {
    EEPROM.begin(EEPROM_SIZE);
    EEPROM.get(0, settings);
    EEPROM.end();
    
    if (settings.magicNumber != 123) {
        Serial.println("Settings not found or corrupted");
        return false;
    }
    
    Serial.println("✅ Settings loaded from EEPROM");
    return true;
}

bool saveSettings() {
    settings.magicNumber = 123;
    EEPROM.begin(EEPROM_SIZE);
    EEPROM.put(0, settings);
    bool result = EEPROM.commit();
    EEPROM.end();
    
    if (result) {
        Serial.println("✅ Settings saved to EEPROM");
    } else {
        Serial.println("❌ Failed to save settings to EEPROM");
    }
    
    return result;
}

// ===== WIFI FUNCTIONS =====
bool connectToWiFi() {
    if (settings.networkCount == 0) return false;
    
    for (int i = 0; i < settings.networkCount; i++) {
        if (!settings.networks[i].configured) continue;
        
        Serial.println("Connecting to: " + String(settings.networks[i].ssid));
        WiFi.begin(settings.networks[i].ssid, settings.networks[i].password);
        
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 20) {
            delay(500);
            Serial.print(".");
            attempts++;
        }
        
        if (WiFi.status() == WL_CONNECTED) {
            isConnectedToWiFi = true;
            apModeActive = false;
            settings.lastConnectedIndex = i;
            
            Serial.println("\n✅ WiFi Connected!");
            Serial.println("SSID: " + WiFi.SSID());
            Serial.println("IP: " + WiFi.localIP().toString());
            Serial.println("RSSI: " + String(WiFi.RSSI()) + " dBm");
            return true;
        }
    }
    
    return false;
}

bool startAPMode() {
    WiFi.disconnect(true);
    delay(1000);
    WiFi.mode(WIFI_AP);
    
    if (WiFi.softAP("ESP32-Portfolio", "12345678")) {
        apModeActive = true;
        isConnectedToWiFi = false;
        Serial.println("✅ AP Mode Started");
        Serial.println("SSID: ESP32-Portfolio");
        Serial.println("Password: 12345678");
        Serial.println("IP: " + WiFi.softAPIP().toString());
        return true;
    }
    
    return false;
}

// ===== API FUNCTIONS =====
String getPortfolioData(String portfolioName) {
    if (!isConnectedToWiFi) return "{}";
    
    String url = String(settings.server) + "/api/device/portfolio/" + 
                String(settings.username) + "?portfolio_name=" + portfolioName;
    
    http.begin(url);
    
    String authString = String(settings.username) + ":" + String(settings.userpass);
    String encodedAuth = base64Encode(authString);
    http.addHeader("Authorization", "Basic " + encodedAuth);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(10000);
    
    Serial.println("Fetching data from: " + url);
    int httpCode = http.GET();
    String result = "{}";
    
    if (httpCode == 200) {
        result = http.getString();
        Serial.println("✅ Data received successfully");
    } else {
        Serial.println("❌ HTTP Error: " + String(httpCode));
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

void processAPIResponse(String jsonData, bool isExitMode) {
    DynamicJsonDocument doc(JSON_BUFFER_SIZE);
    DeserializationError error = deserializeJson(doc, jsonData);
    
    if (error) {
        Serial.println("JSON Parse Error: " + String(error.c_str()));
        return;
    }
    
    if (!doc.containsKey("portfolio")) {
        Serial.println("No portfolio data in response");
        return;
    }
    
    JsonArray portfolioArray = doc["portfolio"];
    int newCount = portfolioArray.size();
    
    Serial.println("Processing " + String(newCount) + " positions for " + 
                   (isExitMode ? "Exit" : "Entry") + " mode");
    
    // Process each position
    for (int i = 0; i < newCount && i < MAX_CRYPTO; i++) {
        JsonObject position = portfolioArray[i];
        
        const char* symbol = position["symbol"] | "UNKNOWN";
        float currentPrice = position["current_price"] | 0.0;
        float changePercent = position["pnl_percent"] | 0.0;
        
        // Check if this symbol already exists
        bool found = false;
        for (int j = 0; j < cryptoCount; j++) {
            if (strcmp(cryptoData[j].symbol, symbol) == 0) {
                // Update existing position
                cryptoData[j].currentPrice = currentPrice;
                cryptoData[j].changePercent = changePercent;
                
                if (isExitMode) {
                    cryptoData[j].monitoringMode |= 0x02; // Set exit mode flag
                } else {
                    cryptoData[j].monitoringMode |= 0x01; // Set entry mode flag
                }
                
                found = true;
                break;
            }
        }
        
        if (!found && cryptoCount < MAX_CRYPTO) {
            // Add new position
            CryptoPosition* pos = &cryptoData[cryptoCount];
            strncpy(pos->symbol, symbol, 15);
            pos->symbol[15] = '\0';
            
            pos->currentPrice = currentPrice;
            pos->entryPrice = position["entry_price"] | 0.0;
            pos->changePercent = changePercent;
            pos->isLong = true; // Default, adjust based on your API
            
            // Initialize alert tracking
            pos->alerted = false;
            pos->severeAlerted = false;
            pos->entryAlerted = false;
            pos->exitAlerted = false;
            pos->exitPriceAlerted = false;
            pos->lastAlertPrice = currentPrice;
            pos->exitAlertLastPrice = currentPrice;
            
            if (isExitMode) {
                pos->monitoringMode = 0x02; // Exit mode only
            } else {
                pos->monitoringMode = 0x01; // Entry mode only
            }
            
            cryptoCount++;
        }
    }
    
    // Update portfolio summary
    if (doc.containsKey("summary")) {
        JsonObject summary = doc["summary"];
        
        if (isExitMode) {
            exitPortfolio.totalInvestment = summary["total_investment"] | 0.0;
            exitPortfolio.totalCurrentValue = summary["total_current_value"] | 0.0;
            exitPortfolio.totalPnl = summary["total_pnl"] | 0.0;
            exitPortfolio.totalPositions = cryptoCount;
            
            if (exitPortfolio.totalInvestment > 0) {
                exitPortfolio.totalPnlPercent = ((exitPortfolio.totalCurrentValue - exitPortfolio.totalInvestment) / 
                                                exitPortfolio.totalInvestment) * 100;
            }
        } else {
            entryPortfolio.totalInvestment = summary["total_investment"] | 0.0;
            entryPortfolio.totalCurrentValue = summary["total_current_value"] | 0.0;
            entryPortfolio.totalPnl = summary["total_pnl"] | 0.0;
            entryPortfolio.totalPositions = cryptoCount;
            
            if (entryPortfolio.totalInvestment > 0) {
                entryPortfolio.totalPnlPercent = ((entryPortfolio.totalCurrentValue - entryPortfolio.totalInvestment) / 
                                                entryPortfolio.totalInvestment) * 100;
            }
        }
    }
    
    Serial.println("✅ " + String(cryptoCount) + " positions loaded for " + 
                   (isExitMode ? "Exit" : "Entry") + " mode");
}

// ===== ALERT FUNCTIONS =====
void checkCryptoAlerts() {
    if (settings.simultaneousMode) {
        checkEntryModeAlerts();
        checkExitModeAlerts();
    } else {
        if (currentDisplayMode == 0) {
            checkEntryModeAlerts();
        } else {
            checkExitModeAlerts();
        }
    }
}

void checkEntryModeAlerts() {
    for (int i = 0; i < cryptoCount; i++) {
        CryptoPosition* pos = &cryptoData[i];
        
        // Only check positions that are monitored in Entry mode
        if (!(pos->monitoringMode & 0x01)) continue;
        
        if (!pos->entryAlerted && pos->changePercent <= settings.alertThreshold) {
            bool isSevere = pos->changePercent <= settings.severeAlertThreshold;
            
            // Trigger alert
            String alertMsg = String(pos->symbol) + " " + 
                            (pos->isLong ? "LONG" : "SHORT") + " " +
                            String(pos->changePercent, 1) + "%";
            
            addToAlertHistory(pos->symbol, pos->currentPrice, pos->changePercent, 
                            pos->isLong, isSevere, false, 0);
            
            // Store color for RGB LED (Mode 1)
            // Different colors for different alert types
            if (isSevere) {
                // Severe alert - Bright red
                rgbMode1Colors[rgbMode1ColorIndex][0] = 255;
                rgbMode1Colors[rgbMode1ColorIndex][1] = 0;
                rgbMode1Colors[rgbMode1ColorIndex][2] = 0;
            } else if (pos->isLong) {
                // Long position alert - Orange
                rgbMode1Colors[rgbMode1ColorIndex][0] = 255;
                rgbMode1Colors[rgbMode1ColorIndex][1] = 100;
                rgbMode1Colors[rgbMode1ColorIndex][2] = 0;
            } else {
                // Short position alert - Purple
                rgbMode1Colors[rgbMode1ColorIndex][0] = 128;
                rgbMode1Colors[rgbMode1ColorIndex][1] = 0;
                rgbMode1Colors[rgbMode1ColorIndex][2] = 128;
            }
            
            rgbMode1ColorIndex = (rgbMode1ColorIndex + 1) % 10;
            
            // Send Bluetooth notification
            sendBluetoothNotification("ENTRY ALERT: " + alertMsg);
            
            // Play sound
            if (isSevere) {
                playTone(400, 300);
                delay(100);
                playTone(300, 300);
                delay(100);
                playTone(200, 300);
            } else {
                playTone(600, 500);
            }
            
            pos->entryAlerted = true;
            pos->alerted = true;
            pos->severeAlerted = isSevere;
            pos->lastAlertTime = millis();
            pos->lastAlertPrice = pos->currentPrice;
            
            // Show on display
            showingAlert = true;
            alertStartTime = millis();
            alertTitle = "ENTRY ALERT";
            alertMessage = alertMsg;
            
            Serial.println("🚨 ENTRY ALERT: " + alertMsg);
        }
    }
}

void checkExitModeAlerts() {
    if (!settings.exitAlertEnabled) return;
    
    for (int i = 0; i < cryptoCount; i++) {
        CryptoPosition* pos = &cryptoData[i];
        
        // Only check positions that are monitored in Exit mode
        if (!(pos->monitoringMode & 0x02)) continue;
        
        if (pos->exitAlertLastPrice == 0) {
            pos->exitAlertLastPrice = pos->currentPrice;
            continue;
        }
        
        float priceChange = fabs((pos->currentPrice - pos->exitAlertLastPrice) / 
                                pos->exitAlertLastPrice * 100);
        
        if (priceChange >= settings.exitAlertPercent && !pos->exitPriceAlerted) {
            bool isProfit = (pos->isLong && pos->changePercent > 0) || 
                          (!pos->isLong && pos->changePercent < 0);
            
            // Trigger alert
            String alertMsg = String(pos->symbol) + " " + 
                            (pos->isLong ? "LONG" : "SHORT") + " " +
                            (isProfit ? "PROFIT" : "LOSS") + " " +
                            String(priceChange, 1) + "%";
            
            addToAlertHistory(pos->symbol, pos->currentPrice, pos->changePercent, 
                            pos->isLong, false, isProfit, 1);
            
            // Update RGB LED color (Mode 2)
            calculateMode2RGBColor(pos->changePercent, pos->isLong);
            
            // Send Bluetooth notification
            sendBluetoothNotification("EXIT ALERT: " + alertMsg);
            
            // Play sound
            if (isProfit) {
                playTone(800, 300);
                delay(100);
                playTone(1000, 300);
            } else {
                playTone(300, 300);
                delay(100);
                playTone(200, 300);
            }
            
            pos->exitPriceAlerted = true;
            pos->exitAlerted = true;
            pos->exitAlertLastPrice = pos->currentPrice;
            
            // Show on display
            showingAlert = true;
            alertStartTime = millis();
            alertTitle = "EXIT ALERT";
            alertMessage = alertMsg;
            
            Serial.println("🚨 EXIT ALERT: " + alertMsg);
        }
    }
}

void addToAlertHistory(String symbol, float price, float pnl, bool isLong, 
                      bool isSevere, bool isProfit, byte mode) {
    if (alertHistoryCount >= MAX_ALERT_HISTORY) {
        // Shift array to make room
        for (int i = 0; i < MAX_ALERT_HISTORY - 1; i++) {
            alertHistory[i] = alertHistory[i + 1];
        }
        alertHistoryCount--;
    }
    
    AlertHistory* alert = &alertHistory[alertHistoryCount];
    
    strncpy(alert->symbol, symbol.c_str(), 15);
    alert->symbol[15] = '\0';
    
    alert->alertPrice = price;
    alert->pnlPercent = pnl;
    alert->isLong = isLong;
    alert->isSevere = isSevere;
    alert->isProfit = isProfit;
    alert->alertMode = mode;
    alert->alertTime = millis();
    
    // Create time string
    String timeStr = getTimeString();
    strncpy(alert->timeString, timeStr.c_str(), 19);
    alert->timeString[19] = '\0';
    
    // Create message
    String msg = symbol + " " + String(pnl, 1) + "%";
    strncpy(alert->message, msg.c_str(), 63);
    alert->message[63] = '\0';
    
    alert->acknowledged = false;
    alertHistoryCount++;
    
    Serial.println("📝 Alert added to history: " + symbol + " at " + timeStr);
}

void sortPositionsByLoss() {
    // Sort for Entry mode display
    for (int i = 0; i < cryptoCount; i++) {
        sortedEntryData[i] = cryptoData[i];
    }
    
    // Simple bubble sort by changePercent
    for (int i = 0; i < cryptoCount - 1; i++) {
        for (int j = 0; j < cryptoCount - i - 1; j++) {
            if (sortedEntryData[j].changePercent > sortedEntryData[j + 1].changePercent) {
                CryptoPosition temp = sortedEntryData[j];
                sortedEntryData[j] = sortedEntryData[j + 1];
                sortedEntryData[j + 1] = temp;
            }
        }
    }
    
    // Copy to Exit mode array (for future use)
    for (int i = 0; i < cryptoCount; i++) {
        sortedExitData[i] = cryptoData[i];
    }
}

// ===== WEB SERVER FUNCTIONS =====
void setupWebServer() {
    server.on("/", handleRoot);
    server.on("/setup", handleSetup);
    server.on("/save", HTTP_POST, handleSaveSettings);
    server.on("/refresh", []() {
        lastDataUpdate = 0; // Force immediate update
        server.send(200, "text/plain", "Refreshing data...");
    });
    server.on("/reset", []() {
        resetAllAlerts();
        server.send(200, "text/plain", "Alerts reset");
    });
    
    server.begin();
    Serial.println("✅ Web server started on port 80");
}

void handleRoot() {
    String html = R"=====(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Portfolio Monitor v4.0</title>
    <style>
        body { 
            font-family: Arial, sans-serif; 
            margin: 20px; 
            background: #1a1a1a; 
            color: #ffffff; 
        }
        .container { 
            max-width: 1200px; 
            margin: 0 auto; 
        }
        .header { 
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); 
            color: white; 
            padding: 30px; 
            border-radius: 15px 15px 0 0; 
            text-align: center;
            box-shadow: 0 4px 15px rgba(0,0,0,0.2);
        }
        .content { 
            display: flex; 
            gap: 20px; 
            margin-top: 20px; 
            flex-wrap: wrap;
        }
        .panel { 
            flex: 1; 
            background: #2d2d2d; 
            padding: 25px; 
            border-radius: 15px; 
            box-shadow: 0 4px 15px rgba(0,0,0,0.3);
            min-width: 300px;
        }
        .history-container { 
            max-height: 500px; 
            overflow-y: auto;
            background: #1a1a1a;
            padding: 15px;
            border-radius: 10px;
        }
        .history-side-by-side {
            display: flex;
            gap: 20px;
            margin-top: 20px;
        }
        .history-column {
            flex: 1;
            min-width: 250px;
        }
        .alert-item { 
            padding: 15px; 
            margin-bottom: 10px; 
            border-radius: 10px;
            transition: all 0.3s ease;
            border-left: 5px solid;
        }
        .alert-item:hover {
            transform: translateY(-2px);
            box-shadow: 0 5px 15px rgba(0,0,0,0.3);
        }
        .entry-alert { 
            border-left-color: #00bcd4; 
            background: linear-gradient(135deg, rgba(0,188,212,0.1) 0%, rgba(0,188,212,0.05) 100%);
        }
        .exit-alert { 
            border-left-color: #9c27b0; 
            background: linear-gradient(135deg, rgba(156,39,176,0.1) 0%, rgba(156,39,176,0.05) 100%);
        }
        .stats { 
            display: grid; 
            grid-template-columns: repeat(2, 1fr); 
            gap: 15px; 
            margin-top: 20px;
        }
        .stat-box { 
            background: linear-gradient(135deg, #3a3a3a 0%, #2a2a2a 100%); 
            padding: 20px; 
            border-radius: 10px; 
            text-align: center;
            border: 1px solid #444;
        }
        .stat-box h3 {
            margin-top: 0;
            color: #ccc;
        }
        .stat-box p {
            font-size: 24px;
            font-weight: bold;
            margin: 10px 0;
        }
        .form-group { 
            margin-bottom: 20px; 
        }
        label { 
            display: block; 
            margin-bottom: 8px; 
            color: #ccc;
        }
        input, select { 
            width: 100%; 
            padding: 12px; 
            margin-top: 5px; 
            border: 1px solid #444; 
            border-radius: 8px; 
            background: #3a3a3a; 
            color: white;
            box-sizing: border-box;
        }
        input:focus, select:focus {
            outline: none;
            border-color: #667eea;
            box-shadow: 0 0 0 2px rgba(102, 126, 234, 0.2);
        }
        button { 
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); 
            color: white; 
            border: none; 
            padding: 14px 28px; 
            border-radius: 8px; 
            cursor: pointer; 
            font-size: 16px;
            font-weight: bold;
            transition: all 0.3s ease;
            width: 100%;
            margin-top: 10px;
        }
        button:hover {
            transform: translateY(-2px);
            box-shadow: 0 7px 20px rgba(102, 126, 234, 0.4);
        }
        .controls {
            display: grid;
            grid-template-columns: repeat(2, 1fr);
            gap: 10px;
            margin-top: 20px;
        }
        .mode-indicator {
            padding: 10px;
            border-radius: 8px;
            margin: 10px 0;
            text-align: center;
            font-weight: bold;
        }
        .mode-entry {
            background: rgba(0, 188, 212, 0.2);
            border: 2px solid #00bcd4;
            color: #00bcd4;
        }
        .mode-exit {
            background: rgba(156, 39, 176, 0.2);
            border: 2px solid #9c27b0;
            color: #9c27b0;
        }
        .positive { color: #4CAF50; }
        .negative { color: #f44336; }
        .neutral { color: #ff9800; }
        
        /* Custom scrollbar */
        .history-container::-webkit-scrollbar {
            width: 8px;
        }
        .history-container::-webkit-scrollbar-track {
            background: #2d2d2d;
            border-radius: 4px;
        }
        .history-container::-webkit-scrollbar-thumb {
            background: #667eea;
            border-radius: 4px;
        }
        .history-container::-webkit-scrollbar-thumb:hover {
            background: #764ba2;
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>📊 Portfolio Monitor v4.0</h1>
            <p>Simultaneous Entry & Exit Mode Monitoring with RGB LED System</p>
        </div>
        
        <div class="content">
            <div class="panel">
                <h2>⚙️ System Configuration</h2>
                <form action="/save" method="post">
                    <div class="form-group">
                        <label>📡 API Server URL:</label>
                        <input type="text" name="server" value="%SERVER%" placeholder="http://your-api-server.com">
                    </div>
                    
                    <div class="form-group">
                        <label>👤 Username:</label>
                        <input type="text" name="username" value="%USERNAME%" placeholder="Your username">
                    </div>
                    
                    <div class="form-group">
                        <label>🔑 Password:</label>
                        <input type="password" name="password" value="%PASSWORD%" placeholder="Your password">
                    </div>
                    
                    <div class="form-group">
                        <label>📈 Entry Portfolio Name:</label>
                        <input type="text" name="entryPortfolio" value="%ENTRY_PORTFOLIO%" placeholder="Main">
                    </div>
                    
                    <div class="form-group">
                        <label>📉 Exit Portfolio Name:</label>
                        <input type="text" name="exitPortfolio" value="%EXIT_PORTFOLIO%" placeholder="ActivePositions">
                    </div>
                    
                    <div class="form-group">
                        <label>🔔 Entry Alert Threshold (%):</label>
                        <input type="number" name="alertThreshold" value="%ALERT_THRESHOLD%" step="0.5" min="-50" max="0">
                    </div>
                    
                    <div class="form-group">
                        <label>⚠️ Exit Alert Percent (%):</label>
                        <input type="number" name="exitAlertPercent" value="%EXIT_ALERT_PERCENT%" step="0.5" min="1" max="20">
                    </div>
                    
                    <button type="submit">💾 Save All Settings</button>
                </form>
                
                <div class="controls">
                    <button onclick="location.href='/refresh'">🔄 Refresh Data</button>
                    <button onclick="location.href='/reset'" style="background:linear-gradient(135deg, #f44336 0%, #d32f2f 100%)">♻️ Reset Alerts</button>
                </div>
            </div>
            
            <div class="panel">
                <h2>📊 System Status</h2>
                
                <div class="mode-indicator mode-entry">
                    📈 ENTRY MODE: %ENTRY_PORTFOLIO%
                </div>
                
                <div class="mode-indicator mode-exit">
                    📉 EXIT MODE: %EXIT_PORTFOLIO%
                </div>
                
                <div class="stats">
                    <div class="stat-box">
                        <h3>Entry Mode P/L</h3>
                        <p class="%ENTRY_PNL_CLASS%">%ENTRY_PNL%%</p>
                        <small>Overall performance</small>
                    </div>
                    
                    <div class="stat-box">
                        <h3>Entry Positions</h3>
                        <p>%ENTRY_POSITIONS%</p>
                        <small>Active positions</small>
                    </div>
                    
                    <div class="stat-box">
                        <h3>Exit Alert %</h3>
                        <p>%EXIT_ALERT_PERCENT%%</p>
                        <small>Price change trigger</small>
                    </div>
                    
                    <div class="stat-box">
                        <h3>Exit Positions</h3>
                        <p>%EXIT_POSITIONS%</p>
                        <small>Monitored positions</small>
                    </div>
                </div>
                
                <h3 style="margin-top: 30px;">🎯 Active Alerts</h3>
                <div class="stats">
                    <div class="stat-box">
                        <h3>Entry Alerts</h3>
                        <p class="neutral">%ENTRY_ALERTS%</p>
                        <small>Active alerts</small>
                    </div>
                    
                    <div class="stat-box">
                        <h3>Exit Alerts</h3>
                        <p class="neutral">%EXIT_ALERTS%</p>
                        <small>Active alerts</small>
                    </div>
                </div>
                
                <h3 style="margin-top: 30px;">🔗 Connection Status</h3>
                <div class="stats">
                    <div class="stat-box">
                        <h3>WiFi</h3>
                        <p class="positive">%WIFI_STATUS%</p>
                        <small>%WIFI_SSID%</small>
                    </div>
                    
                    <div class="stat-box">
                        <h3>Bluetooth</h3>
                        <p class="%BT_CLASS%">%BT_STATUS%</p>
                        <small>%BT_NAME%</small>
                    </div>
                </div>
            </div>
        </div>
        
        <div class="panel" style="flex: 2; min-width: 600px;">
            <h2>📜 Alert History (Last 20 Alerts)</h2>
            <div class="history-side-by-side">
                <div class="history-column">
                    <h3 style="color: #00bcd4;">📈 Entry Mode Alerts</h3>
                    <div class="history-container">
                        %ENTRY_HISTORY%
                    </div>
                </div>
                
                <div class="history-column">
                    <h3 style="color: #9c27b0;">📉 Exit Mode Alerts</h3>
                    <div class="history-container">
                        %EXIT_HISTORY%
                    </div>
                </div>
            </div>
            
            <div style="margin-top: 20px; padding: 15px; background: rgba(255, 255, 255, 0.05); border-radius: 10px;">
                <h3>💡 System Information</h3>
                <p><strong>Last Update:</strong> %LAST_UPDATE%</p>
                <p><strong>Total Positions:</strong> %TOTAL_POSITIONS%</p>
                <p><strong>API Check Interval:</strong> Every %API_INTERVAL% seconds</p>
                <p><strong>RGB LED Mode 1:</strong> Shows last 10 alert colors every 23 seconds</p>
                <p><strong>RGB LED Mode 2:</strong> Color spectrum based on price change %</p>
                <p><strong>LED Colors:</strong> 🟢 Entry OK | 🔴 Entry Alert | 🟡 Exit Profit | 🔴 Exit Loss</p>
            </div>
        </div>
    </div>
    
    <script>
        // Auto-refresh every 30 seconds
        setTimeout(function() {
            location.reload();
        }, 30000);
        
        // Add some interactivity
        document.addEventListener('DOMContentLoaded', function() {
            const alertItems = document.querySelectorAll('.alert-item');
            alertItems.forEach(item => {
                item.addEventListener('click', function() {
                    this.style.opacity = '0.7';
                    setTimeout(() => {
                        this.style.opacity = '1';
                    }, 300);
                });
            });
        });
    </script>
</body>
</html>
)=====";
    
    // Replace placeholders with actual data
    
    // API settings
    html.replace("%SERVER%", String(settings.server));
    html.replace("%USERNAME%", String(settings.username));
    html.replace("%PASSWORD%", String(settings.userpass));
    html.replace("%ENTRY_PORTFOLIO%", String(settings.entryPortfolio));
    html.replace("%EXIT_PORTFOLIO%", String(settings.exitPortfolio));
    
    // Alert thresholds
    html.replace("%ALERT_THRESHOLD%", String(settings.alertThreshold, 1));
    html.replace("%EXIT_ALERT_PERCENT%", String(settings.exitAlertPercent, 1));
    
    // Portfolio stats
    html.replace("%ENTRY_PNL%", String(entryPortfolio.totalPnlPercent, 1));
    html.replace("%ENTRY_POSITIONS%", String(entryPortfolio.totalPositions));
    html.replace("%EXIT_POSITIONS%", String(exitPortfolio.totalPositions));
    
    // Determine P/L class for styling
    String entryPnlClass = "neutral";
    if (entryPortfolio.totalPnlPercent > 0) entryPnlClass = "positive";
    else if (entryPortfolio.totalPnlPercent < 0) entryPnlClass = "negative";
    html.replace("%ENTRY_PNL_CLASS%", entryPnlClass);
    
    // Count active alerts
    int entryAlerts = 0;
    int exitAlerts = 0;
    for (int i = 0; i < cryptoCount; i++) {
        if (cryptoData[i].entryAlerted) entryAlerts++;
        if (cryptoData[i].exitPriceAlerted) exitAlerts++;
    }
    html.replace("%ENTRY_ALERTS%", String(entryAlerts));
    html.replace("%EXIT_ALERTS%", String(exitAlerts));
    
    // Connection status
    String wifiStatus = isConnectedToWiFi ? "Connected" : "Disconnected";
    String wifiSSID = isConnectedToWiFi ? WiFi.SSID() : "Not connected";
    html.replace("%WIFI_STATUS%", wifiStatus);
    html.replace("%WIFI_SSID%", wifiSSID);
    
    String btStatus = settings.bluetoothEnabled ? "Enabled" : "Disabled";
    String btClass = settings.bluetoothEnabled ? "positive" : "neutral";
    html.replace("%BT_STATUS%", btStatus);
    html.replace("%BT_CLASS%", btClass);
    html.replace("%BT_NAME%", String(settings.bluetoothName));
    
    // Alert history
    String entryHistory = "";
    String exitHistory = "";
    
    int entryCount = 0;
    int exitCount = 0;
    
    // Build history from newest to oldest
    for (int i = alertHistoryCount - 1; i >= 0; i--) {
        AlertHistory* alert = &alertHistory[i];
        
        String historyItem = "<div class='alert-item ";
        historyItem += (alert->alertMode == 0) ? "entry-alert" : "exit-alert";
        historyItem += "'>";
        
        historyItem += "<strong>" + String(alert->symbol) + "</strong><br>";
        historyItem += "Mode: " + String(alert->alertMode == 0 ? "ENTRY" : "EXIT") + "<br>";
        historyItem += "P/L: <span class='";
        historyItem += (alert->pnlPercent >= 0 ? "positive" : "negative");
        historyItem += "'>" + String(alert->pnlPercent, 1) + "%</span><br>";
        historyItem += "Price: $" + String(alert->alertPrice, 4) + "<br>";
        historyItem += "Time: " + String(alert->timeString) + "<br>";
        
        if (alert->isSevere) {
            historyItem += "<small style='color: #ff9800;'>⚠️ SEVERE ALERT</small>";
        }
        
        historyItem += "</div>";
        
        if (alert->alertMode == 0 && entryCount < 10) {
            entryHistory += historyItem;
            entryCount++;
        } else if (alert->alertMode == 1 && exitCount < 10) {
            exitHistory += historyItem;
            exitCount++;
        }
        
        if (entryCount >= 10 && exitCount >= 10) break;
    }
    
    if (entryHistory == "") {
        entryHistory = "<div style='padding: 20px; text-align: center; color: #888;'>No entry alerts yet</div>";
    }
    
    if (exitHistory == "") {
        exitHistory = "<div style='padding: 20px; text-align: center; color: #888;'>No exit alerts yet</div>";
    }
    
    html.replace("%ENTRY_HISTORY%", entryHistory);
    html.replace("%EXIT_HISTORY%", exitHistory);
    
    // System info
    html.replace("%LAST_UPDATE%", getTimeString());
    html.replace("%TOTAL_POSITIONS%", String(cryptoCount));
    html.replace("%API_INTERVAL%", String(settings.apiCheckInterval));
    
    server.send(200, "text/html", html);
}

void handleSetup() {
    handleRoot(); // Same as root for now
}

void handleSaveSettings() {
    bool changed = false;
    
    if (server.hasArg("server")) {
        String newServer = server.arg("server");
        if (newServer != settings.server) {
            strncpy(settings.server, newServer.c_str(), 127);
            changed = true;
        }
    }
    
    if (server.hasArg("username")) {
        String newUsername = server.arg("username");
        if (newUsername != settings.username) {
            strncpy(settings.username, newUsername.c_str(), 31);
            changed = true;
        }
    }
    
    if (server.hasArg("password")) {
        String newPassword = server.arg("password");
        if (newPassword != settings.userpass) {
            strncpy(settings.userpass, newPassword.c_str(), 63);
            changed = true;
        }
    }
    
    if (server.hasArg("entryPortfolio")) {
        String newEntryPortfolio = server.arg("entryPortfolio");
        if (newEntryPortfolio != settings.entryPortfolio) {
            strncpy(settings.entryPortfolio, newEntryPortfolio.c_str(), 31);
            changed = true;
        }
    }
    
    if (server.hasArg("exitPortfolio")) {
        String newExitPortfolio = server.arg("exitPortfolio");
        if (newExitPortfolio != settings.exitPortfolio) {
            strncpy(settings.exitPortfolio, newExitPortfolio.c_str(), 31);
            changed = true;
        }
    }
    
    if (server.hasArg("alertThreshold")) {
        float newThreshold = server.arg("alertThreshold").toFloat();
        if (newThreshold != settings.alertThreshold) {
            settings.alertThreshold = newThreshold;
            changed = true;
        }
    }
    
    if (server.hasArg("exitAlertPercent")) {
        float newPercent = server.arg("exitAlertPercent").toFloat();
        if (newPercent != settings.exitAlertPercent) {
            settings.exitAlertPercent = newPercent;
            changed = true;
        }
    }
    
    if (changed) {
        saveSettings();
        server.send(200, "text/html", 
            "<html><body style='text-align:center;padding:50px;background:#1a1a1a;color:white;'>"
            "<h1 style='color:#4CAF50;'>✅ Settings Saved!</h1>"
            "<p>System will now use the new settings.</p>"
            "<a href='/' style='color:#667eea;text-decoration:none;font-size:18px;'>← Back to Dashboard</a>"
            "</body></html>");
    } else {
        server.send(200, "text/html", 
            "<html><body style='text-align:center;padding:50px;background:#1a1a1a;color:white;'>"
            "<h1 style='color:#ff9800;'>⚠️ No Changes Detected</h1>"
            "<p>Settings were already up to date.</p>"
            "<a href='/' style='color:#667eea;text-decoration:none;font-size:18px;'>← Back to Dashboard</a>"
            "</body></html>");
    }
}

// ===== UTILITY FUNCTIONS =====
String getTimeString() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return "--:--:--";
    }
    
    char timeStr[20];
    strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);
    return String(timeStr);
}

void setupResetButton() {
    pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);
    Serial.println("✅ Reset button initialized");
}

void checkResetButton() {
    if (digitalRead(RESET_BUTTON_PIN) == LOW) {
        if (!resetButtonActive) {
            resetButtonActive = true;
            lastResetButtonPress = millis();
            Serial.println("Reset button pressed");
        } else if (millis() - lastResetButtonPress > 5000) {
            factoryReset();
        }
    } else {
        resetButtonActive = false;
    }
}

void factoryReset() {
    Serial.println("Performing factory reset...");
    tft.fillScreen(TFT_RED);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    tft.setCursor(20, 100);
    tft.println("Factory Reset!");
    
    initializeSettings();
    delay(2000);
    ESP.restart();
}

void resetAllAlerts() {
    for (int i = 0; i < cryptoCount; i++) {
        cryptoData[i].alerted = false;
        cryptoData[i].severeAlerted = false;
        cryptoData[i].entryAlerted = false;
        cryptoData[i].exitAlerted = false;
        cryptoData[i].exitPriceAlerted = false;
        cryptoData[i].lastAlertPrice = cryptoData[i].currentPrice;
        cryptoData[i].exitAlertLastPrice = cryptoData[i].currentPrice;
    }
    
    // Reset RGB colors
    for (int i = 0; i < 10; i++) {
        rgbMode1Colors[i][0] = 0;
        rgbMode1Colors[i][1] = 0;
        rgbMode1Colors[i][2] = 0;
    }
    
    rgbMode2Color[0] = 0;
    rgbMode2Color[1] = 0;
    rgbMode2Color[2] = 0;
    
    // Turn off LEDs
    digitalWrite(LED_GREEN_1, LOW);
    digitalWrite(LED_RED_1, LOW);
    digitalWrite(LED_GREEN_2, LOW);
    digitalWrite(LED_RED_2, LOW);
    
    setRGBColor(1, 0, 0, 0);
    setRGBColor(2, 0, 0, 0);
    
    Serial.println("✅ All alerts reset");
}

// ===== MAIN PROGRAM =====
// Note: The actual setup() and loop() functions are defined above