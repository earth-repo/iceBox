// =============================================
// firebase_link_example.ino — ตัวอย่างการเรียก wifi_setup.h + firebase_link.h
// + telegram_link.h
// แสดงว่าเหตุการณ์ไหนต้องส่งค่าอะไรไป Telegram และ Firebase
// =============================================
//
// Libraries ที่ต้องติดตั้ง:
//   1. WiFiManager (by tzapu) — จาก Library Manager
//
// ก่อน compile: คัดลอก config.h มาไว้ในโฟลเดอร์นี้
// Board: ESP32 Dev Module
// =============================================

#include "firebase_link.h"
#include "telegram_link.h"
#include "wifi_setup.h"

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

// ส่งสถานะปัจจุบันทั้งหมด (ประตู: LOW = เปิด)
void sendStatus() {
  fbSendStatus(parcelCount, boxStatus, digitalRead(PIN_INPUT_DOOR) == LOW,
               digitalRead(PIN_OUTPUT_DOOR) == LOW);
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_INPUT_DOOR, INPUT_PULLUP);
  pinMode(PIN_OUTPUT_DOOR, INPUT_PULLUP);
  pinMode(PIN_COUNT_SS, INPUT_PULLUP);
  pinMode(PIN_MAX_SS, INPUT_PULLUP);
  pinMode(PIN_RESET_SW, INPUT_PULLUP);

  // เริ่มนับก่อนต่อ WiFi — พัสดุที่มาระหว่างรอหน้าตั้งค่าจะยังถูกนับ
  attachInterrupt(digitalPinToInterrupt(PIN_COUNT_SS), onCountSensor, FALLING);

  // กดปุ่มรีเซ็ตค้างไว้ตอนเปิดเครื่อง = เปิดหน้าตั้งค่า WiFi + Telegram
  wifiBegin(digitalRead(PIN_RESET_SW) == LOW);
  fbBegin();

  // เริ่มระบบ
  String msg = "🟢 <b>ตู้พัสดุอัจฉริยะเริ่มทำงาน</b>\n";
  msg += "📡 WiFi: " + WiFi.SSID() + "\n";
  msg += "📦 จำนวนพัสดุ: " + String(parcelCount) + " ชิ้น";
  tgSend(msg);

  sendStatus();
  fbAddEvent("🟢", "ระบบเริ่มทำงาน (พัสดุ: " + String(parcelCount) + " ชิ้น)");
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
      sendStatus();
    }
  }

  // 3. กดปุ่มรีเซ็ต
  if (resetState == LOW && prevResetState == HIGH) {
    parcelCount = 0;
    boxStatus = 0;

    String msg = "✅ <b>รับพัสดุแล้ว!</b>\n";
    msg += "📦 รีเซ็ตจำนวนพัสดุเป็น 0 ชิ้น\n";
    msg += "🟢 ตู้พร้อมรับพัสดุ";
    tgSend(msg);

    sendStatus();
    fbAddDaily("resets");
    fbAddEvent("✅", "รีเซ็ต — รับพัสดุแล้ว");
  }
  prevResetState = resetState;

  // 4. ประตูรับพัสดุเข้า
  if (inputDoorState == LOW && prevInputDoorState == HIGH) {
    tgSend("🚪 <b>ประตูรับพัสดุเข้า — เปิด</b>");
    sendStatus();
    fbAddEvent("🚪", "ประตูรับพัสดุเข้า — เปิด");
  }
  if (inputDoorState == HIGH && prevInputDoorState == LOW) {
    tgSend("🔒 <b>ประตูรับพัสดุเข้า — ปิด</b>");
    sendStatus();
    fbAddEvent("🔒", "ประตูรับพัสดุเข้า — ปิด");
  }
  prevInputDoorState = inputDoorState;

  // 5. ประตูนำพัสดุออก — ปิดแล้วถือว่านำพัสดุออกหมด → รีเซ็ต
  if (outputDoorState == LOW && prevOutputDoorState == HIGH) {
    tgSend("🚪 <b>ประตูนำพัสดุออก — เปิด</b>");
    sendStatus();
    fbAddEvent("🚪", "ประตูนำพัสดุออก — เปิด");
  }
  if (outputDoorState == HIGH && prevOutputDoorState == LOW) {
    int oldCount = parcelCount;
    parcelCount = 0;
    boxStatus = 0;

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
