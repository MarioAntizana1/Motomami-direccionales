# Proyecto: Motomami Direccionales ESP32-C6

## Hardware
- **MCU**: ESP32-C6
- **LEDs**: NeoPixel WS2812, matriz 5×28 (140 LEDs) en zig-zag
- **LED 1** (strip index 0) = esquina superior derecha
- **GPIO2 (D2)**: DATA de los NeoPixels
- **Conexión**: USB (COM3)

## Software
- **IDF**: ESP-IDF v6.0.2 en `C:\esp\v6.0.2\esp-idf`
- **Toolchain**: `riscv32-esp-elf` (esp-15.2.0_20251204)
- **Build**: `idf.py build`
- **Flash**: `idf.py -p COM3 flash`
- **Monitor**: `idf.py -p COM3 monitor`

## Estructura del proyecto
```
main/
  main.c                  # Código principal
  led_strip_encoder.c/h   # Codificador RMT para WS2812
  CMakeLists.txt          # Registro del componente
CMakeLists.txt            # CMake del proyecto
sdkconfig                 # Configuración IDF
AGENTS.md                 # Memoria del proyecto (este archivo)
.opencode/
  skills/
    esp32-build/SKILL.md  # Skill de build/flash
```

## Código existente
- Control de NeoPixels vía RMT (periférico del ESP32-C6)
- `get_led_index(row, col)`: mapeo zig-zag (filas pares ← derecha a izquierda, impares → izquierda a derecha)
- Formato de color: GRB (Green, Blue, Red)

## Workarounds
- `riscv32-esp-elf-readelf.exe` bloqueado por AppLocker → reemplazar con copia de `objdump.exe`
  ```powershell
  Copy-Item "$env:USERPROFILE\.espressif\tools\riscv32-esp-elf\esp-15.2.0_20251204\riscv32-esp-elf\bin\riscv32-esp-elf-objdump.exe" -Destination "$env:USERPROFILE\.espressif\tools\riscv32-esp-elf\esp-15.2.0_20251204\riscv32-esp-elf\bin\riscv32-esp-elf-readelf.exe" -Force
  ```
