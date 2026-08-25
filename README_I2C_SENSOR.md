# Sensor AM2301 I2C sobre comando en canal público (XIAO nRF52840 + Wio-SX1262)

Esta variante del firmware companion de MeshCore añade una funcionalidad
adicional: cuando el nodo recibe, en un canal de grupo, un mensaje de
texto que coincide con la palabra de comando configurable (`TEMP_STATUS`
por defecto), consulta por I2C un puente Arduino Nano que lee un sensor
**AM2301** (DHT21) de temperatura/humedad, y responde en el mismo canal
con los valores leídos.

- **Target de hardware**: Seeed XIAO nRF52840 + módulo LoRa Wio-SX1262
  (entorno PlatformIO `variants/xiao_nrf52`), igual que el actuador I2C
  (ver `README_I2C.md`) — comparte el mismo bus I2C (`D6`/`D7`).
- **Firmwares soportados**: `Xiao_nrf52_companion_radio_usb` y
  `Xiao_nrf52_companion_radio_ble`.
- **Punto de enganche en el código**: `MyMesh::onChannelMessageRecv()` en
  `examples/companion_radio/MyMesh.cpp`.
- **Driver del sensor**: `src/helpers/i2c_sensors/AM2301RemoteSensor.h/.cpp`.
- **Puente físico**: `tools/i2c_sensor_am2301_nano/` — un Arduino Nano
  dedicado que lee el AM2301 localmente (protocolo bit-banged de un
  hilo, no I2C nativo) y lo expone como esclavo I2C en la dirección
  `0x21`. A diferencia de `tools/i2c_actuator_nano` (un doble de pruebas
  de un PCF8574 real), este Nano **es** el puente de producción.

## Cómo funciona

1. Cualquier persona que conozca la clave del canal envía la palabra de
   comando (`TEMP_STATUS` por defecto) al canal (mismo mecanismo de
   sufijo que el actuador — el texto real lleva el prefijo
   `"<nombre_del_nodo_emisor>: "`, así que se compara como sufijo, no
   como igualdad exacta).
2. El firmware valida que el canal sea un **canal hashtag autorizado**
   (misma validación que el actuador — ver `README_I2C.md`).
3. Si pasa la validación, lee por I2C el Nano-puente AM2301 (5 bytes:
   estado + temperatura + humedad) y responde en el canal:

   ```
   TEMP=23.5C HUM=45.2%
   ```

   Si la última lectura del AM2301 en el Nano falló pero hay un valor
   previo válido:

   ```
   TEMP=23.5C HUM=45.2% (cached)
   ```

   Si el Nano nunca logró una lectura exitosa desde que arrancó:

   ```
   TEMP=n/a HUM=n/a (no reading yet)
   ```

   Si la transacción I2C con el Nano falla (bus, no el sensor):

   ```
   TEMP=n/a HUM=n/a (i2c error)
   ```

4. Es una consulta pura — no hay flag de opt-in como
   `ACTUATOR_SEND_ACK`, siempre responde cuando el comando y el canal
   son válidos, igual que `PIN_STATUS`.

## a) Configurar el canal

Igual que el actuador: necesitas un canal hashtag propio (nombre que
empiece con `#`), ver la sección "a) Configurar el canal" en
`README_I2C.md` — el mecanismo es idéntico, ambos comandos pueden
convivir en el mismo canal.

## b) Configurar la palabra de comando

```ini
; variants/xiao_nrf52/platformio.ini, envs Xiao_nrf52_companion_radio_usb / _ble
; -D SENSOR_CMD_STATUS='"TEMP_STATUS"'
```

Por defecto (si no defines el flag) es `"TEMP_STATUS"`. La comparación es
exacta y sensible a mayúsculas, igual que los comandos del actuador.

## c) Configurar la dirección I2C del puente

```ini
-D AM2301_SENSOR_I2C_ADDR=0x21   ; dirección I2C del Nano-puente AM2301
```

`0x21` es distinta de `0x20` (PCF8574 del actuador) para que ambos
dispositivos convivan en el mismo bus sin colisión.

## d) Activar la funcionalidad

A diferencia del actuador (activo por defecto), este sensor es **opt-in**:

```ini
; -D HAS_AM2301_SENSOR=1
```

Descomenta esta línea en ambos envs de
`variants/xiao_nrf52/platformio.ini` y recompila (ver `DEPLOY_I2C.md`)
una vez tengas el Nano-puente cableado.

### Conexión física

Mismo bus I2C ya inicializado por el firmware, compartido con el
actuador si también lo usas:

| Señal | Pin XIAO |
|-------|----------|
| SDA   | D7       |
| SCL   | D6       |

Conecta el Nano-puente AM2301 (ver `tools/i2c_sensor_am2301_nano/README.md`
para su propio cableado hacia el sensor):

- `SDA` del Nano → `D7` de la XIAO
- `SCL` del Nano → `D6` de la XIAO
- `GND` del Nano → GND compartido con la XIAO
- Pull-ups en SDA/SCL (4.7kΩ típico) — una sola por línea si el bus ya
  las tiene por el actuador, no dupliques

## Ver también

- `README_I2C.md` — el actuador I2C sobre PCF8574 con el que este sensor
  comparte bus, canal y modelo de seguridad.
- `tools/i2c_sensor_am2301_nano/` — el puente Nano+AM2301, cómo cablearlo
  y flashearlo.
