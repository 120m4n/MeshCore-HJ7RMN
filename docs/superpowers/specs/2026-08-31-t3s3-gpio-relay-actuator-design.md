# Actuador de relay de 4 canales por GPIO directo en LilyGo T3S3 (SX1276)

**Fecha:** 2026-08-31
**Rama:** `feature/t3s3-relay-gpio-actuator` (creada desde `main`)
**Alcance:** `src/helpers/actuators/GPIORelayActuator.h/.cpp`,
`examples/companion_radio/MyMesh.cpp`, `examples/companion_radio/MyMesh.h`,
`variants/lilygo_t3s3_sx1276/LilygoT3S3SX1276Board.h`,
`variants/lilygo_t3s3_sx1276/platformio.ini`, documentación nueva.

## Contexto

MeshCore ya tiene, en la rama `feature/i2c-actuator-public-channel` (no
mergeada a `main`), un patrón de "actuador por comando de canal": un nodo
companion escucha mensajes en un canal de grupo, reconoce comandos
`PIN<n>_ON`/`PIN<n>_OFF`/`PIN_STATUS`, valida que el canal sea un **canal
hashtag autorizado** (`sha256(nombre_del_canal)[:16]` como secreto — ver
`docs/companion_protocol.md`), y actúa sobre un expansor I2C PCF8574 que a su
vez gobierna hasta 8 relés externos. Esa implementación vive en
`src/helpers/actuators/PCF8574Actuator.h/.cpp` y en
`MyMesh::checkActuatorCommand()`.

Este proyecto necesita la misma funcionalidad — comando por canal público
autorizado, controlando un relay — pero en un contexto distinto:

- **Placa objetivo**: LilyGo T3S3 con módulo LoRa SX1276
  (`variants/lilygo_t3s3_sx1276`, env
  `LilyGo_T3S3_sx1276_companion_radio_ble`), no la XIAO nRF52840 del diseño
  original.
- **Actuación directa por GPIO**, no por I2C/PCF8574: el relay de 4 canales
  se conecta directamente a 4 pines GPIO del ESP32-S3, sin chip expansor
  intermedio.
- **4 canales**, no 8.

Se parte de `main` (no de la rama del actuador I2C) porque esa rama trae
además trabajo no relacionado (`TelemetryBroadcaster`). El patrón de diseño
de `README_I2C.md` y `PCF8574Actuator` se usa como **referencia**, no como
código a heredar: esta spec reimplementa el patrón de comando-por-canal de
forma autocontenida para el driver de GPIO directo.

## Decisiones de diseño

### Placa y target

`variants/lilygo_t3s3_sx1276/platformio.ini`, env
`LilyGo_T3S3_sx1276_companion_radio_ble` únicamente (el env `_usb` no se
toca en esta spec; se puede extender después con el mismo patrón).

### Pines del relay: GPIO 16, 15, 39, 40

Referencia inicial: <https://www.espboards.dev/esp32/lilygo-t3s3-v1-0/#pinout>,
que señala **GPIO16, GPIO15 y GPIO18** como "3 pines sin implicación de
boot o de sistema". Esa recomendación es genérica al chip ESP32-S3 y no
tiene en cuenta el cableado propio de este proyecto: **GPIO18 ya está
reservado** en este mismo env como `PIN_BOARD_SDA` (bus I2C compartido por
el display SSD1306 y el RTC `AutoDiscoverRTCClock`, ambos activos en
`LilyGo_T3S3_sx1276_companion_radio_ble`). Usar GPIO18 para el relay
rompería el display y el RTC.

Verificado contra el set completo de pines ya reservados en el env
(`P_LORA_DIO_0=9`, `P_LORA_DIO_1=33`, `P_LORA_NSS=7`, `P_LORA_RESET=8`,
`P_LORA_SCLK=5`, `P_LORA_MISO=3`, `P_LORA_MOSI=6`, `P_LORA_TX_LED=37`,
`PIN_VBAT_READ=1`, `PIN_USER_BTN=0`, `PIN_BOARD_SDA=18`,
`PIN_BOARD_SCL=17`, `PIN_OLED_RESET=21`, `SX127X_RXEN=21`,
`SX127X_TXEN=10`):

