# AGENTS.md — IR-MC

Project instructions for agents. Overrides global `~/.grok/AGENTS.md` on conflict.

Human overview: [README.md](README.md). Keep this file and the README aligned when goals or git contracts change.

## Product

**UserHackable** utility: a **classic ESP32** that joins Wi‑Fi and acts as a
**network-addressable IR transceiver**. MQTT is the primary control surface;
HTTP is for testing and clients without a broker.

| Item | Detail |
|------|--------|
| MCU | Classic ESP32 (WROOM / 38-pin DevKitC-style) |
| IR TX | RMT on **GPIO 18**, 940 nm LED via transistor/MOSFET from 5 V |
| Primary apps | `ir_bridge` (TX+RX), `ir_listen` (RX GPIO19), `ir_blast` (TX GPIO18) |
| Out of scope (v0) | Photodiode carrier measure, 850 nm, TV-B-Gone, HA discovery, OTA |

## Hardware / firmware (agent-facing)

| Role | Part |
|------|------|
| Board | ESP32 Type-C 38-pin narrow DevKitC-style; USB-UART often CP2102 or CH340 |
| IR LED | 940 nm, high-power, **not** GPIO-direct |
| Default console | UART 115200 |

### Toolchain

- Prefer **CLI + vim**, not Arduino IDE GUI. [docs/toolchain.md](docs/toolchain.md).
- **ESP-IDF multi-app** under [`esp32_firmware/apps/`](esp32_firmware/apps/). IDF target **`esp32`**.
- **Single CLI:** [`scripts/fw`](scripts/fw) — `./scripts/fw idf upload ir_bridge`.
- Do not commit ESP-IDF clones or `~/esp/esp-idf`.
- Arduino track is out of scope unless asked.

## Safety / secrets

- Never commit secrets, Wi‑Fi passwords, or MQTT passwords.
- Wi‑Fi: real passwords only in `secrets/wifi.yaml` (gitignored). Provision with `./scripts/fw idf nvs-wifi`.
- MQTT: optional `secrets/mqtt.yaml` is packed into the **same** NVS image (flashing NVS wipes the whole partition).
- IR LEDs are invisible; do not stare into high-power emitters at close range.
- Do not drive the IR LED from an ESP32 pin without a transistor/MOSFET.

## Git

| Setting | Value |
|---------|-------|
| Default branch | `master` |
| Canonical remote | `github` → `git@github.com:UserHackable/IR-MC.git` |
| Mirror remote | `gitlab` → `git@gitlab.com:user-hackable/IR-MC.git` |

Dual-push after meaningful shared work:

```bash
git push github && git push gitlab
```

### Phrase shortcuts

| Phrase | Means |
|--------|--------|
| **send it** | Commit if needed (only when asked to commit), then **dual-push** `github` and `gitlab`. |
| **ship it** / **cut a release** | Same as **send it** until a release doc exists. |

**Push policy:** Do **not** push unless the user says **send it** (or **ship it** / **cut a release**).

Never force-push `master` unless the user explicitly asks.

## Style

- Prefer **minimal, focused diffs**. No drive-by refactors.
- Match sibling UserHackable ESP32 repos (`scripts/fw`, NVS Wi‑Fi, multi-app IDF).
- Prefer complete sentences in commit messages: *what* and *why*.
