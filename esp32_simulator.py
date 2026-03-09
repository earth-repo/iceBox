"""
esp32_simulator.py — จำลองการทำงานของ ESP32 กล่องรับพัสดุ
ใช้ทดสอบ Firebase + Telegram + Dashboard โดยไม่ต้องมี Hardware

*** อ่านค่าจาก config.h อัตโนมัติ ***
แก้ค่าใน config.h ที่เดียว ใช้ได้ทั้ง ESP32 และ Simulator

วิธีใช้:
  1. ติดตั้ง: pip install requests
  2. แก้ค่าใน config.h (ไฟล์เดียวกับที่ ESP32 ใช้)
  3. รัน: python esp32_simulator.py
  4. เลือกเมนู: ส่งพัสดุ / ตู้เต็ม / รีเซ็ต
  5. เปิด dashboard.html ดูผลแบบ real-time
"""

import requests
import json
import time
from datetime import date, datetime
import os
import re

# =============================================
# อ่านค่าจาก config.h อัตโนมัติ
# =============================================
def read_config_h():
    """อ่าน #define จาก config.h แล้วดึงค่ามาใช้ (รองรับ multi-line ด้วย \\)"""
    config_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "config.h")

    if not os.path.exists(config_path):
        print(f"  ❌ ไม่พบไฟล์ config.h ที่: {config_path}")
        print(f"  👆 กรุณาสร้างไฟล์ config.h ก่อน")
        return {}

    config = {}
    with open(config_path, "r", encoding="utf-8") as f:
        content = f.read()

    # รวมบรรทัดที่ต่อด้วย \ ให้เป็นบรรทัดเดียว
    content = content.replace("\\\r\n", " ").replace("\\\n", " ")

    for line in content.splitlines():
        line = line.strip()
        if not line.startswith("#define"):
            continue
        # จับ: #define KEY "VALUE" (รองรับ whitespace เยอะๆ)
        match = re.match(r'#define\s+(\w+)\s+"([^"]*)"', line)
        if match:
            config[match.group(1)] = match.group(2)

    return config

# โหลดค่าจาก config.h
_config = read_config_h()

# แปลงค่าจาก config.h → ตัวแปร Python
# config.h: FIREBASE_HOST      → Simulator: FIREBASE_URL (เติม https://)
# config.h: FIREBASE_API_KEY   → Simulator: FIREBASE_KEY
# config.h: TELEGRAM_BOT_TOKEN → Simulator: TELEGRAM_BOT_TOKEN
# config.h: TELEGRAM_CHAT_ID   → Simulator: TELEGRAM_CHAT_ID
_fb_host = _config.get("FIREBASE_HOST", "YOUR_PROJECT.firebaseio.com")
FIREBASE_URL       = f"https://{_fb_host}"
FIREBASE_KEY       = _config.get("FIREBASE_API_KEY", "")
TELEGRAM_BOT_TOKEN = _config.get("TELEGRAM_BOT_TOKEN", "YOUR_BOT_TOKEN")
TELEGRAM_CHAT_ID   = _config.get("TELEGRAM_CHAT_ID", "YOUR_CHAT_ID")

# =============================================
# สถานะจำลอง
# =============================================
parcel_count = 0
box_status = 0       # 0=ว่าง, 1=มีพัสดุ, 2=เต็ม
door_input = 0       # 0=ปิด, 1=เปิด
door_output = 0      # 0=ปิด, 1=เปิด
status_labels = {
    0: "ตู้ว่าง — พร้อมรับพัสดุ",
    1: "มีพัสดุอยู่ในตู้",
    2: "ตู้เต็ม — กรุณามารับพัสดุ"
}


# =============================================
# อ่านสถานะปัจจุบันจาก Firebase (เหมือน loadCountFromFlash)
# =============================================
def firebase_read_state():
    """อ่านค่า parcelCount, boxStatus, doors จาก Firebase เมื่อเริ่มต้น"""
    global parcel_count, box_status, door_input, door_output
    url = f"{FIREBASE_URL}/parcelBox.json"
    if FIREBASE_KEY:
        url += f"?auth={FIREBASE_KEY}"

    try:
        r = requests.get(url, timeout=5)
        if r.status_code == 200 and r.json():
            data = r.json()
            parcel_count = data.get("parcelCount", 0)
            box_status = data.get("boxStatus", 0)
            doors = data.get("doors", {})
            door_input = doors.get("input", 0)
            door_output = doors.get("output", 0)
            print(f"  ✅ อ่านสถานะจาก Firebase สำเร็จ")
            print(f"     📦 จำนวนพัสดุ: {parcel_count} ชิ้น")
            print(f"     📊 สถานะตู้: {status_labels.get(box_status, '?')}")
        else:
            print(f"  ⚠️ ไม่พบข้อมูลใน Firebase (เริ่มต้นค่าเป็น 0)")
    except Exception as e:
        print(f"  ⚠️ อ่าน Firebase ไม่ได้: {e} (เริ่มต้นค่าเป็น 0)")

