# Feedback por radio del actuador I2C: ack de escritura y consulta de estado

**Fecha:** 2026-08-19
**Rama:** `feature/i2c-actuator-public-channel`
**Alcance:** `examples/companion_radio/MyMesh.cpp`, `examples/companion_radio/MyMesh.h`,
`src/helpers/actuators/PCF8574Actuator.h`, `variants/xiao_nrf52/platformio.ini`,
`README_I2C.md`, `DEBUG_I2C.md`.

## Contexto

El actuador I2C sobre PCF8574 (ver `README_I2C.md`) permite controlar 8 salidas
independientes vía comandos `PIN<n>_ON` / `PIN<n>_OFF` en un canal hashtag
autorizado. Hoy el comando **no genera ninguna respuesta por la malla** — la
única confirmación es un parpadeo de LED local, y desde esta sesión, solo en
builds de debug (`MESH_DEBUG`). Un operador remoto que envía un comando no
tiene forma de verificar que llegó, ni qué pin cambió, ni el estado de las
demás salidas, sin estar físicamente presente junto al nodo.

Esta spec cubre tres mejoras evaluadas para el mismo subsistema:

1. **Ack de escritura** — mensaje de confirmación al canal tras un comando
   `PIN<n>_ON/OFF` válido.
2. **Consulta de estado** (`PIN_STATUS`) — comando de solo lectura que
   devuelve el estado de los 8 pines sin modificar nada.
3. **Caché de autorización de canal hashtag** — evaluada y **descartada**
   (ver sección dedicada al final).

## Decisiones de diseño

Todas las decisiones de esta sección fueron confirmadas explícitamente con
el usuario antes de escribir esta spec.

### Ack de escritura: opt-in, apagado por defecto

El ack añade tráfico LoRa (TX extra) en cada comando. Para un nodo a
batería/panel solar dentro de un gabinete — el mismo contexto que motivó
gatear el parpadeo del LED a `MESH_DEBUG` — este costo debe ser opcional, no
automático. Se controla con un nuevo build flag de presencia (no de valor):

```ini
-D ACTUATOR_SEND_ACK=1
```

Sin este flag definido, el comportamiento es exactamente el actual: sin
tráfico de radio adicional. El flag se añade **comentado** en ambos envs de
`variants/xiao_nrf52/platformio.ini`, como ejemplo, siguiendo el mismo
patrón que `ACTUATOR_CMD_PREFIX` y compañía.

### Consulta de estado: siempre disponible, sin flag propio

`PIN_STATUS` solo genera tráfico cuando alguien lo pide explícitamente — el
costo de batería lo decide quien pregunta, no el firmware en reposo. No
tiene el mismo perfil de costo que el ack automático en cada escritura, así
que está disponible en cualquier build con `HAS_PCF8574_ACTUATOR`, sin
gatear tras `ACTUATOR_SEND_ACK` ni un flag nuevo.

### Autorización de `PIN_STATUS`: misma que escritura

`PIN_STATUS` requiere la **misma validación de canal hashtag** que
`PIN<n>_ON/OFF` (ver `isHashtagChannel()` en `MyMesh.cpp`). Es
consistente con el modelo de seguridad actual — evita filtrar el estado de
las salidas a cualquiera en el canal `Public` u otro canal no autorizado —
y no introduce un segundo nivel de sensibilidad que documentar y mantener.

### Formato del estado: `STATE=b<8 bits>`, orden por índice de pin

El estado de los 8 pines se representa como una cadena de 8 caracteres
`'0'`/`'1'` prefijada con `b`, donde **la posición del carácter (de
izquierda a derecha, 0-indexada) es el número de pin**, no el orden
binario MSB-first estándar de un byte:

```
STATE=b00101100
       01234567   <- índice de pin (posición = pin)
```

En el ejemplo, los pines `2`, `4` y `5` están activos (`'1'`). Esto
**no es una conversión binaria directa del byte** `_out_state` en el orden
habitual (bit 7 primero) — es deliberadamente el orden natural de lectura
pin-por-pin, consistente con cómo ya se nombran los pines en el resto de la
documentación (`PIN0`, `PIN1`, ...). Cualquier cliente o humano que lo
decodifique como binario estándar MSB-first lo leerá mal; este punto se
documenta explícitamente en `README_I2C.md`.

Algorítmicamente, para `state` de tipo `uint8_t`:

```cpp
for (uint8_t i = 0; i < 8; i++) {
  out[i] = (state & (1 << i)) ? '1' : '0';
}
out[8] = 0;
```

(Nótese que esto es, de hecho, más simple de implementar que un binario
MSB-first — es un mapeo directo bit→posición sin invertir nada.)

### Mensajes de salida

