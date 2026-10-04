# References — Smart Parcel Box (IoT)

Reference list for the ESP32-based Smart Parcel Receiving Box: parcel counting and
box-full detection via a single photoelectric sensor, three-state LED indication, Telegram push notification, and a
Firebase-backed web dashboard.

---

## 1. Telegram Notification

**[1] Telegram Messenger LLP, "Telegram Bot API."** Official API specification.
https://core.telegram.org/bots/api
> Defines the `sendMessage` method used by the firmware to push alerts. Request form:
> `https://api.telegram.org/bot<TOKEN>/sendMessage` with required fields `chat_id` and
> `text`; optional `parse_mode` (`HTML` / `MarkdownV2`) for formatting. This is the
> primary normative source for the notification layer.

**[2] Telegram Messenger LLP, "Bots FAQ — My bot is hitting limits, how do I avoid this?"**
https://core.telegram.org/bots/faq
> States the official rate limits that justify the firmware send-cooldown
> (`TELEGRAM_COOLDOWN_MS`): approximately **30 messages per second** globally, no more
> than **one message per second in a single chat** (short bursts tolerated, then HTTP
> `429`), and **20 messages per minute** in a group. Cite this when explaining why
> notifications are throttled rather than sent on every sensor edge.

**[3] Telegram Messenger LLP, "Bot API changelog."**
https://core.telegram.org/bots/api-changelog
> Version history of the Bot API. Use to pin the API revision the project was built and
> tested against, so the report remains reproducible as the API evolves.

**[4] EJASET, "Development of an Internet-of-Things (IoT) Based Security System Using ESP32 MCU and the Telegram Bot,"** *European Journal of Applied Science, Engineering and Technology*.
https://ejaset.com/index.php/journal/article/view/524
> Peer-reviewed precedent for the exact architecture used here — ESP32 as the sensing
> node with Telegram as the mobile notification channel. Suitable as the primary
> "related work" citation for the notification design.

**[5] IETF, RFC 8446 — "The Transport Layer Security (TLS) Protocol Version 1.3,"** E. Rescorla, Aug. 2018.
https://www.rfc-editor.org/rfc/rfc8446
> The Telegram Bot API is HTTPS-only; the sketch uses `WiFiClientSecure`. Cite this when
> discussing transport security, and note as a documented limitation that
> `client.setInsecure()` skips certificate-chain verification (see [16] for the
> recommended production alternative).

---

## 2. IoT Devices & Hardware

### 2.1 Controller — ESP32

**[6] Espressif Systems, "ESP32 Series Datasheet."**
https://www.espressif.com/sites/default/files/documentation/esp32_datasheet_en.pdf
> Authoritative source for pin definitions, absolute-maximum and recommended operating
> conditions, GPIO current limits, and integrated Wi-Fi 802.11 b/g/n specifications.

**[7] Espressif Systems, "ESP32 Technical Reference Manual."**
https://www.espressif.com/sites/default/files/documentation/esp32_technical_reference_manual_en.pdf
> Register- and peripheral-level detail: GPIO matrix, input-only pins, ADC/RTC domains,
> and the NVS/flash subsystem behind the `Preferences` library used for non-volatile
> parcel-count storage.

**[8] Espressif Systems, "ESP32-WROOM-32 Datasheet."**
https://www.espressif.com/sites/default/files/documentation/esp32-wroom-32_datasheet_en.pdf
> Module-level datasheet for the specific package on typical ESP32 Dev Module boards —
> antenna, module dimensions, and recommended PCB layout.

> **Design note worth citing [7]:** GPIO **34–39** are **input-only and have no internal
> pull-up/pull-down**, so they are not used. The photoelectric sensor reaches the ESP32
> through the normally-open contact of a relay on GPIO 14 (`PIN_COUNT_SS = 14`), where the
> firmware enables the internal pull-up (`INPUT_PULLUP`); the contact only has to pull the
> pin to ground, so no external pull-up resistor is required.

**[9] Espressif Systems, "ESP-IDF Programming Guide — Hardware Reference (ESP32)."**
https://docs.espressif.com/projects/esp-idf/en/stable/esp32/hw-reference/index.html
> Maintained hardware-reference index and the canonical entry point to current Espressif
> documentation.

### 2.2 Photoelectric Sensor (parcel counting and box-full detection)

**[10] OMRON Corporation, "E3F2 Photoelectric Sensor — Datasheet."**
https://www.omron-pro.ru/doc/sensor/photo/e3f2.PDF
> Datasheet of a comparable general-purpose photoelectric sensor family, cited for the
> operating principle, output ratings, supply range, sensing distance and response time of
> this class of sensor. The sensor installed in the box is an **OPTEX ZD-70P**
> (diffuse-reflective; label: DC 10–30 V, output 0.1 A max.), powered from its own 9 V
> adaptor; its output drives a relay whose normally-open contact signals the ESP32, so the
> sensor supply never reaches the 3.3 V input.

