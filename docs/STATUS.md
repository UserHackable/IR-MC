# Project status (snapshot)

Last updated: **2026-09-03** (breadboard emitter on Noelle programmed as `ir22` / `.22`).

## Live boards

| Name | IP | Identity | Where | TX |
|------|----|----------|-------|----|
| `ir` | `192.168.72.20` | `ir-mc-582abd78af4c` | still on LAN | `ir_bridge` 0.3.4 |
| `ir21` | `192.168.72.21` | `ir-mc-582abd785820` | Haruna USB `/dev/ttyUSB0` | GPIO-direct TK16 |
| **`ir22`** | **`192.168.72.22`** | **`ir-mc-489d31e61010`** | **Noelle USB `/dev/ttyUSB0`** | **breadboard 2N2222 + 5 V IR LED** |

Phone remotes: `http://192.168.72.22/` (this USB board) and `http://192.168.72.21/` (Haruna). Lab UI: `/lab`.

## Live rig (Noelle) — `ir22`

Original breadboard transistor emitter, MAC `48:9d:31:e6:10:10`. Flashed **`ir_bridge` 0.3.4** with NVS hostname `ir22` and static `192.168.72.22`. TX is GPIO **18** through the 2N2222 (not GPIO-direct). Flash this CP2102 at **115200**.

```bash
PORT=/dev/ttyUSB0 ESPTOOL_BAUD=115200 \
  WIFI_HOSTNAME=ir22 WIFI_IP=192.168.72.22 ./scripts/fw idf nvs-wifi
PORT=/dev/ttyUSB0 ./scripts/fw idf upload ir_bridge   # or idf.py -b 115200 flash
```

## Live rig (Haruna) — `ir21`

One classic ESP32 on Haruna USB (`/dev/ttyUSB0`, CP2102), running **`ir_bridge`**. This is a **placement module** (MAC `58:2a:bd:78:58:20`). The previous board was `ir-mc-582abd78af4c` at `192.168.72.20` / `ir.lan`.

| Item | Value |
|------|--------|
| Identity | `ir-mc-582abd785820` |
| Name | `ir21` |
| IP | `192.168.72.21` (static) |
| App | `ir_bridge` 0.3.4 |
| TK16 (TX LED) | GPIO **18**, GPIO-direct (too weak beyond ~8 ft) |
| TK15 (demod RX) | GPIO **19**, 3.3 V |
| TK64 (raw phototransistor) | GPIO **34** ADC, 3.3 V |

Flash this CP2102 at **115200** — 460800 dropped the stub. Stop the serial logger first:

```bash
sudo systemctl stop ir-mc-serial-log
# esptool -b 115200 … or idf.py -b 115200 flash
sudo systemctl start ir-mc-serial-log
```

Haruna ethernet (`192.168.72.3`) can time out on HTTP to this STA; phones on Wi‑Fi are fine.

IR frames are logged as `IRMSG …` on UART. Haruna service `ir-mc-serial-log` appends them to `~/User-Hackable/IR-MC/logs/ir-serial.log`.

Haruna HDMI-A-2 is connected to a Sceptre **H5** (1080p). HDMI-A-1 is empty. No `/dev/cec*` — cannot ask the TV if it is showing that input.

## Sceptre IR (learned)

Sony 12-bit, address **1**, 40 kHz:

| Command | Code | Notes |
|---------|------|--------|
| Volume up | 18 | Repeat while held; same code the whole time |
| Volume down | 19 | Same |
| Power | 21 | **Toggle** — remote never sends a different off code |
| Discrete off (tried) | 47 | Standard Sony discrete; no confirmed effect |
| Power2 (tried) | 109 | No confirmed effect |

A real remote tap is about **3–8 frames** (~45 ms apart). Long 40-frame holds are unlike the handheld.

Exact captured power timings were replayed as `sceptre/power_remote` (raw). That **did** toggle the TV on at least once from a close blaster.

When the blaster was close enough for TK15 to decode, stored `sceptre/from_blast` matched the Sony encoder to about ±12 µs (addr 1 / cmd 18). Ceiling-aimed TX is **not** that: Info and Vol+ still work on the TV, but TK15 reports `distorted` raw scatter. Do not treat a bad local echo as proof the code is wrong.

## Soundbar IR (learned 2026-09-03)

