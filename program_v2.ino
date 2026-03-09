// =============================================
// program_v2.ino — กล่องรับพัสดุอัจฉริยะ + IoT
// Smart Parcel Box with Telegram & Firebase
// *** ใช้บริการฟรีทั้งหมด ***
// =============================================
//
// Libraries ที่ต้องติดตั้ง:
//   1. ArduinoJson (by Benoit Blanchon) — จาก Library Manager
//   *** ไม่ต้องติดตั้ง library อื่นเพิ่ม ***
//   WiFi, HTTPClient มาพร้อม ESP32 Board Package
//
// Board: ESP32 Dev Module
// =============================================

#include "config.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>

// =============================================
// Flash Memory (NVS) — เก็บค่าแม้ไฟดับ
// =============================================
Preferences prefs;

// =============================================
// ตัวแปร Global
// =============================================
int parcelCount = 0; // จำนวนพัสดุในตู้
int boxStatus = 0;   // 0=ว่าง, 1=มีพัสดุ, 2=เต็ม

unsigned long lastCountTime = 0;
unsigned long lastFirebaseUpdate = 0;
unsigned long lastTelegramSend = 0;
unsigned long lastWiFiRetry = 0;

// Max sensor — 10-second sustained trigger
unsigned long maxSensorStartTime = 0;
bool maxSensorActive = false;
bool maxSensorTriggered = false;
#define MAX_SENSOR_DELAY_MS 5000 // 5 วินาที

bool prevCountState = HIGH;
bool prevMaxState = HIGH;
bool prevResetState = HIGH;
bool prevInputDoorState = HIGH;
bool prevOutputDoorState = HIGH;

// =============================================
// Telegram Bot — ส่งข้อความแจ้งเตือน
// =============================================
void sendTelegram(String message) {
  unsigned long now = millis();
  if (now - lastTelegramSend < TELEGRAM_COOLDOWN_MS)
    return;
  lastTelegramSend = now;

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[TG] WiFi not connected, skipping...");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure(); // ข้าม SSL verify (สำหรับ ESP32)

  HTTPClient http;
  String url = "https://api.telegram.org/bot" + String(TELEGRAM_BOT_TOKEN) +
               "/sendMessage";
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");

  // สร้าง JSON payload
  JsonDocument doc;
  doc["chat_id"] = TELEGRAM_CHAT_ID;
  doc["text"] = message;
  doc["parse_mode"] = "HTML";

  String payload;
  serializeJson(doc, payload);

  int httpCode = http.POST(payload);

  if (httpCode > 0) {
    Serial.printf("[TG] Sent OK (HTTP %d)\n", httpCode);
  } else {
    Serial.printf("[TG] Error: %s\n", http.errorToString(httpCode).c_str());
  }
  http.end();
}

// =============================================
// Firebase — อัปเดตข้อมูล Real-time (ฟรี Spark Plan)
// =============================================
void updateFirebase() {
  if (WiFi.status() != WL_CONNECTED)
    return;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  String url = "https://" + String(FIREBASE_HOST) +
               "/parcelBox.json?auth=" + String(FIREBASE_API_KEY);
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");

  // สร้าง JSON ข้อมูลทั้งหมด
  JsonDocument doc;
  doc["parcelCount"] = parcelCount;
  doc["boxStatus"] = boxStatus;

  JsonObject leds = doc["leds"].to<JsonObject>();
  leds["red"] = (boxStatus == 2) ? 1 : 0;
  leds["yellow"] = (boxStatus == 1) ? 1 : 0;
  leds["green"] = (boxStatus == 0) ? 1 : 0;

  // สถานะประตู
  JsonObject doors = doc["doors"].to<JsonObject>();
  doors["input"] = (digitalRead(PIN_INPUT_DOOR) == LOW) ? 1 : 0;
  doors["output"] = (digitalRead(PIN_OUTPUT_DOOR) == LOW) ? 1 : 0;

  // สร้าง timestamp
  doc["lastUpdate"] = millis() / 1000;

  // สร้างข้อความสถานะ
  switch (boxStatus) {
  case 0:
    doc["statusText"] = "ตู้ว่าง — พร้อมรับพัสดุ";
    break;
  case 1:
    doc["statusText"] = "มีพัสดุอยู่ในตู้";
    break;
  case 2:
    doc["statusText"] = "ตู้เต็ม — กรุณามารับพัสดุ";
    break;
  }

  String payload;
  serializeJson(doc, payload);

  int httpCode = http.PATCH(payload);

  if (httpCode > 0) {
    Serial.printf("[FB] Updated OK (HTTP %d)\n", httpCode);
  } else {
    Serial.printf("[FB] Error: %s\n", http.errorToString(httpCode).c_str());
  }
  http.end();
}

