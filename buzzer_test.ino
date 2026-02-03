#include <Arduino.h>

#define BUZZER_PIN 22  // پین بازر

// ساختار برای ذخیره تنظیمات
struct BuzzerSettings {
    int volume;        // 0 تا 100
    bool enabled;
    int minVolume;
    int maxVolume;
};

BuzzerSettings buzzer = {50, true, 0, 100};  // پیش‌فرض: 50% حجم

// ==================== توابع کنترل بازر ====================

/**
 * تنظیم حجم بازر (0-100)
 */
void setBuzzerVolume(int volume) {
    buzzer.volume = constrain(volume, buzzer.minVolume, buzzer.maxVolume);
    Serial.print("حجم بازر تنظیم شد به: ");
    Serial.print(buzzer.volume);
    Serial.println("%");
}

/**
 * افزایش حجم
 */
void increaseVolume(int step = 10) {
    int newVolume = buzzer.volume + step;
    if (newVolume > buzzer.maxVolume) newVolume = buzzer.maxVolume;
    setBuzzerVolume(newVolume);
    
    // بازخورد صوتی
    playVolumeFeedback();
}

/**
 * کاهش حجم
 */
void decreaseVolume(int step = 10) {
    int newVolume = buzzer.volume - step;
    if (newVolume < buzzer.minVolume) newVolume = buzzer.minVolume;
    setBuzzerVolume(newVolume);
    
    // بازخورد صوتی
    playVolumeFeedback();
}

/**
 * فعال/غیرفعال کردن بازر
 */
void toggleBuzzer() {
    buzzer.enabled = !buzzer.enabled;
    Serial.print("بازر ");
    Serial.println(buzzer.enabled ? "فعال شد" : "غیرفعال شد");
    
    // بازخورد صوتی
    if (buzzer.enabled) {
        tone(BUZZER_PIN, 1000, 100);
        delay(120);
        tone(BUZZER_PIN, 1200, 100);
    }
}

/**
 * پخش صدا با حجم تنظیم شده
 */
void playTone(int frequency, int durationMs) {
    if (!buzzer.enabled || buzzer.volume == 0) {
        return;
    }
    
    // محاسبه مدت زمان واقعی بر اساس حجم
    int actualDuration = map(buzzer.volume, 0, 100, 0, durationMs);
    
    if (actualDuration <= 0) {
        return;
    }
    
    // روش 1: برای حجم‌های پایین - پالس‌های کوتاه
    if (buzzer.volume < 30) {
        int pulseCount = actualDuration / 30;
        int pulseDuration = 20;
        int pauseDuration = 30 - pulseDuration;
        
        for (int i = 0; i < pulseCount; i++) {
            tone(BUZZER_PIN, frequency, pulseDuration);
            delay(pauseDuration);
        }
    }
    // روش 2: برای حجم‌های متوسط
    else if (buzzer.volume < 70) {
        tone(BUZZER_PIN, frequency, actualDuration);
        delay(actualDuration + 10);
    }
    // روش 3: برای حجم‌های بالا - کامل
    else {
        tone(BUZZER_PIN, frequency, durationMs);
        delay(durationMs + 10);
    }
}

/**
 * پخش ملودی با حجم تنظیم شده
 */
void playMelody() {
    Serial.println("🎵 پخش ملودی با حجم فعلی...");
    
    // نت‌های ملودی ساده
    int melody[] = {262, 294, 330, 349, 392, 440, 494, 523};
    int durations[] = {200, 200, 200, 200, 300, 300, 200, 400};
    
    for (int i = 0; i < 8; i++) {
        playTone(melody[i], durations[i]);
        delay(50);
    }
}

/**
 * بازخورد صوتی برای تغییر حجم
 */
void playVolumeFeedback() {
    if (!buzzer.enabled) return;
    
    // فرکانس بر اساس حجم (هر چه بلندتر، فرکانس بالاتر)
    int freq = map(buzzer.volume, 0, 100, 300, 1500);
    
    // مدت زمان بر اساس حجم
    int duration = map(buzzer.volume, 0, 100, 50, 200);
    
    tone(BUZZER_PIN, freq, duration);
    delay(duration + 20);
}

/**
 * تست محدوده حجم
 */
void testVolumeRange() {
    Serial.println("\n🔊 تست محدوده حجم (0-100%):");
    
    for (int vol = 0; vol <= 100; vol += 10) {
        setBuzzerVolume(vol);
        Serial.print("حجم: ");
        Serial.print(vol);
        Serial.print("% | ");
        
        // تست با سه فرکانس مختلف
        if (vol > 0) {
            playTone(440, 100);  // لا
            delay(150);
            playTone(523, 100);  // دو
            delay(150);
            playTone(659, 100);  // می
        }
        
        delay(300);
        Serial.println("✅");
    }
    
    // بازگشت به حجم 50%
    setBuzzerVolume(50);
}

/**
 * نمایش منو
 */
