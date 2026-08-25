# Diseño: Sensor AM2301 vía I2C sobre el patrón de comando por canal

Fecha: 2026-08-24
Branch: `feature/i2c-actuator-public-channel`

## Contexto

El branch actual implementa un actuador I2C (XIAO nRF52840 como maestro I2C,
PCF8574 como esclavo) que reacciona a comandos de texto en un canal hashtag
autorizado (`PIN<n>_ON/OFF`, `PIN_STATUS`) — ver `README_I2C.md`. Existe
también un Arduino Nano (`tools/i2c_actuator_nano/`) que emula el PCF8574
para pruebas de banco sin el chip real.

Se quiere agregar un sensor de temperatura/humedad **AM2301** (DHT21). A
diferencia del PCF8574, el AM2301 no es I2C nativo: usa un protocolo de un
solo hilo con timing estricto (bit-banging), normalmente leído con una
librería DHT en un microcontrolador dedicado. Por eso un segundo Arduino
Nano lee el AM2301 localmente y lo expone como esclavo I2C hacia la XIAO,
reutilizando el mismo bus I2C (D6/D7) ya cableado para el actuador.

A diferencia del Nano-actuador (que es un doble de pruebas de un chip real),
este Nano-sensor **es** el puente permanente de producción — no emula nada,
es la única forma práctica de exponer el AM2301 por I2C.

## Decisiones de diseño (confirmadas con el usuario)

1. **Camino de integración**: comando de texto por canal (`TEMP_STATUS`),
   igual que `PIN_STATUS` — sin autenticación, sin usar el framework formal
   de telemetría (`SensorManager`/CayenneLPP/`GET_TELEMETRY_DATA`), que
   vive en el firmware `simple_sensor` y no en `companion_radio`.
2. **Topología de hardware**: Nano dedicado y separado del Nano-actuador,
   en su propia dirección I2C (`0x21`) en el mismo bus.
3. **Palabra de comando**: `TEMP_STATUS`, configurable vía build flag
   `SENSOR_CMD_STATUS` (mismo patrón que `ACTUATOR_CMD_STATUS`).

## Fuera de alcance (YAGNI)

- Doble de pruebas sintético del sensor sin hardware real (el propio
  Nano-sensor ya es el puente real; no hay chip que "emular").
- Integración con el framework formal `SensorManager`/CayenneLPP.
- Comandos de escritura hacia el Nano-sensor (es un dispositivo de solo
  lectura desde la perspectiva I2C).
- Ack opcional tipo `ACTUATOR_SEND_ACK` — `TEMP_STATUS` es consulta pura,
  siempre responde, igual que `PIN_STATUS`.

## Arquitectura

```
Canal hashtag "#..."  --TEMP_STATUS-->  XIAO (companion_radio)
                                           |  I2C read (5 bytes) @ 0x21
                                           v
                                    Nano-sensor AM2301
                                           |  bit-bang DHT, cada >=2s
                                           v
                                        AM2301
```

### 1. Firmware del Nano-sensor (`tools/i2c_sensor_am2301_nano/`)

Proyecto PlatformIO nuevo e independiente, paralelo a
`tools/i2c_actuator_nano/`.

