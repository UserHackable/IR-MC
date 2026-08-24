# Project status (snapshot)

Last updated: **2026-08-24** (bench wrap-up).

## Live rig (Haruna)

One classic ESP32 on Haruna USB (`/dev/ttyUSB0`, CP2102), running **`ir_bridge`**:

| Item | Value |
|------|--------|
| Identity | `ir-mc-582abd78af4c` |
| IP | `192.168.72.143` |
| App | `ir_bridge` 0.2.0 |
| TK16 (TX LED) | GPIO **18** |
| TK15 (demod RX) | GPIO **19**, 3.3 V |
| TK64 (raw phototransistor) | GPIO **34** ADC, 3.3 V |

Flash/build on Haruna. Stop the serial logger first:

```bash
sudo systemctl stop ir-mc-serial-log
./scripts/fw idf upload ir_bridge
sudo systemctl start ir-mc-serial-log
```

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

## Range / drive

- Transistor + 3×100 Ω (~33 Ω) + IR LED on the original `ir-mc-489d31e61010`: ~**95 mA**. Volume reliable inside ~**8 ft**, iffy beyond.
- TK16 driven **GPIO-direct** on GPIO 18 (current Haruna board): weaker; TV only acted on **some** of a continuous volume-up loop. HTTP bursts leave gaps; TK15 on the same board also drops some repeats (`n=128` jammed captures when TX floods RX).

## Done (firmware)

- `ir_bridge` / `ir_listen` / `ir_blast` roles; RMT RX idle fix
- LED anode sense `GET/POST /adc/led` (GPIO18 DC, ADC GPIO32, ×2 estimate)
- TK64 analog `GET /adc/tk64`; learn JSON includes `tk64` min/max/mean
- `IRMSG` UART lines + Haruna systemd logger

## Next bench

1. Stronger TK16 path (transistor + ~33 Ω) on the Haruna board if 8 ft+ is required
2. One long RMT stream (no HTTP gap) for holds
3. Confirm discrete 47 vs toggle 21 on a known-on TV from ~1 ft
4. Wire TK64 to GPIO 34 if not already, compare analog vs TK15

## Not in scope (v0)

- 850 nm second LED
- TV-B-Gone blast-all
- Home Assistant MQTT discovery
- OTA firmware updates
- Classic ESP32 USB gadget mode (UART via CP2102 only)