# =============================================
# Firebase Functions
# =============================================
def firebase_update():
    """อัปเดตข้อมูลไปที่ Firebase (เหมือนที่ ESP32 ทำ)"""
    url = f"{FIREBASE_URL}/parcelBox.json"
    if FIREBASE_KEY:
        url += f"?auth={FIREBASE_KEY}"

    data = {
        "parcelCount": parcel_count,
        "boxStatus": box_status,
        "statusText": status_labels.get(box_status, ""),
        "leds": {
            "red": 1 if box_status == 2 else 0,
            "yellow": 1 if box_status == 1 else 0,
            "green": 1 if box_status == 0 else 0,
        },
        "doors": {
            "input": door_input,
            "output": door_output,
        },
        "lastUpdate": int(time.time())
    }

    try:
        r = requests.patch(url, json=data, timeout=5)
        if r.status_code == 200:
            print(f"  ✅ Firebase อัปเดตสำเร็จ")
        else:
            print(f"  ❌ Firebase Error: {r.status_code} — {r.text}")
    except Exception as e:
        print(f"  ❌ Firebase Error: {e}")


def firebase_update_stats(event_type):
    """อัปเดตสถิติรายวัน (arrive หรือ reset)"""
    today = date.today().isoformat()  # YYYY-MM-DD
    url = f"{FIREBASE_URL}/parcelBox/stats/daily/{today}.json"
    if FIREBASE_KEY:
        url += f"?auth={FIREBASE_KEY}"

    # อ่านค่าเดิมก่อน
    try:
        r = requests.get(url, timeout=5)
        existing = r.json() if r.status_code == 200 and r.json() else {}
    except Exception:
        existing = {}

    # Increment ค่าที่ต้องการ
    if event_type == "arrive":
        existing["count"] = existing.get("count", 0) + 1
    elif event_type == "reset":
        existing["resets"] = existing.get("resets", 0) + 1

    # เขียนกลับ
    try:
        r = requests.put(url, json=existing, timeout=5)
        if r.status_code == 200:
            print(f"  ✅ Stats อัปเดต ({today}: {existing})")
        else:
            print(f"  ⚠️ Stats error: {r.status_code}")
    except Exception as e:
        print(f"  ⚠️ Stats error: {e}")


def firebase_add_event(icon, text):
    """เพิ่ม event log ไปที่ Firebase"""
    url = f"{FIREBASE_URL}/parcelBox/events.json"
    if FIREBASE_KEY:
        url += f"?auth={FIREBASE_KEY}"

    data = {
        "icon": icon,
        "text": text,
        "timestamp": int(time.time())
    }

    try:
        r = requests.post(url, json=data, timeout=5)
        if r.status_code == 200:
            print(f"  ✅ Firebase event logged")
        else:
            print(f"  ⚠️ Firebase event error: {r.status_code}")
    except Exception as e:
        print(f"  ⚠️ Firebase event error: {e}")


# =============================================
# Telegram Functions
# =============================================
def send_telegram(message):
    """ส่งข้อความแจ้งเตือนผ่าน Telegram (เหมือนที่ ESP32 ทำ)"""
    url = f"https://api.telegram.org/bot{TELEGRAM_BOT_TOKEN}/sendMessage"

    data = {
        "chat_id": TELEGRAM_CHAT_ID,
        "text": message,
        "parse_mode": "HTML"
    }

    try:
        r = requests.post(url, json=data, timeout=5)
        if r.status_code == 200:
            print(f"  ✅ Telegram ส่งสำเร็จ")
        else:
            result = r.json()
            print(f"  ❌ Telegram Error: {result.get('description', r.status_code)}")
    except Exception as e:
        print(f"  ❌ Telegram Error: {e}")


# =============================================
# Simulator Actions
# =============================================
def simulate_parcel_arrive():
    """จำลอง: พัสดุมาส่ง (Counter sensor triggered)"""
    global parcel_count, box_status
    parcel_count += 1
    box_status = 1

    print(f"\n📦 พัสดุมาส่ง! จำนวน: {parcel_count} ชิ้น")

    # ส่ง Telegram
    msg = f"📦 <b>มีพัสดุมาส่ง!</b>\n"
    msg += f"📊 จำนวนพัสดุในตู้: <b>{parcel_count}</b> ชิ้น\n"
    msg += "🟢 ยังรับพัสดุได้"
    send_telegram(msg)

    # อัปเดต Firebase
    firebase_update()
    firebase_update_stats("arrive")
    firebase_add_event("📦", f"พัสดุมาส่ง (จำนวน: {parcel_count} ชิ้น)")


