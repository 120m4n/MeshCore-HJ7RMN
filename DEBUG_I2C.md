# Debug en placa real: comando de canal público → actuador I2C

Flujo paso a paso para verificar, con la XIAO nRF52840 + Wio-SX1262 física,
que (1) el mensaje del canal público llega y se compara contra la palabra
clave, y (2) el valor efectivamente se escribe por I2C. Este documento es
exploratorio/de investigación — no requiere ni implica cambios permanentes
al código, solo a un build flag que se revierte al terminar (ver el
[último paso](#volver-al-estado-normal)).

## Por qué usar el companion **BLE** para esta sesión de debug

En `examples/companion_radio/main.cpp:117`, `Serial.begin(115200)` se llama
siempre, en cualquier target. Pero solo el env
`Xiao_nrf52_companion_radio_usb` ata ese mismo `Serial` al protocolo
companion (`usb_serial_interface.begin(Serial)` en la línea 215, activo
solo si `ENABLE_USB_INTERFACE` está definido).

Esto importa porque `MESH_DEBUG_PRINTLN` también imprime por `Serial`
(`Serial.printf`, ver `src/MeshCore.h`). Consecuencia práctica:

- **Env USB**: activar `MESH_DEBUG` mezclaría texto de debug con los frames
  binarios del protocolo companion en el mismo puerto → puede romper la
  comunicación con la app mientras esté conectada.
- **Env BLE**: el companion habla por Bluetooth, así que `Serial`/USB queda
  libre. Es seguro usarlo como consola de debug limpia sin afectar el
  companion.

Por eso el flujo de abajo flashea el firmware como **companion BLE**, y usa
el mismo cable USB solo para alimentación + consola serie de debug (no para
el protocolo companion).

## Prerrequisitos

- PlatformIO Core instalado (`pio --version`).
- XIAO nRF52840 + Wio-SX1262, conectada por USB-C.
- Recomendado: el Nano de `tools/i2c_actuator_nano/` como actuador de
  prueba, cableado a SDA(D7)/SCL(D6)/GND de la XIAO, con su propio cable
  USB a la máquina para ver su monitor serie por separado.
- Una app/cliente companion BLE (app móvil MeshCore, `meshcore_py`,
  `meshcore.js`) para emparejar y enviar el mensaje al canal.

## Paso 1 — Activar logging de debug en el build BLE

En `variants/xiao_nrf52/platformio.ini`, dentro de
`[env:Xiao_nrf52_companion_radio_ble]`, descomenta:

```ini
-D MESH_DEBUG=1
```

Opcional, más verboso (tráfico de paquetes LoRa en crudo):

```ini
-D MESH_PACKET_LOGGING=1
```

Es un cambio temporal para esta investigación — reviértelo en el
[último paso](#volver-al-estado-normal).

## Paso 2 — Compilar

```bash
sh build.sh build-firmware Xiao_nrf52_companion_radio_ble
```

(o `pio run -e Xiao_nrf52_companion_radio_ble`, ver `DEPLOY_I2C.md` para
el detalle de rutas de salida).

## Paso 3 — Flashear (UF2)

1. Conecta la XIAO por USB.
2. Doble clic rápido en el botón de reset → aparece el volumen
   `XIAO-SENSE` / `NRF52BOOT`.
3. Copia el `.uf2`:

   ```bash
   cp out/Xiao_nrf52_companion_radio_ble-*.uf2 /Volumes/XIAO-SENSE/
   ```

4. La placa se reinicia sola con el nuevo firmware.

## Paso 4 — Abrir la consola de debug por USB con PlatformIO

Con la placa ya arrancada en firmware normal (no en modo bootloader),
identifica el puerto:

```bash
pio device list
```

Busca algo como `/dev/cu.usbmodemXXXX` (macOS). Luego:

```bash
pio device monitor -e Xiao_nrf52_companion_radio_ble -b 115200
```

o apuntando el puerto explícitamente:

```bash
pio device monitor -p /dev/cu.usbmodemXXXX -b 115200
```

Deberías ver las líneas `DEBUG: ...` de arranque (init de radio,
filesystem, etc.) y cualquier `MESH_DEBUG_PRINTLN` que se dispare después
— incluyendo el aviso de error I2C de `PCF8574Actuator.cpp` si el actuador
no responde.

## Paso 5 — Emparejar por BLE y enviar el comando

1. Desde la app companion, escanea y empareja con el nodo (nombre con
   prefijo `MeshCore-...`, PIN = `BLE_PIN_CODE`, por defecto `123456` en
   `variants/xiao_nrf52/platformio.ini`).
2. Únete a un canal hashtag propio (ver `README_I2C.md` — el canal `Public`
   nunca dispara el actuador).
3. Envía el mensaje de texto `PIN0_ON` al canal.

Con `MESH_DEBUG` activo, `MyMesh::checkActuatorCommand()` ahora imprime una
línea explícita al hacer match:

```
DEBUG: checkActuatorCommand: keyword matched, setting pin 0 to 1
```

(pin y valor según el dígito del comando y si fue sufijo `_ON`/`_OFF`).
Si el texto recibido no coincide con ningún comando (o el dígito está
fuera de `0`-`7`), no se imprime nada — solo verás el tráfico genérico de
radio/mesh de la recepción del mensaje.

Para probar también la consulta de estado, envía `PIN_STATUS` al mismo
canal — no requiere ningún flag adicional, responde en cualquier build. En
la app companion deberías ver llegar un mensaje `STATE=b........` de
vuelta al canal (ver `README_I2C.md` para el formato). Si además
compilaste con `-D ACTUATOR_SEND_ACK=1`, cada `PIN0_ON`/`PIN0_OFF` también
genera un mensaje `PIN0=ON STATE=b........` / `PIN0=OFF STATE=b........`.

`PIN_STATUS` lee el PCF8574 real por I2C en el momento, no el caché en
memoria del firmware — para probar la detección de drift, escribe `r` +
Enter en el monitor serie del Nano (ver `tools/i2c_actuator_nano/README.md`)
para simular que el chip perdió su estado por un corte de energía, y luego
envía `PIN_STATUS`: la respuesta debería llegar como
`STATE=b11111111 (resynced)` en vez del estado que tenía antes del `r`.

Para probar el reset, enciende un par de pines (`PIN0_ON`, `PIN3_ON`) y
luego envía `PIN_RESET` al mismo canal — tampoco requiere ningún flag. En
la consola de debug deberías ver:

```
DEBUG: checkActuatorCommand: reset matched, setting all pins OFF
```

y en la app companion el mensaje `RESET STATE=b00000000` llega siempre
(esta confirmación no depende de `ACTUATOR_SEND_ACK`, a diferencia del ack
de `PIN<n>_ON/OFF`).

## Paso 6 — Confirmar la escritura I2C con el Nano de prueba

En paralelo, abre el monitor serie del Nano (Arduino IDE Serial Monitor, o
`pio device monitor` apuntando a su puerto, 115200 baudios). Al enviar
`PIN0_ON` deberías ver:

```
I2C write: 0x01
```

y al enviar `PIN0_OFF`:

```
I2C write: 0x00
```

Prueba también con otro pin, ej. `PIN3_ON`, y confirma que solo cambia el
bit correspondiente (`I2C write: 0x08`), sin afectar el estado de los
demás pines.

Con varios pines activos (ej. `PIN0_ON` + `PIN3_ON`, `I2C write: 0x09`),
envía `PIN_RESET` y confirma que llega **una sola** escritura:

```
I2C write: 0x00
```

en vez de una escritura por pin — así se verifica que el reset es
atómico (una transacción I2C), no una secuencia de `setPin()`.

Esto confirma sin ambigüedad que el byte llegó por el bus físico SDA/SCL,
independientemente de lo que se vea (o no) en la consola de debug de la
XIAO.

## Qué confirma cada paso

- **Pasos 4-5**: el nodo arrancó, se conectó por BLE y procesó el mensaje
  del canal.
- **Paso 6**: el comando efectivamente disparó una escritura I2C con el
  valor esperado.
- El parpadeo del LED integrado es la confirmación visual más simple e
  inmediata, sin necesidad de consola — pero solo está activo en este
  build de debug (`MESH_DEBUG`). En producción no parpadea, para no
  gastar batería en un nodo alimentado por panel solar dentro de un
  gabinete.

## Volver al estado normal

Al terminar la sesión de debug, vuelve a comentar `-D MESH_DEBUG=1` (y
`MESH_PACKET_LOGGING` si lo activaste) en
`variants/xiao_nrf52/platformio.ini`, recompila y reflashea antes de dejar
el nodo en uso normal, para no arrastrar el overhead de logging de forma
permanente.

## Ver también

- `README_I2C.md` — configuración de canal, palabra clave y dirección I2C.
- `DEPLOY_I2C.md` — build y flasheo normal (sin debug) para USB y BLE.
- `tools/i2c_actuator_nano/` — sketch del Nano usado como sonda I2C.