// Firebase — เพิ่ม event log
void addFirebaseEvent(String icon, String text) {
  if (WiFi.status() != WL_CONNECTED)
    return;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  String url = "https://" + String(FIREBASE_HOST) +
               "/parcelBox/events.json?auth=" + String(FIREBASE_API_KEY);
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");

  JsonDocument doc;
  doc["icon"] = icon;
  doc["text"] = text;
  doc["timestamp"] = millis() / 1000;

  String payload;
  serializeJson(doc, payload);

  // POST = push (auto-generate key)
  int httpCode = http.POST(payload);
  if (httpCode > 0) {
    Serial.printf("[FB] Event logged (HTTP %d)\n", httpCode);
  }
  http.end();
}

// =============================================
// NTP — ดึงวันที่จริง
// =============================================
String getDateString() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("[NTP] Failed to get time");
    return "unknown";
  }
  char buf[11]; // YYYY-MM-DD
  strftime(buf, sizeof(buf), "%Y-%m-%d", &timeinfo);
  return String(buf);
}

// =============================================
// สถิติรายวัน — อัปเดตไปที่ Firebase
// =============================================
void updateDailyStats(String eventType) {
  if (WiFi.status() != WL_CONNECTED)
    return;

  String dateStr = getDateString();
  if (dateStr == "unknown")
    return;

  WiFiClientSecure client;
  client.setInsecure();

  // 1. อ่านค่าเดิม
  HTTPClient http;
  String url = "https://" + String(FIREBASE_HOST) + "/parcelBox/stats/daily/" +
               dateStr + ".json?auth=" + String(FIREBASE_API_KEY);
  http.begin(client, url);
  int httpCode = http.GET();

  int count = 0;
  int resets = 0;

  if (httpCode == 200) {
    String payload = http.getString();
    if (payload != "null") {
      JsonDocument doc;
      deserializeJson(doc, payload);
      count = doc["count"] | 0;
      resets = doc["resets"] | 0;
    }
  }
  http.end();

  // 2. Increment
  if (eventType == "arrive") {
    count++;
  } else if (eventType == "reset") {
    resets++;
  }

  // 3. เขียนกลับ
  HTTPClient http2;
  http2.begin(client, url);
  http2.addHeader("Content-Type", "application/json");

  JsonDocument doc2;
  doc2["count"] = count;
  doc2["resets"] = resets;

  String body;
  serializeJson(doc2, body);

  int putCode = http2.PUT(body);
  if (putCode > 0) {
    Serial.printf("[STATS] %s updated (%s: count=%d, resets=%d)\n",
                  dateStr.c_str(), eventType.c_str(), count, resets);
  } else {
    Serial.printf("[STATS] Error: %s\n", http2.errorToString(putCode).c_str());
  }
  http2.end();
}

// =============================================
// ฟังก์ชัน LED (เดิมจาก v1)
// =============================================
void red_on() { digitalWrite(PIN_RED_LED, HIGH); }
void red_off() { digitalWrite(PIN_RED_LED, LOW); }
void yellow_on() { digitalWrite(PIN_YELLOW_LED, HIGH); }
void yellow_off() { digitalWrite(PIN_YELLOW_LED, LOW); }
void green_on() { digitalWrite(PIN_GREEN_LED, HIGH); }
void green_off() { digitalWrite(PIN_GREEN_LED, LOW); }

