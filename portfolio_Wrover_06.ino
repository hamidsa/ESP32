/* ============================================================================
   PORTFOLIO MONITOR - ESP32-WROVER
   نسخه اصلاح شده برای مشکلات شما
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

// ===== تنظیمات نمایشگر ST7789 =====
TFT_eSPI tft = TFT_eSPI();
#define TFT_WIDTH 240
#define TFT_HEIGHT 240
#define TFT_BL_PIN 5

// ===== بلوتوث =====
BluetoothSerial SerialBT;

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

// ===== تنظیمات بازر =====
#define BUZZER_PIN 13

// تعریف نوع بازر (یکی را انتخاب کنید)
// #define BUZZER_TYPE_ACTIVE    // بازر اکتیو (2 پایه، با ولتاژ فعال می‌شود)
#define BUZZER_TYPE_PASSIVE   // بازر پسیو (نیاز به فرکانس - معمولاً با علامت S)

// ===== تنظیمات اصلاح شده =====
#define MAX_BRIGHTNESS 30    // کاهش شدید روشنایی (بدون مقاومت)
#define HISTORY_CYCLE_TIME 23000
#define EMAIL_CHECK_INTERVAL 23000
#define DATA_UPDATE_INTERVAL 15000

// ===== ساختارها =====
enum AlertType {
    ALERT_LONG_ENTRY,
    ALERT_SHORT_ENTRY,
    ALERT_PROFIT_EXIT,
    ALERT_LOSS_EXIT,
    ALERT_EMAIL
};

struct RGBColor {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

// ===== متغیرهای جهانی =====
unsigned long lastHistoryCycle = 0;
unsigned long lastEmailCheck = 0;
unsigned long lastDataUpdate = 0;
unsigned long lastDisplayUpdate = 0;

RGBColor alertHistoryColors[10];
int alertHistoryCount = 0;
int currentHistoryIndex = 0;

bool newEmailAlert = false;

// ===== توابع RGB با روشنایی کم =====
void setRGB1Color(uint8_t r, uint8_t g, uint8_t b) {
    // تقسیم بر 8 برای کاهش شدید روشنایی (بدون مقاومت)
    analogWrite(RGB1_RED, constrain(r/8, 0, MAX_BRIGHTNESS));
    analogWrite(RGB1_GREEN, constrain(g/8, 0, MAX_BRIGHTNESS));
    analogWrite(RGB1_BLUE, constrain(b/8, 0, MAX_BRIGHTNESS));
}

void setRGB2Color(uint8_t r, uint8_t g, uint8_t b) {
    // تقسیم بر 8 برای کاهش شدید روشنایی (بدون مقاومت)
    analogWrite(RGB2_RED, constrain(r/8, 0, MAX_BRIGHTNESS));
    analogWrite(RGB2_GREEN, constrain(g/8, 0, MAX_BRIGHTNESS));
    analogWrite(RGB2_BLUE, constrain(b/8, 0, MAX_BRIGHTNESS));
}

void addAlertToHistory(AlertType type) {
    RGBColor color;
    
    switch(type) {
        case ALERT_LONG_ENTRY:
            color = {180, 0, 0};      // قرمز
            break;
        case ALERT_SHORT_ENTRY:
            color = {255, 100, 0};    // نارنجی
            break;
        case ALERT_PROFIT_EXIT:
            color = {0, 180, 0};      // سبز
            break;
        case ALERT_LOSS_EXIT:
            color = {255, 50, 50};    // قرمز روشن
            break;
        case ALERT_EMAIL:
            color = {255, 255, 0};    // زرد
            break;
    }
    
    if (alertHistoryCount < 10) {
        alertHistoryColors[alertHistoryCount++] = color;
    } else {
        for (int i = 0; i < 9; i++) {
            alertHistoryColors[i] = alertHistoryColors[i + 1];
        }
        alertHistoryColors[9] = color;
    }
    
    Serial.print("Alert added to history: ");
    Serial.print(color.r); Serial.print(", ");
    Serial.print(color.g); Serial.print(", ");
    Serial.println(color.b);
}

void updateHistoryRGBDisplay() {
    if (alertHistoryCount == 0) {
        setRGB1Color(0, 0, 0);
        return;
    }
    
    static unsigned long lastColorChange = 0;
    if (millis() - lastColorChange > 500) {
        lastColorChange = millis();
        currentHistoryIndex = (currentHistoryIndex + 1) % alertHistoryCount;
        
        RGBColor color = alertHistoryColors[currentHistoryIndex];
        setRGB1Color(color.r, color.g, color.b);
    }
}

void updateThresholdRGB(float percentChange, bool isProfit) {
    RGBColor color;
    
    if (isProfit) {
        float absPercent = fabs(percentChange);
        
        if (absPercent <= 10.0) {
            color.r = 0;
            color.g = 150;  // کاهش روشنایی
            color.b = map(absPercent, 0, 10, 0, 50);
        } else if (absPercent <= 20.0) {
            color.r = 0;
            color.g = map(absPercent, 10, 20, 150, 0);
            color.b = 100;  // کاهش روشنایی
        } else {
            color.r = 0;
            color.g = 0;
            color.b = 80;   // کاهش شدید
        }
    } else {
        float absPercent = fabs(percentChange);
        
        if (absPercent <= 10.0) {
            color.r = 150;  // کاهش روشنایی
            color.g = map(absPercent, 0, 10, 150, 80);
            color.b = 0;
        } else if (absPercent <= 20.0) {
            color.r = 150;  // کاهش روشنایی
            color.g = map(absPercent, 10, 20, 80, 0);
            color.b = 0;
        } else {
            color.r = 100;  // کاهش شدید
            color.g = 0;
            color.b = 0;
        }
    }
    
    setRGB2Color(color.r, color.g, color.b);
}

// ===== توابع بازر ساده شده =====
void playBuzzer(int duration = 200, int frequency = 1000) {
    #ifdef BUZZER_TYPE_PASSIVE
    // برای بازر پسیو - استفاده از tone()
    tone(BUZZER_PIN, frequency, duration);
    delay(duration);  // منتظر پایان بوق می‌مانیم
    noTone(BUZZER_PIN);
    #else
    // برای بازر اکتیو (2 پایه) - دیجیتال
    digitalWrite(BUZZER_PIN, HIGH);
    delay(duration);
    digitalWrite(BUZZER_PIN, LOW);
    #endif
}

void playStartupSound() {
    Serial.println("Playing startup sound...");
    
    #ifdef BUZZER_TYPE_PASSIVE
    // صدای راه‌اندازی برای بازر پسیو
    tone(BUZZER_PIN, 1000, 100);  // بوق کوتاه با فرکانس 1000Hz
    delay(150);
    tone(BUZZER_PIN, 1200, 100);  // بوق با فرکانس بالاتر
    delay(150);
    tone(BUZZER_PIN, 1500, 200);  // بوق بلندتر
    delay(250);
    noTone(BUZZER_PIN);
    #else
    // نسخه دیجیتال برای بازر اکتیو
    digitalWrite(BUZZER_PIN, HIGH);
    delay(100);
    digitalWrite(BUZZER_PIN, LOW);
    delay(100);
    digitalWrite(BUZZER_PIN, HIGH);
    delay(100);
    digitalWrite(BUZZER_PIN, LOW);
    delay(100);
    digitalWrite(BUZZER_PIN, HIGH);
    delay(200);
    digitalWrite(BUZZER_PIN, LOW);
    #endif
    
    Serial.println("Startup sound complete");
}

// ===== توابع TFT اصلاح شده =====
void setupTFT() {
    Serial.println("Initializing TFT display...");
    tft.init();
    
    // ===== اصلاح مهم: جهت نمایشگر =====
    // امتحان کنید: 0, 1, 2, 3, 4
    tft.setRotation(0);  // حالت اول را امتحان کنید
    
    tft.fillScreen(TFT_BLACK);
    
    // روشنایی
    pinMode(TFT_BL_PIN, OUTPUT);
    analogWrite(TFT_BL_PIN, 128);  // 50% روشنایی
    
    Serial.println("TFT ready - Rotation: 0");
}

void updateTFTDisplay() {
    static int screenIndex = 0;
    static unsigned long lastScreenChange = 0;
    
    if (millis() - lastScreenChange > 8000) {
        lastScreenChange = millis();
        screenIndex = (screenIndex + 1) % 3;
    }
    
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(1);
    
    switch(screenIndex) {
        case 0:
            showMainStatus();
            break;
        case 1:
            showAlertStatus();
            break;
        case 2:
            showSystemInfo();
            break;
    }
}

void showMainStatus() {
    tft.setTextSize(2);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setCursor(40, 10);
    tft.println("PORTFOLIO");
    tft.setCursor(60, 35);
    tft.println("MONITOR");
    
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    
    tft.setCursor(10, 70);
    tft.print("LED Test Mode");
    
    tft.setCursor(10, 90);
    tft.print("RGB1: History");
    
    tft.setCursor(10, 110);
    tft.print("RGB2: Threshold");
    
    tft.setCursor(10, 140);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.print("Brightness: LOW");
    
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(10, 170);
    tft.print("Fix: Add 220Ω resistors");
    
    tft.setCursor(10, 210);
    tft.print("Screen 1/3");
}

void showAlertStatus() {
    tft.setTextSize(2);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setCursor(30, 10);
    tft.println("ALERT");
    tft.setCursor(20, 35);
    tft.println("TEST");
    
    tft.setTextSize(1);
    
    tft.setCursor(10, 70);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.print("RGB1: Should show RED");
    
    tft.setCursor(10, 90);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.print("RGB2: Should show GREEN");
    
    tft.setCursor(10, 110);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.print("LEDs: Should blink");
    
    tft.setCursor(10, 140);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.print("Buzzer: Test sound");
    
    tft.setCursor(10, 170);
    tft.print("Check Serial Monitor");
    
    tft.setCursor(10, 210);
    tft.print("Alerts: ");
    tft.print(alertHistoryCount);
}

void showSystemInfo() {
    tft.setTextSize(2);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setCursor(40, 10);
    tft.println("SYSTEM");
    tft.setCursor(30, 35);
    tft.println("INFO");
    
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    
    tft.setCursor(10, 70);
    tft.print("RGB Pins: 32,33,25");
    
    tft.setCursor(10, 90);
    tft.print("RGB Pins: 26,14,12");
    
    tft.setCursor(10, 110);
    tft.print("LED Pins: 22,21,19,27");
    
    tft.setCursor(10, 130);
    tft.print("Buzzer: GPIO 13");
    
    tft.setCursor(10, 150);
    tft.print("TFT BL: GPIO 5");
    
    tft.setCursor(10, 170);
    tft.print("Max Brightness: ");
    tft.print(MAX_BRIGHTNESS);
    
    tft.setCursor(10, 190);
    tft.print("Resistors: NOT USED");
    
    tft.setCursor(10, 210);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.print("Add 220Ω resistors!");
}

// ===== تست سخت‌افزار =====
void testAllHardware() {
    Serial.println("\n=== HARDWARE TEST STARTING ===");
    
    // 1. تست LEDهای معمولی
    Serial.println("1. Testing Normal LEDs...");
    digitalWrite(LED_GREEN_1, HIGH);
    digitalWrite(LED_RED_2, HIGH);
    delay(1000);
    digitalWrite(LED_GREEN_1, LOW);
    digitalWrite(LED_RED_2, LOW);
    
    digitalWrite(LED_RED_1, HIGH);
    digitalWrite(LED_GREEN_2, HIGH);
    delay(1000);
    digitalWrite(LED_RED_1, LOW);
    digitalWrite(LED_GREEN_2, LOW);
    
    // 2. تست RGB1
    Serial.println("2. Testing RGB1 (History)...");
    setRGB1Color(100, 0, 0);  // قرمز کم
    delay(1000);
    setRGB1Color(0, 100, 0);  // سبز کم
    delay(1000);
    setRGB1Color(0, 0, 100);  // آبی کم
    delay(1000);
    setRGB1Color(0, 0, 0);
    
    // 3. تست RGB2
    Serial.println("3. Testing RGB2 (Threshold)...");
    setRGB2Color(100, 0, 0);  // قرمز کم
    delay(1000);
    setRGB2Color(0, 100, 0);  // سبز کم
    delay(1000);
    setRGB2Color(0, 0, 100);  // آبی کم
    delay(1000);
    setRGB2Color(0, 0, 0);
    
    // 4. تست بازر
    Serial.println("4. Testing Buzzer...");
    #ifdef BUZZER_TYPE_PASSIVE
    Serial.println("   Mode: Passive Buzzer (tone)");
    Serial.println("   Testing different frequencies...");
    
    // تست فرکانس‌های مختلف
    tone(BUZZER_PIN, 500, 300);  // فرکانس پایین
    delay(400);
    tone(BUZZER_PIN, 1000, 300); // فرکانس متوسط
    delay(400);
    tone(BUZZER_PIN, 2000, 300); // فرکانس بالا
    delay(400);
    tone(BUZZER_PIN, 1500, 500); // فرکانس میانی
    delay(600);
    noTone(BUZZER_PIN);
    #else
    Serial.println("   Mode: Active Buzzer (digital)");
    for (int i = 0; i < 3; i++) {
        digitalWrite(BUZZER_PIN, HIGH);
        delay(200);
        digitalWrite(BUZZER_PIN, LOW);
        delay(300);
    }
    #endif
    
    Serial.println("=== HARDWARE TEST COMPLETE ===");
}

// ===== setup اصلی =====
void setup() {
    Serial.begin(115200);
    delay(2000);  // زمان بیشتر برای اتصال سریال
    
    Serial.println("\n\n========================================");
    Serial.println("   PORTFOLIO MONITOR - DEBUG MODE");
    Serial.println("   Problems to fix:");
    Serial.println("   1. TFT rotation wrong");
    Serial.println("   2. LEDs too bright (no resistors)");
    Serial.println("   3. Buzzer volume low");
    Serial.println("========================================\n");
    
    Serial.println("SOLUTIONS:");
    Serial.println("1. Change tft.setRotation(0) to 1,2,3,4");
    Serial.println("2. Add 220Ω resistors to RGB LEDs");
    Serial.println("3. Check buzzer type (active/passive)");
    Serial.println("4. Check buzzer voltage (3.3V or 5V)");
    
    // نمایش نوع بازر
    #ifdef BUZZER_TYPE_PASSIVE
    Serial.println("BUZZER TYPE: PASSIVE (using tone())");
    #else
    Serial.println("BUZZER TYPE: ACTIVE (using digitalWrite())");
    #endif
    
    // راه‌اندازی سخت‌افزار
    setupTFT();
    
    // راه‌اندازی LEDها
    pinMode(LED_GREEN_1, OUTPUT);
    pinMode(LED_RED_1, OUTPUT);
    pinMode(LED_GREEN_2, OUTPUT);
    pinMode(LED_RED_2, OUTPUT);
    
    pinMode(RGB1_RED, OUTPUT);
    pinMode(RGB1_GREEN, OUTPUT);
    pinMode(RGB1_BLUE, OUTPUT);
    pinMode(RGB2_RED, OUTPUT);
    pinMode(RGB2_GREEN, OUTPUT);
    pinMode(RGB2_BLUE, OUTPUT);
    
    // راه‌اندازی بازر
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
    
    // راه‌اندازی بلوتوث
    if (!SerialBT.begin("Portfolio-Debug")) {
        Serial.println("Bluetooth failed!");
    }
    
    // خاموش کردن همه
    digitalWrite(LED_GREEN_1, LOW);
    digitalWrite(LED_RED_1, LOW);
    digitalWrite(LED_GREEN_2, LOW);
    digitalWrite(LED_RED_2, LOW);
    setRGB1Color(0, 0, 0);
    setRGB2Color(0, 0, 0);
    
    // نمایش پیام راه‌اندازی
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(30, 100);
    tft.println("DEBUG MODE");
    
    // صدای راه‌اندازی
    playStartupSound();
    
    // تست کامل
    delay(1000);
    testAllHardware();
    
    Serial.println("\n✅ Debug mode started!");
    Serial.println("Send commands via Serial Monitor:");
    Serial.println("  'r' - Test rotation (0,1,2,3,4)");
    Serial.println("  'l' - Test LEDs");
    Serial.println("  'b' - Test buzzer (simple)");
    Serial.println("  'f' - Test buzzer frequencies");
    Serial.println("  's' - Show status");
    Serial.println("  'a' - Test Active buzzer mode");
    Serial.println("  'p' - Test Passive buzzer mode");
}

// ===== loop اصلی =====
void loop() {
    // دستورات سریال مانیتور
    if (Serial.available()) {
        char cmd = Serial.read();
        
        switch(cmd) {
            case 'r':  // تست rotation
                static int rotation = 0;
                rotation = (rotation + 1) % 5;
                tft.setRotation(rotation);
                tft.fillScreen(TFT_BLACK);
                tft.setTextColor(TFT_YELLOW, TFT_BLACK);
                tft.setTextSize(2);
                tft.setCursor(50, 100);
                tft.print("ROT: ");
                tft.print(rotation);
                Serial.print("Rotation set to: ");
                Serial.println(rotation);
                break;
                
            case 'l':  // تست LEDها
                Serial.println("LED Test");
                setRGB1Color(50, 0, 0);  // قرمز کم
                setRGB2Color(0, 50, 0);  // سبز کم
                delay(1000);
                setRGB1Color(0, 50, 0);
                setRGB2Color(0, 0, 50);
                delay(1000);
                setRGB1Color(0, 0, 0);
                setRGB2Color(0, 0, 0);
                break;
                
            case 'b':  // تست ساده بازر
                Serial.println("Simple Buzzer Test");
                playBuzzer(500, 1000);
                break;
                
            case 'f':  // تست فرکانس‌های مختلف
                Serial.println("Buzzer Frequency Test");
                Serial.println("Testing frequencies: 200, 500, 1000, 1500, 2000 Hz");
                
                tone(BUZZER_PIN, 200, 300);
                delay(400);
                tone(BUZZER_PIN, 500, 300);
                delay(400);
                tone(BUZZER_PIN, 1000, 300);
                delay(400);
                tone(BUZZER_PIN, 1500, 300);
                delay(400);
                tone(BUZZER_PIN, 2000, 300);
                delay(400);
                noTone(BUZZER_PIN);
                break;
                
            case 'a':  // تست حالت اکتیو
                Serial.println("Testing ACTIVE buzzer mode (digital)");
                for (int i = 0; i < 5; i++) {
                    digitalWrite(BUZZER_PIN, HIGH);
                    delay(100);
                    digitalWrite(BUZZER_PIN, LOW);
                    delay(100);
                }
                break;
                
            case 'p':  // تست حالت پسیو
                Serial.println("Testing PASSIVE buzzer mode (tone)");
                tone(BUZZER_PIN, 1000, 1000);
                delay(1100);
                noTone(BUZZER_PIN);
                break;
                
            case 's':  // وضعیت
                Serial.println("\n=== STATUS ===");
                Serial.println("Max Brightness: " + String(MAX_BRIGHTNESS));
                Serial.println("RGB1 Pins: 32,33,25");
                Serial.println("RGB2 Pins: 26,14,12");
                Serial.println("LED Pins: 22,21,19,27");
                Serial.println("Buzzer Pin: 13");
                Serial.println("TFT BL Pin: 5");
                #ifdef BUZZER_TYPE_PASSIVE
                Serial.println("Buzzer Type: Passive (using tone())");
                #else
                Serial.println("Buzzer Type: Active (using digitalWrite())");
                #endif
                Serial.println("=== END STATUS ===");
                break;
        }
    }
    
    // بروزرسانی نمایشگر
    static unsigned long lastUpdate = 0;
    if (millis() - lastUpdate > 3000) {
        lastUpdate = millis();
        updateTFTDisplay();
        
        // چشمک زدن LEDها
        static bool ledState = false;
        ledState = !ledState;
        digitalWrite(LED_GREEN_1, ledState);
        digitalWrite(LED_RED_2, ledState);
    }
    
    delay(100);
}