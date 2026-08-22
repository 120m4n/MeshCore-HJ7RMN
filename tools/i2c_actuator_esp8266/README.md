# i2c_actuator_esp8266

Test double para un expansor I2C **PCF8574**, corriendo en una tarjeta
**ESP8266-12E NodeMCU** real. Permite probar de extremo a extremo la función
"actuador I2C sobre comando en canal público" del firmware companion de
MeshCore (ver `README_I2C.md` en la raíz del repo) sin necesidad de tener un
PCF8574 físico ni el actuador que gobierna: la NodeMCU actúa como el esclavo
I2C con el que habla la XIAO nRF52840, y refleja el estado de los bits en
pines digitales que puedes observar con LEDs, un multímetro o un
osciloscopio.

Es un derivado **independiente** de `../i2c_actuator_nano` (el mismo test
double corriendo en un Arduino Nano). Ambos son proyectos PlatformIO
separados y autocontenidos: ninguno depende del otro, y modificar uno no
afecta al otro.

Es un proyecto PlatformIO **independiente** del firmware principal de
MeshCore (no forma parte de `platformio.ini` de la raíz ni de
`variants/*/platformio.ini`) — se compila y flashea desde esta carpeta.

## Diferencia clave frente al Nano: solo 7 de 8 bits tienen pin físico

El Arduino Nano tiene 8 pines digitales libres para reflejar los 8 bits del
PCF8574. La NodeMCU, en cambio, solo tiene **7** GPIO libres una vez
reservados los pines de I2C (`D1`/`D2`) y del UART USB (`D9`/`D10`, cableado
de fábrica al chip serie-USB, no se puede reasignar). Por eso el bit 7 solo se
rastrea en software (aparece en el log serie y en la lectura I2C cruda,
así que `PIN_STATUS` / `readState()` del companion siguen funcionando igual),
pero no tiene un pin físico que puedas medir con un multímetro.



## Cableado

Pines I2C (mapeo por defecto de `Wire` en ESP8266):

| Señal | Pin NodeMCU | Conecta a |
|-------|-------------|-----------|
| SDA   | D2 (GPIO4)  | XIAO `D7` (`PIN_WIRE_SDA`) |
| SCL   | D1 (GPIO5)  | XIAO `D6` (`PIN_WIRE_SCL`) |
| GND   | GND         | GND común con la XIAO |

Agrega pull-ups de 4.7kΩ en SDA/SCL a **3.3V** (no 5V — el ESP8266 no es
tolerante a 5V) si tu cableado no las trae ya (un módulo PCF8574 real
normalmente las incluye).

Pines de salida, bits 0-6 del PCF8574 emulado:

| Bit PCF8574 | Pin NodeMCU | GPIO |
|-------------|-------------|------|
| 0 | D0 | 16 |
| 1 | D3 | 0  |
| 2 | D4 | 2  |
| 3 | D5 | 14 |
| 4 | D6 | 12 |
| 5 | D7 | 13 |
| 6 | D8 | 15 |
| 7 | — (solo software) | — |

El firmware de MeshCore puede controlar cualquiera de los 8 bits de forma
independiente con el comando `PIN<n>_ON` / `PIN<n>_OFF` (`<n>` de `0` a `7`).
Para `n` de `0` a `6` verás el pin correspondiente de la tabla cambiar; para
`n = 7` solo lo verás en el monitor serie (línea `I2C write: 0x..` y el
detalle de qué bit cambió), no hay pin físico asociado.

Notas sobre los pines de salida:

- `D3`/`D4`/`D8` (GPIO0/GPIO2/GPIO15) son pines de "bootstrap" del ESP8266:
  solo importan durante el reset/arranque; una vez corriendo el firmware
  funcionan como GPIO normales, igual que en cualquier proyecto de relés
  sobre NodeMCU.
- `D4` suele estar cableado también al LED azul integrado (activo en LOW)
  en la mayoría de las NodeMCU, así que el bit 2 también hará parpadear ese
  LED.

## Relés activo-bajo (active-low)

Muchos módulos de relé comerciales activan el relé con el pin en **LOW**
(no en HIGH). Para que este test double coincida con esa polaridad,
descomenta en `platformio.ini`:

```ini
-D ACTIVE_LOW_RELAYS=1
```

Esto solo invierte el nivel físico que se escribe en cada pin de salida —
el byte del protocolo I2C (lo que ve `PCF8574Actuator`/`PIN_STATUS` del
companion) no cambia. También mantiene coherente el estado "todo apagado"
del arranque (`last_state = 0x00`): con el flag activo, ese `0x00` se
traduce a pines en HIGH físicamente (relé apagado), no en LOW. El bit 7
(software-only) no se ve afectado al no tener pin físico.

## Compilar y flashear

```sh
cd tools/i2c_actuator_esp8266
pio run -t upload
```

Esto usa el env `nodemcuv2` (ESP8266-12E, 115200 baudios de upload).

Monitor serie (imprime cada escritura I2C recibida, ej. `I2C write: 0x01`,
más una línea por cada bit cuyo estado cambió respecto a la escritura
anterior, ej. `  pin 0 -> HIGH`, útil para verificar de un vistazo un
comando que solo debería tocar un bit):

```sh
pio device monitor
```

## Simular pérdida de estado del chip

El NodeMCU responde a lecturas I2C (no solo escrituras) devolviendo su
`last_state`, así que también sirve para probar `PCF8574Actuator::readState()`
del firmware companion — la lectura real que usa el comando `PIN_STATUS`
para detectar cuando el caché del companion ya no coincide con el chip real
(ver `README_I2C.md`).

Para simular ese escenario (ej. el PCF8574 pierde alimentación en su propio
riel, sin contingencia, mientras la XIAO sigue corriendo con el caché
desactualizado), escribe `r` + Enter en el monitor serie de la NodeMCU:

```
Simulated reset: chip lost power, all pins HIGH (0xFF)
```

Esto fuerza `last_state` a `0xFF` (estado de power-on del PCF8574) **sin**
pasar por la escritura I2C normal — el companion no se entera hasta que
consulta `PIN_STATUS` y lo detecta como drift.

## Ver también

- `../i2c_actuator_nano/README.md` — la misma herramienta en Arduino Nano
  (8 bits con pin físico, en vez de 7+1).
- `README_I2C.md` (raíz del repo) — la funcionalidad de actuador que este
  tool permite probar de extremo a extremo, y por qué el actuador solo
  reacciona a canales "hashtag".
