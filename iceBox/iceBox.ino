// =============================================
// iceBox.ino — กล่องรับพัสดุอัจฉริยะ + IoT
// Smart Parcel Box with Telegram & Firebase
// *** ใช้บริการฟรีทั้งหมด ***
// =============================================
//
// รวม program_v2 (LED หน้าตู้ + จำจำนวนพัสดุหลังไฟดับ) กับ
// firebase_link_example (หน้าตั้งค่า WiFi/Telegram + นับพัสดุด้วย interrupt)
//
// ทุกไฟล์อยู่ในโฟลเดอร์นี้:
//   iceBox.ino       โปรแกรมหลัก
//   config.h         ค่าตั้งค่าทั้งหมด (สร้างจาก config.h.example)
//   wifi_setup.h     ต่อ WiFi + หน้าตั้งค่าบนมือถือ
//   telegram_link.h  ส่งแจ้งเตือน Telegram
//   firebase_link.h  ส่งข้อมูลขึ้น Firebase (ใช้คู่กับ dashboard.html)
//
// Libraries ที่ต้องติดตั้ง:
//   1. WiFiManager (by tzapu) — จาก Library Manager
//
// Board: ESP32 Dev Module
// =============================================

#include "firebase_link.h"
#include "telegram_link.h"
#include "wifi_setup.h"
#include <Preferences.h>

int parcelCount = 0; // จำนวนพัสดุในตู้
int boxStatus = 0;   // 0=ว่าง, 1=มีพัสดุ, 2=เต็ม

// Counter sensor นับด้วย interrupt
// ระหว่างส่งข้อมูล loop หยุดรอหลายวินาที ถ้าอ่านด้วย digitalRead ใน loop
// พัสดุที่ผ่าน sensor ช่วงนั้นจะไม่ถูกนับ
volatile int newParcels = 0;
volatile unsigned long lastCountTime = 0;

// Max sensor — ต้องถูกบังต่อเนื่อง MAX_SENSOR_DELAY_MS ถึงนับว่าเต็ม
unsigned long maxSensorStartTime = 0;
bool maxSensorActive = false;

bool prevResetState = HIGH;
bool prevInputDoorState = HIGH;
bool prevOutputDoorState = HIGH;

unsigned long lastFirebaseUpdate = 0;

void IRAM_ATTR onCountSensor() {
  unsigned long now = millis();
  if (now - lastCountTime > DEBOUNCE_MS) {
    lastCountTime = now;
    newParcels = newParcels + 1;
  }
}

// =============================================
// LED หน้าตู้ — เขียว=ว่าง, เหลือง=มีพัสดุ, แดง=เต็ม
// =============================================
void updateLEDs() {
  digitalWrite(PIN_GREEN_LED, boxStatus == 0);
  digitalWrite(PIN_YELLOW_LED, boxStatus == 1);
  digitalWrite(PIN_RED_LED, boxStatus == 2);
}

// =============================================
// บันทึก/อ่าน จำนวนพัสดุจาก Flash — ไฟดับแล้วค่าไม่หาย
// =============================================
void saveCount() {
  Preferences prefs;
  prefs.begin("parcelbox", false); // false = read-write
  prefs.putInt("count", parcelCount);
  prefs.end();
  Serial.printf("[FLASH] Saved count = %d\n", parcelCount);
}

int loadCount() {
  Preferences prefs;
  prefs.begin("parcelbox", true); // true = read-only
  int count = prefs.getInt("count", 0);
  prefs.end();
  Serial.printf("[FLASH] Loaded count = %d\n", count);
  return count;
}

// รีเซ็ตจำนวนพัสดุเป็น 0
void resetCount() {
  parcelCount = 0;
  boxStatus = 0;
  saveCount();
  updateLEDs();
}

