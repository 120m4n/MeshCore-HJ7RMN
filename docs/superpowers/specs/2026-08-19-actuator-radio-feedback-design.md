# Feedback por radio del actuador I2C: ack de escritura y consulta de estado

**Fecha:** 2026-08-19
**Rama:** `feature/i2c-actuator-public-channel`
**Alcance:** `examples/companion_radio/MyMesh.cpp`, `examples/companion_radio/MyMesh.h`,
`src/helpers/actuators/PCF8574Actuator.h/.cpp`, `variants/xiao_nrf52/platformio.ini`,
`tools/i2c_actuator_nano/src/i2c_actuator_nano.ino`, `README_I2C.md`, `DEBUG_I2C.md`.

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
4. **Lectura real del chip para `PIN_STATUS`** (`readState()`) — añadida
   tras aprobar (1) y (2): un `getState()` basado solo en caché no detecta
   que el PCF8574 pudo perder su estado por un corte de energía en su
   propio riel (si tiene fuente separada de la XIAO, ver `README_I2C.md`)
   sin que el firmware companion se entere. `PIN_STATUS` pasa a leer el
   chip de verdad en vez de confiar ciegamente en el caché.

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

### `PIN_STATUS` lee el chip real, no solo el caché

**Motivación:** `_out_state` es fiable mientras el companion sea el único
que pueda cambiar el estado del PCF8574 — cierto en operación normal, pero
no si el PCF8574 tiene su propia alimentación (ver "Conexión física" en
`README_I2C.md`) y esa alimentación sufre un corte total sin contingencia
(sin batería de respaldo en ese riel): el chip vuelve a su estado de
power-on (`0xFF`, todos los pines HIGH) sin que la XIAO —que puede seguir
alimentada y con el firmware corriendo normalmente— se entere. En ese
escenario, `getState()` reportaría un estado que ya no es cierto.

**Cuándo se lee de verdad:** únicamente al responder `PIN_STATUS`. No hay
polling periódico en `loop()` — evita añadir un temporizador y tráfico I2C
de fondo por una verificación que solo importa cuando alguien
efectivamente pregunta.

**Resolución de drift (lectura real ≠ caché):** el chip manda. Se
actualiza `_out_state` para reflejar la realidad leída y se reporta esa
realidad — el firmware **no** reescribe el chip para forzar el último
estado comandado. Justificación de seguridad: si una salida se apagó por
un corte de energía legítimo, reactivarla automáticamente sin que nadie lo
decidió explícitamente podría ser peligroso para lo que esa salida
controle (relé, actuador externo). El firmware informa la realidad; la
decisión de volver a encenderla es de quien opera el sistema.

**Fallo de lectura I2C** (error de bus, no mismatch de valores): se
responde igual con el último valor en caché, pero marcado como no
verificado — mejor una respuesta útil aunque no confirmada que ninguna.

Los tres casos posibles en la respuesta a `PIN_STATUS`:

| Caso | Mensaje |
|---|---|
| Lectura OK, coincide con el caché | `STATE=b00101100` |
| Lectura OK, **no** coincide (drift real) | `STATE=b00101100 (resynced)` |
| Lectura I2C falla | `STATE=b00101100 (cached)` |

El ack de escritura (`ACTUATOR_SEND_ACK`) sigue usando `getState()`
(caché), sin cambios: justo se acaba de escribir ese valor, así que releerlo
por I2C no aporta información nueva.

## Componentes

| Archivo | Cambio |
|---|---|
| `src/helpers/actuators/PCF8574Actuator.h` | Nuevo getter `uint8_t getState() const { return _out_state; }` — lectura de RAM, sin I2C. Nueva declaración `bool readState(uint8_t* out, bool* drifted = NULL)` — lectura I2C real, resincroniza `_out_state` con lo leído. |
| `src/helpers/actuators/PCF8574Actuator.cpp` | Implementación de `readState()`: `Wire.requestFrom()` + `Wire.read()`, compara contra `_out_state`, resincroniza, loguea drift con `MESH_DEBUG_PRINTLN`. |
| `examples/companion_radio/MyMesh.cpp` | `checkActuatorCommand()` reestructurado para reconocer también `PIN_STATUS`; nuevo helper `buildStateBits()`; helper `textEndsWithCmd()` reintroducido (match exacto de sufijo, sin dígito) para el comando de consulta; envío de ack/status vía `sendGroupMessage()`; la rama `PIN_STATUS` usa `readState()` en vez de `getState()` y anota `(resynced)`/`(cached)` según el resultado. |
| `examples/companion_radio/MyMesh.cpp` (defines) | Nuevo default `ACTUATOR_CMD_STATUS` = `"PIN_STATUS"`. |
| `variants/xiao_nrf52/platformio.ini` | Nuevo flag opt-in `ACTUATOR_SEND_ACK` (comentado, ejemplo, off por defecto) en ambos envs; `ACTUATOR_CMD_STATUS` (comentado, ejemplo) junto a los flags de prefijo/sufijo existentes. |
| `tools/i2c_actuator_nano/src/i2c_actuator_nano.ino` | Nuevo `Wire.onRequest()` — el Nano ahora responde a una lectura I2C con `last_state` (antes solo reaccionaba a escrituras). Nuevo comando por consola serie (`r` + Enter) que fuerza `last_state = 0xFF` sin pasar por `onI2CReceive`, simulando un corte de energía en el chip sin que el firmware companion se entere hasta la próxima lectura. |
| `README_I2C.md` | Documentar ack, `PIN_STATUS`, formato `STATE=b........` con la nota de orden de bits, las anotaciones `(resynced)`/`(cached)`, y el nuevo flag `ACTUATOR_SEND_ACK`. |
| `DEBUG_I2C.md` | Mencionar el ack/status como parte del flujo de verificación con `MESH_DEBUG`, más el paso para simular el corte de energía con el comando `r` del Nano. |
| `tools/i2c_actuator_nano/README.md` | Documentar el nuevo comando `r` de simulación de reset. |

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

    uint8_t chip_state;
    bool drifted = false;
    bool verified = actuator.readState(&chip_state, &drifted);
    buildStateBits(chip_state, bits);

    char msg[40];
    if (!verified) {
      snprintf(msg, sizeof(msg), "STATE=b%s (cached)", bits);
    } else if (drifted) {
      snprintf(msg, sizeof(msg), "STATE=b%s (resynced)", bits);
    } else {
      snprintf(msg, sizeof(msg), "STATE=b%s", bits);
    }
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
- Fallo de lectura I2C en `readState()` (respondiendo `PIN_STATUS`) → no se
  descarta la respuesta; se envía con el bitfield en caché y la anotación
  `(cached)`, más `MESH_DEBUG_PRINTLN` del error (mismo estilo que el resto
  de fallos I2C del driver).
- Drift detectado en `readState()` (lectura real ≠ caché) → **no** es un
  error: se resincroniza `_out_state` a la realidad leída, se responde con
  `(resynced)`, y se loguea con `MESH_DEBUG_PRINTLN` para quien esté
  depurando en consola. El firmware nunca reescribe el chip para forzar el
  último estado comandado.

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
  - Enviar `PIN3_ON`, luego escribir `r` + Enter en el monitor serie del
    Nano (simula el corte de energía del chip), y enviar `PIN_STATUS` →
    confirmar que la respuesta llega con `(resynced)` y refleja `0xFF`
    (todos los pines HIGH), no el estado que tenía antes del `r`.
  - Con el Nano desconectado, enviar `PIN_STATUS` → confirmar que la
    respuesta llega con `(cached)`.
