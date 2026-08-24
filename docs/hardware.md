# Hardware

Target for v0: **classic ESP32** (WROOM / 38-pin DevKitC-style), same family as
[ESP32_CAM](../../ESP32_CAM/docs/esp32-board.md). USB-UART for flash and
console. IR LED is driven from **5 V**, not the 3.3 V rail.

## Pins

| Function | GPIO | Notes |
|----------|------|--------|
| IR transmit (RMT) | **18** | Active high → transistor on → LED on |
| Status LED | **2** | Onboard LED on many DevKits; on during TX |
| Button | **0** | BOOT button after boot sends `demo/power` |
| IR receive (TSOP) | **19** | 38 kHz demodulator, idle HIGH; next to GPIO 18 |
| Raw photodiode (future) | 34/35 | Input-only; unused in v0 |

Override `IR_TX_GPIO` at compile time if the wiring differs.

## LED drive (required)

Do **not** drive a high-power IR LED directly from an ESP32 GPIO. Use a
transistor as a low-side switch. **Use the 2N2222**, not the 2N3904 (the
3904 is only 200 mA absolute max).

### Bench wiring (USB-C at the top)

| Net | Board pin | Where |
|-----|-----------|--------|
| GPIO 18 | silk **18** | **Right** header, **9th down** from USB |
| 5 V | silk **5V** | **Left** header, **bottom** pin |
| GND | silk **GND** | Right header **top**, or right header 7th down |

Unplug USB while you solder / jumper. Shorting 5 V to a GPIO will kill the ESP32.

```text
                    5V  (left header, bottom)
                     │
                 [ Rled ]
                     │
                 IR LED anode (+)
                     │
                 IR LED cathode (−)
                     │
                  collector
 GPIO18 ──[ Rb ]── base          2N2222
                  emitter
                     │
                    GND
```

### 2N2222 TO-92 pinout

Flat face **toward you**, leads down, left → right is usually **E B C**:

```text
       flat
      ┌─────┐
      │     │
      E  B  C
```

Some markings (especially **P2N2222A**) are **C B E** — the reverse. If the
LED never lights, swap the two outer leads and try once. Confirm with a diode
test: base→emitter and base→collector both look like ~0.6 V diodes.

### Resistors

| Role | First-test (5 mm LED, ~50 mA) | Stronger blast (~150 mA) |
|------|-------------------------------|---------------------------|
| **Rb** (GPIO → base) | 470 Ω | 330 Ω |
| **Rled** (5 V → LED) | 68 Ω (¼ W) | 22 Ω (½ W) |

Rled ≈ (5.0 − Vf − 0.2) / Ipeak. Typical 940 nm Vf is 1.2–1.6 V.

A 1 kΩ base resistor is too weak to saturate the 2N2222 at IR-blast current.
Stay under ~12 mA out of the GPIO: 330 Ω gives about 8 mA of base current.

If you have no IR LED yet, a **visible red LED** + 220 Ω as Rled is a valid
smoke test: it should flash on `POST /ir/demo/power` (carrier looks like a
dim glow). Then swap in 940 nm.

Wavelength: **940 nm** is the consumer-remote standard. 850 nm is optional /
experimental and not wired in v0.

## IR receiver (learn)

Part: **TSOP38238**, **TSOP38438**, or cheap **VS1838B** — 38 kHz demodulator.
Power it from **3.3 V**, not 5 V (OUT must be 3.3 V-safe for the ESP32).

| TSOP pin (dome facing you, pins down — typical VS1838 / TSOP38238) | ESP32 |
|------------------------------------------------------------------|-------|
| OUT (left) | GPIO **19** (right header, **8th down**, silk `19`) |
| GND (middle) | GND |
| VCC (right) | **3V3** |

Some brands reverse OUT/VCC. If learn never triggers, swap the outer pins.

```text
  3V3 ── TSOP VCC
  GND ── TSOP GND
  GPIO19 ── TSOP OUT     (idle HIGH, pulses LOW on IR)
```

GPIO 19 sits one pin above GPIO 18 on the right header.

After wiring:

```bash
./scripts/fw idf upload ir_bridge
curl -X POST http://IR_MC_IP:8080/ir/learn \
  -H 'content-type: application/json' \
  -d '{"timeout_ms":8000,"device":"sceptre","command":"power"}'
```

Then point the Sceptre remote at the dome and press **Power**. The JSON is saved and can be re-sent with `POST /ir/sceptre/power`.

## Transistor identifier (`tx_probe`)

Header pins 3–5 (silk **VP / VN / 34** = GPIO36, 39, 34) are **ADC input-only**.
They cannot drive a base, so they cannot identify a TO-92 pinout by themselves.

Use the next three pins down instead — all are GPIO in **and** out:

| Left header (USB-C at top) | Silk | GPIO | Why |
|----------------------------|------|------|-----|
| 7th | **32** | GPIO32 | ADC1, output OK |
| 8th | **33** | GPIO33 | ADC1, output OK |
| 9th | **25** | GPIO25 | ADC2/DAC, output OK (no Wi‑Fi in this app) |

Put **1 kΩ in series on each lead** (limits GPIO current if you guess E/B/C wrong).
No 5 V, no IR LED — just the transistor and three resistors.

```text
  GPIO32 ── 1 kΩ ── TO-92 lead
  GPIO33 ── 1 kΩ ── TO-92 lead
  GPIO25 ── 1 kΩ ── TO-92 lead
```

Any rotation is fine. Firmware maps diodes, then saturates to tell E from C.

```bash
./scripts/fw idf upload tx_probe
./scripts/fw idf monitor tx_probe
```

Expect a line like `RESULT  NPN  GPIO32=E  GPIO33=B  GPIO25=C`. That tells you
which way to plug the same 2N2222 into the IR driver (GPIO18 / 5 V / GND).

## Power

USB 5 V is enough for the ESP32 plus a pulsed IR LED. Keep LED current off the
3.3 V regulator.

## BOM (minimum)

| Qty | Part |
|-----|------|
| 1 | ESP32 38-pin DevKitC-style (this board) |
| 1 | **2N2222** TO-92 (not 2N3904) |
| 1 | 940 nm IR LED (or a visible red LED for the first test) |
| 1 | Rb 330–470 Ω |
| 1 | Rled 22–68 Ω (see table above) |
| — | Hookup wire / protoboard; 5 V and GND from the DevKit |
