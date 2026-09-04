# Telemetría BMP280 en Heltec Wireless Tracker (heltec_tracker), env repeater

Este firmware añade un sensor ambiental BMP280 (temperatura, presión
barométrica, altitud) a la respuesta estándar de telemetría — junto con la
posición GPS que este board ya reporta. **Aplica solo al entorno PlatformIO
`Heltec_Wireless_Tracker_repeater`** — es el único build de este board que
compila el driver BMP280; los demás entornos (`companion_radio_usb`,
`companion_radio_ble`, `repeater_bridge_espnow`, `room_server`,
`kiss_modem`) no lo incluyen.

- **Target de hardware**: Heltec Wireless Tracker v1, con módulo LoRa
  SX1262, compilado como repetidor (entorno PlatformIO
  `Heltec_Wireless_Tracker_repeater`).
- **Punto de enganche en el código**: `sensors.querySensors(...)`, ya
  existente y sin cambios en `examples/companion_radio/MyMesh.cpp` — este
  firmware no añade ningún comando nuevo, reutiliza el mecanismo binario de
  telemetría (CayenneLPP) que la app companion ya usa para pedir telemetría
  a un nodo remoto de la malla (el repetidor no tiene conexión directa
  USB/BLE con el teléfono; la telemetría llega enrutada por la malla).
- **Driver del sensor**: `src/helpers/sensors/EnvironmentSensorManager.h/.cpp`
  (genérico, compartido con otras placas del repo — no específico de este
  board). El GPS se mantiene funcionando, pero con algunos cambios de
  comportamiento respecto a la implementación anterior — ver la sección
  "GPS: qué cambió" más abajo.

## Cómo funciona

1. Al arrancar, `EnvironmentSensorManager::begin()` escanea el bus I2C
   principal (`Wire`, en los pines `PIN_BOARD_SDA`/`PIN_BOARD_SCL` de este
   board) probando qué direcciones responden — **antes** de tocar
   cualquier librería de sensor. Si no hay un BMP280 conectado, el escaneo
   simplemente no lo encuentra y el firmware sigue funcionando con GPS
   solamente, sin errores ni cuelgues.
2. Si la dirección `0x76` responde, se inicializa el `Adafruit_BMP280` y el
   sensor queda activo.
3. Cuando llega una solicitud de telemetría propia (self telemetry) por la
   malla, el firmware añade al payload CayenneLPP: temperatura (°C),
   presión barométrica (hPa) y altitud estimada (m, calculada desde la
   presión con 1013.25 hPa como referencia de nivel del mar) — en un canal
   LPP independiente del canal de GPS.
4. No hay comando de texto ni respuesta por canal de grupo — a diferencia
   del actuador de relay de otra variante de este repo, esto es telemetría
   estándar, no un comando por chat.

## Pines I2C del heltec_tracker

Este board trae un bus I2C (`Wire`, en `PIN_BOARD_SDA`/`PIN_BOARD_SCL`)
inicializado automáticamente por `ESP32Board::begin()`, pero **sin ningún
dispositivo I2C de fábrica conectado a él** — la pantalla de este board es
SPI (`PIN_TFT_SDA`/`PIN_TFT_SCL`/etc., pines distintos), no I2C. El BMP280
es, en la práctica, el único dispositivo en ese bus:

| Función | GPIO | Build flag |
|---|---|---|
| SDA | 45 | `PIN_BOARD_SDA` |
| SCL | 46 | `PIN_BOARD_SCL` |

Como no hay otros dispositivos que ya traigan resistencias pull-up en ese
bus, usa un breakout de BMP280 con pull-ups integradas (la inmensa mayoría
las trae) o añade las tuyas (4.7 kΩ a 3V3 en SDA y SCL) si usas el sensor
sin encapsular.

## Pinout del sensor BMP280

Los breakout boards de BMP280 más comunes (ej. GY-BMP280, variantes con 4 o
6 pines) traen esta serigrafía:

| Pin del módulo BMP280 | Función | Conectar a (heltec_tracker) |
|---|---|---|
| `VCC` (o `VIN`) | Alimentación, 3.3 V | 3V3 |
| `GND` | Tierra | GND |
| `SCL` | Reloj I2C | GPIO 46 (`PIN_BOARD_SCL`) |
| `SDA` | Datos I2C | GPIO 45 (`PIN_BOARD_SDA`) |
| `CSB` (solo en módulos de 6 pines) | Chip Select | a `VCC` (fuerza modo I2C; en modo SPI el sensor no sirve para este firmware) |
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
                          (a VCC → modo I2C)
