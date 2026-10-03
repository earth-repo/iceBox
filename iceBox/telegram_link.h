// =============================================
// telegram_link.h — ส่งข้อความแจ้งเตือนจาก ESP32 ไป Telegram
// กล่องรับพัสดุอัจฉริยะ
// =============================================
//
// วิธีใช้:
//   1. วางไฟล์นี้กับ config.h ไว้ในโฟลเดอร์เดียวกับ sketch
//   2. #include "telegram_link.h"
//   3. ต่อ WiFi ก่อน (wifiBegin() ใน wifi_setup.h ต่อให้ และโหลดค่า Telegram ให้)
//   4. tgSend("ข้อความ");
//
// ไม่ต้องติดตั้ง library เพิ่ม — WiFi, HTTPClient มากับ ESP32 Board Package
//
// Bot token กับ Chat ID เก็บใน Flash ของ ESP32 — แก้ได้จากหน้าตั้งค่า
// (wifi_setup.h) โดยไม่ต้อง upload โค้ดใหม่
//
// ---------------------------------------------
// ค่าที่ "ส่ง" ไป Telegram
// ---------------------------------------------
//   tgSend()  POST https://api.telegram.org/bot<TOKEN>/sendMessage
//     chat_id     คนหรือกลุ่มที่รับแจ้งเตือน (จากหน้าตั้งค่า)
//     text        ข้อความ ขึ้นบรรทัดใหม่ด้วย \n ทำตัวหนาด้วย <b>...</b>
//     parse_mode  "HTML"
//
// ไม่มีค่าที่ต้อง "รับ" จาก Telegram — ดูแค่ว่าส่งสำเร็จ (HTTP 200) หรือไม่
// =============================================

#ifndef TELEGRAM_LINK_H
#define TELEGRAM_LINK_H

#include "config.h"
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

// ค่าเริ่มต้นจาก config.h (ถ้ามี) — ใช้เมื่อยังไม่เคยตั้งผ่านหน้าตั้งค่า
#ifndef TELEGRAM_BOT_TOKEN
#define TELEGRAM_BOT_TOKEN ""
#endif
#ifndef TELEGRAM_CHAT_ID
#define TELEGRAM_CHAT_ID ""
#endif

String tgToken;  // Bot token
String tgChatId; // คนหรือกลุ่มที่รับแจ้งเตือน (กลุ่มเป็นเลขติดลบ)

// client กับ http ใช้ตัวเดิมตลอด — ข้อความที่ส่งติดกันใช้ connection เดิม
// ไม่ต้องทำ TLS handshake ใหม่ทุกข้อความ
WiFiClientSecure tgClient;
HTTPClient tgHttp;

// =============================================
// บันทึก/อ่าน ค่า Telegram จาก Flash
// =============================================
void tgLoadSettings() {
  Preferences prefs;
  prefs.begin("telegram", true); // true = read-only
  tgToken = prefs.getString("token", TELEGRAM_BOT_TOKEN);
  tgChatId = prefs.getString("chat_id", TELEGRAM_CHAT_ID);
  prefs.end();
}

void tgSaveSettings(String token, String chatId) {
  token.trim();
  chatId.trim();
  tgToken = token;
  tgChatId = chatId;

  Preferences prefs;
  prefs.begin("telegram", false); // false = read-write
  prefs.putString("token", tgToken);
  prefs.putString("chat_id", tgChatId);
  prefs.end();
  Serial.println("[TG] Settings saved");
}

// =============================================
// ส่งข้อความแจ้งเตือน
// *** ห้ามมี < > & ในข้อความ ยกเว้น tag <b> </b> ***
// =============================================
void tgSend(String message) {
  if (tgToken == "" || tgChatId == "") {
    Serial.println("[TG] Token / Chat ID not set, skipping...");
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[TG] WiFi not connected, skipping...");
    return;
  }

  // แปลงอักขระพิเศษให้ใส่ใน JSON ได้
  message.replace("\\", "\\\\");
  message.replace("\"", "\\\"");
  message.replace("\n", "\\n");

  String body = "{";
  body += "\"chat_id\":\"" + tgChatId + "\",";
  body += "\"text\":\"" + message + "\",";
  body += "\"parse_mode\":\"HTML\"";
  body += "}";

  unsigned long start = millis();
  tgClient.setInsecure(); // ข้าม SSL verify (สำหรับ ESP32)

  String url = "https://api.telegram.org/bot" + tgToken + "/sendMessage";

  // connection เดิมอาจถูกปิดไปแล้ว — ส่งไม่ผ่านให้ต่อใหม่แล้วลองอีกครั้งเดียว
  int httpCode = 0;
  for (int attempt = 0; attempt < 2; attempt++) {
    tgHttp.begin(tgClient, url);
    tgHttp.addHeader("Content-Type", "application/json");
    httpCode = tgHttp.POST(body);
    if (httpCode > 0)
      break;
    tgHttp.end();
    tgClient.stop();
  }

  if (httpCode == 200) {
    Serial.printf("[TG] Sent OK (%lu ms)\n", millis() - start);
  } else {
    Serial.printf("[TG] Error (HTTP %d)\n", httpCode);
  }
  tgHttp.end();
}

#endif
