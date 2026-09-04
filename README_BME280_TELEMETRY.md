# Telemetría BME280 en Heltec Wireless Tracker (heltec_tracker), env repeater

Este firmware añade un sensor ambiental BME280 (temperatura, humedad
relativa, presión barométrica, altitud) a la respuesta estándar de
telemetría — junto con la posición GPS que este board ya reporta.
**Aplica solo al entorno PlatformIO `Heltec_Wireless_Tracker_repeater`** —
es el único build de este board que compila el driver del sensor; los
demás entornos (`companion_radio_usb`, `companion_radio_ble`,
`repeater_bridge_espnow`, `room_server`, `kiss_modem`) no lo incluyen.

> **Nota**: este archivo originalmente documentaba un BMP280. Verificado
> en hardware real (`MESH_DEBUG=1`, ver "Cómo se diagnosticó" abajo): el
> chip que responde en `0x76` es un **BME280** (mismo footprint/dirección
> de 4 pines, chip-ID distinto) — de propina, reporta humedad relativa
> además de temperatura/presión/altitud.

- **Target de hardware**: Heltec Wireless Tracker v1, con módulo LoRa
  SX1262, compilado como repetidor (entorno PlatformIO
  `Heltec_Wireless_Tracker_repeater`).
- **Punto de enganche en el código**: `sensors.querySensors(...)` en
  `examples/simple_repeater/MyMesh.cpp:250` (el firmware de repeater tiene
  su propio manejo de telemetría, distinto del de `companion_radio`) — sin
  cambios en esa llamada, reutiliza el mecanismo binario de telemetría
  (CayenneLPP) que ya existe para pedir telemetría a un nodo remoto de la
  malla (el repetidor no tiene conexión directa USB/BLE con el teléfono;
  la telemetría llega enrutada por la malla).
- **Driver del sensor**: `src/helpers/sensors/EnvironmentSensorManager.h/.cpp`
  (genérico, compartido con otras placas del repo — no específico de este
  board). El GPS se mantiene funcionando, pero con algunos cambios de
  comportamiento respecto a la implementación anterior — ver la sección
  "GPS: qué cambió" más abajo.

## Cómo funciona

1. Al arrancar, `EnvironmentSensorManager::begin()` escanea el bus I2C
   principal (`Wire`, en los pines `PIN_BOARD_SDA`/`PIN_BOARD_SCL` de este
   board) probando qué direcciones responden — **antes** de tocar
   cualquier librería de sensor. Si no hay sensor conectado, el escaneo
   simplemente no lo encuentra y el firmware sigue funcionando con GPS
   solamente, sin errores ni cuelgues.
2. Si la dirección `0x76` responde, se inicializa el `Adafruit_BME280` y el
   sensor queda activo.
3. **Permisos — este firmware NO usa `telemetry_mode_env`** (ese ajuste es
   exclusivo del firmware `companion_radio`, no existe en `simple_repeater`).
   En el repeater el control de acceso es por rol ACL
   (`examples/simple_repeater/MyMesh.cpp:90-128,240-250`,
   `src/helpers/ClientACL.h:7-11`):
   - Un cliente que se loguea por la malla con la contraseña **admin**
     (`ADMIN_PASSWORD`) del repeater recibe lo que pida en su query —
     incluida la lectura del sensor ambiental — sin ningún flag adicional
     que activar.
   - Un cliente que se loguea con la contraseña **guest** siempre queda
     forzado a solo telemetría base (batería) — nunca ve el sensor
     ambiental ni el GPS.
4. Cuando llega una solicitud de telemetría por la malla (de un cliente
   admin), el firmware añade al payload CayenneLPP: temperatura (°C),
   humedad relativa (%), presión barométrica (hPa) y altitud estimada (m,
   calculada desde la presión con 1013.25 hPa como referencia de nivel del
   mar) — en un canal LPP independiente del canal de GPS.
5. No hay comando de texto ni respuesta por canal de grupo — a diferencia
   del actuador de relay de otra variante de este repo, esto es telemetría
   estándar, no un comando por chat.

## Pines I2C del heltec_tracker

Este board trae un bus I2C (`Wire`, en `PIN_BOARD_SDA`/`PIN_BOARD_SCL`)
inicializado automáticamente por `ESP32Board::begin()`, pero **sin ningún
dispositivo I2C de fábrica conectado a él** — la pantalla de este board es
SPI (`PIN_TFT_SDA`/`PIN_TFT_SCL`/etc., pines distintos), no I2C. El sensor
ambiental es, en la práctica, el único dispositivo en ese bus:

| Función | GPIO | Build flag |
|---|---|---|
| SDA | 45 | `PIN_BOARD_SDA` |
| SCL | 46 | `PIN_BOARD_SCL` |