- **Ack de escritura** (solo si `ACTUATOR_SEND_ACK` definido y
  `actuator.setPin()` retornó `true`):
  `"PIN<n>=<ON|OFF> STATE=b<8 bits>"`, ej. `"PIN3=ON STATE=b00101100"`.
  Si el write I2C falla, no se envía ack (mismo comportamiento silencioso +
  `MESH_DEBUG_PRINTLN` que ya existe para ese caso — no se debe confirmar
  un estado que no se pudo escribir).
- **Respuesta a `PIN_STATUS`** (siempre, sin flag):
  `"STATE=b<8 bits>"`, ej. `"STATE=b00101100"`. No hay operación I2C
  involucrada — usa un nuevo getter en memoria (ver más abajo).

Ambos mensajes caben cómodamente dentro de `MAX_TEXT_LEN`
(`10*CIPHER_BLOCK_SIZE`, ver `src/helpers/BaseChatMesh.h:8`), muy por
encima de los ~25 caracteres del mensaje más largo.

## Componentes

| Archivo | Cambio |
|---|---|
| `src/helpers/actuators/PCF8574Actuator.h` | Nuevo getter `uint8_t getState() const { return _out_state; }` — lectura de RAM, sin I2C, sin fallo posible (el companion es el único maestro I2C del bus, así que `_out_state` siempre refleja lo último escrito al chip). |
| `examples/companion_radio/MyMesh.cpp` | `checkActuatorCommand()` reestructurado para reconocer también `PIN_STATUS`; nuevo helper `buildStateBits()`; helper `textEndsWithCmd()` reintroducido (match exacto de sufijo, sin dígito) para el comando de consulta; envío de ack/status vía `sendGroupMessage()`. |
| `examples/companion_radio/MyMesh.cpp` (defines) | Nuevo default `ACTUATOR_CMD_STATUS` = `"PIN_STATUS"`. |
| `variants/xiao_nrf52/platformio.ini` | Nuevo flag opt-in `ACTUATOR_SEND_ACK` (comentado, ejemplo, off por defecto) en ambos envs; `ACTUATOR_CMD_STATUS` (comentado, ejemplo) junto a los flags de prefijo/sufijo existentes. |
| `README_I2C.md` | Documentar ack, `PIN_STATUS`, formato `STATE=b........` con la nota de orden de bits, y el nuevo flag `ACTUATOR_SEND_ACK`. |
| `DEBUG_I2C.md` | Mencionar el ack/status como parte del flujo de verificación con `MESH_DEBUG`. |

No hay cambios en `PCF8574Actuator.cpp` (el getter es trivial, inline en el
header) ni en `tools/i2c_actuator_nano/` (el simulador solo emula el bus
I2C; el ack/status es tráfico de radio, fuera de su alcance).

## Flujo (`checkActuatorCommand`)

```cpp
void MyMesh::checkActuatorCommand(const mesh::GroupChannel& channel, const char* text) {
  uint8_t pin;
  bool state;
  bool is_write = parseActuatorCmd(text, &pin, &state);
  bool is_status = !is_write && textEndsWithCmd(text, ACTUATOR_CMD_STATUS);
  if (!is_write && !is_status) return;   // not an actuator command

  int idx = findChannelIdx(channel);
  ChannelDetails details;
  if (idx < 0 || !getChannel(idx, details) || !isHashtagChannel(details.name, channel)) {
    MESH_DEBUG_PRINTLN("checkActuatorCommand: keyword matched but channel is not an authorized hashtag channel, ignoring");
    return;
  }

  char bits[9];
  bool did_act = false;

  if (is_write) {
    MESH_DEBUG_PRINTLN("checkActuatorCommand: keyword matched, setting pin %d to %d", (uint32_t)pin, (uint32_t)state);
    did_act = actuator.setPin(pin, state);
#ifdef ACTUATOR_SEND_ACK
    if (did_act) {
      buildStateBits(actuator.getState(), bits);
      char msg[32];
      snprintf(msg, sizeof(msg), "PIN%u=%s STATE=b%s", (unsigned)pin, state ? "ON" : "OFF", bits);
      sendGroupMessage(getRTCClock()->getCurrentTimeUnique(), details.channel, _prefs.node_name, msg, strlen(msg));
    }
#endif
  } else {   // is_status
    MESH_DEBUG_PRINTLN("checkActuatorCommand: status query matched");
    did_act = true;
    buildStateBits(actuator.getState(), bits);
    char msg[24];
    snprintf(msg, sizeof(msg), "STATE=b%s", bits);
    sendGroupMessage(getRTCClock()->getCurrentTimeUnique(), details.channel, _prefs.node_name, msg, strlen(msg));
  }

#if defined(PIN_LED) && defined(MESH_DEBUG)
  if (did_act) {
    digitalWrite(PIN_LED, LOW); delay(100); digitalWrite(PIN_LED, HIGH);
  }
#endif
}
```