**[11] OMRON Industrial Automation, "E3F3 Photoelectric Sensor — Specifications."**
https://www.ia.omron.com/products/family/3130/specification.html
> Manufacturer specification page for a comparable photoelectric sensor family (Photo-IC,
> built-in amplifier, high noise immunity). Useful as an accessible, live manufacturer
> citation for typical ratings of this class of sensor.

**[12] OMRON Corporation, "E3Z-F Compact Photoelectric Sensor with Built-in Amplifier — Datasheet."**
https://files.omron.eu/downloads/latest/datasheet/en/e76i_e3z-f_compact_photoelectric_sensor_with_built-in_amplifier_datasheet_en.pdf
> Comparative reference for photoelectric sensing modes (through-beam, retroreflective,
> diffuse-reflective) and their trade-offs — supports the justification for the chosen
> sensing mode of the single sensor that both counts parcels and detects a full box.

### 2.3 Limit Switches & Push Switches (Door detection, Reset)

**[13] J. Ganssle, "A Guide to Debouncing" (Parts 1 & 2), The Ganssle Group.**
https://www.ganssle.com/debouncing.htm and https://www.ganssle.com/debouncing-pt2.htm
> The standard engineering reference on contact bounce, containing **measured bounce
> durations for real mechanical switches** plus both hardware (RC) and software debounce
> implementations. This is the citation that justifies the 50 ms debounce on the reset
> push button, the 1 s re-arm time of the photoelectric sensor (`DEBOUNCE_MS = 1000`), and
> the edge-detection logic in the firmware.

**[14] J. Ganssle, "Solving Switch Bounce Problems," *Embedded.com*.**
https://www.embedded.com/solving-switch-bounce-problems/
> Published trade-press version of the same empirical work; convenient short citation.

---

## 3. Backend, Dashboard & Data

**[15] Google, "Firebase Realtime Database — REST API: Installation & Setup."**
https://firebase.google.com/docs/database/rest/start
> Normative description of the REST interface the firmware uses: any database path
> becomes an endpoint by appending `.json`, with `PUT`/`PATCH`/`GET` over HTTPS. Direct
> source for the device-to-cloud write path.

**[16] Google, "Firebase Realtime Database — Authenticate REST Requests."**
https://firebase.google.com/docs/database/rest/auth
> Covers authenticating REST calls via a Firebase ID token (`auth=<ID_TOKEN>`) or a
> service-account OAuth 2.0 access token. Cite in the security section — this is the
> documented path away from open/test database rules.

**[17] Google, "Firebase Realtime Database — Retrieving Data."**
https://firebase.google.com/docs/database/rest/retrieve-data
> Documents REST reads and the **EventSource / Server-Sent Events** streaming protocol
> (`Accept: text/event-stream`), the mechanism underlying the dashboard live status and
> parcel-count updates.

**[18] Google, "Firebase Realtime Database — Documentation (overview)."**
https://firebase.google.com/docs/database
> Data model (JSON tree), scaling characteristics, and Security Rules overview; supports
> the justification for choosing RTDB over a relational store for this low-volume,
> real-time telemetry workload.

**[19] B. Blanchon, "ArduinoJson — Official Documentation," v7.**
https://arduinojson.org/v7/doc/
> API and memory-model reference for the `JsonDocument` used to build the Telegram and
> Firebase payloads; the "Memory model" chapter is the citation for heap-usage
> discussion on a constrained MCU.

**[20] IETF, RFC 8259 — "The JavaScript Object Notation (JSON) Data Interchange Format,"** T. Bray, Dec. 2017.
https://www.rfc-editor.org/rfc/rfc8259
> The normative definition of the JSON wire format used on both the Telegram and
> Firebase interfaces.

**[21] IETF, RFC 9110 — "HTTP Semantics,"** R. Fielding, M. Nottingham, J. Reschke, Jun. 2022.
https://www.rfc-editor.org/rfc/rfc9110
> Current normative HTTP specification — method semantics (`GET`/`PUT`/`PATCH`/`POST`)
> and status codes (including `429 Too Many Requests`, relevant to [2]).

---

## 4. Standards, Architecture & Security

**[22] ITU-T Recommendation Y.4000/Y.2060, "Overview of the Internet of Things," Jun. 2012.**
https://www.itu.int/rec/T-REC-Y.2060-201206-I
> The internationally recognised definition of IoT — "a global infrastructure for the
> information society, enabling advanced services by interconnecting (physical and
> virtual) things" — plus the **IoT reference model** (device, network, service support,
> application layers). Use this to frame the system architecture chapter: sensors/ESP32 =
> device layer, Wi-Fi/HTTPS = network layer, Firebase = service support layer,
> Telegram + dashboard = application layer.