def simulate_box_full():
    """จำลอง: ตู้เต็ม (Max sensor triggered)"""
    global box_status
    box_status = 2

    print(f"\n🔴 ตู้พัสดุเต็มแล้ว! (จำนวน: {parcel_count} ชิ้น)")

    msg = f"🔴 <b>ตู้พัสดุเต็มแล้ว!</b>\n"
    msg += f"📊 จำนวนพัสดุ: {parcel_count} ชิ้น\n"
    msg += "⚠️ กรุณามารับพัสดุ"
    send_telegram(msg)

    firebase_update()
    firebase_add_event("🔴", "ตู้พัสดุเต็ม!")


def simulate_reset():
    """จำลอง: กดปุ่ม Reset (รับพัสดุแล้ว)"""
    global parcel_count, box_status
    parcel_count = 0
    box_status = 0

    print(f"\n✅ รีเซ็ตแล้ว! จำนวน: 0 ชิ้น")

    msg = "✅ <b>รับพัสดุแล้ว!</b>\n"
    msg += "📦 รีเซ็ตจำนวนพัสดุเป็น 0 ชิ้น\n"
    msg += "🟢 ตู้พร้อมรับพัสดุ"
    send_telegram(msg)

    firebase_update()
    firebase_update_stats("reset")
    firebase_add_event("✅", "รีเซ็ต — รับพัสดุแล้ว")


def simulate_boot():
    """จำลอง: ESP32 เริ่มทำงาน (อ่านค่าจาก Flash = เก็บค่าเดิม)"""
    global box_status
    # ไม่รีเซ็ต parcel_count — จำลอง loadCountFromFlash()
    if parcel_count > 0:
        box_status = 1
    else:
        box_status = 0

    print(f"\n🟢 ระบบเริ่มทำงาน! (กู้คืนจาก Flash: {parcel_count} ชิ้น)")

    msg = f"🟢 <b>ตู้พัสดุอัจฉริยะเริ่มทำงาน</b>\n"
    msg += "📡 WiFi: Simulator\n"
    msg += f"📦 จำนวนพัสดุ: {parcel_count} ชิ้น"
    if parcel_count > 0:
        msg += "\n♻️ (กู้คืนค่าจาก Flash หลังไฟดับ)"
    send_telegram(msg)

    firebase_update()
    firebase_add_event("🟢", f"ระบบเริ่มทำงาน (พัสดุ: {parcel_count} ชิ้น)")


def auto_demo():
    """จำลองอัตโนมัติ: ส่งพัสดุ 5 ชิ้น → เต็ม → รีเซ็ต"""
    print("\n🎬 เริ่มจำลองอัตโนมัติ...")
    print("   (เปิด dashboard.html ดูผลแบบ real-time)\n")

    simulate_boot()
    time.sleep(3)

    for i in range(5):
        simulate_parcel_arrive()
        time.sleep(2)

    simulate_box_full()
    time.sleep(3)

    simulate_reset()
    print("\n🎬 จบการจำลอง!")


def simulate_open_input_door():
    """จำลอง: เปิดประตูรับพัสดุเข้า"""
    global door_input
    door_input = 1
    print("\n🚪 ประตูรับพัสดุเข้า — เปิด")
    send_telegram("🚪 <b>ประตูรับพัสดุเข้า — เปิด</b>")
    firebase_update()
    firebase_add_event("🚪", "ประตูรับพัสดุเข้า — เปิด")


def simulate_close_input_door():
    """จำลอง: ปิดประตูรับพัสดุเข้า"""
    global door_input
    door_input = 0
    print("\n🔒 ประตูรับพัสดุเข้า — ปิด")
    send_telegram("🔒 <b>ประตูรับพัสดุเข้า — ปิด</b>")
    firebase_update()
    firebase_add_event("🔒", "ประตูรับพัสดุเข้า — ปิด")


def simulate_open_output_door():
    """จำลอง: เปิดประตูนำพัสดุออก"""
    global door_output
    door_output = 1
    print("\n🚪 ประตูนำพัสดุออก — เปิด")
    send_telegram("🚪 <b>ประตูนำพัสดุออก — เปิด</b>")
    firebase_update()
    firebase_add_event("🚪", "ประตูนำพัสดุออก — เปิด")


def simulate_close_output_door():
    """จำลอง: ปิดประตูนำพัสดุออก → รีเซ็ต (นำพัสดุออกทั้งหมดแล้ว)"""
    global door_output, parcel_count, box_status
    door_output = 0

    # นำพัสดุออกทั้งหมดแล้ว → รีเซ็ตจำนวนเป็น 0
    old_count = parcel_count
    parcel_count = 0
    box_status = 0

    print(f"\n🔒 ประตูนำพัสดุออก — ปิด")
    print(f"✅ นำพัสดุออกแล้ว {old_count} ชิ้น → รีเซ็ตเป็น 0")

    msg = "🔒 <b>ประตูนำพัสดุออก — ปิด</b>\n"
    msg += f"✅ นำพัสดุออกแล้ว {old_count} ชิ้น\n"
    msg += "📦 รีเซ็ตจำนวนพัสดุเป็น 0 ชิ้น\n"
    msg += "🟢 ตู้พร้อมรับพัสดุ"
    send_telegram(msg)

    firebase_update()
    firebase_update_stats("reset")
    firebase_add_event("🔒", f"ประตูนำพัสดุออก — ปิด (นำออก {old_count} ชิ้น → รีเซ็ต)")