- **Librería**: Adafruit "DHT sensor library" (+ "Adafruit Unified
  Sensor" como dependencia transitiva), tipo `DHT21`. Es la librería DHT
  más conocida/estable en el ecosistema Arduino; alternativa considerada:
  `Rob Tillaart/DHTNew` (mejor timing, pero menos usada) — se documenta
  como alternativa en el README del proyecto, no se implementa en v1.
- **Pin de datos**: un pin digital (ej. D2), con pull-up de 10kΩ a VCC si
  el módulo AM2301 no lo trae integrado (la mayoría de módulos de 3-4 pines
  ya lo incluyen — documentar igual que la nota de pull-ups del actuador).
- **`loop()`**: lee el AM2301 cada `AM2301_READ_INTERVAL_MS` (default
  5000ms; el datasheet exige ≥2000ms entre lecturas). Nunca se lee el
  sensor dentro de `onRequest()` — el bit-banging del AM2301 mantiene
  interrupciones deshabilitadas varios ms, lo cual es incompatible con el
  contexto ISR de `Wire::onRequest`/`onReceive` (mismo motivo por el que el
  Nano-actuador solo hace `digitalWrite` barato en sus callbacks I2C).
- **Estado cacheado** (variables globales, similar a `last_state` en el
  Nano-actuador):
  - `float cached_temp_c`, `float cached_hum_pct`
  - `uint8_t status`: `0 = OK` (última lectura exitosa), `1 = cached`
    (última lectura falló, se sirve el valor previo), `2 = no_reading_yet`
    (nunca hubo lectura exitosa desde el boot — `cached_temp_c`/`hum` sin
    inicializar no se deben servir; usar NAN/0 y dejar que `status=2` lo
    indique).
  - Cuando `dht.read()` devuelve NAN (checksum/timeout), `status` pasa a
    `1` si ya había una lectura previa exitosa, o se mantiene en `2` si
    nunca la hubo. No hay reintento inmediato — se reintenta en el próximo
    ciclo de `loop()`.
- **`onRequest()`**: serializa el paquete fijo de 5 bytes y responde con
  `Wire.write(buf, 5)`:

  | Byte(s) | Contenido | Formato |
  |---|---|---|
  | 0 | `status` | `0`/`1`/`2` |
  | 1-2 | `temp_c * 10` | `int16_t`, big-endian, permite negativos |
  | 3-4 | `hum_pct * 10` | `uint16_t`, big-endian |

  Ejemplo: 23.5°C, 45.2% → `00 00 EB 01 C4`.
- **Sin `onReceive()`**: el dispositivo es de solo lectura por I2C, no
  necesita aceptar comandos de escritura.
- **Serial de depuración**: igual que el Nano-actuador, imprime cada
  lectura (o fallo) por Serial a 115200 baud para debug de banco.

### 2. Driver en la XIAO (`src/helpers/i2c_sensors/AM2301RemoteSensor.h/.cpp`)

Carpeta nueva `src/helpers/i2c_sensors/`, deliberadamente separada de
`src/helpers/sensors/` (que pertenece al framework formal `SensorManager`
descartado en esta ronda) para no mezclar los dos conceptos.

```cpp
class AM2301RemoteSensor {
public:
  void begin(TwoWire& wire, uint8_t i2c_addr = AM2301_SENSOR_I2C_ADDR);
  // Returns false only on I2C bus error (no ack / wrong byte count).
  // On success, *status reports the Nano-sensor's own status byte
  // (0=OK, 1=cached, 2=no_reading_yet) - a true I2C bus error is
  // orthogonal to that and always leaves *status untouched by the caller.
  bool read(float* temp_c, float* hum_pct, uint8_t* status);
private:
  TwoWire* _wire = NULL;
  uint8_t _addr = AM2301_SENSOR_I2C_ADDR;
};
```

Calcado de `PCF8574Actuator::readState()`: `requestFrom(addr, 5)`,
verifica que llegaron 5 bytes, decodifica big-endian, devuelve `false` solo
ante error de bus (sin ack, timeout, byte count incorrecto) — un error de
bus es distinto del `status` cacheado del propio Nano (que sigue siendo un
read I2C exitoso, solo que con datos de sensor stale).

### 3. Integración en `MyMesh.cpp` / `MyMesh.h`

- **Refactor previo necesario**: `isHashtagChannel()` y
  `textEndsWithCmd()` hoy están declaradas dentro de
  `#ifdef HAS_PCF8574_ACTUATOR` (líneas ~591-607). Se sacan de ese guard
  (quedan siempre compiladas si `HAS_PCF8574_ACTUATOR` **o**
  `HAS_AM2301_SENSOR` está definido) porque ambas features las necesitan.
  Usar un guard combinado, ej.:
  ```cpp
  #if defined(HAS_PCF8574_ACTUATOR) || defined(HAS_AM2301_SENSOR)
  static bool textEndsWithCmd(...) { ... }
  static bool isHashtagChannel(...) { ... }
  #endif
  ```
- **`MyMesh.h`**: agrega, en paralelo al miembro `actuator`:
  ```cpp
  #ifdef HAS_AM2301_SENSOR
  #include <helpers/i2c_sensors/AM2301RemoteSensor.h>
  ...
  AM2301RemoteSensor sensor_am2301;
  void checkSensorCommand(const mesh::GroupChannel& channel, const char* text);
  #endif
  ```
- **`MyMesh.cpp`**: nueva función `checkSensorCommand()`, paralela a
  `checkActuatorCommand()`:
  ```cpp
  #ifdef HAS_AM2301_SENSOR
  #ifndef SENSOR_CMD_STATUS
  #define SENSOR_CMD_STATUS "TEMP_STATUS"
  #endif

  void MyMesh::checkSensorCommand(const mesh::GroupChannel& channel, const char* text) {
    if (!textEndsWithCmd(text, SENSOR_CMD_STATUS)) return;

    int idx = findChannelIdx(channel);
    ChannelDetails details;
    if (idx < 0 || !getChannel(idx, details) || !isHashtagChannel(details.name, channel)) {
      MESH_DEBUG_PRINTLN("checkSensorCommand: keyword matched but channel is not an authorized hashtag channel, ignoring");
      return;
    }

    float temp_c, hum_pct;
    uint8_t status;
    bool ok = sensor_am2301.read(&temp_c, &hum_pct, &status);

    char msg[48];
    if (!ok) {
      snprintf(msg, sizeof(msg), "TEMP=n/a HUM=n/a (i2c error)");
    } else if (status == 2) {
      snprintf(msg, sizeof(msg), "TEMP=n/a HUM=n/a (no reading yet)");
    } else if (status == 1) {
      snprintf(msg, sizeof(msg), "TEMP=%.1fC HUM=%.1f%% (cached)", temp_c, hum_pct);
    } else {
      snprintf(msg, sizeof(msg), "TEMP=%.1fC HUM=%.1f%%", temp_c, hum_pct);
    }
    sendGroupMessage(getRTCClock()->getCurrentTimeUnique(), details.channel, _prefs.node_name, msg, strlen(msg));
  }
  #endif
  ```
- **`onChannelMessageRecv()`**: agrega junto al chequeo del actuador:
  ```cpp
  #ifdef HAS_AM2301_SENSOR
  checkSensorCommand(channel, text);
  #endif
  ```
- **`setup()`/inicialización I2C**: llamar `sensor_am2301.begin(Wire,
  AM2301_SENSOR_I2C_ADDR)` junto a donde hoy se llama `actuator.begin(...)`
  (mismo bus `Wire` ya inicializado por `XiaoNrf52Board::begin()`).

### 4. Build flags (`variants/xiao_nrf52/platformio.ini`)

Agregar en ambos envs (`Xiao_nrf52_companion_radio_usb` y
`Xiao_nrf52_companion_radio_ble`), junto al bloque existente del actuador:

```ini
; -D HAS_AM2301_SENSOR=1              ; opt-in, comentado por defecto
-D AM2301_SENSOR_I2C_ADDR=0x21
; -D SENSOR_CMD_STATUS='"TEMP_STATUS"'
```

`HAS_AM2301_SENSOR` queda **apagado por defecto** (a diferencia de
`HAS_PCF8574_ACTUATOR=1`, que sí está activo por defecto) — es hardware
nuevo que no todos los nodos existentes tienen cableado; cada quien lo
activa al conectar su Nano-sensor.

### 5. Documentación

Nuevo `README_I2C_SENSOR.md` en la raíz del repo, mismo estilo/estructura
que `README_I2C.md` (cómo funciona, tabla del comando, formato de
respuesta, configuración del canal/comando/dirección I2C, conexión física,
sección "Ver también" cruzada con `README_I2C.md` y
`tools/i2c_sensor_am2301_nano/`). No se modifica `README_I2C.md` en sí
(es específico del actuador) más allá de un link cruzado en su sección
"Ver también".

## Manejo de errores — resumen

| Escenario | `read()` (driver XIAO) | Respuesta en canal |
|---|---|---|
| Bus I2C sin respuesta / NACK | `false` | `TEMP=n/a HUM=n/a (i2c error)` |
| Nano nunca leyó el AM2301 con éxito | `true`, `status=2` | `TEMP=n/a HUM=n/a (no reading yet)` |
| Última lectura del AM2301 falló, hay valor previo | `true`, `status=1` | `TEMP=X HUM=Y (cached)` |
| Lectura del AM2301 fresca | `true`, `status=0` | `TEMP=X HUM=Y` |

## Plan de pruebas

1. **Build**: `pio run -e Xiao_nrf52_companion_radio_usb` con
   `HAS_AM2301_SENSOR=1` descomentado — debe compilar sin errores/overflow
   de linker (vigilar `%RAM`, igual que se hizo con `Xiao_nrf52_sensor`).
2. **Nano-sensor standalone**: `cd tools/i2c_sensor_am2301_nano && pio run
   -t upload`, verificar por Serial que lee el AM2301 real cada ciclo y
   cachea valores razonables.
3. **Extremo a extremo**: XIAO + Nano-sensor cableados en el mismo bus
   I2C que el Nano-actuador (direcciones `0x20`/`0x21` distintas, sin
   colisión). Enviar `TEMP_STATUS` a un canal hashtag autorizado y
   verificar la respuesta. Confirmar que el mismo canal sigue soportando
   `PIN<n>_ON/OFF`/`PIN_STATUS` sin regresión (bus compartido).
4. **Canal no autorizado**: confirmar que `TEMP_STATUS` en `Public` o un
   canal privado no genera respuesta (igual que el actuador).
5. **Simulación de fallo**: desconectar el AM2301 del Nano-sensor en
   caliente, confirmar que tras el primer fallo el estado pasa a
   `(cached)` y no a un valor basura.

## Preguntas abiertas / decisiones diferidas

Ninguna — todas las decisiones de diseño clave fueron confirmadas con el
usuario antes de escribir este documento.
