// =============================================
// firebase_link.h — เชื่อม ESP32 กับ Firebase Realtime Database
// กล่องรับพัสดุอัจฉริยะ (ใช้คู่กับ dashboard.html)
// =============================================
//
// วิธีใช้:
//   1. วางไฟล์นี้กับ config.h ไว้ในโฟลเดอร์เดียวกับ sketch
//   2. #include "firebase_link.h"
//   3. ต่อ WiFi ก่อน (wifiBegin() ใน wifi_setup.h ต่อให้)
//   4. เรียก fbBegin() ใน setup() แล้วเรียกฟังก์ชันส่งค่าตามเหตุการณ์
//
// ไม่ต้องติดตั้ง library เพิ่ม — WiFi, HTTPClient มากับ ESP32 Board Package
//
// ---------------------------------------------
// ค่าที่ "ส่ง" ไป Firebase
// ---------------------------------------------
//   fbSendStatus()  PATCH /parcelBox
//     parcelCount   จำนวนพัสดุในตู้ (int)
//     boxStatus     0=ว่าง, 1=มีพัสดุ, 2=เต็ม
//     statusText    ข้อความสถานะ (สร้างจาก boxStatus ให้เอง)
//     leds          {red, yellow, green} 0/1 (สร้างจาก boxStatus ให้เอง)
//     doors         {input, output} 0=ปิด, 1=เปิด
//     lastUpdate    เวลา Unix (วินาที)
//
//   fbAddEvent()    POST /parcelBox/events
//     icon          emoji 1 ตัว เช่น "📦"
//     text          ข้อความเหตุการณ์
//     timestamp     เวลา Unix (วินาที)
//
//   fbAddDaily()    PUT /parcelBox/stats/daily/YYYY-MM-DD/<field>
//     count         จำนวนพัสดุที่มาส่งในวันนั้น
//     resets        จำนวนครั้งที่รีเซ็ตในวันนั้น
//
// ---------------------------------------------
// ค่าที่ "รับ" จาก Firebase
// ---------------------------------------------
//   มีจุดเดียว: fbAddDaily() อ่านค่าเดิมของวันนั้นก่อน แล้วบวก 1 เขียนกลับ
//     GET /parcelBox/stats/daily/YYYY-MM-DD/<field>  → ได้ตัวเลข หรือ null
//   Dashboard ไม่ได้ส่งคำสั่งกลับมาที่ ESP32
// =============================================

#ifndef FIREBASE_LINK_H
#define FIREBASE_LINK_H

#include "config.h"
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>

// =============================================
// ตั้งเวลา NTP (เรียกครั้งเดียวใน setup หลังต่อ WiFi)
// =============================================
void fbBegin() {
  // เวลาไทย UTC+7 — ใช้ทำ timestamp และวันที่ของสถิติรายวัน
  configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 5000)) {
    Serial.println("[NTP] Time sync failed (will retry later)");
  }
}

// เวลา Unix (วินาที) — ได้ 0 ถ้ายัง sync NTP ไม่สำเร็จ
unsigned long fbNow() {
  time_t now = time(nullptr);
  return (now > 1600000000) ? now : 0;
}

// =============================================
// ส่ง HTTP ไป Firebase
// method: "GET", "PUT", "PATCH", "POST"
// คืนค่า: ข้อความที่ Firebase ตอบกลับ ("" = ส่งไม่สำเร็จ)
// =============================================
// client กับ http ใช้ตัวเดิมตลอด — connection ที่เปิดไว้ถูกใช้ซ้ำ
// ไม่ต้องทำ TLS handshake ใหม่ทุกครั้ง (ครั้งละ 1-2 วินาที)
WiFiClientSecure fbClient;
HTTPClient fbHttp;