void showMenu() {
    Serial.println("\n════════════════════════════════════════");
    Serial.println("         🎵 کنترل‌کننده صدای بازر 🎵");
    Serial.println("════════════════════════════════════════");
    Serial.println("دستورات:");
    Serial.println("  +   : افزایش حجم (+10%)");
    Serial.println("  ++  : افزایش حجم (+20%)");
    Serial.println("  -   : کاهش حجم (-10%)");
    Serial.println("  --  : کاهش حجم (-20%)");
    Serial.println("  0-9 : تنظیم مستقیم حجم (0=0%, 9=90%)");
    Serial.println("  a   : تنظیم حجم 100%");
    Serial.println("  m   : پخش ملودی");
    Serial.println("  t   : تست محدوده حجم");
    Serial.println("  o   : خاموش/روشن کردن بازر");
    Serial.println("  s   : نمایش وضعیت");
    Serial.println("  h   : نمایش این منو");
    Serial.println("════════════════════════════════════════");
    Serial.print("> ");
}

/**
 * نمایش وضعیت فعلی
 */
void showStatus() {
    Serial.println("\n📊 وضعیت فعلی:");
    Serial.println("════════════════════════════");
    Serial.print("  پین بازر: GPIO");
    Serial.println(BUZZER_PIN);
    Serial.print("  وضعیت: ");
    Serial.println(buzzer.enabled ? "✅ فعال" : "❌ غیرفعال");
    Serial.print("  حجم: ");
    Serial.print(buzzer.volume);
    Serial.println("%");
    
    // نمایش نوار پیشرفت حجم
    Serial.print("  [");
    int bars = map(buzzer.volume, 0, 100, 0, 20);
    for (int i = 0; i < 20; i++) {
        if (i < bars) Serial.print("█");
        else Serial.print("░");
    }
    Serial.println("]");
    Serial.println("════════════════════════════");
}

// ==================== Setup و Loop ====================

void setup() {
    Serial.begin(115200);
    delay(2000);  // منتظر باز شدن Serial Monitor
    
    Serial.println("\n✨ برنامه کنترل صدای بازر ESP32 ✨");
    Serial.println("نسخه: 2.0 - کنترل کامل حجم صدا");
    
    // راه‌اندازی بازر
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
    
    // تست اولیه
    Serial.println("\n🔊 تست اولیه...");
    tone(BUZZER_PIN, 1000, 200);
    delay(300);
    tone(BUZZER_PIN, 1500, 200);
    delay(300);
    
    Serial.println("✅ سیستم آماده است!");
    
    showMenu();
    showStatus();
}

void loop() {
    if (Serial.available() > 0) {
        String command = Serial.readStringUntil('\n');
        command.trim();
        
        Serial.print("📥 دستور: ");
        Serial.println(command);
        
        if (command == "+") {
            increaseVolume(10);
        }
        else if (command == "++") {
            increaseVolume(20);
        }
        else if (command == "-") {
            decreaseVolume(10);
        }
        else if (command == "--") {
            decreaseVolume(20);
        }
        else if (command == "0") { setBuzzerVolume(0); playVolumeFeedback(); }
        else if (command == "1") { setBuzzerVolume(10); playVolumeFeedback(); }
        else if (command == "2") { setBuzzerVolume(20); playVolumeFeedback(); }
        else if (command == "3") { setBuzzerVolume(30); playVolumeFeedback(); }
        else if (command == "4") { setBuzzerVolume(40); playVolumeFeedback(); }
        else if (command == "5") { setBuzzerVolume(50); playVolumeFeedback(); }
        else if (command == "6") { setBuzzerVolume(60); playVolumeFeedback(); }
        else if (command == "7") { setBuzzerVolume(70); playVolumeFeedback(); }
        else if (command == "8") { setBuzzerVolume(80); playVolumeFeedback(); }
        else if (command == "9") { setBuzzerVolume(90); playVolumeFeedback(); }
        else if (command == "a") { setBuzzerVolume(100); playVolumeFeedback(); }
        else if (command == "m") {
            playMelody();
        }
        else if (command == "t") {
            testVolumeRange();
        }
        else if (command == "o") {
            toggleBuzzer();
        }
        else if (command == "s") {
            showStatus();
        }
        else if (command == "h") {
            showMenu();
        }
        else if (command == "test") {
            // تست ویژه: پخش با حجم‌های مختلف
            Serial.println("🎚️ تست تغییر تدریجی حجم:");
            for (int i = 0; i <= 10; i++) {
                int vol = i * 10;
                setBuzzerVolume(vol);
                Serial.print("حجم: ");
                Serial.print(vol);
                Serial.println("% - بوق!");
                
                tone(BUZZER_PIN, 800, 200);
                delay(300);
            }
            setBuzzerVolume(50);
        }
        else if (command.length() > 0) {
            Serial.println("❌ دستور نامعرف! برای راهنما 'h' را وارد کنید");
        }
        
        Serial.print("\n> ");
    }
}