// ส่งสถานะปัจจุบันทั้งหมด (ประตู: LOW = เปิด)
void sendStatus() {
  fbSendStatus(parcelCount, boxStatus, digitalRead(PIN_INPUT_DOOR) == LOW,
               digitalRead(PIN_OUTPUT_DOOR) == LOW);
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n=============================");
  Serial.println("  Smart Parcel Box v3.0");
  Serial.println("  Telegram + Firebase (FREE)");
  Serial.println("=============================\n");

  pinMode(PIN_RED_LED, OUTPUT);
  pinMode(PIN_YELLOW_LED, OUTPUT);
  pinMode(PIN_GREEN_LED, OUTPUT);
  pinMode(PIN_INPUT_DOOR, INPUT_PULLUP);
  pinMode(PIN_OUTPUT_DOOR, INPUT_PULLUP);
  pinMode(PIN_COUNT_SS, INPUT_PULLUP);
  pinMode(PIN_MAX_SS, INPUT_PULLUP);
  pinMode(PIN_RESET_SW, INPUT_PULLUP);

  // กดปุ่มรีเซ็ตค้างไว้ตอนเปิดเครื่อง = เปิดหน้าตั้งค่า WiFi + Telegram
  bool forceSetup = (digitalRead(PIN_RESET_SW) == LOW);

  // LED Test (เดิมจาก v1)
  digitalWrite(PIN_RED_LED, HIGH);
  delay(300);
  digitalWrite(PIN_RED_LED, LOW);
  digitalWrite(PIN_YELLOW_LED, HIGH);
  delay(300);
  digitalWrite(PIN_YELLOW_LED, LOW);
  digitalWrite(PIN_GREEN_LED, HIGH);
  delay(300);
  digitalWrite(PIN_GREEN_LED, LOW);
  delay(300);

  // โหลดจำนวนพัสดุจาก Flash (กรณีไฟดับแล้วกลับมา)
  parcelCount = loadCount();
  boxStatus = (parcelCount > 0) ? 1 : 0;
  updateLEDs();

  // เริ่มนับก่อนต่อ WiFi — พัสดุที่มาระหว่างรอหน้าตั้งค่าจะยังถูกนับ
  attachInterrupt(digitalPinToInterrupt(PIN_COUNT_SS), onCountSensor, FALLING);

  wifiBegin(forceSetup);
  fbBegin();

  // เริ่มระบบ
  String msg = "🟢 <b>ตู้พัสดุอัจฉริยะเริ่มทำงาน</b>\n";
  msg += "📡 WiFi: " + WiFi.SSID() + "\n";
  msg += "📦 จำนวนพัสดุ: " + String(parcelCount) + " ชิ้น";
  if (parcelCount > 0) {
    msg += "\n♻️ (กู้คืนค่าจาก Flash หลังไฟดับ)";
  }
  tgSend(msg);

  sendStatus();
  fbAddEvent("🟢", "ระบบเริ่มทำงาน (พัสดุ: " + String(parcelCount) + " ชิ้น)");

  Serial.println("[READY] System initialized");
}

