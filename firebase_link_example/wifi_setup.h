// =============================================
// wifi_setup.h — หน้าตั้งค่า WiFi + Telegram บนตัว ESP32
// กล่องรับพัสดุอัจฉริยะ
// =============================================
//
// แก้ WiFi, Telegram Bot Token, Telegram Chat ID ได้จากมือถือ
// ไม่ต้อง upload โค้ดใหม่ — ค่าเก็บใน Flash ไฟดับก็ไม่หาย
//
// Library ที่ต้องติดตั้ง:
//   WiFiManager (by tzapu) — จาก Library Manager
//
// วิธีใช้:
//   1. วางไฟล์นี้กับ telegram_link.h และ config.h ไว้ในโฟลเดอร์เดียวกับ sketch
//   2. #include "wifi_setup.h"
//   3. เรียก wifiBegin(...) ใน setup() และ wifiKeep() ใน loop()
//
// หน้าตั้งค่าเปิดเมื่อ:
//   - ยังไม่เคยตั้ง WiFi หรือต่อ WiFi เดิมไม่ได้ตอนเปิดเครื่อง
//   - เรียก wifiBegin(true) เช่น กดปุ่มรีเซ็ตค้างไว้ตอนเปิดเครื่อง
//
// วิธีเข้าหน้าตั้งค่า:
//   มือถือต่อ WiFi ชื่อ SETUP_AP_NAME รหัส SETUP_AP_PASSWORD
//   หน้าตั้งค่าจะเด้งขึ้นเอง (ถ้าไม่เด้ง เปิด http://192.168.4.1)
//     ปุ่ม "Configure WiFi" = เลือก WiFi บ้าน + ใส่รหัส
//     ปุ่ม "Setup"          = ใส่ Telegram Bot Token + Chat ID
// =============================================

#ifndef WIFI_SETUP_H
#define WIFI_SETUP_H

#include "config.h"
#include "telegram_link.h"
#include <WiFi.h>
#include <WiFiManager.h>

// แก้ค่าพวกนี้ได้โดย #define ใน config.h
#ifndef SETUP_AP_NAME
#define SETUP_AP_NAME "ParcelBox-Setup" // ชื่อ WiFi ของหน้าตั้งค่า
#endif
#ifndef SETUP_AP_PASSWORD
#define SETUP_AP_PASSWORD "12345678" // รหัส WiFi ของหน้าตั้งค่า (8 ตัวขึ้นไป)
#endif
#ifndef SETUP_TIMEOUT_S
#define SETUP_TIMEOUT_S 180 // ไม่มีใครเข้าหน้าตั้งค่านานเท่านี้ (วินาที) ให้ปิด
#endif

// =============================================
// เชื่อมต่อ WiFi (เรียกครั้งเดียวใน setup)
// forceSetup: true = เปิดหน้าตั้งค่าเสมอ แล้วเริ่มเครื่องใหม่เมื่อเสร็จ
// =============================================
void wifiBegin(bool forceSetup) {
  tgLoadSettings();

  WiFiManager wm;
  WiFiManagerParameter paramToken("tg_token", "Telegram Bot Token",
                                  tgToken.c_str(), 64);
  WiFiManagerParameter paramChatId("tg_chat",
                                   "Telegram Chat ID (กลุ่มเป็นเลขติดลบ)",
                                   tgChatId.c_str(), 20);
  wm.addParameter(&paramToken);
  wm.addParameter(&paramChatId);

  // กด Save ในหน้า Setup → บันทึกค่า Telegram ลง Flash
  wm.setSaveParamsCallback([&]() {
    tgSaveSettings(paramToken.getValue(), paramChatId.getValue());
  });

  const char *menu[] = {"wifi", "param", "sep", "exit"};
  wm.setMenu(menu, 4);
  wm.setTitle("ParcelBox");
  wm.setConnectTimeout(20);
  wm.setConfigPortalTimeout(SETUP_TIMEOUT_S);
  wm.setAPClientCheck(true); // มีมือถือต่ออยู่ ไม่นับเวลาปิด

  if (forceSetup) {
    Serial.printf("[SETUP] Portal opened: WiFi \"%s\"\n", SETUP_AP_NAME);
    wm.startConfigPortal(SETUP_AP_NAME, SETUP_AP_PASSWORD);
    Serial.println("[SETUP] Done, restarting...");
    delay(500);
    ESP.restart();
  }

  // ต่อ WiFi เดิม — ถ้าต่อไม่ได้จะเปิดหน้าตั้งค่ารอ SETUP_TIMEOUT_S
  if (wm.autoConnect(SETUP_AP_NAME, SETUP_AP_PASSWORD)) {
    Serial.printf("[WiFi] Connected to %s, IP: %s\n", WiFi.SSID().c_str(),
                  WiFi.localIP().toString().c_str());
  } else {
    Serial.println("[WiFi] Connection FAILED! Continuing offline...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(); // ลองต่อ WiFi เดิมต่อไป
  }
}

// =============================================
// WiFi หลุดให้ลองต่อใหม่ทุก WIFI_RETRY_MS (เรียกใน loop)
// =============================================
void wifiKeep() {
  static unsigned long lastWiFiRetry = 0;

  if (WiFi.status() == WL_CONNECTED)
    return;

  unsigned long now = millis();
  if (now - lastWiFiRetry > WIFI_RETRY_MS) {
    lastWiFiRetry = now;
    Serial.println("[WiFi] Reconnecting...");
    WiFi.reconnect();
  }
}

#endif
