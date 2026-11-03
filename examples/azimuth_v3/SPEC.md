# SPEC-AZ-V3 — MeshAzimuth en una sola Heltec V3

> **Feature especial.** Rama `special/azimuth-heltec-v3`. No modifica `examples/trace_pinger` ni sus envs
> (`Xiao_nrf52_trace_pinger`, `ProMicro_trace_pinger`). Reusa `tracecore/` en solo lectura.

Un único dispositivo montado en el boom de la Yagi: apunta (GPS + brújula) y mide (pinger TRACE).
Sustituye a la pareja iPhone + pinger nRF52. El teléfono es opcional: solo carga repetidores y descarga el CSV por Wi-Fi.

Sustituye a `docs/spec-kit` (SPEC-MC-V3-C0), que asumía `sdkconfig` propio. Este repo usa Arduino
precompilado (`espressif32@6.11.0`), donde el Wi-Fi está clavado al core 0 sin opción de moverlo.
Por eso la ley se invierte: **el núcleo limpio es el core 1 y es de la radio.**

## 1. Hardware

| Pieza | Modelo | Bus |
|---|---|---|
| Placa | Heltec WiFi LoRa 32 **V3** (ESP32-S3, SX1262, CP2102, 8 MB, sin PSRAM) | — |
| GPS + brújula | SEQURE M10-25Q: u-blox M10 (115200 8N1, 10 Hz) y QMC5883L (I2C `0x0D`) en un solo módulo | UART1 + Wire1 |
| Alimentación del módulo | Boost a 5 V desde la batería | — |
| Nivel lógico | Convertidor bidireccional 3.3 V ↔ 5 V (tipo BSS138, 4 canales: TX, RX, SDA, SCL) | — |
| Pantalla | SSD1306 128×64 integrada | Wire |

Antena IPEX conectada antes de alimentar. El pinger transmite a 22 dBm por defecto.

El M10-25Q pide 5 V, y el pin 5V de la V3 solo da tensión con USB. Por eso va un boost desde la batería.
Las fichas no dicen a qué nivel trabajan sus líneas, y el ESP32-S3 no tolera 5 V: las cuatro señales pasan por
el convertidor. Si se comprueba que el módulo trabaja a 3.3 V, el convertidor sobra pero no hace daño.
Los convertidores BSS138 ya llevan pull-ups en ambos lados, así que no se añaden otros en Wire1.

Conector SH1.0 de 6 pines (TX, RX, GND, 5V, SCL, SDA). El orden exacto se confirma con el cable del módulo antes de soldar.

## 2. Pines

| Señal | GPIO | Núcleo | Nota |
|---|---|---|---|
| SX1262 NSS / SCK / MOSI / MISO | 8 / 9 / 10 / 11 | 1 | igual que `Heltec_lora32_v3` |
| SX1262 BUSY / DIO1 | 13 / 14 | 1 | RST sin usar (`RADIOLIB_NC`), como el resto del repo |
| LED TX | 35 | 1 | lo maneja el driver de radio |
| OLED SDA / SCL / RST | 17 / 18 / 21 | 0 | Wire |
| Vext (OLED, activo en bajo) | 36 | 0 | |
| QMC5883L SDA / SCL | 33 / 34 | 0 | Wire1 vía convertidor, mismos pines que `ENV_PIN_SDA/SCL` del repeater |
| M10 TX → ESP RX | 47 | 0 | `PIN_GPS_RX`, vía convertidor |
| M10 RX ← ESP TX | 48 | 0 | `PIN_GPS_TX`, vía convertidor. **Obligatorio**: hace falta para activar NMEA (§5) |
| Botón PRG | 0 | 0 | cambiar de pantalla |
| VBAT ADC / ADC ctrl | 1 / 37 | 0 | |

El M10-25Q no tiene pin de enable: `PIN_GPS_EN` no se usa.

Montaje del módulo: plano, con la antena cerámica hacia el cielo, la flecha impresa hacia el director, y lejos del cable coaxial y de tornillos de acero.
No hay acelerómetro, así que **no hay compensación de inclinación**: el boom tiene que estar nivelado al leer el rumbo.

## 3. Ley de núcleos

