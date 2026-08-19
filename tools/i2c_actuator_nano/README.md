# i2c_actuator_nano

Test double para un expansor I2C **PCF8574**, corriendo en un Arduino Nano
real. Permite probar de extremo a extremo la función "actuador I2C sobre
comando en canal público" del firmware companion de MeshCore (ver
`README_I2C.md` en la raíz del repo) sin necesidad de tener un PCF8574 físico
ni el actuador que gobierna: el Nano actúa como el esclavo I2C con el que
habla la XIAO nRF52840, y refleja el estado de 8 bits en 8 pines digitales
que puedes observar con LEDs, un multímetro o un osciloscopio.

Es un proyecto PlatformIO **independiente** del firmware principal de
MeshCore (no forma parte de `platformio.ini` de la raíz ni de
`variants/*/platformio.ini`) — se compila y flashea desde esta carpeta.

## Cableado

Pines I2C hardware del Arduino Nano:

| Señal | Pin Nano | Conecta a |
|-------|----------|-----------|
| SDA   | A4       | XIAO `D7` (`PIN_WIRE_SDA`) |
| SCL   | A5       | XIAO `D6` (`PIN_WIRE_SCL`) |
| GND   | GND      | GND común con la XIAO |

Agrega pull-ups de 4.7kΩ en SDA/SCL a 3.3V/5V si tu cableado no las trae ya
(un módulo PCF8574 real normalmente las incluye).

Los pines `D2`..`D9` del Nano reflejan los bits 0..7 del PCF8574 emulado.
El firmware de MeshCore puede controlar cualquiera de los 8 de forma
independiente: el comando `PIN<n>_ON` / `PIN<n>_OFF` (con `<n>` de `0` a
`7`) selecciona el bit, así que `PIN0_ON` mueve `D2`, `PIN3_ON` mueve `D5`,
etc. (`Dx` = `D2 + n`).

## Compilar y flashear

```sh
cd tools/i2c_actuator_nano
pio run -t upload
```

Esto usa el env `nano` (bootloader "nuevo"/optiboot, 115200 baudios), el más
común en clones vendidos desde ~2018. Si la placa no sincroniza durante el
upload, prueba con el bootloader "antiguo" (57600 baudios):

```sh
pio run -e nano_old_bootloader -t upload
```

Monitor serie (imprime cada escritura I2C recibida, ej. `I2C write: 0x01`,
más una línea por cada pin cuyo estado cambió respecto a la escritura
anterior, ej. `  pin 0 -> HIGH`, útil para verificar de un vistazo un
comando que solo debería tocar un pin):

```sh
pio device monitor
```

## Simular pérdida de estado del chip

El Nano responde a lecturas I2C (no solo escrituras) devolviendo su
`last_state`, así que también sirve para probar `PCF8574Actuator::readState()`
del firmware companion — la lectura real que usa el comando `PIN_STATUS`
para detectar cuando el caché del companion ya no coincide con el chip real
(ver `README_I2C.md`).

Para simular ese escenario (ej. el PCF8574 pierde alimentación en su propio
riel, sin contingencia, mientras la XIAO sigue corriendo con el caché
desactualizado), escribe `r` + Enter en el monitor serie del Nano:

```
Simulated reset: chip lost power, all pins HIGH (0xFF)
```

Esto fuerza `last_state` a `0xFF` (estado de power-on del PCF8574) **sin**
pasar por la escritura I2C normal — el companion no se entera hasta que
consulta `PIN_STATUS` y lo detecta como drift.

## Ver también

- `README_I2C.md` (raíz del repo) — la funcionalidad de actuador que este
  tool permite probar de extremo a extremo, y por qué el actuador solo
  reacciona a canales "hashtag".
