# Telemetría BMP280 en Heltec Wireless Tracker (heltec_tracker)

Esta variante del firmware companion de MeshCore añade un sensor ambiental
BMP280 (temperatura, presión barométrica, altitud) a la respuesta estándar
de telemetría que ya existe en la app companion — junto con la posición GPS
que este board ya reporta.

- **Target de hardware**: Heltec Wireless Tracker v1, con módulo LoRa
  SX1262 (entorno base PlatformIO `Heltec_tracker_base`, del cual heredan
  `Heltec_Wireless_Tracker_companion_radio_usb`,
  `Heltec_Wireless_Tracker_companion_radio_ble`,
  `Heltec_Wireless_Tracker_repeater`,
  `Heltec_Wireless_Tracker_repeater_bridge_espnow`,
  `Heltec_Wireless_Tracker_room_server` y
  `Heltec_Wireless_Tracker_kiss_modem`).
- **Punto de enganche en el código**: `sensors.querySensors(...)`, ya
  existente y sin cambios en `examples/companion_radio/MyMesh.cpp` — este
  firmware no añade ningún comando nuevo, reutiliza el mecanismo binario de
  telemetría (CayenneLPP) que la app companion ya usa para pedir "self
  telemetry".
- **Driver del sensor**: `src/helpers/sensors/EnvironmentSensorManager.h/.cpp`
  (genérico, compartido con otras placas del repo — no específico de este
  board). El GPS se mantiene funcionando igual que antes: esta clase acepta
  el mismo `LocationProvider` que la implementación anterior
  (`HWTSensorManager`, ahora eliminada de esta variante).

## Cómo funciona

1. Al arrancar, `EnvironmentSensorManager::begin()` escanea el bus I2C
   principal (`Wire`, en los pines `PIN_BOARD_SDA`/`PIN_BOARD_SCL` de este
   board) probando qué direcciones responden — **antes** de tocar
   cualquier librería de sensor. Si no hay un BMP280 conectado, el escaneo
   simplemente no lo encuentra y el firmware sigue funcionando con GPS
   solamente, sin errores ni cuelgues.
2. Si la dirección `0x76` responde, se inicializa el `Adafruit_BMP280` y el
   sensor queda activo.
3. Cuando la app companion pide telemetría propia (self telemetry), el
   firmware añade al payload CayenneLPP: temperatura (°C), presión
   barométrica (hPa) y altitud estimada (m, calculada desde la presión con
   1013.25 hPa como referencia de nivel del mar) — en un canal LPP
   independiente del canal de GPS.
4. No hay comando de texto ni respuesta por canal de grupo — a diferencia
   del actuador de relay de otra variante de este repo, esto es telemetría
   estándar, no un comando por chat.

## Pines I2C del heltec_tracker

Este board ya trae un bus I2C activo por defecto (usado también por la
pantalla y otros periféricos internos vía `PIN_BOARD_SDA`/`PIN_BOARD_SCL`,
inicializado automáticamente por `ESP32Board::begin()`). El BMP280 se
conecta a ese mismo bus — no necesita pines dedicados ni un segundo bus I2C:

| Función | GPIO | Build flag |
|---|---|---|
| SDA | 45 | `PIN_BOARD_SDA` |
| SCL | 46 | `PIN_BOARD_SCL` |

## Pinout del sensor BMP280

Los breakout boards de BMP280 más comunes (ej. GY-BMP280, variantes con 4 o
6 pines) traen esta serigrafía:

| Pin del módulo BMP280 | Función | Conectar a (heltec_tracker) |
|---|---|---|
| `VCC` (o `VIN`) | Alimentación, 3.3 V | 3V3 |
| `GND` | Tierra | GND |
| `SCL` | Reloj I2C | GPIO 46 (`PIN_BOARD_SCL`) |
| `SDA` | Datos I2C | GPIO 45 (`PIN_BOARD_SDA`) |
| `CSB` (solo en módulos de 6 pines) | Chip Select | dejar sin conectar o a `VCC` (fuerza modo I2C; en modo SPI el sensor no sirve para este firmware) |
| `SDO` (solo en módulos de 6 pines) | Selección de dirección I2C | **a `GND`** — ver nota abajo |

```
        BMP280 breakout (vista superior, 6 pines típico)
        ┌─────────────────────────┐
        │  ┌───────────────────┐  │
        │  │      BMP280       │  │
        │  │   (chip sensor)   │  │
        │  └───────────────────┘  │
        │                         │
        │  VCC GND SCL SDA CSB SDO│
        └───┬────┬───┬───┬───┬──┬─┘
            │    │   │   │   │  │
           3V3  GND GPIO GPIO │ GND
                    46  45   │  (fija dirección 0x76)
                          (a VCC o sin conectar → modo I2C)
```

**Nota sobre la dirección I2C — importante**: este firmware usa la
dirección `0x76` por defecto (`TELEM_BMP280_ADDRESS`, definida en
`src/helpers/sensors/EnvironmentSensorManager.cpp`). El pin `SDO` del
BMP280 selecciona la dirección:

- `SDO` a `GND` → dirección `0x76` (la que este firmware espera).
- `SDO` a `VCC` → dirección `0x77` (el escaneo de arranque **no** la
  detectará con la configuración actual; el sensor simplemente no
  aparecerá en la telemetría).

Si tu módulo trae `SDO` flotante por diseño (sin pull-down), asegúrate de
puentearlo a `GND` explícitamente — dejarlo al aire puede leer cualquiera
de las dos direcciones de forma inconsistente entre arranques.

Módulos de 4 pines (`VCC`, `GND`, `SCL`, `SDA` únicamente) ya vienen fijos
en modo I2C con dirección `0x76` de fábrica en la gran mayoría de los casos
— conéctalos directo sin pines adicionales.

## Habilitar / deshabilitar GPS

El GPS sigue expuesto igual que antes vía el setting `"gps"` (activado por
defecto en OFF hasta que la app companion lo enciende) — este cambio no
modifica ese comportamiento, solo añade el sensor ambiental al mismo
`querySensors()`.

## Compilar el firmware

```bash
pio run -e Heltec_Wireless_Tracker_companion_radio_ble
# o _usb, _repeater, _repeater_bridge_espnow, _room_server, _kiss_modem
```

Si prefieres el flujo con `build.sh` (nombre de archivo versionado, copiado
a `out/`), revisa `README_RELAY_GPIO.md` en este mismo repo — el
procedimiento de `build.sh` es idéntico independientemente de la placa,
solo cambia el nombre del entorno de PlatformIO.

## Verificar en campo

1. Flashea el firmware y conecta la app companion (USB o BLE según el env).
2. Pide telemetría propia desde la app ("self telemetry" / node info).
3. Si el BMP280 está bien cableado, la respuesta debe incluir temperatura,
   presión y altitud en un canal LPP distinto al de la posición GPS. Si el
   sensor no aparece, confirma con un escáner I2C (`MESH_DEBUG=1` en el
   build imprime qué direcciones se detectaron durante el arranque) que
   `0x76` realmente responde antes de sospechar del firmware.

## Ver también

- `docs/superpowers/plans/2026-09-03-heltec-tracker-bmp280-telemetry.md` —
  plan de implementación completo, incluyendo las decisiones sobre por qué
  se reemplazó `HWTSensorManager` por `EnvironmentSensorManager` en esta
  variante.