Extended NEC, 38 kHz, 16-bit address **46723** (`0xB683`). Phone UI and catalog use the same pair. A real **volume up** on TK15 (ceiling bounce of the handheld) decoded as:

| Command | Code | Notes |
|---------|------|--------|
| Volume up | 11 | One 32-bit data frame, then three AGC repeats |
| Volume down | 14 | Same shape (earlier learns) |
| Power | 77 | Catalog; not re-captured this session |
| Mute | 67 | Catalog |

Handheld volume-up tap (UART `IRMSG`):

1. Full NEC frame, n=67, addr **46723** / cmd **11** (bits match the encoder).
2. Three AGC bursts `~9000, 2250, 560` about **110 ms** apart — not extra full frames.

Firmware **0.3.3** retransmitted the whole 32-bit frame `repeat` times. That is not what the remote sends. **0.3.4** sends 1 data frame + (`repeat`−1) AGC repeats on a 110 ms leading-edge period. Phone Soundbar keys use `repeat: 4` (~344 ms). Hard-refresh `http://ir.lan/` after flash.

Ceiling TK16 scatter into TK15 is garbled for this protocol too. Bar response is the only verdict. GPIO-direct may still be too weak; next hardware step is 2N2222 + 5 V.

## Range / drive

- Transistor + 3×100 Ω (~33 Ω) + IR LED on the original `ir-mc-489d31e61010`: ~**95 mA**. Volume reliable inside ~**8 ft**, iffy beyond.
- **2026-09-04 placement board (`ir21` / `.21`):** GPIO-direct TK16. Both Sceptre and soundbar work to about **8 ft**, then drop. Codes and bounce angle are fine; current is the limit. Next is a discrete **2N2222 + 5 V** transmitter — disconnect TK16 from GPIO 18 first. See [hardware.md](hardware.md).
- TK16 driven **GPIO-direct** on GPIO 18: **too hot** when the LED sits centimetres from the TV IR window. Inverse-square: GPIO-direct at ~2 in can still outshine a 100 mA blast at 8 ft.
- **Working geometry (2026-09-03):** TK16 aimed at the **ceiling**, bounce into the Sceptre IR window. GPIO-direct, duty **0.33**, Sony gap **25 ms** (RMT-accurate), power repeat 5. Phone remotes at `http://ir.lan/` reliable.
- Pointing the LED into the IR window (centimetres away) saturates the TV AGC. Carrier-duty dimming (0.12) does not drop peak current and did not help.
- TK15 is **not** a proxy once the LED points at the ceiling (it only sees scatter; probe may report `distorted` while the TV is fine).
- TK64 (GPIO34) is stuck at **142 mV** with no swing during TX — unwired or dead.
- Pre-0.3.0 Sony repeats used `vTaskDelay(25 ms)` at a 10 ms tick (~11 ms sleep), gluing frames. Gaps are RMT-encoded now.

## Done (firmware)

- `ir_bridge` / `ir_listen` / `ir_blast` roles; RMT RX idle fix
- LED anode sense `GET/POST /adc/led` (GPIO18 DC, ADC GPIO32, ×2 estimate)
- TK64 analog `GET /adc/tk64`; learn JSON includes `tk64` min/max/mean
- `IRMSG` UART lines + Haruna systemd logger
- NEC TX: 1 data frame + AGC repeats (9/2.25 ms) on a 110 ms period (`ir_bridge` 0.3.4)

## Next bench (solder)

1. Handheld soundbar remote **works** from the same ceiling bounce. Path is fine; GPIO-direct dies around **8 ft**. See [hardware.md Stronger ceiling LED](hardware.md).
2. Unplug USB. Disconnect TK16 from GPIO 18. Solder **2N2222 + 5 V** (Rb 330 Ω, Rled 22–68 Ω) on a ceiling-aimed 940 nm LED. Do not GPIO-direct. Do not aim at the TV IR window.
3. After solder: TV Info still works, then Soundbar VOL± (`http://192.168.72.21/` Soundbar tab). Firmware 0.3.4 already sends 1 NEC data frame + 3 AGC repeats.
4. Second LED (forward) only if step 3 is still deaf.
5. One long RMT stream for holds — later. Rewire TK64 if analog echo is wanted.

## Not in scope (v0)

- 850 nm second LED
- TV-B-Gone blast-all
- Home Assistant MQTT discovery
- OTA firmware updates
- Classic ESP32 USB gadget mode (UART via CP2102 only)
