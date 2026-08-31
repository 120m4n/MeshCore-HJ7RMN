# Debug + Deploy I2C en modo BLE (XIAO nRF52840 + Wio-SX1262)

Guía paso a paso para compilar y flashear el firmware companion **BLE** con
debug I2C activado, verificar que el comando del canal público llega y
confirmar la escritura I2C en el actuador real.

Basado en `DEBUG_I2C.md` y `DEPLOY_I2C.md` — consulta esos documentos para
el detalle completo (razonamiento, formato de comandos, `README_I2C.md`
para configuración de canal/dirección I2C).

## Paso 1 — Activar debug logging (build BLE)

Edita `variants/xiao_nrf52/platformio.ini`, dentro de
`[env:Xiao_nrf52_companion_radio_ble]`, descomenta:

```ini
-D MESH_DEBUG=1
```

Opcional (más verboso, tráfico LoRa crudo):

```ini
-D MESH_PACKET_LOGGING=1
```

`ACTUATOR_SEND_ACK=1` ya está activo por defecto, no hace falta tocarlo.

## Paso 2 — Compilar

```bash
export FIRMWARE_VERSION=v1.0.0
sh build.sh build-firmware Xiao_nrf52_companion_radio_ble
```

Salida en `out/Xiao_nrf52_companion_radio_ble-<version>.uf2` (+ `.bin` y
`.zip`).

## Paso 3 — Flashear con meshcore.io/flasher

1. Conecta la XIAO por USB-C (no hace falta entrar manualmente en modo
   bootloader UF2; el flasher web maneja el protocolo Secure DFU
   directamente).
2. Abre [meshcore.io/flasher](https://meshcore.io/flasher).
3. Cuando pida un firmware personalizado / custom firmware, sube el
   archivo `out/Xiao_nrf52_companion_radio_ble-<version>.zip` generado en
   el paso 2.
4. Sigue las instrucciones en pantalla del flasher para completar la
   subida.

## Paso 4 — Consola de debug por USB (serie)

```bash
pio device list
pio device monitor -e Xiao_nrf52_companion_radio_ble -b 115200
```

Aquí solo verás logs de debug (`Serial` queda libre porque el companion
habla por BLE, no por USB).

## Paso 5 — Emparejar por BLE y enviar comando

1. Desde la app companion, empareja con el nodo (`MeshCore-...`, PIN
   `123456`).
2. Únete a un canal hashtag propio (el canal `Public` nunca dispara el
   actuador).
3. Envía `PIN0_ON`. Con `MESH_DEBUG` activo deberías ver en la consola:
   ```
   DEBUG: checkActuatorCommand: keyword matched, setting pin 0 to 1
   ```
4. Prueba también `PIN_STATUS` (funciona en cualquier build) — responde
   `STATE=b........`.

## Paso 6 — Confirmar escritura I2C con el Nano de prueba

En el monitor serie del Nano (`tools/i2c_actuator_nano/`), al enviar
`PIN0_ON`/`PIN0_OFF` deberías ver:

```
I2C write: 0x01
I2C write: 0x00
```

## Volver al estado normal

Al terminar, vuelve a comentar `-D MESH_DEBUG=1` (y `MESH_PACKET_LOGGING`
si lo activaste) en el mismo archivo, recompila y reflashea (por
meshcore.io/flasher de nuevo) para dejar el nodo en modo producción sin
overhead de logging.

## Ver también

- `DEBUG_I2C.md` — flujo de debug completo y razonamiento.
- `DEPLOY_I2C.md` — build y flasheo normal (sin debug) para USB y BLE.
- `README_I2C.md` — configuración de canal, palabra clave y dirección I2C.
- `tools/i2c_actuator_nano/` — sketch del Nano usado como sonda I2C.
