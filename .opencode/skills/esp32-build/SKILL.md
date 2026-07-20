---
name: esp32-build
description: Use when building, compiling, flashing, or monitoring ESP32-C6 projects with ESP-IDF v6.0.2. Also use when the user mentions "compilar", "subir", "grabar", "flash", "build", "monitor", or similar ESP32 dev workflow terms. Do NOT use for general C/C++ questions unrelated to ESP32.
---

# ESP32-C6 Build Skill

This project targets **ESP32-C6** using **ESP-IDF v6.0.2** (installed at `C:\esp\v6.0.2\esp-idf`).

## Pre-requisites

Before any build command, the ESP-IDF environment must be exported:

```powershell
Set-Location 'C:\esp\v6.0.2\esp-idf'; .\export.ps1
```

## Build

```powershell
idf.py build
```

## Flash (upload to board via USB)

If the board is on a specific COM port:

```powershell
idf.py -p COM3 flash
```

If unsure of the port, detect it first:

```powershell
idf.py -p COM3 flash
```

Common COM ports for ESP32-C6: `COM3`, `COM4`, `COM5`.

## Monitor (serial console)

```powershell
idf.py -p COM3 monitor
```

## Combined: Build + Flash + Monitor

```powershell
idf.py -p COM3 build flash monitor
```

## Clean

```powershell
idf.py clean       # clean project build artifacts
idf.py fullclean   # full clean including CMake cache
```

## Project structure

```
main/
  main.c             # Main application code (NeoPixel WS2812 matrix 5x28 on GPIO2)
  led_strip_encoder.c/h  # RMT encoder for WS2812 (from ESP-IDF example)
  CMakeLists.txt     # Component registration
CMakeLists.txt       # Project-level CMake
sdkconfig            # ESP-IDF configuration
```

## PIN Mapping

| Pin | Function |
|-----|----------|
| D2 (GPIO2) | NeoPixel WS2812 data |

## LED Matrix Layout

Matrix 5 rows x 28 cols = 140 LEDs, zig-zag:
- LED 1 (index 0) = top-right corner
- Even rows: right-to-left
- Odd rows: left-to-right

Function `get_led_index(row, col)` maps (row, col) to strip index.

## Known issues

- Windows AppLocker/Application Control may block `riscv32-esp-elf-readelf.exe`.
  Workaround: copy `objdump` over `readelf`:
  ```powershell
  Copy-Item "$env:USERPROFILE\.espressif\tools\riscv32-esp-elf\esp-15.2.0_20251204\riscv32-esp-elf\bin\riscv32-esp-elf-objdump.exe" -Destination "$env:USERPROFILE\.espressif\tools\riscv32-esp-elf\esp-15.2.0_20251204\riscv32-esp-elf\bin\riscv32-esp-elf-readelf.exe" -Force
  ```