void loop() {
  unsigned long now = millis();

  wifiKeep();

  bool maxState = digitalRead(PIN_MAX_SS);
  bool resetState = digitalRead(PIN_RESET_SW);
  bool inputDoorState = digitalRead(PIN_INPUT_DOOR);
  bool outputDoorState = digitalRead(PIN_OUTPUT_DOOR);

  // 1. พัสดุมาส่ง (ทีละชิ้น ชิ้นที่เหลือทำในรอบถัดไป)
  if (newParcels > 0) {
    noInterrupts();
    newParcels = newParcels - 1;
    interrupts();

    parcelCount++;
    if (boxStatus != 2)
      boxStatus = 1;
    saveCount();
    updateLEDs();
    Serial.printf("[SENSOR] Parcel detected! Count = %d\n", parcelCount);

    String msg = "📦 <b>มีพัสดุมาส่ง!</b>\n";
    msg += "📊 จำนวนพัสดุในตู้: <b>" + String(parcelCount) + "</b> ชิ้น\n";
    if (boxStatus == 2) {
      msg += "🔴 ตู้พัสดุเต็มแล้ว!";
    } else {
      msg += "🟢 ยังรับพัสดุได้";
    }
    tgSend(msg);

    sendStatus();
    fbAddDaily("count");
    fbAddEvent("📦", "พัสดุมาส่ง (จำนวน: " + String(parcelCount) + " ชิ้น)");
  }

  // 2. ตู้เต็ม / หายเต็ม
  if (maxState == LOW) {
    if (!maxSensorActive) {
      maxSensorActive = true;
      maxSensorStartTime = now;
    } else if (boxStatus != 2 &&
               (now - maxSensorStartTime >= MAX_SENSOR_DELAY_MS)) {
      boxStatus = 2;
      updateLEDs();
      Serial.println("[SENSOR] Box is FULL!");

      String msg = "🔴 <b>ตู้พัสดุเต็มแล้ว!</b>\n";
      msg += "📊 จำนวนพัสดุ: " + String(parcelCount) + " ชิ้น\n";
      msg += "⚠️ กรุณามารับพัสดุ";
      tgSend(msg);

      sendStatus();
      fbAddEvent("🔴", "ตู้พัสดุเต็ม!");
    }
  } else {
    maxSensorActive = false;
    if (boxStatus == 2) {
      boxStatus = (parcelCount > 0) ? 1 : 0;
      updateLEDs();
      sendStatus();
    }
  }

  // 3. กดปุ่มรีเซ็ต
  if (resetState == LOW && prevResetState == HIGH) {
    delay(50); // debounce
    if (digitalRead(PIN_RESET_SW) == LOW) {
      Serial.println("[RESET] Reset button pressed");
      resetCount();

      String msg = "✅ <b>รับพัสดุแล้ว!</b>\n";
      msg += "📦 รีเซ็ตจำนวนพัสดุเป็น 0 ชิ้น\n";
      msg += "🟢 ตู้พร้อมรับพัสดุ";
      tgSend(msg);

      sendStatus();
      fbAddDaily("resets");
      fbAddEvent("✅", "รีเซ็ต — รับพัสดุแล้ว");
    }
  }
  prevResetState = resetState;

  // 4. ประตูรับพัสดุเข้า
  if (inputDoorState == LOW && prevInputDoorState == HIGH) {
    Serial.println("[DOOR] Input door OPENED");
    tgSend("🚪 <b>ประตูรับพัสดุเข้า — เปิด</b>");
    sendStatus();
    fbAddEvent("🚪", "ประตูรับพัสดุเข้า — เปิด");
  }
  if (inputDoorState == HIGH && prevInputDoorState == LOW) {
    Serial.println("[DOOR] Input door CLOSED");
    tgSend("🔒 <b>ประตูรับพัสดุเข้า — ปิด</b>");
    sendStatus();
    fbAddEvent("🔒", "ประตูรับพัสดุเข้า — ปิด");
  }
  prevInputDoorState = inputDoorState;

  // 5. ประตูนำพัสดุออก — ปิดแล้วถือว่านำพัสดุออกหมด → รีเซ็ต
  if (outputDoorState == LOW && prevOutputDoorState == HIGH) {
    Serial.println("[DOOR] Output door OPENED");
    tgSend("🚪 <b>ประตูนำพัสดุออก — เปิด</b>");
    sendStatus();
    fbAddEvent("🚪", "ประตูนำพัสดุออก — เปิด");
  }
  if (outputDoorState == HIGH && prevOutputDoorState == LOW) {
    Serial.println("[DOOR] Output door CLOSED — resetting parcel count");
    int oldCount = parcelCount;
    resetCount();

    String msg = "🔒 <b>ประตูนำพัสดุออก — ปิด</b>\n";
    msg += "✅ นำพัสดุออกแล้ว " + String(oldCount) + " ชิ้น\n";
    msg += "📦 รีเซ็ตจำนวนพัสดุเป็น 0 ชิ้น\n";
    msg += "🟢 ตู้พร้อมรับพัสดุ";
    tgSend(msg);

    sendStatus();
    fbAddDaily("resets");
    fbAddEvent("🔒", "ประตูนำพัสดุออก — ปิด (นำออก " + String(oldCount) +
                         " ชิ้น → รีเซ็ต)");
  }
  prevOutputDoorState = outputDoorState;

  // 6. ส่งสถานะเป็นระยะ
  if (now - lastFirebaseUpdate > FIREBASE_UPDATE_MS) {
    lastFirebaseUpdate = now;
    sendStatus();
  }

  delay(10);
}