Como no hay otros dispositivos que ya traigan resistencias pull-up en ese
bus, usa un breakout con pull-ups integradas (la inmensa mayoría las trae)
o añade las tuyas (4.7 kΩ a 3V3 en SDA y SCL) si usas el sensor sin
encapsular.

## Pinout del sensor (módulo de 4 pines)

El caso confirmado en este despliegue es un breakout de 4 pines (sin
`CSB`/`SDO`, fijo en modo I2C y dirección `0x76` de fábrica):

| Pin del módulo | Función | Conectar a (heltec_tracker) |
|---|---|---|
| `VCC` (o `VIN`) | Alimentación, 3.3 V | 3V3 |
| `GND` | Tierra | GND |
| `SCL` | Reloj I2C | GPIO 46 (`PIN_BOARD_SCL`) |
| `SDA` | Datos I2C | GPIO 45 (`PIN_BOARD_SDA`) |

```
        Breakout BME280/BMP280 de 4 pines (vista superior)
        ┌─────────────────────┐
        │  ┌───────────────┐  │
        │  │  BME280/BMP280│  │
        │  │  (chip sensor)│  │
        │  └───────────────┘  │
        │                     │
        │   VCC GND SCL SDA   │
        └────┬───┬───┬───┬────┘
             │   │   │   │
            3V3 GND GPIO GPIO
                    46  45
```

Si en cambio tienes un módulo de 6 pines (con `CSB` y `SDO` adicionales):
conecta `CSB` a `VCC` (fuerza modo I2C) y `SDO` a `GND` (fija dirección
`0x76`, la que este firmware espera). `SDO` a `VCC` da dirección `0x77` —
el escaneo de arranque no la detecta con la configuración por defecto; si
no quieres recablear, añade en `variants/heltec_tracker/platformio.ini`,
dentro de `[env:Heltec_Wireless_Tracker_repeater]`:
```ini
-D TELEM_BME280_ADDRESS=0x77
```

## Cómo se diagnosticó BMP280 vs BME280

Si el sensor no aparece o quieres confirmar qué chip tienes realmente,
compila con `-D MESH_DEBUG=1` (descomenta esa línea en
`[env:Heltec_Wireless_Tracker_repeater]`), flashea, y reinicia el nodo
mirando el log serie del arranque. Mensajes posibles:

- `Found BME280 at address: 76` — sensor detectado e inicializado
  correctamente (caso confirmado en este despliegue).
- `BMP280 found at 76 but failed to initialize` — la dirección I2C
  responde (cableado OK) pero el chip-ID no coincide con BMP280. Causa
  típica: el módulo es en realidad un **BME280** mal etiquetado como
  BMP280 — extremadamente común en breakouts genéricos de 4 pines, ambos
  chips comparten footprint y dirección `0x76`. Prueba habilitando
  `-D ENV_INCLUDE_BME280=1` en vez de (o junto a) `ENV_INCLUDE_BMP280`.
- Ninguna de las dos líneas — la dirección `0x76` no respondió en el
  escaneo: problema de cableado (revisa SDA/SCL/alimentación) antes de
  sospechar del chip.

Recuerda volver a comentar `MESH_DEBUG=1` para el firmware de producción
— agrega overhead de logging por serial en cada paquete.

## GPS: qué cambió

Este cambio reemplazó la clase de manejo de sensores específica de esta
placa (`HWTSensorManager`) por la genérica `EnvironmentSensorManager` que
ya usan otras placas del repo. El GPS sigue funcionando, pero con 4
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

Para flashear directo a un puerto serie conocido:
```bash
pio run -e Heltec_Wireless_Tracker_repeater -t upload --upload-port /dev/cu.usbmodemXXXX
```

## Verificar en campo

1. Flashea el firmware.
2. Desde otro nodo de la malla (conectado por USB/BLE a la app companion o
   `meshcli` sin `-r`), inicia sesión contra este repeater con su
   **contraseña admin** (`ADMIN_PASSWORD` en
   `variants/heltec_tracker/platformio.ini` — cámbiala del valor por
   defecto `"password"` antes de desplegar en serio) y pide telemetría.
3. La respuesta debe incluir temperatura, humedad, presión y altitud en un
   canal LPP distinto al de la posición GPS. Si el sensor no aparece, ve a
   la sección "Cómo se diagnosticó BMP280 vs BME280" arriba.
4. Confirma también que el toggle `"gps"` sigue apareciendo en la app y que
   la posición se reporta una vez el GPS obtiene fix — ver la sección "GPS:
   qué cambió" arriba.

## Ver también

- `docs/superpowers/plans/2026-09-03-heltec-tracker-bmp280-telemetry.md` —
  plan de implementación original (escrito antes de confirmar en hardware
  que el chip real es BME280), incluyendo las decisiones sobre por qué se
  reemplazó `HWTSensorManager` por `EnvironmentSensorManager` en esta
  variante.