**[23] M. Fagan, K. N. Megas, K. Scarfone, and M. Smith, "IoT Device Cybersecurity Capability Core Baseline," NISTIR 8259A, NIST, May 2020.**
https://nvlpubs.nist.gov/nistpubs/ir/2020/NIST.IR.8259a.pdf
> Defines the six baseline device capabilities — device identification, device
> configuration, data protection, logical access to interfaces, software update, and
> cybersecurity state awareness. The standard checklist for the security-analysis and
> future-work sections (e.g. credentials in `config.h`, secure OTA update, and the
> `setInsecure()` limitation).

**[24] NIST, "NISTIR 8259 Series — Foundational Cybersecurity Activities for IoT Device Manufacturers."**
https://www.nist.gov/itl/applied-cybersecurity/nist-cybersecurity-iot-program/nistir-8259-series
> Series landing page giving the pre-market/post-market activity context surrounding [23].

---

## 5. Related Work (Smart Parcel / Mail Boxes)

**[25] "Development of A Smart Box Prototype for Mail and Parcel Posts Using IoT and Solar Energy," in *Proc. 2022 5th International Conference on Information and Communications Technology (ICOIACT)*, Yogyakarta, Indonesia, 2022, pp. 77–81.**
DOI: 10.1109/ICOIACT55506.2022.9972195 — https://ieeexplore.ieee.org/document/9972195/
> **The closest IEEE-indexed precedent.** A smart box that receives postal parcels and
> pushes an arrival notification to the recipient phone (via LINE Notify), with a
> reported average performance of **96%**. Excellent comparison baseline: same problem,
> different notification channel — supports the choice of Telegram (free, open Bot API,
> no per-message quota at this scale).

**[26] "IoT-Based Smart Locker: Innovation in Automatic Package Reception," *TELKA — Telekomunikasi, Elektronika, Komputasi dan Kontrol*, vol. 12, no. 1, pp. 17–22.**
https://telka.ee.uinsgd.ac.id/TELKA/article/download/telka.v12n1.17-22/telka.v12n1.17-22_pdf/3075
> Peer-reviewed automatic package-reception locker; useful for comparing mechanical
> design and reception workflow.

**[27] "IoT-Based Package Drop Box System Using Arduino Uno and ESP32-CAM," *Jurnal Sistem Cerdas*.**
https://apic.id/jurnal/index.php/jsc/article/view/581
> ESP32 + Telegram package drop box using an ultrasonic sensor for arrival detection
> (~10 cm) and ESP32-CAM for security. Direct comparison point for the **detection method**:
> ultrasonic ranging vs. the photoelectric beam-break counting used in this project.

**[28] "Design and implementation of an IoT-based automatic package box security system with vibration sensors and GSM notifications using ESP32," *Jurnal Sains, Teknologi & Komputer*.**
https://jurnal.larisma.or.id/index.php/SAINTEK/article/view/1630
> ESP32 parcel box with vibration-based anti-theft and GSM notification. Comparison point
> for **notification transport** (GSM/SMS vs. Wi-Fi/Internet) and for future anti-theft work.

**[29] "Smart Parcel Delivery Receiving Box for Secure Parcel Drop-Off," *International Journal of Research Publication and Reviews (IJRPR)*, vol. 6, no. 10.**
https://ijrpr.com/uploads/V6ISSUE10/IJRPR54313.pdf
> Recent survey-style treatment of the secure parcel drop-off problem; useful for the
> introduction/problem-statement (e-commerce growth, porch theft, failed deliveries).

**[30] "A Solar-Powered IoT Connected Physical Mailbox Interfaced with Smart Devices," *IoT (MDPI)*, vol. 1, no. 1, art. 8.**
https://www.mdpi.com/2624-831X/1/1/8
> Open-access MDPI paper on an IoT mailbox with smart-device integration — a citable,
> freely accessible reference for the mailbox-notification literature and a starting
> point for solar-powered future work.

---

## Mapping: Requirement → Reference

| Project requirement | References |
|---|---|
| Notify phone when a parcel is delivered | [1], [2], [4], [25] |
| Notification throttling / cooldown design | [2], [21] |
| Photoelectric sensor (parcel counting & box-full detection) | [10], [11], [12] |
| Limit switches (Input/Output doors), reset button | [13], [14] |
| Controller selection & GPIO/pin constraints | [6], [7], [8], [9] |
| Non-volatile parcel count across power loss | [7], [9] |
| Power supply (220 V → 5 V adaptor for ESP32, 220 V → 9 V adaptor for sensor) | [6], [10] |
| Dashboard showing parcel count, box status and door status | [15], [17], [18] |
| Live dashboard updates | [17] |
| JSON payload construction on the MCU | [19], [20] |
| Overall IoT system architecture / layer model | [22] |
| Security analysis & limitations | [5], [16], [23], [24] |
| Related work & comparison | [25], [26], [27], [28], [29], [30] |

---

*All URLs verified accessible as of 7 September 2026.*
