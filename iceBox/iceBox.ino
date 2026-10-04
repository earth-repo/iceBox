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
#include <Ticker.h>

int parcelCount = 0; // จำนวนพัสดุในตู้
int boxStatus = 0;   // 0=ว่าง, 1=มีพัสดุ, 2=เต็ม

// Counter sensor อ่านด้วย interrupt
// ระหว่างส่งข้อมูล loop หยุดรอหลายวินาที ถ้าอ่านด้วย digitalRead ใน loop
// พัสดุที่ผ่าน sensor ช่วงนั้นจะไม่ถูกนับ
//   ถูกบังแล้วปล่อยภายใน FULL_DELAY_MS = พัสดุผ่าน 1 ชิ้น
//   ถูกบังค้างถึง FULL_DELAY_MS        = พัสดุกองสูงถึง sensor = ตู้เต็ม (ไม่นับเป็นชิ้น)
volatile int newParcels = 0; // พัสดุที่ผ่าน sensor แล้ว รอ loop รับไปนับ
volatile bool sensorBlocked = false;
volatile unsigned long sensorBlockedAt = 0; // เวลาที่ sensor เริ่มถูกบัง
volatile unsigned long lastReleaseTime = 0; // เวลาที่ sensor กลับมาโล่งครั้งล่าสุด
volatile unsigned long sensorEdges = 0;     // จำนวนครั้งที่สัญญาณ sensor เปลี่ยน (ไว้ดูใน Serial)

bool prevResetState = HIGH;
bool prevInputDoorState = HIGH;
bool prevOutputDoorState = HIGH;

unsigned long lastFirebaseUpdate = 0;

bool parcelWaiting = false;     // มีพัสดุที่นับแล้ว รอตัดสินว่าทำให้ตู้เต็มหรือไม่
unsigned long parcelSeenAt = 0; // เวลาที่เริ่มรอ

Ticker ledTicker;

void IRAM_ATTR onCountSensor() {
  unsigned long now = millis();
  sensorEdges = sensorEdges + 1;

  if (digitalRead(PIN_COUNT_SS) == LOW) { // เริ่มถูกบัง
    if (!sensorBlocked) {
      sensorBlocked = true;
      sensorBlockedAt = now;
    }
    return;
  }

  if (!sensorBlocked)
    return;
  sensorBlocked = false;

  // พัสดุชิ้นเดียวอาจบัง sensor หลายจังหวะ (พลิก เด้ง สายรัดแกว่ง)
  // ถ้าเริ่มถูกบังหลัง sensor เพิ่งโล่งไม่ถึง DEBOUNCE_MS = ยังเป็นชิ้นเดิม — ไม่นับซ้ำ
  bool sameParcel = sensorBlockedAt - lastReleaseTime < DEBOUNCE_MS;
  lastReleaseTime = now;

  // ประตูนำพัสดุออกเปิดอยู่ = กำลังหยิบของออก มืออาจบัง sensor — ไม่นับ
  if (digitalRead(PIN_OUTPUT_DOOR) == LOW)
    return;

  if (now - sensorBlockedAt < FULL_DELAY_MS && !sameParcel)
    newParcels = newParcels + 1;
}

// sensor ถูกบังค้างต่อเนื่องถึง FULL_DELAY_MS หรือยัง
bool sensorFull() {
  if (digitalRead(PIN_COUNT_SS) == HIGH)
    return false;
  unsigned long since = sensorBlockedAt; // อ่านก่อน millis() — ผลลบไม่ติดลบ
  return millis() - since >= FULL_DELAY_MS;
}

// sensor โล่งต่อเนื่องครบ DEBOUNCE_MS หรือยัง = พัสดุชิ้นล่าสุดผ่านพ้น sensor ไปแล้วจริง
bool sensorClear() {
  if (digitalRead(PIN_COUNT_SS) == LOW)
    return false;
  unsigned long since = lastReleaseTime; // อ่านก่อน millis() — ผลลบไม่ติดลบ
  return millis() - since >= DEBOUNCE_MS;
}