| Canal | Pin | Justificación |
|---|---|---|
| `RELAY_PIN0` | GPIO16 | Recomendado por la fuente, sin conflicto. |
| `RELAY_PIN1` | GPIO15 | Recomendado por la fuente, sin conflicto. |
| `RELAY_PIN2` | GPIO39 | JTAG (no usado si no se depura por hardware), sin conflicto. |
| `RELAY_PIN3` | GPIO40 | JTAG (no usado si no se depura por hardware), sin conflicto. |

GPIO18 se descarta explícitamente para evitar el conflicto con I2C.
GPIO39/40 son parte del bus JTAG del ESP32-S3 (junto con 41/42); reutilizar
JTAG como GPIO de propósito general cuando no hay depuración por hardware es
práctica estándar en placas ESP32-S3 y no tiene ningún rol de boot/strapping.

**Advertencia explícita para quien opere el hardware**: estos 4 pines no han
sido verificados físicamente contra el header/castellated pads reales de la
placa — solo contra la documentación pública del pinout y contra los build
flags ya presentes en este repo. Antes de energizar el relay, confirmar
continuidad de cada pin contra el conector usado.

### Polaridad: `RELAY_ACTIVE_LOW=1` por defecto

El módulo de relay de 4 canales previsto es del tipo común basado en
`SRD-05VDC-SL-C`: **GPIO en LOW energiza el relé, HIGH lo desactiva**. El
build flag `RELAY_ACTIVE_LOW` (flag de presencia, igual que
`ACTUATOR_SEND_ACK` en el diseño I2C) se define por defecto en el env de
T3S3. El resto del firmware (comandos, caché de estado, mensajes de
`STATE=b....`) trabaja siempre en términos **lógicos** (`ON`/`OFF` = relé
energizado/no energizado); solo `GPIORelayActuator::setPin()` conoce la
traducción a nivel eléctrico:

```cpp
bool physical_high = RELAY_ACTIVE_LOW ? !state : state;
digitalWrite(pin, physical_high ? HIGH : LOW);
```

### Seguridad de arranque: doble inicialización a OFF

Un GPIO de un ESP32-S3 no tiene un nivel garantizado antes de que el
firmware llame a `pinMode()`/`digitalWrite()` — para un relay activo-bajo,
una ventana flotante podría interpretarse como energizado. Para minimizar
esa ventana:

1. `LilygoT3S3SX1276Board::begin()` (nuevo override) fija los 4 pines a
   `OUTPUT` + nivel OFF **como primera acción**, antes de cualquier otra
   inicialización de la placa — llamado desde `board.begin()` en
   `examples/companion_radio/main.cpp:125`, el punto más temprano posible
   de `setup()`. Mismo patrón que `TinyRelayBoard::begin()` en
   `variants/tiny_relay/target.h`.
2. `GPIORelayActuator::begin()` (llamado más tarde, desde `MyMesh::begin()`,
   mismo punto donde hoy se llama `actuator.begin(Wire)` para el PCF8574)
   repite `pinMode`/nivel OFF y inicializa su propio caché de estado.

La doble inicialización es intencional (defensa en profundidad), no un
error: el primer paso protege el hardware lo antes posible; el segundo
establece el estado que el driver usará para responder a comandos.

### `PIN_STATUS` sin distinción cached/resynced

El diseño I2C original distingue tres respuestas posibles para
`PIN_STATUS` (`STATE=b........`, `(resynced)`, `(cached)`) porque el
PCF8574 es un chip externo que puede tener su propia alimentación y perder
estado sin que el companion se entere — de ahí la necesidad de releerlo por
I2C y comparar contra el caché.

Con GPIO directo **no existe esa divergencia posible**: el registro de
salida del propio ESP32-S3 es la única fuente de verdad, y coincide siempre
con lo que el firmware tiene en memoria (si el ESP32-S3 pierde alimentación,
todo el firmware se reinicia y vuelve al estado OFF seguro descrito arriba;
no hay un "chip separado" que pueda derivar de forma independiente).
`PIN_STATUS` responde siempre con el caché lógico, sin anotaciones:

