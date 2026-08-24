# IR-MC

UserHackable **network-addressable IR transceiver**: an ESP32 on Wi‑Fi that
receives commands from phones, computers, or automation and transmits the
matching infrared remote signal.

MQTT is the everyday control path. HTTP is there so you can test with `curl`
or a browser without a broker. Split apps cover a dedicated **listener**
(TSOP learn on GPIO 19) and a dedicated **blaster** (RMT TX on GPIO 18).

```text
  phone / PC / Home Assistant
           │  MQTT or HTTP
           ▼
     [ ESP32 IR-MC ]
           │  RMT → transistor
           ▼
      940 nm IR LED  →  TV, AC, …
```

## Product shape (v0)

| Principle | Choice |
|-----------|--------|
| MCU | Classic **ESP32** (WROOM / 38-pin DevKitC-style) |
| Network | STA joins home Wi‑Fi (NVS credentials); UDP announce + HTTP + MQTT |
| IR TX | ESP32 **RMT**, per-code carrier frequency, GPIO **18** |
| LED | 940 nm, high-current drive from **5 V** (transistor / MOSFET) |
| Codes | Named `device/command` in SPIFFS JSON + compiled-in `demo` fallback |
| Raw | Timings + frequency on the fly (`POST /ir/send` or `ir/raw`) |

Living spec: [ESP32-IR-Remote-Bridge.md](ESP32-IR-Remote-Bridge.md).

## Status

Firmware compiles against ESP-IDF 5.3.x. Bench modules are on the LAN:
**listen** (`ir-rx-*`, GPIO 19 TSOP) and **blast** (`ir-tx-*`, GPIO 18 LED).
Sceptre volume-up/down (Sony 12-bit addr 1 / cmd 18 and 19) were learned and
replayed over IR.

| Piece | State |
|-------|--------|
| Docs (architecture, hardware, API) | Landed |
| CLI `./scripts/fw` (IDF multi-app, target `esp32`) | Landed |
| App `ir_bridge` (TX + RX) | Landed |
| App `ir_listen` (RX-only, GPIO 19) | Landed |
| App `ir_blast` (TX-only, GPIO 18) | Landed |
| Learn / TSOP receive | Landed |
| Home Assistant discovery | Future |
| OTA | Future |

## Quick start

```bash
cd ~/User-Hackable/IR-MC

./scripts/fw idf install
cp secrets/wifi.yaml.example secrets/wifi.yaml   # edit real SSID/pass
cp secrets/mqtt.yaml.example secrets/mqtt.yaml   # optional local broker
./scripts/fw idf nvs-wifi
./scripts/fw idf upload ir_bridge
./scripts/fw idf monitor ir_bridge
# then open http://<ip>:8080/
```

Port override: `PORT=/dev/ttyUSB0 ./scripts/fw idf upload ir_bridge`.

Wire GPIO18 through a transistor to a 940 nm LED on 5 V. See
[docs/hardware.md](docs/hardware.md).

```bash
curl -X POST http://<ip>:8080/ir/demo/power
mosquitto_pub -h <broker> -t ir/demo/power -m ''
```

## Layout

```text
IR-MC/
  README.md
  AGENTS.md
  ESP32-IR-Remote-Bridge.md   # living specification
  docs/
    architecture.md
    hardware.md
    api.md
    wifi-config.md
    toolchain.md
  scripts/fw                  # idf | port | nvs-wifi
  esp32_firmware/apps/        # ir_bridge, ir_listen, ir_blast, …
  secrets/                    # wifi.yaml / mqtt.yaml (gitignored)
  data/codes.example.json
```

## Docs

| Doc | Topic |
|-----|--------|
| [docs/STATUS.md](docs/STATUS.md) | Snapshot: done / next |
| [docs/architecture.md](docs/architecture.md) | Control path, catalog, discovery |
| [docs/hardware.md](docs/hardware.md) | Pins, LED drive, BOM |
| [docs/api.md](docs/api.md) | MQTT topics + HTTP endpoints |
| [docs/wifi-config.md](docs/wifi-config.md) | NVS Wi‑Fi / MQTT provision |
| [docs/toolchain.md](docs/toolchain.md) | ESP-IDF CLI |

## Related projects

| Repo | Relation |
|------|----------|
| [ESP32_CAM](../ESP32_CAM/) | Same classic ESP32 CLI / NVS / IDF layout |
| [ESP32-Remote-Programmer](../ESP32-Remote-Programmer/) | Sibling ESP32-S3 utility (same `scripts/fw` idea) |

## Git remotes

| Remote | URL | Role |
|--------|-----|------|
| `gluttony` | `git@gluttony:UserHackable/IR-MC.git` | Canonical (LAN) |
| `github` | `git@github.com:UserHackable/IR-MC.git` | Mirror (when created) |
| `gitlab` | `git@gitlab.com:user-hackable/IR-MC.git` | Mirror |

**send it** (agent phrase): commit if needed, then push `gluttony` (and GitHub/GitLab
when those remotes exist). See [AGENTS.md](AGENTS.md).
