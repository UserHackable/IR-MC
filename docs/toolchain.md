# Toolchain

CLI-first **ESP-IDF** for classic **esp32**. No Arduino track in the scaffold
(the spec prefers native RMT over Arduino IR libraries).

## Prerequisites

- Linux host, **mise** (cmake/ninja via `esp32_firmware/mise.toml`)
- USB serial access (`uucp` / `dialout`)
- Shared IDF install: `~/esp/esp-idf` (default), version pinned in `install-idf.sh`

## Commands

```bash
./scripts/fw idf install              # once: IDF + esp32 tools
./scripts/fw idf list
./scripts/fw idf build  ir_bridge
./scripts/fw idf upload ir_bridge     # build + flash
./scripts/fw idf monitor ir_bridge
./scripts/fw idf nvs-wifi
./scripts/fw port
```

Override serial port: `PORT=/dev/ttyUSB0 ./scripts/fw idf upload ir_bridge`.

Apps: `ir_bridge` (TX+RX), `ir_listen` (RX-only), `ir_blast` (TX-only).

## Build host (Haruna)

One ESP32 on **Haruna** (`192.168.72.3`, SSH `haruna`, `/dev/ttyUSB0`).
Prefer **`ir_bridge`** (TX + RX + TK64 ADC):

```bash
ssh haruna
cd ~/User-Hackable/IR-MC
sudo systemctl stop ir-mc-serial-log   # USB serial logger holds the port
./scripts/fw idf upload ir_bridge
sudo systemctl start ir-mc-serial-log
```

IR lines (`IRMSG`) are appended to `~/User-Hackable/IR-MC/logs/ir-serial.log`.
Unit: `ir-mc-serial-log.service`.

Serial group `uucp`. ESP-IDF at `~/esp/esp-idf` (v5.3.2).

## Layout

```text
esp32_firmware/
  mise.toml
  scripts/          # build flash monitor install export-idf
  apps/<name>/      # each is an idf.py project
```

Do not commit `apps/*/build/` or `sdkconfig` (local). Defaults live in
`sdkconfig.defaults` per app (`CONFIG_IDF_TARGET="esp32"`, flash size, custom
partition table with SPIFFS).