// =============================================
// LED หน้าตู้
//   เขียว  = ว่าง
//   เหลือง = มีพัสดุ
//   แดง    = ตู้เต็ม
//   ประตูรับพัสดุเข้าเปิด = แดงกะพริบ (เขียว/เหลืองตามจำนวนพัสดุ)
//   ประตูนำพัสดุออกเปิด = เขียวติด เหลืองดับ แดงกะพริบ
//
// ledTicker เรียกทุก LED_BLINK_MS และอ่าน sensor กับประตูเองโดยตรง
// ไฟจึงเปลี่ยนทันทีแม้ loop กำลังรอส่งข้อมูล
// =============================================
void updateLEDs() {
  static bool blink = false;
  blink = !blink;

  bool full = sensorFull();
  bool hasParcel = (parcelCount > 0 || newParcels > 0);

  bool green = !hasParcel && !full;
  bool yellow = hasParcel;
  bool red = full;

  if (digitalRead(PIN_INPUT_DOOR) == LOW)
    red = blink;

  if (digitalRead(PIN_OUTPUT_DOOR) == LOW) {
    green = true;
    yellow = false;
    red = blink;
  }

  digitalWrite(PIN_GREEN_LED, green);
  digitalWrite(PIN_YELLOW_LED, yellow);
  digitalWrite(PIN_RED_LED, red);
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
  newParcels = 0;
  parcelCount = 0;
  boxStatus = 0;
  saveCount();
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
  pinMode(PIN_RESET_SW, INPUT_PULLUP);

  // กดปุ่มรีเซ็ตค้างไว้ตอนเปิดเครื่อง = เปิดหน้าตั้งค่า WiFi + Telegram
  bool forceSetup = (digitalRead(PIN_RESET_SW) == LOW);

  // LED Test
  digitalWrite(PIN_RED_LED, HIGH);
  delay(500);
  digitalWrite(PIN_RED_LED, LOW);
  digitalWrite(PIN_YELLOW_LED, HIGH);
  delay(500);
  digitalWrite(PIN_YELLOW_LED, LOW);
  digitalWrite(PIN_GREEN_LED, HIGH);
  delay(500);
  digitalWrite(PIN_GREEN_LED, LOW);
  delay(500);

  // โหลดจำนวนพัสดุจาก Flash (กรณีไฟดับแล้วกลับมา)
  parcelCount = loadCount();
  boxStatus = (parcelCount > 0) ? 1 : 0;
  ledTicker.attach_ms(LED_BLINK_MS, updateLEDs);

  // เริ่มนับก่อนต่อ WiFi — พัสดุที่มาระหว่างรอหน้าตั้งค่าจะยังถูกนับ
  attachInterrupt(digitalPinToInterrupt(PIN_COUNT_SS), onCountSensor, CHANGE);

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

  bool resetState = digitalRead(PIN_RESET_SW);
  bool inputDoorState = digitalRead(PIN_INPUT_DOOR);
  bool outputDoorState = digitalRead(PIN_OUTPUT_DOOR);

  // 1. พัสดุมาส่ง (ทีละชิ้น ชิ้นที่เหลือทำในรอบถัดไป)
  // รอให้รู้ก่อนว่าพัสดุชิ้นนี้ทำให้ตู้เต็มหรือไม่ แล้วแจ้งครั้งเดียว
  //   sensor กลับมาโล่ง          = พัสดุผ่านไปแล้ว ตู้ยังไม่เต็ม
  //   sensor ถูกบังค้างจนครบเวลา = พัสดุชิ้นนี้กองถึง sensor = ตู้เต็ม
  if (newParcels == 0) {
    parcelWaiting = false;
  } else if (!parcelWaiting) {
    parcelWaiting = true;
    parcelSeenAt = now;
  }
  // sensor กะพริบไม่หยุดจนตัดสินไม่ได้ — ไม่รอเกินนี้ พัสดุจะได้ไม่ค้างไม่ถูกนับ
  bool waitedOut =
      parcelWaiting && now - parcelSeenAt >= FULL_DELAY_MS + DEBOUNCE_MS;

  if (newParcels > 0 && (sensorClear() || sensorFull() || waitedOut)) {
    parcelWaiting = false;

    // เพิ่ม parcelCount ก่อนลด newParcels — LED เหลืองจะไม่ดับวูบระหว่างสองบรรทัดนี้
    parcelCount++;
    noInterrupts();
    newParcels = newParcels - 1;
    interrupts();

    // ประตูนำพัสดุออกเปิดอยู่ไม่ถือว่าเต็ม — มือที่หยิบของอาจบัง sensor ค้าง
    bool becameFull = boxStatus != 2 && sensorFull() &&
                      digitalRead(PIN_OUTPUT_DOOR) == HIGH;
    if (becameFull)
      boxStatus = 2;
    else if (boxStatus != 2)
      boxStatus = 1;
    saveCount();
    Serial.printf("[SENSOR] Parcel detected! Count = %d\n", parcelCount);
    if (becameFull)
      Serial.println("[SENSOR] Box is FULL!");

    String msg = "📦 <b>มีพัสดุมาส่ง!</b>\n";
    msg += "📊 จำนวนพัสดุในตู้: <b>" + String(parcelCount) + "</b> ชิ้น\n";
    if (boxStatus == 2) {
      msg += "🔴 ตู้พัสดุเต็มแล้ว!\n";
      msg += "⚠️ กรุณามารับพัสดุ";
    } else {
      msg += "🟢 ยังรับพัสดุได้";
    }
    tgSend(msg);

    sendStatus();
    fbAddDaily("count");
    fbAddEvent("📦", "พัสดุมาส่ง (จำนวน: " + String(parcelCount) + " ชิ้น)");
    if (becameFull)
      fbAddEvent("🔴", "ตู้พัสดุเต็ม!");
  }

  // 2. ตู้เต็ม / หายเต็ม
  // ประตูนำพัสดุออกเปิดอยู่ไม่ตรวจ — มือที่หยิบของอาจบัง sensor ค้าง
  if (boxStatus != 2 && sensorFull() &&
      digitalRead(PIN_OUTPUT_DOOR) == HIGH) {
    boxStatus = 2;
    Serial.println("[SENSOR] Box is FULL!");

    String msg = "🔴 <b>ตู้พัสดุเต็มแล้ว!</b>\n";
    msg += "📊 จำนวนพัสดุ: " + String(parcelCount) + " ชิ้น\n";
    msg += "⚠️ กรุณามารับพัสดุ";
    tgSend(msg);

    sendStatus();
    fbAddEvent("🔴", "ตู้พัสดุเต็ม!");
  } else if (boxStatus == 2 && digitalRead(PIN_COUNT_SS) == HIGH) {
    boxStatus = (parcelCount > 0) ? 1 : 0;
    sendStatus();
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
    // ตู้เต็มแล้วยังมีคนเปิด-ปิดประตู = อาจมีคนมาส่งแต่ใส่ไม่ได้ — เตือนซ้ำในข้อความเดียวกัน
    String msg = "🔒 <b>ประตูรับพัสดุเข้า — ปิด</b>";
    if (boxStatus == 2) {
      msg += "\n🔴 ตู้ยังเต็มอยู่ — กรุณามารับพัสดุ";
    }
    tgSend(msg);
    sendStatus();
    fbAddEvent("🔒", "ประตูรับพัสดุเข้า — ปิด");
  }
  prevInputDoorState = inputDoorState;

  // 5. ประตูนำพัสดุออก — เปิดแล้วถือว่านำพัสดุออกหมด → รีเซ็ต
  if (outputDoorState == LOW && prevOutputDoorState == HIGH) {
    Serial.println("[DOOR] Output door OPENED — resetting parcel count");
    int oldCount = parcelCount;
    resetCount();

    String msg = "🚪 <b>ประตูนำพัสดุออก — เปิด</b>\n";
    msg += "✅ นำพัสดุออก " + String(oldCount) + " ชิ้น\n";
    msg += "📦 รีเซ็ตจำนวนพัสดุเป็น 0 ชิ้น";
    tgSend(msg);

    sendStatus();
    fbAddDaily("resets");
    fbAddEvent("🚪", "ประตูนำพัสดุออก — เปิด (นำออก " + String(oldCount) +
                         " ชิ้น → รีเซ็ต)");
  }
  if (outputDoorState == HIGH && prevOutputDoorState == LOW) {
    Serial.println("[DOOR] Output door CLOSED");
    tgSend("🔒 <b>ประตูนำพัสดุออก — ปิด</b>\n🟢 ตู้พร้อมรับพัสดุ");
    sendStatus();
    fbAddEvent("🔒", "ประตูนำพัสดุออก — ปิด");
  }
  prevOutputDoorState = outputDoorState;

  // 6. ส่งสถานะเป็นระยะ
  if (now - lastFirebaseUpdate > FIREBASE_UPDATE_MS) {
    lastFirebaseUpdate = now;
    sendStatus();

    // สัญญาณดิบของ sensor นับพัสดุ — ไว้ตรวจว่าถูกบังนิ่งหรือกะพริบ
    bool blockedNow = digitalRead(PIN_COUNT_SS) == LOW;
    unsigned long since = sensorBlockedAt;
    Serial.printf("[SENSOR] pin=%s blocked=%lums edges=%lu status=%d heap=%u\n",
                  blockedNow ? "LOW(blocked)" : "HIGH(clear)",
                  blockedNow ? millis() - since : 0UL,
                  (unsigned long)sensorEdges, boxStatus, ESP.getFreeHeap());
  }

  delay(10);
}
