# Actuador I2C sobre comando en canal público (XIAO nRF52840 + Wio-SX1262)

Esta variante del firmware companion de MeshCore añade una funcionalidad
adicional: cuando el nodo recibe, en un canal de grupo, un mensaje de texto
que coincide con un patrón de comando configurable (`PIN<n>_ON` /
`PIN<n>_OFF`), activa o desactiva el pin `<n>` (0-7) de un expansor I2C
**PCF8574** conectado por los pines SDA/SCL de la placa. Cada uno de los 8
pines es direccionable de forma independiente, y puede gobernar, por
ejemplo, un relé que a su vez controla un actuador externo.

- **Target de hardware**: Seeed XIAO nRF52840 + módulo LoRa Wio-SX1262
  (entorno PlatformIO `variants/xiao_nrf52`).
- **Firmwares soportados**: `Xiao_nrf52_companion_radio_usb` (companion por
  USB) y `Xiao_nrf52_companion_radio_ble` (companion por Bluetooth LE). La
  funcionalidad es idéntica en ambos: el actuador reacciona al mensaje de
  canal recibido por la malla LoRa, no al transporte companion (USB o BLE)
  usado para hablar con la app — ese transporte solo sirve para
  configurar el nodo y leer/enviar mensajes, no interviene en este flujo.
- **Punto de enganche en el código**: `MyMesh::onChannelMessageRecv()` en
  `examples/companion_radio/MyMesh.cpp`.
- **Driver del actuador**: `src/helpers/actuators/PCF8574Actuator.h/.cpp`.

## Cómo funciona

1. Cualquier persona que conozca la clave del canal envía un mensaje de
   texto al canal (desde la app companion de MeshCore, ej. `PIN3_ON` para
   activar el pin 3). El texto real que viaja por la malla siempre lleva
   el prefijo `"<nombre_del_nodo_emisor>: "` (lo agrega `sendGroupMessage()`),
   por eso el firmware compara el comando como **sufijo** del mensaje, no
   como igualdad exacta.
2. El nodo companion descifra el mensaje del canal (esto ya ocurre siempre,
   sea o no un comando de actuador) y compara el sufijo del texto contra el
   patrón `<prefijo><dígito 0-7><sufijo _ON o _OFF>` configurado.
3. Si coincide, el firmware valida que el canal por el que llegó sea un
   **canal hashtag autorizado** (ver más abajo) antes de actuar.
4. Si también pasa esa validación, escribe por I2C sobre el PCF8574 para
   poner en alto o en bajo el pin indicado en el comando (0-7). Cualquiera
   de los 8 pines es controlable de forma independiente con el mismo canal
   — no hay restricción de autorización por pin, solo a nivel de canal.
5. Por defecto el comando **no** genera respuesta por la malla (no hay
   tráfico de radio adicional) ni confirmación visual en builds de
   producción — el parpadeo del LED integrado como confirmación local solo
   está activo en builds compilados con `MESH_DEBUG` (ver `DEBUG_I2C.md`),
   para no gastar batería en un nodo dentro de un gabinete alimentado por
   panel solar. Opcionalmente, con el flag `ACTUATOR_SEND_ACK` (ver más
   abajo), el nodo responde al mismo canal confirmando el cambio.

## Consultar y confirmar el estado por radio

Además de encender/apagar pines, el firmware soporta dos mecanismos para
verificar el estado de las salidas de forma remota, sin acceso físico al
nodo:

- **Consulta de estado (`PIN_STATUS`)**: siempre disponible (no requiere
  ningún flag) en cualquier canal hashtag autorizado. Envía la palabra de
  comando `PIN_STATUS` al canal (mismo mecanismo de sufijo que
  `PIN<n>_ON/OFF`) y el nodo responde al canal con el estado de los 8
  pines. A diferencia del ack de escritura, esta consulta **lee el chip
  PCF8574 por I2C en el momento** (no confía ciegamente en el estado que el
  firmware tiene en memoria) — importante si el PCF8574 tiene su propia
  alimentación (ver "Conexión física" más abajo) y esa alimentación sufrió
  algún corte sin contingencia, ya que el chip pudo volver a su estado de
  power-on sin que el firmware se enterara:

  ```
  STATE=b00101100
  ```

  Si la lectura revela que el chip real no coincidía con lo que el
  firmware tenía en memoria, la respuesta lo indica explícitamente:

  ```
  STATE=b11111111 (resynced)
  ```

  (el firmware **no** reescribe el chip para forzar el último estado
  comandado — reporta la realidad leída y actualiza su propio registro
  interno para coincidir con ella; la decisión de volver a activar una
  salida tras un corte de energía es de quien opera el sistema, no
  automática). Si la lectura I2C falla (no un mismatch, sino un error de
  bus), la respuesta usa el último valor conocido, marcado como no
  verificado:

  ```
  STATE=b00101100 (cached)
  ```

