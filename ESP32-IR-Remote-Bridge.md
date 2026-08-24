# ESP32 IR Remote Bridge

**Project Codename:** IR-MC / IR-Bridge  
**Status:** Scaffold (firmware + docs; untested on hardware)  
**Primary Goal:** A flexible network-to-IR bridge that receives commands from phones, computers, or automation systems and transmits the corresponding infrared remote signals to TVs, ACs, and other IR-controlled devices.

---

## 1. Project Vision

Build a reliable, flexible ESP32-based module whose **primary job** is:

> Receive a message from a computer or cell phone → look up / interpret the command → transmit the correct IR signal to the target device.

Secondary capabilities (nice-to-have / future):
- Act as a classic TV-B-Gone style universal power-off blaster
- Learn new IR codes from existing remotes
- Measure unknown carrier frequencies
- Support both 940 nm (standard) and 850 nm IR LEDs

The design prioritizes **flexibility** and long-term maintainability over minimal hardware.

---

## 2. Core Requirements

### Must Have
- [x] ESP32 (any common variant: WROOM, C3, S3, etc.) — **v0 = classic ESP32 / DevKitC**
- [x] WiFi connectivity
- [x] Primary control interface: **MQTT**
- [x] Secondary control interface: simple **HTTP REST API**
- [x] Transmit IR signals with correct carrier frequency (36 / 38 / 40 / 56 kHz etc.)
- [x] Support for named devices + commands (e.g. `tv/power`, `ac/temp_24`)
- [x] Ability to send raw IR timings + frequency on the fly
- [ ] Proper high-current drive for IR LED(s) using transistor or MOSFET — **documented, not built**
- [ ] 940 nm IR LED as primary transmitter — **documented, not built**

### Should Have
- [x] Store IR codes in LittleFS / SPIFFS so new devices can be added without reflashing — **SPIFFS JSON in v0**
- [x] Basic status / acknowledgment publishing over MQTT
- [x] Simple web page for testing and manual control
- [ ] Support for multiple simultaneous IR LEDs or higher-power output for better range

### Nice to Have
- [ ] Learn mode (capture IR from an existing remote and save it under a name)
- [ ] Raw photodiode input for measuring unknown carrier frequencies
- [ ] Dual wavelength support / awareness (850 nm + 940 nm)
- [ ] TV-B-Gone style “blast all power codes” mode
- [ ] Home Assistant discovery / easy integration
- [ ] OTA firmware updates

---

## 3. Hardware Outline

### Core Components
- ESP32 development board
- 940 nm IR LED (primary) – high-power preferred
- Optional second 850 nm IR LED
- NPN transistor (2N2222 / S8050) or logic-level N-MOSFET for LED drive
- Current-limiting resistor(s) sized for desired peak current (typically 100–300 mA pulsed)
- Optional: additional IR LEDs in parallel for wider coverage / longer range
- Optional: TSOP38238 or equivalent 38 kHz IR receiver (for normal reception / learning)
- Optional: Bare photodiode or phototransistor + simple amplifier (for carrier frequency measurement)
- Power: USB or regulated 5 V (IR LED drive benefits from 5 V)

### Suggested Pin Assignments (flexible)
| Function              | Suggested GPIO | Notes                          |
|-----------------------|----------------|--------------------------------|
| IR Transmit (PWM)     | GPIO 4 or 18   | LEDC or RMT                    |
| IR Receiver (TSOP)    | GPIO 15 or 19  | Optional                       |
| Raw Photodiode        | GPIO 34/35/etc | Optional, input only           |
| Status LED            | GPIO 2         | Built-in on many boards        |
| Button (learn/trigger)| GPIO 0 or 5    | Optional                       |

---

## 4. Software Architecture

### Control Interfaces

**Primary: MQTT**
- Connect to configurable broker
- Subscribe to topic prefix, e.g. `ir/#` or `home/ir/#`
- Example topics:
  - `ir/tv/power`
  - `ir/tv/volume_up`
  - `ir/living_ac/power`
  - `ir/living_ac/temp_24`
  - `ir/raw` (payload contains timings + frequency)
- Optional retained status topic: `ir/status` or `ir/<device>/ack`