| ID | Regla |
|---|---|
| AZ-C1-01 | El core 1 es de la radio. Solo corren ahí `loopTask` (el pinger), `IDLE1` e `ipc1`. |
| AZ-C1-02 | El pinger vive en `loop()`, que en Arduino ya corre en el core 1. En `setup()`, `loopTask` sube a prioridad 20. |
| AZ-C1-03 | Todo lo demás es una sola tarea `app` creada con `xTaskCreatePinnedToCore(..., 0)`: Wi-Fi, servidor web, OLED, GPS, brújula, NVS de los repetidores y CSV. |
| AZ-C1-04 | `-D ARDUINO_EVENT_RUNNING_CORE=0`: los eventos de Wi-Fi no tocan el core 1. |
| AZ-C1-05 | Prohibido `xTaskCreate` sin núcleo en código propio. Servidor web síncrono (`WebServer` del core Arduino), sondeado desde `app`: no crea tareas, a diferencia de AsyncTCP. |
| AZ-C1-06 | Los núcleos solo se comunican por dos `xQueue` de líneas (`kMaxLine`): `cmd` (app → radio) y `evt` (radio → app). No hay estado compartido. |
| AZ-C1-07 | La ISR de DIO1 sigue siendo el `setFlag()` actual: solo levanta un bit. |
| AZ-C1-08 | Auditoría de arranque, ver §6. Si `VIOLATIONS` ≠ 0, la OLED muestra `CORE1 VIOLATION`. |

Las tareas del sistema sin afinidad (por ejemplo `Tmr Svc`, prioridad 1) pueden ocupar el core 1 solo cuando `loopTask` cede. Eso es aceptable por diseño: la prioridad 20 de `loopTask` las deja fuera mientras atiende la radio.

## 4. Interfaz entre núcleos: el protocolo `proto=1`

La tarea `app` habla con el pinger **en el mismo protocolo de líneas que hoy usa la app iOS por BLE**
(`specs/001-trace-pinger-firmware/contracts/line-protocol.md` en MeshAzimuth). La cola `cmd`
sustituye a Serial y BLE como transporte. `tracecore::Pinger` no cambia y sus tests siguen valiendo.

- `app` envía, por ejemplo: `SET hash=4F`, `PING`, `SET freq=…`, `SAVE`.
- `app` recibe y analiza: `P,…`, `S,…`, `#CFG …`, `#WARN …`, `!ERR …`.
- USB serie (115200) se mantiene como segundo transporte de depuración, sondeado en `loop()` igual que en el pinger actual.

## 5. Funciones de la tarea `app` (core 0)

| Función | Detalle |
|---|---|
| Repetidores | Lista en NVS (`Preferences`): nombre, lat, lon, altitud opcional y hash (2 hex). Uno de ellos es el objetivo. |
| Apuntado | Azimut verdadero por gran círculo, distancia y elevación desde la posición GPS al objetivo. |
| GPS | En cada arranque se envía a UART1 un comando UBX-CFG-VALSET (solo a RAM) que activa NMEA GGA y RMC a 115200. Así funciona aunque el módulo venga solo en UBX, como lo configura INAV, y no depende de la pila de respaldo. NMEA se lee con `MicroNMEA`. |
| Rumbo | QMC5883L con tres ajustes que se cambian desde la web y se guardan en NVS: **orientación** (intercambio y signo de los ejes X e Y, porque en los GPS de drone los ejes del chip no siguen la flecha impresa), **calibración de hierro duro** (offsets por min/max girando 360°) y **declinación magnética** manual. |
| En vivo | Un `PING` cada 2 s (igual que la app). Guarda SNR↑, SNR↓, RSSI, RTT y pérdida junto al rumbo y la posición. |
| CSV | Buffer circular en RAM con las últimas 500 mediciones, descargable en `/log.csv`. Se pierde al reiniciar. |
| Wi-Fi | SoftAP `Azimuth-XXXX` con clave aleatoria de 8 caracteres generada en el primer arranque y guardada en NVS. Página en `http://192.168.4.1`. |
| Web | Repetidores (alta, edición y baja), elegir el objetivo, calibrar la brújula, ajustar la declinación, configurar la radio (pasa los `SET` al pinger), estado en vivo (JSON sondeado cada 1 s) y CSV. La página no usa GPS ni brújula del teléfono: sin HTTPS el navegador no los da. |
| OLED | **Pantalla 1, conexión:** SSID y clave del SoftAP (sustituye al PIN BLE). **Pantalla 2, apuntado:** azimut del objetivo, rumbo actual, flecha de error en grados, SNR↑, SNR↓ y estado del fix GPS. Se cambia con PRG. `CORE1 VIOLATION` tiene prioridad sobre todas. |

