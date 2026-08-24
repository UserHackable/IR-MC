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

Both the **listen** and **blast** modules are USB-serial on **Haruna**
(`192.168.72.3`, SSH `haruna`). Build and flash there:

```bash
ssh haruna
cd ~/User-Hackable/IR-MC
./scripts/fw idf upload ir_listen   # GPIO19 TSOP
./scripts/fw idf upload ir_blast    # GPIO18 LED
./scripts/fw idf nvs-wifi
```

Override the port when more than one adapter is present:
`PORT=/dev/ttyUSB1 ./scripts/fw idf upload ir_blast`.

Serial devices are in group `uucp`. ESP-IDF lives at `~/esp/esp-idf`
(v5.3.2), same path as other UserHackable ESP32 machines.

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