**Secondary: HTTP REST API**
- Simple endpoints for testing and non-MQTT clients
- Examples:
  - `POST /ir/tv/power`
  - `POST /ir/send` (with JSON body for raw or named command)
  - `GET /devices` / `GET /commands`

**Optional:**
- Lightweight web UI for manual testing
- WebSocket for real-time feedback

### IR Code Storage
Preferred approach for flexibility:
1. Named commands stored in a JSON (or simple text) file on LittleFS
2. Ability to add / update / delete codes over MQTT or HTTP
3. Fallback to compiled-in codes for critical devices
4. Support for both protocol-based (NEC, Samsung, Sony, etc.) and raw timings + carrier frequency

### Transmission Engine
- Use ESP32 RMT peripheral (preferred) or LEDC for clean carrier generation
- Support arbitrary carrier frequency per command
- Support repeat counts and inter-command delays
- Clean mark/space timing

---

## 5. Key Technical Decisions

| Topic                    | Decision / Recommendation                          | Rationale |
|--------------------------|----------------------------------------------------|---------|
| Carrier frequency        | Store and use the correct frequency per code       | Better range & reliability than always using 38 kHz |
| Optical wavelength       | Primary = 940 nm. 850 nm optional / experimental   | Almost all consumer devices use ~940 nm |
| Measuring carrier freq   | Optional raw photodiode path                       | Standard TSOP modules demodulate and hide the carrier |
| Network protocol         | MQTT primary + HTTP secondary                      | Maximum flexibility from phones, computers, and automation |
| Code storage             | LittleFS JSON + optional compiled-in defaults      | Add devices without constant reflashing |
| Library landscape        | Prefer modern ESP32-native (RMT) approaches; IRremote / IRremoteESP8266 as reference | Cleaner long-term than pure Arduino libraries |

---

## 6. Suggested Development Phases

### Phase 1 – Minimum Viable Bridge
- ESP32 + IR LED + transistor
- Hard-coded set of IR commands
- MQTT subscription → send IR
- Basic HTTP endpoint for testing
- Confirm reliable transmission to at least one real TV and one other device

### Phase 2 – Flexible Command System
- Move codes to LittleFS
- Named device/command structure
- Ability to add new codes without reflashing
- Status / ACK publishing

### Phase 3 – Learning & Advanced Features
- IR receive + learn mode
- Raw photodiode carrier frequency measurement
- Web UI
- TV-B-Gone style blast mode
- Home Assistant friendly discovery

### Phase 4 – Polish
- OTA updates
- Better error handling and logging
- Enclosure + power options
- Documentation for family use

---

## 7. Open Questions / Decisions Needed

Decisions for v0 (2026-08-23):

| Question | v0 decision |
|----------|-------------|
| MQTT broker | Local Mosquitto (or any LAN broker). URI in NVS via `secrets/mqtt.yaml` or `POST /config/mqtt`. HTTP works with no broker. |
| ESP32 variant | Classic **ESP32 WROOM / 38-pin DevKitC** (same family as ESP32_CAM). C3/S3 later if we care about size or extra RMT. |
| Offline vs Wi‑Fi | Always-on Wi‑Fi STA. No AP-mode config portal yet. |
| Form factor | USB-powered proto / small box later. |
| Physical button | BOOT (GPIO 0) after boot sends `demo/power` (later: configurable local command / learn). |
| 850 nm | Deferred. 940 nm only. |

Still open: enclosure, which real TV/AC to encode first, whether Home Assistant discovery is worth doing before learn mode.

---

## 8. Reference Material (from earlier research)

- Classic TV-B-Gone / Ken Shirriff Arduino port – good source of power codes and timing approach
- Mitch Altman’s newer ESP32 TV-B-Gone kit work
- ESP32 RMT peripheral for precise IR generation
- Common carrier frequencies: 38 kHz (majority), 36 kHz, 40 kHz, 56 kHz
- Standard consumer remotes almost exclusively use ~940 nm

---

## 9. Success Criteria

The project is successful when:

1. A phone or computer can reliably turn a known TV on/off and control volume via MQTT or HTTP.
2. New devices/commands can be added without rewriting and reflashing the entire firmware.
3. The module is stable enough to leave running 24/7.
4. The design is clean enough that family members or future-you can understand and extend it.

---

*This document is the living specification for the ESP32 IR Remote Bridge project. Update it as decisions are made and phases are completed.*
