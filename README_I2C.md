# Actuador I2C sobre comando en canal público (XIAO nRF52840 + Wio-SX1262)

Esta variante del firmware companion de MeshCore añade una funcionalidad
adicional: cuando el nodo recibe, en un canal de grupo (por defecto el canal
público "Public"), un mensaje de texto que coincide exactamente con una
palabra clave configurable, activa o desactiva un pin de un expansor I2C
**PCF8574** conectado por los pines SDA/SCL de la placa. Ese pin puede
gobernar, por ejemplo, un relé que a su vez controla un actuador externo.

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
   texto al canal (desde la app companion de MeshCore, ej. `ACTUATOR_ON`).
   El texto real que viaja por la malla siempre lleva el prefijo
   `"<nombre_del_nodo_emisor>: "` (lo agrega `sendGroupMessage()`), por eso
   el firmware compara la palabra clave como **sufijo** del mensaje, no
   como igualdad exacta.
2. El nodo companion descifra el mensaje del canal (esto ya ocurre siempre,
   sea o no un comando de actuador) y compara el sufijo del texto contra
   las dos palabras clave configuradas.
3. Si coincide, el firmware valida que el canal por el que llegó sea un
   **canal hashtag autorizado** (ver más abajo) antes de actuar.
4. Si también pasa esa validación, escribe por I2C sobre el PCF8574 para
   poner en alto o en bajo el pin configurado como salida del actuador.
5. Como única confirmación, el LED integrado de la placa parpadea
   brevemente. El comando **no** genera respuesta por la malla (no hay
   tráfico de radio adicional).

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

## b) Configurar la palabra clave

Las palabras clave que activan/desactivan el actuador son build flags de
PlatformIO, con valores por defecto si no se especifican:

```ini
; variants/xiao_nrf52/platformio.ini, env Xiao_nrf52_companion_radio_usb
-D ACTUATOR_CMD_ON='"ACTUATOR_ON"'
-D ACTUATOR_CMD_OFF='"ACTUATOR_OFF"'
```

Por defecto (si no defines estos flags) son `"ACTUATOR_ON"` y
`"ACTUATOR_OFF"`. La comparación es **exacta y sensible a mayúsculas**
(`strcmp`), no se procesan prefijos ni parámetros.

Para cambiarlas, descomenta y edita esas dos líneas en
`variants/xiao_nrf52/platformio.ini` dentro del env
`Xiao_nrf52_companion_radio_usb`, y recompila (ver `DEPLOY_I2C.md`).

## c) Configurar la dirección I2C del actuador

También son build flags del mismo env:

```ini
-D PCF8574_I2C_ADDR=0x20      ; dirección I2C del PCF8574 (0x20-0x27 según A0-A2)
-D PCF8574_ACTUATOR_PIN=0     ; pin del PCF8574 (0-7) que gobierna el actuador
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
- El pin de salida configurado (`PCF8574_ACTUATOR_PIN`) del PCF8574 al
  actuador externo (relé, driver, etc.)

## Comportamiento del pin

El comando es tipo **toggle**: `ACTUATOR_ON` deja el pin activo de forma
indefinida hasta recibir `ACTUATOR_OFF`; no hay apagado automático por
tiempo. Si necesitas un pulso momentáneo en vez de un toggle persistente,
modifica `MyMesh::checkActuatorCommand()` en
`examples/companion_radio/MyMesh.cpp`.

## Ver también

- `DEPLOY_I2C.md` — cómo compilar y flashear esta variante del firmware.
- `tools/i2c_actuator_nano/` — proyecto PlatformIO independiente para un
  Arduino Nano que emula un PCF8574 real, útil para probar el comando de
  extremo a extremo sin necesidad de tener el chip físico
  (`cd tools/i2c_actuator_nano && pio run -t upload`, ver su `README.md`).