Fuera de alcance en esta rama: BLE, mapas, compensación de inclinación y OTA.

## 6. Auditoría de arranque

Al final de `setup()` se recorre la lista con `pxTaskGetNext()` y, por cada tarea, se lee `pcTaskGetName()`,
`xTaskGetAffinity()` y `uxTaskPriorityGet()`. El firmware imprime una tabla y una línea `VIOLATIONS N`.
Es violación:

- una tarea con afinidad al core 1 cuyo nombre no sea `loopTask`, `IDLE1` ni `ipc1`, o
- una tarea sin afinidad (`tskNO_AFFINITY`) con prioridad ≥ la de `loopTask`.

La lista blanca se revisa con la tabla del primer arranque real. Cualquier tarea que se añada se documenta aquí con su motivo.

## 7. Build

Nuevo env `Heltec_v3_azimuth` en `variants/heltec_v3/platformio.ini`. Solo se **añade** el env: no se toca ninguno de los existentes.

```ini
[env:Heltec_v3_azimuth]
extends = Heltec_lora32_v3
build_unflags = -std=gnu++11
build_flags =
  ${Heltec_lora32_v3.build_flags}
  -std=gnu++17
  -D ARDUINO_EVENT_RUNNING_CORE=0
  -D ENV_PIN_SDA=33
  -D ENV_PIN_SCL=34
  -I examples/trace_pinger/tracecore/include
  -D LORA_FREQ=915.8
  -D LORA_BW=250
  -D LORA_SF=10
  -D LORA_CR=5
build_src_filter = ${Heltec_lora32_v3.build_src_filter}
  +<helpers/ui/SSD1306Display.cpp>
  +<../examples/azimuth_v3/*.cpp>
  +<../examples/trace_pinger/tracecore/src/*.cpp>
lib_deps = ${Heltec_lora32_v3.lib_deps}
```

La base deja `ESP32_CPU_FREQ=80` (lo aplica `board.begin()`). El firmware llama a `setCpuFrequencyMhz(240)` justo después, en vez de redefinir la macro: con Wi-Fi activo, 80 MHz se queda corto.

Sin dependencias nuevas:

- `WebServer` y `Preferences` vienen con el core Arduino.
- NMEA con `MicroNMEA`, que ya está en el repo.
- OLED con `SSD1306Display`, que ya está en el repo.
- QMC5883L con un driver propio de unas 40 líneas: registros `0x09`/`0x0B` y lectura de `0x00`–`0x05`.

## 8. Flasheo

```bash
pio run -e Heltec_v3_azimuth -t upload        # CP2102; si no engancha: mantener PRG al conectar
pio device monitor -b 115200                  # tabla de tareas + VIOLATIONS 0
```

Primera vez: `pio run -e Heltec_v3_azimuth -t erase` antes del upload, para borrar el NVS viejo.
Imagen para flasher web: `.pio/build/Heltec_v3_azimuth/firmware-merged.bin` en `0x0` (la genera `merge-bin.py`).

## 9. Acta — conforme solo si

- [ ] `examples/trace_pinger/` y sus envs no cambian (`git diff main -- examples/trace_pinger` vacío).
- [ ] Los tests de `tracecore` pasan (`examples/trace_pinger/run_tests.sh`).
- [ ] El log de arranque muestra la tabla y `VIOLATIONS 0`.
- [ ] Banco, antes de conectar a la V3: tensión de reposo medida en TX, SDA y SCL del módulo, con el boost a 5 V, y anotada aquí.
- [ ] Un escáner I2C ve el QMC5883L en `0x0D` a través del convertidor.
- [ ] Con el comando UBX del arranque llegan tramas `$GNGGA` y `$GNRMC` a 115200.
- [ ] La OLED muestra el SSID y la clave, y el teléfono entra a `http://192.168.4.1`.
- [ ] Un repetidor dado de alta desde la web sobrevive a un reinicio.
- [ ] El rumbo de la OLED coincide con una brújula de referencia en ±5° tras calibrar, con la placa nivelada.
- [ ] El azimut y la distancia coinciden con los de la app MeshAzimuth para el mismo par de puntos (±1°, ±1 %).
- [ ] En vivo: `P,…,ok` contra un repetidor real y el CSV descargado contiene rumbo, SNR↑, SNR↓, RSSI y RTT.
- [ ] El RTT medio con el Wi-Fi activo y un cliente conectado no supera en más de 5 ms al del pinger nRF52 contra el mismo repetidor.