- **Ack de escritura** (opt-in, build flag `ACTUATOR_SEND_ACK`): si está
  activo, cada `PIN<n>_ON`/`PIN<n>_OFF` que se ejecute con éxito genera
  además una respuesta al canal:

  ```
  PIN3=ON STATE=b00101100
  ```

  Este ack **no** se envía si la escritura I2C falla (el estado real es
  incierto en ese caso) ni si `ACTUATOR_SEND_ACK` no está definido — por
  defecto sigue apagado, ya que añade tráfico LoRa en cada comando y en un
  nodo a batería/panel solar ese costo debe ser una decisión explícita, no
  automática.

### Formato de `STATE=b........`

Los 8 caracteres tras `b` representan el estado de los 8 pines del
PCF8574, pero **en un orden que no es el binario estándar**: la posición
del carácter (de izquierda a derecha, empezando en 0) es directamente el
número de pin — no el orden MSB-first habitual de un byte.

```
STATE=b00101100
       01234567   <- número de pin en esa posición
```

En el ejemplo, los pines `2`, `4` y `5` están en `1` (activos); el resto en
`0`. Si decodificas esto como un binario estándar (bit 7 primero) vas a
leer los pines equivocados — la posición **es** el número de pin.

### Restricción de seguridad: solo canales hashtag

El actuador **solo** se dispara si el mensaje llegó por un canal cuyo
secreto sea verificablemente `sha256(nombre_del_canal)[:16]` — la
convención de "canal hashtag" documentada en `docs/companion_protocol.md`
(típicamente canales cuyo nombre empieza con `#`, cuya clave se deriva del
propio nombre). Esto excluye automáticamente:

- El canal **`Public`**: su clave (`PUBLIC_GROUP_PSK`) es un valor fijo
  no derivado de la palabra `"Public"`, así que nunca pasa esta
  validación, sin importar qué mensaje llegue.
- Cualquier **canal privado** (clave aleatoria, no derivada del nombre) —
  aunque conozcas su PSK y lo hayas unido correctamente, el actuador lo
  ignora.

La implementación está en `isHashtagChannel()` /
`MyMesh::checkActuatorCommand()`, en `examples/companion_radio/MyMesh.cpp`.
Si el canal no pasa la validación, se descarta en silencio (visible solo
con `MESH_DEBUG`, ver `DEBUG_I2C.md`).

## a) Configurar el canal

Por defecto, MeshCore precarga un canal llamado `"Public"` con una PSK fija
compartida por toda la red MeshCore (`PUBLIC_GROUP_PSK`, definida en
`examples/companion_radio/MyMesh.cpp`) — pero, por la restricción de
seguridad de arriba, el actuador **nunca** reacciona a comandos en ese
canal.

Para que el actuador funcione, necesitas un canal hashtag propio:

1. Desde la app companion (o cualquier cliente que hable el protocolo, ver
   `docs/companion_protocol.md`), crea/únete a un canal cuyo nombre
   empiece con `#` (p. ej. `#mi-actuador-xyz`) — usa algo poco obvio si no
   quieres que cualquiera que adivine el nombre pueda controlarlo, ya que
   la clave se deriva determinísticamente de ese nombre (`sha256(nombre)`,
   ver `docs/companion_protocol.md:439-441`).
2. Repite el mismo nombre exacto (mayúsculas/minúsculas incluidas) en
   cualquier otro nodo desde el que quieras enviar el comando — no hace
   falta compartir ningún secreto a mano, la app lo deriva igual en ambos.
3. Envía la palabra clave de comando a ese canal.

No es necesario tocar el firmware para cambiar de canal: es una operación
en tiempo de ejecución vía el protocolo companion.

## b) Configurar el patrón de comando

El comando tiene la forma `<prefijo><pin>_ON` / `<prefijo><pin>_OFF`, donde
`<pin>` es un único dígito `0`-`7` que identifica el pin del PCF8574 a
controlar (ej. `PIN3_ON` activa el pin 3, `PIN3_OFF` lo desactiva). El
prefijo y los dos sufijos son build flags de PlatformIO, con valores por
defecto si no se especifican:

