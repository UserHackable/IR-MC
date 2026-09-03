# Project status (snapshot)

Last updated: **2026-09-03** (TV bounce works; soundbar bounce path works with the handheld, blaster is too weak).

## Live rig (Haruna)

One classic ESP32 on Haruna USB (`/dev/ttyUSB0`, CP2102), running **`ir_bridge`**:

| Item | Value |
|------|--------|
| Identity | `ir-mc-582abd78af4c` |
| Name | `ir` / `ir.lan` (`ir-mc.lan` still aliases) |
| IP | `192.168.72.20` (static, below GFiber DHCP) |
| App | `ir_bridge` 0.3.3 |
| TK16 (TX LED) | GPIO **18** |
| TK15 (demod RX) | GPIO **19**, 3.3 V |
| TK64 (raw phototransistor) | GPIO **34** ADC, 3.3 V |

Flash/build on Haruna. Stop the serial logger first:

```bash
sudo systemctl stop ir-mc-serial-log
./scripts/fw idf upload ir_bridge
sudo systemctl start ir-mc-serial-log
```

Phone remotes: `http://ir.lan/` or `http://192.168.72.20/` (Sceptre + soundbar). Lab UI: `/lab`.

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
- TK16 driven **GPIO-direct** on GPIO 18 (current Haruna board): weaker at a distance; **too hot** when the LED sits centimetres from the TV IR window. Inverse-square: GPIO-direct at ~2 in can still outshine a 100 mA blast at 8 ft.
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

## Next bench (solder)

1. Handheld soundbar remote **works** from the same ceiling bounce. Path is fine; GPIO-direct is too weak. See [hardware.md Stronger ceiling LED](hardware.md).
2. Solder **2N2222 + 5 V** on the existing ceiling-aimed LED. Unplug USB. Do not GPIO-direct. Do not aim at the TV IR window.
3. After solder: TV Info still works, then Soundbar VOL± (`http://ir.lan/` Soundbar tab). Firmware 0.3.3 already sends soundbar NEC **repeat 3**.
4. Second LED (forward) only if step 3 is still deaf.
5. One long RMT stream for holds — later. Rewire TK64 if analog echo is wanted.

## Not in scope (v0)

- 850 nm second LED
- TV-B-Gone blast-all
- Home Assistant MQTT discovery
- OTA firmware updates
- Classic ESP32 USB gadget mode (UART via CP2102 only)