```
STATE=b0000
```

4 caracteres, misma convención posición=índice de pin (0-3, izquierda a
derecha) que el diseño I2C.

### Protocolo de comando: mismo patrón, 4 pines

Comandos reconocidos, solo en canal hashtag autorizado (misma validación
`isHashtagChannel()` que el diseño I2C — recalculada aquí sin depender de
esa rama):

- `PIN0_ON` .. `PIN3_ON` / `PIN0_OFF` .. `PIN3_OFF` — escritura, dígito
  único `0`-`3` (`RELAY_MAX_PIN = 3`).
- `PIN_STATUS` — consulta de solo lectura, siempre disponible, sin flag
  propio (igual que el diseño I2C).

Build flags configurables, mismos nombres y mismos defaults que el diseño
I2C para minimizar sorpresas a quien ya conozca ese patrón:

```ini
-D ACTUATOR_CMD_PREFIX='"PIN"'        ; default si no se define
-D ACTUATOR_CMD_ON_SUFFIX='"_ON"'     ; default si no se define
-D ACTUATOR_CMD_OFF_SUFFIX='"_OFF"'   ; default si no se define
-D ACTUATOR_CMD_STATUS='"PIN_STATUS"' ; default si no se define
```

Comparación exacta, sensible a mayúsculas, como sufijo del mensaje de canal
(el texto siempre lleva el prefijo `"<nombre_nodo>: "` que agrega
`sendGroupMessage()`).

### Ack de escritura: opt-in, apagado por defecto

`ACTUATOR_SEND_ACK` se mantiene como flag de presencia, **comentado por
defecto** en el env de T3S3 — mismo criterio conservador que el diseño I2C
(tráfico LoRa adicional en cada comando). Si está activo y `setPin()` tiene
éxito:

```
PIN2=ON STATE=b0100
```

### Componentes

| Archivo | Cambio |
|---|---|
| `src/helpers/actuators/GPIORelayActuator.h` | Nueva clase. `begin()`, `setPin(uint8_t pin, bool state)`, `getState() const` (caché lógico, 4 bits en un `uint8_t`). Sin dependencia de `Wire`/I2C. Guardas `#ifdef HAS_GPIO_RELAY_ACTUATOR` / `RELAY_MAX_PIN = 3`. |
| `src/helpers/actuators/GPIORelayActuator.cpp` | Traducción lógico→físico según `RELAY_ACTIVE_LOW`; `pinMode`/`digitalWrite` sobre `RELAY_PIN0..RELAY_PIN3`. |
| `variants/lilygo_t3s3_sx1276/LilygoT3S3SX1276Board.h` | Override de `begin()`: fija los 4 `RELAY_PINx` a `OUTPUT` + nivel OFF (según `RELAY_ACTIVE_LOW`) antes de cualquier otra cosa, bajo `#ifdef HAS_GPIO_RELAY_ACTUATOR`. |
| `examples/companion_radio/MyMesh.h` | `#include <helpers/actuators/GPIORelayActuator.h>` bajo `#ifdef HAS_GPIO_RELAY_ACTUATOR`; miembro `GPIORelayActuator actuator;`; declaración `checkActuatorCommand()`. |
| `examples/companion_radio/MyMesh.cpp` | `checkActuatorCommand()`, `parseActuatorCmd()` (dígito 0-3), `isHashtagChannel()`, `buildStateBits()` (4 bits) — helpers `static`, reimplementados sin copiar de la otra rama. Hook en `onChannelMessageRecv()`. `actuator.begin()` en `MyMesh::begin()`. Todo bajo `#ifdef HAS_GPIO_RELAY_ACTUATOR`. |
| `variants/lilygo_t3s3_sx1276/platformio.ini` | En env `LilyGo_T3S3_sx1276_companion_radio_ble`: `HAS_GPIO_RELAY_ACTUATOR=1`, `RELAY_PIN0..3=16,15,39,40`, `RELAY_ACTIVE_LOW=1`, flags de comando comentados como ejemplo; `+<helpers/actuators/GPIORelayActuator.cpp>` en `build_src_filter` (el filtro base no incluye subdirectorios de `helpers/`, hay que añadirlo explícito — mismo patrón que usa `xiao_nrf52` para `PCF8574Actuator.cpp`). |
| Documentación nueva (ej. `README_RELAY_GPIO.md`) | Estructura análoga a `README_I2C.md`: cómo funciona, cómo configurar el canal, cómo configurar el patrón de comando, pines/polaridad, advertencia de verificación física. |