```ini
; variants/xiao_nrf52/platformio.ini, env Xiao_nrf52_companion_radio_usb
-D ACTUATOR_CMD_PREFIX='"PIN"'
-D ACTUATOR_CMD_ON_SUFFIX='"_ON"'
-D ACTUATOR_CMD_OFF_SUFFIX='"_OFF"'
-D ACTUATOR_CMD_STATUS='"PIN_STATUS"'
-D ACTUATOR_SEND_ACK=1
```

Por defecto (si no defines los tres primeros flags) son `"PIN"`, `"_ON"` y
`"_OFF"`, lo que da comandos `PIN0_ON`..`PIN7_ON` / `PIN0_OFF`..`PIN7_OFF`.
La comparación es **exacta y sensible a mayúsculas**, no se procesan
parámetros adicionales, y un dígito fuera de `0`-`7` (ej. `PIN9_ON`) no
coincide con ningún comando. `ACTUATOR_CMD_STATUS` (default `"PIN_STATUS"`)
es la palabra de comando para la consulta de estado — ver
["Consultar y confirmar el estado por radio"](#consultar-y-confirmar-el-estado-por-radio)
más arriba.

`ACTUATOR_SEND_ACK` es distinto a los demás: es un flag de **presencia**
(no un string), y por defecto está **apagado** — sin él definido, el
comportamiento es el mismo de siempre, sin tráfico de radio extra. Actívalo
solo si necesitas confirmación remota de que el comando se ejecutó y
aceptas el costo de batería que implica.

Para cambiarlas, descomenta y edita esas líneas en
`variants/xiao_nrf52/platformio.ini` dentro del env
`Xiao_nrf52_companion_radio_usb`, y recompila (ver `DEPLOY_I2C.md`).

## c) Configurar la dirección I2C del actuador

También es un build flag del mismo env:

```ini
-D PCF8574_I2C_ADDR=0x20      ; dirección I2C del PCF8574 (0x20-0x27 según A0-A2)
```

La dirección `0x20` corresponde a un PCF8574 con los pines de dirección
A0/A1/A2 a masa. Si tu módulo usa otra combinación de A0-A2, ajusta el valor
(rango típico `0x20`-`0x27`; los módulos PCF8574A usan `0x38`-`0x3F`).

### Conexión física

En la XIAO nRF52840, el bus I2C ya está inicializado por el firmware
(`Wire.begin()` en `XiaoNrf52Board::begin()`) sobre estos pines fijos de la
placa (definidos en `variants/xiao_nrf52/platformio.ini`):

| Señal | Pin XIAO |
|-------|----------|
| SDA   | D7       |
| SCL   | D6       |

Conecta el PCF8574 así:

- `SDA` del PCF8574 → `D7` de la XIAO
- `SCL` del PCF8574 → `D6` de la XIAO
- `VCC`/`GND` del PCF8574 → alimentación compartida con la XIAO (o su propia
  fuente, si el actuador consume más corriente de la que la XIAO puede dar)
- Pull-ups en SDA/SCL (4.7kΩ típico) si tu módulo PCF8574 no las trae
  integradas
- Cualquiera de los 8 pines de salida del PCF8574 (0-7, según el comando
  recibido) al actuador externo correspondiente (relé, driver, etc.)

## Comportamiento del pin

El comando es tipo **toggle**: `PIN<n>_ON` deja el pin `<n>` activo de
forma indefinida hasta recibir `PIN<n>_OFF`; no hay apagado automático por
tiempo. Cada pin se controla de forma independiente — activar uno no
afecta el estado de los demás. Si necesitas un pulso momentáneo en vez de
un toggle persistente, modifica `MyMesh::checkActuatorCommand()` en
`examples/companion_radio/MyMesh.cpp`.

## Ver también

- `DEPLOY_I2C.md` — cómo compilar y flashear esta variante del firmware.
- `tools/i2c_actuator_nano/` — proyecto PlatformIO independiente para un
  Arduino Nano que emula un PCF8574 real, útil para probar el comando de
  extremo a extremo sin necesidad de tener el chip físico
  (`cd tools/i2c_actuator_nano && pio run -t upload`, ver su `README.md`).
- `README_I2C_SENSOR.md` — comando `TEMP_STATUS` para consultar un sensor
  AM2301 por el mismo bus I2C y canal hashtag.