// =============================================
// อัปเดต LED ตามสถานะ
// =============================================
void updateLEDs() {
  switch (boxStatus) {
  case 0: // ว่าง
    green_on();
    yellow_off();
    red_off();
    break;
  case 1: // มีพัสดุ
    green_off();
    yellow_on();
    red_off();
    break;
  case 2: // เต็ม
    green_off();
    yellow_off();
    red_on();
    break;
  }
}

// =============================================
// อัปเดตสถานะตู้
// =============================================
void updateBoxStatus() {
  bool isFull = (digitalRead(PIN_MAX_SS) == LOW);

  if (isFull) {
    boxStatus = 2;
  } else if (parcelCount > 0) {
    boxStatus = 1;
  } else {
    boxStatus = 0;
  }
}

// =============================================
// บันทึก/อ่าน จำนวนพัสดุจาก Flash
// =============================================
void saveCountToFlash() {
  prefs.begin("parcelbox", false); // false = read-write
  prefs.putInt("count", parcelCount);
  prefs.end();
  Serial.printf("[FLASH] Saved count = %d\n", parcelCount);
}

int loadCountFromFlash() {
  prefs.begin("parcelbox", true);       // true = read-only
  int count = prefs.getInt("count", 0); // default = 0
  prefs.end();
  Serial.printf("[FLASH] Loaded count = %d\n", count);
  return count;
}

// =============================================
// รีเซ็ตจำนวนพัสดุ
// =============================================
void resetParcelCount() {
  parcelCount = 0;
  boxStatus = 0;
  saveCountToFlash(); // บันทึก 0 ลง Flash
  yellow_off();
  updateLEDs();
  updateFirebase();
  Serial.println("[RESET] Parcel count = 0");
}

// =============================================
// เชื่อมต่อ WiFi
// =============================================
void connectWiFi() {
  Serial.printf("[WiFi] Connecting to %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 20) {
    delay(500);
    Serial.print(".");
    digitalWrite(PIN_GREEN_LED, retries % 2);
    retries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[WiFi] Connected! IP: %s\n",
                  WiFi.localIP().toString().c_str());
    green_on();
  } else {
    Serial.println("\n[WiFi] Connection FAILED! Continuing offline...");
    for (int i = 0; i < 3; i++) {
      red_on();
      delay(200);
      red_off();
      delay(200);
    }
  }
}

// =============================================
// SETUP
// =============================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n=============================");
  Serial.println("  Smart Parcel Box v2.0");
  Serial.println("  Telegram + Firebase (FREE)");
  Serial.println("=============================\n");

  // ตั้งค่า Pin (เดิมจาก v1)
  pinMode(PIN_RED_LED, OUTPUT);
  pinMode(PIN_YELLOW_LED, OUTPUT);
  pinMode(PIN_GREEN_LED, OUTPUT);
  pinMode(PIN_INPUT_DOOR, INPUT_PULLUP);
  pinMode(PIN_OUTPUT_DOOR, INPUT_PULLUP);
  pinMode(PIN_COUNT_SS, INPUT_PULLUP);
  pinMode(PIN_MAX_SS, INPUT_PULLUP);
  pinMode(PIN_RESET_SW, INPUT_PULLUP);

  // LED Test (เดิมจาก v1)
  red_off();
  yellow_off();
  green_off();
  red_on();
  delay(300);
  red_off();
  yellow_on();
  delay(300);
  yellow_off();
  green_on();
  delay(300);
  green_off();
  delay(300);

  // เชื่อมต่อ WiFi
  connectWiFi();

  // ตั้งเวลา NTP (ใช้ timezone ไทย UTC+7)
  configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  Serial.println("[NTP] Syncing time...");
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 5000)) {
    Serial.printf("[NTP] Time: %04d-%02d-%02d %02d:%02d:%02d\n",
                  timeinfo.tm_year + 1900, timeinfo.tm_mon + 1,
                  timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min,
                  timeinfo.tm_sec);
  } else {
    Serial.println("[NTP] Time sync failed (will retry later)");
  }

  // โหลดจำนวนพัสดุจาก Flash (กรณีไฟดับแล้วกลับมา)
  parcelCount = loadCountFromFlash();
  updateBoxStatus();
  updateLEDs();

  // ส่ง Telegram แจ้งระบบเริ่มทำงาน
  if (WiFi.status() == WL_CONNECTED) {
    String msg = "🟢 <b>ตู้พัสดุอัจฉริยะเริ่มทำงาน</b>\n";
    msg += "📡 WiFi: " + String(WIFI_SSID) + "\n";
    msg += "📦 จำนวนพัสดุ: " + String(parcelCount) + " ชิ้น";
    if (parcelCount > 0) {
      msg += "\n♻️ (กู้คืนค่าจาก Flash หลังไฟดับ)";
    }
    sendTelegram(msg);

    // อัปเดต Firebase ครั้งแรก
    updateFirebase();
    addFirebaseEvent("🟢",
                     "ระบบเริ่มทำงาน (พัสดุ: " + String(parcelCount) + " ชิ้น)");
  }

  Serial.println("[READY] System initialized");
}