```

**Nota sobre la dirección I2C — importante**: este firmware usa la
dirección `0x76` por defecto (`TELEM_BMP280_ADDRESS`, definida en
`src/helpers/sensors/EnvironmentSensorManager.cpp`). El pin `SDO` del
BMP280 selecciona la dirección:

- `SDO` a `GND` → dirección `0x76` (la que este firmware espera por
  defecto).
- `SDO` a `VCC` → dirección `0x77` (algunos breakouts, incluido el propio
  de Adafruit, vienen así de fábrica). El escaneo de arranque no la
  detectará con la configuración por defecto — el sensor simplemente no
  aparecerá en la telemetría. Si tu módulo está strapeado a `0x77` y no
  quieres recablear `SDO`, añade en
  `variants/heltec_tracker/platformio.ini`, dentro de
  `[env:Heltec_Wireless_Tracker_repeater]`:
  ```ini
  -D TELEM_BMP280_ADDRESS=0x77
  ```

Si tu módulo trae `SDO` flotante por diseño (sin pull-down), asegúrate de
puentearlo a `GND` explícitamente — dejarlo al aire puede leer cualquiera
de las dos direcciones de forma inconsistente entre arranques.

Módulos de 4 pines (`VCC`, `GND`, `SCL`, `SDA` únicamente) ya vienen fijos
en modo I2C con dirección `0x76` de fábrica en la gran mayoría de los casos
— conéctalos directo sin pines adicionales.

## GPS: qué cambió

Este cambio reemplazó la clase de manejo de sensores específica de esta
placa (`HWTSensorManager`) por la genérica `EnvironmentSensorManager` que
ya usan otras placas del repo. El GPS sigue funcionando, pero con 3
diferencias de comportamiento respecto al firmware anterior que vale la
pena conocer antes de desplegar:

- El toggle `"gps"` en la app companion ahora solo aparece si el firmware
  detectó el GPS durante el arranque (una ventana de ~1 segundo escuchando
  el puerto serial). Antes, el toggle siempre estaba presente
  independientemente de si el GPS respondía.
- La posición GPS en la telemetría ahora solo se reporta si el GPS está
  activo (`gps_active`). Antes se reportaba siempre que quien pedía
  telemetría tuviera permiso de ubicación, incluso con el GPS apagado
  (en cuyo caso se enviaban coordenadas en 0 o desactualizadas).
- Hay un nuevo setting, `gps_interval`, configurable desde la app, que no
  existía antes.
- El firmware anterior enviaba, cada vez que se encendía el GPS, la
  sentencia `$CFGSYS,h35155*68` para forzar el uso de todas las
  constelaciones satelitales disponibles (GPS+GLONASS+Galileo+BeiDou). La
  ruta genérica no envía esta sentencia — el GPS arranca con su
  configuración de constelaciones por defecto de fábrica, lo que puede
  significar un *time-to-first-fix* algo más lento o peor cobertura en
  entornos urbanos. Este es un trade-off consciente por reutilizar el
  código genérico (la otra placa Heltec Tracker de este repo,
  `heltec_tracker_v2`, tampoco envía esta sentencia).

## Compilar el firmware

```bash
pio run -e Heltec_Wireless_Tracker_repeater
```

Para generar un `.bin` con nombre versionado en `out/` en vez de usar
`pio run` directo, exporta `FIRMWARE_VERSION` y usa el script `build.sh` de
la raíz del repo:

```bash
export FIRMWARE_VERSION=v1.0.0
sh build.sh build-firmware Heltec_Wireless_Tracker_repeater
```

## Verificar en campo

1. Flashea el firmware.
2. Desde otro nodo de la malla (conectado por USB/BLE a la app companion),
   pide telemetría a este nodo repetidor.
3. Si el BMP280 está bien cableado, la respuesta debe incluir temperatura,
   presión y altitud en un canal LPP distinto al de la posición GPS. Si el
   sensor no aparece, compila con `-D MESH_DEBUG=1` — el arranque imprime
   una línea indicando si `0x76` (BMP280) respondió o no durante el
   escaneo I2C — y confirma con eso que el sensor realmente responde antes
   de sospechar del firmware.
4. Confirma también que el toggle `"gps"` sigue apareciendo en la app y que
   la posición se reporta una vez el GPS obtiene fix — ver la sección "GPS:
   qué cambió" arriba.

## Ver también

- `docs/superpowers/plans/2026-09-03-heltec-tracker-bmp280-telemetry.md` —
  plan de implementación completo, incluyendo las decisiones sobre por qué
  se reemplazó `HWTSensorManager` por `EnvironmentSensorManager` en esta
  variante.