## Flujo (`checkActuatorCommand`)

```cpp
void MyMesh::checkActuatorCommand(const mesh::GroupChannel& channel, const char* text) {
  uint8_t pin;
  bool state;
  bool is_write = parseActuatorCmd(text, &pin, &state);   // dígito 0-3
  bool is_status = !is_write && textEndsWithCmd(text, ACTUATOR_CMD_STATUS);
  if (!is_write && !is_status) return;

  int idx = findChannelIdx(channel);
  ChannelDetails details;
  if (idx < 0 || !getChannel(idx, details) || !isHashtagChannel(details.name, channel)) {
    MESH_DEBUG_PRINTLN("checkActuatorCommand: unauthorized channel, ignoring");
    return;
  }

  char bits[5];   // 4 canales + '\0'
  bool did_act;

  if (is_write) {
    did_act = actuator.setPin(pin, state);
#ifdef ACTUATOR_SEND_ACK
    if (did_act) {
      buildStateBits(actuator.getState(), bits);
      char msg[32];
      snprintf(msg, sizeof(msg), "PIN%u=%s STATE=b%s", (unsigned)pin, state ? "ON" : "OFF", bits);
      sendGroupMessage(getRTCClock()->getCurrentTimeUnique(), details.channel, _prefs.node_name, msg, strlen(msg));
    }
#endif
  } else {   // is_status - siempre "verificado", no hay lectura I2C que pueda fallar
    did_act = true;
    buildStateBits(actuator.getState(), bits);
    char msg[24];
    snprintf(msg, sizeof(msg), "STATE=b%s", bits);
    sendGroupMessage(getRTCClock()->getCurrentTimeUnique(), details.channel, _prefs.node_name, msg, strlen(msg));
  }
}
```

## Manejo de errores

- Comando no reconocido → `return` silencioso.
- Canal no autorizado (no-hashtag) → `return` con `MESH_DEBUG_PRINTLN`, igual
  para escritura y `PIN_STATUS`.
- `setPin()` no puede fallar por hardware (no hay bus I2C que pueda dar
  error) — siempre devuelve `true` tras `digitalWrite()`. El ack, si está
  activo, se envía siempre que `is_write` sea `true` y el canal esté
  autorizado.
- Fallo de `sendGroupMessage()` (ej. cola llena) → sin reintento, sin
  logging nuevo, mismo comportamiento que el resto del archivo.

## Testing

- Compilar `LilyGo_T3S3_sx1276_companion_radio_ble` en su configuración por
  defecto (con `HAS_GPIO_RELAY_ACTUATOR`, sin `ACTUATOR_SEND_ACK`) — debe
  compilar limpio.
- Compilar con `-D ACTUATOR_SEND_ACK=1` añadido temporalmente, para
  verificar que el código bajo ese `#ifdef` compila.
- No hay prueba end-to-end automatizada sin hardware real. Antes de operar
  con relés físicos conectados:
  - Verificar con multímetro que `RELAY_PIN0..3` (GPIO16, 15, 39, 40) llegan
    al conector/header que se va a usar en la placa física.
  - Confirmar que el módulo de relay es efectivamente activo-bajo antes de
    energizar cargas reales — si resulta ser activo-alto, invertir
    `RELAY_ACTIVE_LOW` (quitar el flag) antes de operar, no después.
  - Enviar `PIN0_ON` a un canal hashtag propio y confirmar que el relay 0
    energiza; `PIN0_OFF` para confirmar que vuelve a reposo.
  - Enviar `PIN_STATUS` y confirmar que `STATE=b....` refleja el estado real
    de los 4 canales.
  - Enviar cualquier comando por el canal `Public` (u otro canal no
    hashtag) y confirmar que no hay reacción.