# =============================================
# Main Menu
# =============================================
def main():
    os.system('cls' if os.name == 'nt' else 'clear')
    print("=" * 50)
    print("  📦 ESP32 Simulator — กล่องรับพัสดุอัจฉริยะ")
    print("  จำลองการทำงานโดยไม่ต้องมี Hardware")
    print("=" * 50)

    # แสดงค่าที่อ่านจาก config.h
    config_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "config.h")
    print(f"\n  📄 อ่านค่าจาก: config.h")
    print(f"  {'─' * 44}")
    print(f"  FIREBASE_HOST      → {_fb_host}")
    print(f"  FIREBASE_API_KEY   → {'(ตั้งค่าแล้ว)' if FIREBASE_KEY else '(ว่าง)'}")
    print(f"  TELEGRAM_BOT_TOKEN → {TELEGRAM_BOT_TOKEN[:10]}..." if len(TELEGRAM_BOT_TOKEN) > 10 else f"  TELEGRAM_BOT_TOKEN → {TELEGRAM_BOT_TOKEN}")
    print(f"  TELEGRAM_CHAT_ID   → {TELEGRAM_CHAT_ID}")

    # อ่านสถานะปัจจุบันจาก Firebase (เหมือน ESP32 อ่านจาก Flash)
    print(f"\n  📡 กำลังอ่านสถานะจาก Firebase...")
    firebase_read_state()

    # ตรวจสอบการตั้งค่า
    config_ok = True
    if "YOUR_PROJECT" in FIREBASE_URL:
        print("\n  ⚠️  ยังไม่ได้ตั้งค่า FIREBASE_HOST ใน config.h")
        config_ok = False
    if "YOUR_BOT_TOKEN" in TELEGRAM_BOT_TOKEN:
        print("  ⚠️  ยังไม่ได้ตั้งค่า TELEGRAM_BOT_TOKEN ใน config.h")
        config_ok = False
    if "YOUR_CHAT_ID" in TELEGRAM_CHAT_ID:
        print("  ⚠️  ยังไม่ได้ตั้งค่า TELEGRAM_CHAT_ID ใน config.h")
        config_ok = False

    if not config_ok:
        print("\n  👆 แก้ค่าในไฟล์ config.h (ใช้ร่วมกับ ESP32)")

    while True:
        print(f"\n{'─' * 40}")
        print(f"  สถานะ: {status_labels.get(box_status, '?')}")
        print(f"  จำนวนพัสดุ: {parcel_count} ชิ้น")
        print(f"  ประตูเข้า: {'🔴 เปิด' if door_input else '🟢 ปิด'}  |  ประตูออก: {'🔴 เปิด' if door_output else '🟢 ปิด'}")
        print(f"{'─' * 40}")
        print("  [1] 📦 พัสดุมาส่ง (Counter sensor)")
        print("  [2] 🔴 ตู้เต็ม (Max sensor)")
        print("  [3] ✅ รีเซ็ต (Reset button)")
        print("  [4] 🟢 เริ่มระบบใหม่ (Boot)")
        print("  [5] 🎬 จำลองอัตโนมัติ (Auto demo)")
        print("  [6] 🚪 เปิดประตูรับพัสดุเข้า")
        print("  [7] 🔒 ปิดประตูรับพัสดุเข้า")
        print("  [8] 🚪 เปิดประตูนำพัสดุออก")
        print("  [9] 🔒 ปิดประตูนำพัสดุออก")
        print("  [0] ออก")
        print()

        try:
            choice = input("  เลือก: ").strip()
        except (KeyboardInterrupt, EOFError):
            break

        if choice == '1':
            simulate_parcel_arrive()
        elif choice == '2':
            simulate_box_full()
        elif choice == '3':
            simulate_reset()
        elif choice == '4':
            simulate_boot()
        elif choice == '5':
            auto_demo()
        elif choice == '6':
            simulate_open_input_door()
        elif choice == '7':
            simulate_close_input_door()
        elif choice == '8':
            simulate_open_output_door()
        elif choice == '9':
            simulate_close_output_door()
        elif choice == '0':
            print("\n👋 ออกจาก Simulator")
            break
        else:
            print("  ❓ เลือก 0-9")


if __name__ == "__main__":
    main()