`buildStateBits(uint8_t state, char out[9])` y `textEndsWithCmd()` van como
funciones `static` a nivel de archivo, junto a `parseActuatorCmd()` e
`isHashtagChannel()`.

**Timestamp y remitente:** `getRTCClock()->getCurrentTimeUnique()` — mismo
patrón usado en `MyMesh.cpp:1189` para timestamps generados localmente (evita
colisionar con la protección anti-replay que usaría un timestamp repetido).
`_prefs.node_name` como remitente — mismo patrón que el único otro call site
de `sendGroupMessage()` en el archivo (`MyMesh.cpp:1231`). El canal ya está
disponible como `details.channel` (campo mutable de la `ChannelDetails`
local que ya se obtuvo para la validación de autorización), así que no hace
falta ninguna copia adicional para satisfacer la firma
`sendGroupMessage(..., mesh::GroupChannel& channel, ...)` (no-const).

## Manejo de errores

- Comando no reconocido (ni escritura ni `PIN_STATUS`) → `return` silencioso,
  sin cambios respecto al comportamiento actual.
- Canal no autorizado (no-hashtag) → `return` con `MESH_DEBUG_PRINTLN`, sin
  cambios respecto al comportamiento actual. Se aplica igual a `PIN_STATUS`.
- Fallo de escritura I2C (`setPin()` retorna `false`) → no se envía ack
  (aunque `ACTUATOR_SEND_ACK` esté activo), ya logueado por el cambio
  anterior en `PCF8574Actuator::setPin()`.
- Fallo de `sendGroupMessage()` (ej. cola de envío llena) → no se reintenta;
  queda sin más rastro que el comportamiento por defecto de
  `sendGroupMessage()` (no añade logging nuevo, consistente con el resto del
  archivo, que no verifica el valor de retorno en la mayoría de los casos).

## Punto descartado: caché de autorización de canal hashtag

Evaluado y **descartado** — no se diseña implementación.

`isHashtagChannel()` recalcula un sha256 en cada mensaje de canal que
matchea el patrón de un comando de actuador (escritura o `PIN_STATUS`),
incluso si el canal resulta no autorizado. El ahorro de cachear ese
resultado es marginal: sha256 es barato en un nRF52840, y los mensajes de
canal son poco frecuentes frente al resto del tráfico del nodo.

El riesgo de implementarlo supera el beneficio: `ChannelDetails`
(`src/helpers/ChannelDetails.h`) no es una estructura propia del actuador —
es la estructura compartida de todo el protocolo companion, persistida en
flash vía `DataStore.cpp` y expuesta como `BaseChatMesh::channels[MAX_GROUP_CHANNELS]`.
Si un canal se edita vía la app companion (cambio de PSK) sin un hook de
invalidación explícito conectado a `setChannel()`, una caché de autorización
quedaría **obsoleta en silencio** — podría seguir autorizando (o denegando)
comandos de actuador sobre un canal que ya cambió de estado, lo cual es un
fallo de seguridad silencioso, no solo un bug de rendimiento.

Se documenta aquí como decisión arquitectónica para no volver a evaluarla
sin una razón de peso nueva (ej. que el tráfico de comandos de actuador
crezca varios órdenes de magnitud).

## Testing

- Compilar ambos envs (`Xiao_nrf52_companion_radio_ble`,
  `Xiao_nrf52_companion_radio_usb`) en su configuración por defecto (sin
  `ACTUATOR_SEND_ACK`) — debe compilar y comportarse igual que hoy salvo por
  `PIN_STATUS`.
- Compilar con `-D ACTUATOR_SEND_ACK=1` añadido temporalmente a un env, para
  verificar que el código bajo ese `#ifdef` compila.
- Prueba manual end-to-end con `tools/i2c_actuator_nano/`:
  - Enviar `PIN3_ON` a un canal hashtag con `ACTUATOR_SEND_ACK` activo →
    confirmar que el Nano refleja el bit 3, y que un cliente companion en el
    canal recibe `"PIN3=ON STATE=b00101100"` (con los demás pines que ya
    estuvieran activos reflejados en el bitfield).
  - Enviar `PIN_STATUS` (con o sin `ACTUATOR_SEND_ACK`) → confirmar que
    llega `"STATE=b........"` reflejando el estado real, sin que el Nano
    registre ninguna escritura I2C nueva.
  - Enviar `PIN_STATUS` por un canal no-hashtag (ej. `Public`) → confirmar
    que no hay respuesta.
  - Provocar un fallo I2C (desconectar el Nano) y enviar `PIN0_ON` con
    `ACTUATOR_SEND_ACK` activo → confirmar que no se envía ack.