// =============================================
// LOOP
// =============================================
void loop() {
  unsigned long now = millis();

  // --- WiFi Reconnect ---
  if (WiFi.status() != WL_CONNECTED) {
    if (now - lastWiFiRetry > WIFI_RETRY_MS) {
      lastWiFiRetry = now;
      Serial.println("[WiFi] Reconnecting...");
      WiFi.reconnect();
    }
  }

  // --- อัปเดต Firebase เป็นระยะ ---
  if (now - lastFirebaseUpdate > FIREBASE_UPDATE_MS) {
    lastFirebaseUpdate = now;
    updateFirebase();
  }

  // --- อ่านค่า Sensor ---
  bool countState = digitalRead(PIN_COUNT_SS);
  bool maxState = digitalRead(PIN_MAX_SS);
  bool resetState = digitalRead(PIN_RESET_SW);

  // =============================================
  // 1. Counter Sensor — นับพัสดุ (falling edge + debounce)
  // =============================================
  if (countState == LOW && prevCountState == HIGH) {
    if (now - lastCountTime > DEBOUNCE_MS) {
      lastCountTime = now;
      parcelCount++;
      saveCountToFlash(); // บันทึกลง Flash ทันที
      Serial.printf("[SENSOR] Parcel detected! Count = %d\n", parcelCount);

      updateBoxStatus();
      updateLEDs();

      // แจ้ง Telegram
      String msg = "📦 <b>มีพัสดุมาส่ง!</b>\n";
      msg += "📊 จำนวนพัสดุในตู้: <b>" + String(parcelCount) + "</b> ชิ้น\n";
      if (boxStatus == 2) {
        msg += "🔴 ตู้พัสดุเต็มแล้ว!";
      } else {
        msg += "🟢 ยังรับพัสดุได้";
      }
      sendTelegram(msg);

      // อัปเดต Firebase + Event + Stats
      updateFirebase();
      updateDailyStats("arrive");
      addFirebaseEvent("📦", "พัสดุมาส่ง (จำนวน: " + String(parcelCount) + " ชิ้น)");
    }
  }
  prevCountState = countState;

  // =============================================
  // 2. Max Sensor — ตรวจจับเต็ม (รอ 10 วินาทีก่อนแจ้ง)
  // =============================================
  if (maxState == LOW) {
    if (!maxSensorActive) {
      // เริ่มจับเวลา
      maxSensorActive = true;
      maxSensorStartTime = now;
      Serial.println("[SENSOR] Max sensor triggered — waiting 10s...");
    } else if (!maxSensorTriggered &&
               (now - maxSensorStartTime >= MAX_SENSOR_DELAY_MS)) {
      // ผ่าน 10 วินาทีแล้ว → แจ้งตู้เต็ม
      maxSensorTriggered = true;
      Serial.println("[SENSOR] Box is FULL! (sustained 10s)");
      boxStatus = 2;
      updateLEDs();

      String msg = "🔴 <b>ตู้พัสดุเต็มแล้ว!</b>\n";
      msg += "📊 จำนวนพัสดุ: " + String(parcelCount) + " ชิ้น\n";
      msg += "⚠️ กรุณามารับพัสดุ";
      sendTelegram(msg);

      updateFirebase();
      addFirebaseEvent("🔴", "ตู้พัสดุเต็ม!");
    }
  } else {
    // sensor ไม่ถูกบังแล้ว → reset timer
    if (maxSensorActive) {
      maxSensorActive = false;
      if (maxSensorTriggered) {
        maxSensorTriggered = false;
        updateBoxStatus();
        updateLEDs();
        updateFirebase();
      }
    }
  }

  // =============================================
  // 2.5 Door Sensors — แจ้งเตือนเปิด/ปิดประตู
  // =============================================
  bool inputDoorState = digitalRead(PIN_INPUT_DOOR);
  bool outputDoorState = digitalRead(PIN_OUTPUT_DOOR);

  // ประตูรับพัสดุเข้า
  if (inputDoorState == LOW && prevInputDoorState == HIGH) {
    Serial.println("[DOOR] Input door OPENED");
    sendTelegram("🚪 <b>ประตูรับพัสดุเข้า — เปิด</b>");
    updateFirebase();
    addFirebaseEvent("🚪", "ประตูรับพัสดุเข้า — เปิด");
  }
  if (inputDoorState == HIGH && prevInputDoorState == LOW) {
    Serial.println("[DOOR] Input door CLOSED");
    sendTelegram("🔒 <b>ประตูรับพัสดุเข้า — ปิด</b>");
    updateFirebase();
    addFirebaseEvent("🔒", "ประตูรับพัสดุเข้า — ปิด");
  }
  prevInputDoorState = inputDoorState;

  // ประตูนำพัสดุออก
  if (outputDoorState == LOW && prevOutputDoorState == HIGH) {
    Serial.println("[DOOR] Output door OPENED");
    sendTelegram("🚪 <b>ประตูนำพัสดุออก — เปิด</b>");
    updateFirebase();
    addFirebaseEvent("🚪", "ประตูนำพัสดุออก — เปิด");
  }
  if (outputDoorState == HIGH && prevOutputDoorState == LOW) {
    // ประตูนำพัสดุออก ปิดแล้ว → นำพัสดุออกทั้งหมด → รีเซ็ต
    Serial.println("[DOOR] Output door CLOSED — resetting parcel count");
    int oldCount = parcelCount;
    resetParcelCount(); // รีเซ็ตจำนวนเป็น 0

    String msg = "🔒 <b>ประตูนำพัสดุออก — ปิด</b>\n";
    msg += "✅ นำพัสดุออกแล้ว " + String(oldCount) + " ชิ้น\n";
    msg += "📦 รีเซ็ตจำนวนพัสดุเป็น 0 ชิ้น\n";
    msg += "🟢 ตู้พร้อมรับพัสดุ";
    sendTelegram(msg);

    updateDailyStats("reset");
    addFirebaseEvent("🔒", "ประตูนำพัสดุออก — ปิด (นำออก " + String(oldCount) +
                               " ชิ้น → รีเซ็ต)");
  }
  prevOutputDoorState = outputDoorState;

  // =============================================
  // 3. Reset Switch — รีเซ็ต (physical button)
  // =============================================
  if (resetState == LOW && prevResetState == HIGH) {
    delay(50); // debounce
    if (digitalRead(PIN_RESET_SW) == LOW) {
      Serial.println("[RESET] Physical reset button pressed");
      resetParcelCount();

      String msg = "✅ <b>รับพัสดุแล้ว!</b>\n";
      msg += "📦 รีเซ็ตจำนวนพัสดุเป็น 0 ชิ้น\n";
      msg += "🟢 ตู้พร้อมรับพัสดุ";
      sendTelegram(msg);

      updateDailyStats("reset");
      addFirebaseEvent("✅", "รีเซ็ต — รับพัสดุแล้ว");

      while (digitalRead(PIN_RESET_SW) == LOW) {
        delay(10);
      }
      delay(50);
    }
  }
  prevResetState = resetState;

  // --- Small delay เพื่อลด CPU load ---
  delay(10);
}
