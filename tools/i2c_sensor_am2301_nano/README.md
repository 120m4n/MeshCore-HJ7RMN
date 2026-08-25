# i2c_sensor_am2301_nano

Puente I2C real para un sensor **AM2301** (DHT21) de temperatura/humedad,
corriendo en un Arduino Nano. A diferencia de `tools/i2c_actuator_nano`
(un doble de pruebas para un PCF8574 real), este Nano **es** el puente de
producción: el AM2301 no tiene interfaz I2C nativa, así que un
microcontrolador que lo lea y lo re-exponga por I2C es la solución
definitiva, no un simulacro de banco.

Permite que el firmware companion de MeshCore (XIAO nRF52840) consulte
temperatura/humedad con el comando de canal `TEMP_STATUS` (ver
`README_I2C_SENSOR.md` en la raíz del repo).

Es un proyecto PlatformIO **independiente** del firmware principal de
MeshCore — se compila y flashea desde esta carpeta.

## Cableado

Pines I2C hardware del Arduino Nano:

| Señal | Pin Nano | Conecta a |
|-------|----------|-----------|
| SDA   | A4       | XIAO `D7` (`PIN_WIRE_SDA`) |
| SCL   | A5       | XIAO `D6` (`PIN_WIRE_SCL`) |
| GND   | GND      | GND común con la XIAO |

Agrega pull-ups de 4.7kΩ en SDA/SCL a 3.3V/5V si tu cableado no las trae
ya — si este Nano comparte bus con `i2c_actuator_nano`, no dupliques las
pull-ups, una sola por línea alcanza.

El AM2301 se conecta así:

| Señal AM2301 | Pin Nano |
|---|---|
| DATA | D2 (con pull-up de 10kΩ a VCC si el módulo no la trae integrada) |
| VCC | 3.3V/5V según el módulo |
| GND | GND |

## Dirección I2C

Este Nano responde en `0x21` — distinta de `0x20` (PCF8574), así que
ambos rigs pueden convivir en el mismo bus sin colisión.

## Compilar y flashear

```sh
cd tools/i2c_sensor_am2301_nano
pio run -t upload
```

Esto usa el env `nano` (bootloader "nuevo"/optiboot, 115200 baudios). Si
la placa no sincroniza durante el upload, prueba con el bootloader
"antiguo" (57600 baudios):

```sh
pio run -e nano_old_bootloader -t upload
```

Monitor serie (imprime cada lectura del AM2301, exitosa o fallida):

```sh
pio device monitor
```

## Protocolo

Solo lectura desde la XIAO: una lectura I2C de 5 bytes devuelve
`[status][temp_x10 int16 BE][hum_x10 uint16 BE]`. `status` es `0` (lectura
fresca), `1` (sirviendo el último valor bueno tras un fallo de lectura del
AM2301) o `2` (nunca hubo una lectura exitosa desde el arranque). El AM2301
se lee en `loop()`, nunca dentro del callback de I2C — el protocolo
bit-banged del sensor necesita interrupciones deshabilitadas varios
milisegundos, incompatible con el contexto de la ISR de `Wire`.

## Consultar el valor cacheado por USB (solo pruebas)

Además del log automático que imprime cada lectura periódica, puedes pedir
el valor cacheado en cualquier momento sin esperar al siguiente ciclo y
sin que la XIAO esté conectada ni haga ninguna transacción I2C: con el
monitor serie abierto (`pio device monitor`), escribe `s` + Enter:

```
Cached: status=OK temp=23.5C hum=45.2%
```

Útil para verificar el cableado del AM2301 de forma aislada, antes de
involucrar al resto de la malla. Es una ayuda de banco, no forma parte del
protocolo I2C que consume la XIAO (que siempre lee los 5 bytes crudos, no
esta salida de texto).

## Ver también

- `README_I2C_SENSOR.md` (raíz del repo) — el comando `TEMP_STATUS` que
  este puente permite responder de extremo a extremo.
- `tools/i2c_actuator_nano/` — el rig equivalente para el actuador
  PCF8574, con el que este puede compartir el mismo bus I2C.