String fbRequest(const char *method, String path, String body) {
  if (WiFi.status() != WL_CONNECTED)
    return "";

  unsigned long start = millis();
  fbClient.setInsecure(); // ข้าม SSL verify (สำหรับ ESP32)

  String url = "https://" + String(FIREBASE_HOST) + path +
               ".json?auth=" + String(FIREBASE_API_KEY);

  // connection เดิมอาจถูกปิดไปแล้ว — ส่งไม่ผ่านให้ต่อใหม่แล้วลองอีกครั้งเดียว
  int httpCode = 0;
  for (int attempt = 0; attempt < 2; attempt++) {
    fbHttp.begin(fbClient, url);
    fbHttp.addHeader("Content-Type", "application/json");
    httpCode = fbHttp.sendRequest(method, body);
    if (httpCode > 0)
      break;
    fbHttp.end();
    fbClient.stop();
  }

  String response = "";
  if (httpCode == 200) {
    response = fbHttp.getString();
    Serial.printf("[FB] %s %s OK (%lu ms)\n", method, path.c_str(),
                  millis() - start);
  } else {
    Serial.printf("[FB] %s %s Error (HTTP %d)\n", method, path.c_str(),
                  httpCode);
  }
  fbHttp.end();
  return response;
}

// =============================================
// ส่งสถานะตู้ — เรียกทุกครั้งที่ค่าเปลี่ยน และเป็นระยะทุก FIREBASE_UPDATE_MS
// boxStatus: 0=ว่าง, 1=มีพัสดุ, 2=เต็ม | door: 0=ปิด, 1=เปิด
// =============================================
void fbSendStatus(int parcelCount, int boxStatus, int doorInput,
                  int doorOutput) {
  String statusText = "ตู้ว่าง — พร้อมรับพัสดุ";
  if (boxStatus == 1)
    statusText = "มีพัสดุอยู่ในตู้";
  if (boxStatus == 2)
    statusText = "ตู้เต็ม — กรุณามารับพัสดุ";

  String body = "{";
  body += "\"parcelCount\":" + String(parcelCount) + ",";
  body += "\"boxStatus\":" + String(boxStatus) + ",";
  body += "\"statusText\":\"" + statusText + "\",";
  body += "\"leds\":{";
  body += "\"red\":" + String(boxStatus == 2 ? 1 : 0) + ",";
  body += "\"yellow\":" + String(boxStatus == 1 ? 1 : 0) + ",";
  body += "\"green\":" + String(boxStatus == 0 ? 1 : 0) + "},";
  body += "\"doors\":{";
  body += "\"input\":" + String(doorInput ? 1 : 0) + ",";
  body += "\"output\":" + String(doorOutput ? 1 : 0) + "},";
  body += "\"lastUpdate\":" + String(fbNow());
  body += "}";

  // PATCH = แก้เฉพาะ key ที่ส่ง ไม่ลบ events กับ stats
  fbRequest("PATCH", "/parcelBox", body);
}

// =============================================
// เพิ่ม event log — Dashboard แสดง 10 รายการล่าสุด
// *** ห้ามมีเครื่องหมาย " ใน icon และ text ***
// =============================================
void fbAddEvent(String icon, String text) {
  String body = "{";
  body += "\"icon\":\"" + icon + "\",";
  body += "\"text\":\"" + text + "\",";
  body += "\"timestamp\":" + String(fbNow());
  body += "}";

  // POST = push (Firebase สร้าง key ให้เอง)
  fbRequest("POST", "/parcelBox/events", body);
}

// =============================================
// บวกสถิติรายวัน +1
// field: "count" = พัสดุมาส่ง, "resets" = รีเซ็ต
// =============================================
void fbAddDaily(String field) {
  time_t now = fbNow();
  if (now == 0)
    return; // ยังไม่รู้วันที่

  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  char dateStr[11]; // YYYY-MM-DD
  strftime(dateStr, sizeof(dateStr), "%Y-%m-%d", &timeinfo);

  String path = "/parcelBox/stats/daily/" + String(dateStr) + "/" + field;

  // 1. อ่านค่าเดิม (ได้ตัวเลข หรือ "null" ถ้ายังไม่มี)
  String oldValue = fbRequest("GET", path, "");
  if (oldValue == "")
    return; // อ่านไม่สำเร็จ — ไม่เขียนทับ กันสถิติหาย

  // 2. บวก 1 แล้วเขียนกลับ
  fbRequest("PUT", path, String(oldValue.toInt() + 1));
}

#endif
