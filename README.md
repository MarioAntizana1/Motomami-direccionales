# Motomami Direccionales - ESP32-C6

Control de matriz NeoPixel WS2812 de 5×28 (140 LEDs) con ESP32-C6 vía RMT.

## Hardware

| Componente | Detalle |
|------------|---------|
| MCU | ESP32-C6 (RISC-V) |
| LEDs | NeoPixel WS2812, matriz 5×28 en zig-zag |
| DATA pin | GPIO2 (D2) |
| Conexión | USB (COM3) |

### Layout de la matriz

Los LEDs se conectan en zig-zag de 5 filas × 28 columnas:

- **LED 1** (strip index 0) = esquina superior derecha
- Filas pares (0, 2, 4): → derecha a izquierda
- Filas impares (1, 3): ← izquierda a derecha

La función `get_led_index(row, col)` en `main/main.c` mapea coordenadas (fila, columna) al índice lineal del strip.

## Software

| Herramienta | Versión / Ruta |
|-------------|----------------|
| ESP-IDF | v6.0.2 en `C:\esp\v6.0.2\esp-idf` |
| Toolchain | `riscv32-esp-elf` (esp-15.2.0_20251204) |
| Python | 3.14 (entorno virtual IDF) |

### Comandos rápidos

```powershell
idf.py build                        # Compilar
idf.py -p COM3 flash                # Grabar al ESP32-C6
idf.py -p COM3 monitor              # Consola serie
idf.py -p COM3 build flash monitor  # Todo en uno
```

## Estructura del proyecto

```
├── main/
│   ├── main.c                   # Código principal (NeoPixel matrix control)
│   ├── led_strip_encoder.c      # Codificador RMT para WS2812
│   ├── led_strip_encoder.h      # Header del encoder
│   └── CMakeLists.txt           # Registro del componente
├── CMakeLists.txt               # CMake del proyecto
├── sdkconfig                    # Configuración IDF
├── AGENTS.md                    # Memoria del proyecto (para opencode)
├── opencode.json                # Configuración de opencode
├── graphify-out/                # Grafo de conocimiento del proyecto
│   ├── graph.json
│   ├── GRAPH_REPORT.md
│   └── graph.html
└── .opencode/
    └── skills/
        └── esp32-build/SKILL.md # Skill de build/flash para opencode
```

## Changelog

### 2026-07-19 — Inicialización del proyecto

- Configuración del proyecto ESP-IDF v6.0.2 para ESP32-C6
- Implementación del control de NeoPixels WS2812 vía RMT (periférico del ESP32-C6)
- Matriz 5×28 en zig-zag con mapeo `get_led_index(row, col)`
- Encender todos los LEDs en blanco por 10 segundos (prueba inicial)
- Workaround para AppLocker: `readelf.exe` bloqueado → reemplazado por `objdump.exe`
- Configuración de opencode: skill `esp32-build`, AGENTS.md con reglas de workflow automático
- Graphify: grafo de conocimiento del código (20 nodos, 24 edges, 5 comunidades)
- Primer commit y push a `main` en GitHub
