# Telemetría vectorial `tv` (XIAO nRF52840 + Wio-SX1262, repeater)

Variante del firmware **repeater** de MeshCore que guarda 24 h de
temperatura (y humedad o presión, según el sensor) en un ring en RAM y las
entrega como vectores de texto compactos cuando se consulta con el comando
`tv <since>`. Pensado para un backend que hace polling periódico (cada
hora, per la spec), no para lectura instantánea por la malla.

- **Target de hardware**: Seeed XIAO nRF52840 + módulo LoRa Wio-SX1262
  (entorno PlatformIO `variants/xiao_nrf52`), mismo bus I2C (`D6`/`D7`) que
  las demás variantes I2C de este directorio.
- **Firmware**: `Xiao_nrf52_repeater_tv` — extiende `Xiao_nrf52_repeater`
  agregando `-D WITH_TV_TELEMETRY=1`. El repeater plano
  (`Xiao_nrf52_repeater`) no incluye nada de esto: todo el código nuevo
  está detrás de ese flag y no toca ningún otro build.
- **Spec completa**: `docs/Spec telemetría vectorial MeshCore (XIAO nRF52).md`
  — formato del vector, decisiones de diseño, decoder TypeScript de
  referencia.
- **Código**: `src/helpers/tv_telemetry.h` (ring + encoder, sin floats, sin
  filesystem), `src/helpers/tv_sensor.h/.cpp` (detección de sensor +
  lectura entera), `examples/simple_repeater/MyMesh.cpp` (comandos CLI +
  gancho en `loop()`).

## Sensor: BME280 o BMP280, detectado una sola vez al boot

Al primer `loop()`, el firmware prueba `0x76`/`0x77` en el bus I2C: primero
un **BME280** (chip ID `0x60`), si no responde prueba un **BMP280** (chip
ID `0x58`). El resultado se cachea para siempre — si no se encuentra
ninguno de los dos, **el muestreo nunca arranca** (ni siquiera si conectás
el sensor después sin reiniciar el nodo).

El campo `H` del vector cambia de significado según qué se detectó:

| Sensor detectado | Campo `H` del vector |
| --- | --- |
| BME280 | %RH real (0-100) |
| BMP280 (no mide humedad) | presión atmosférica escalada: `hPa - 800`, saturado a `[0,255]` (cubre ~800-1055 hPa) |
| Ninguno | el muestreo no arranca; `tv <since>` siempre responde `-` |

El vector **no lleva un discriminador propio** para esto — hay que
consultar `tv sensor` para saber cómo interpretar `H` antes de decodificar.

## Comandos CLI

| Comando | Respuesta | Notas |
| --- | --- | --- |
| `tv sensor` | `BME280`, `BMP280` o `none` | Diagnóstico: qué detectó el firmware al boot. |
| `tv <since>` | Un vector (`T,H,t;dT,dH,dt;...`) o `-` | `since=0` pide todo lo que haya; ver paginación abajo. |
| `clock` | Fecha/hora UTC legible (`HH:MM - D/M/AAAA UTC`) | **No** da epoch en minutos crudo — ver la receta de uso más abajo. |

El reply de `tv` está limitado a `TV_REPLY_CAP = 150` bytes (el buffer de
reply real de `MyMesh::onPeerDataRecv` es de 161 bytes; se deja margen).

## Paginación: qué pasa cuando el ring está lleno

96 slots (uno cada 15 min = 24 h) no caben en un solo paquete de 150 bytes.
`encode()` corta limpio — nunca parte un registro — y el backend tiene que
repetir la consulta actualizando `since` hasta vaciar el ring, tal como
describe la spec.

**Ejemplo concreto**, con lecturas estables todo el día (`dT=0`, `dH=0`,
como en una habitación sin grandes cambios), anchor en epoch `29840000`,
`T=280` (28.0 °C), campo `H`/presión `=155`:

**1ª llamada — `tv 0`:**

```
280,155,29840000;0,0,15;0,0,30;0,0,45;0,0,60;0,0,75;0,0,90;0,0,105;0,0,120;0,0,135;0,0,150;0,0,165;0,0,180;0,0,195;0,0,210;0,0,225;0,0,240;0,0,255
```

Anchor + 17 deltas = **18 registros**, 146 bytes. El siguiente registro
(`dt=270`) no entra (146+8 ≥ 150) → se corta ahí. Cubre desde el anchor
hasta 4 h 15 min después.

**2ª llamada — `tv 29840255`** (`since` = epoch del último registro
recibido = `29840000+255`): el registro que quedó afuera (`dt=270`
original) pasa a ser el **nuevo anchor**, con valores absolutos otra vez —
los deltas se resetean a chico y vuelven a caber ~17-18 registros. Así
sucesivamente.

Por qué cada respuesta rinde ~18 registros y no menos: los deltas son
siempre contra el anchor *de esa respuesta*, nunca contra un anchor fijo
del día — por eso `dt` vuelve a arrancar en 2 dígitos en cada llamada, en
vez de acumular hasta 4 dígitos (`dt` podría llegar a `1425` si el anchor
fuera fijo para todo el día).

**Round trips para vaciar el ring completo:** `96 / ~18 ≈ 6 idas y
vueltas` — el backend para cuando la respuesta es `-` o trae menos
registros de los que el paquete podría contener (ver "Flujo del backend"
en la spec).

Si el backend hace polling una vez al día en vez de cada hora (recomendado
por la spec), corre el riesgo de quedar justo en el borde de perder las
muestras más viejas: el ring solo retiene 24 h, así que cualquier atraso
extra ya las sobrescribió.

## Receta de uso manual (CLI serial/mesh)

1. **`clock`** — confirma que el reloj interno del nodo esté en un valor
   razonable (fecha/hora UTC legible). Es solo una referencia humana: no
   devuelve epoch en minutos, así que no sirve para calcular `since` a
   mano.
2. **`tv 0`** — primer paquete. El anchor (primer registro) trae el
   `epoch_min` real en su tercer campo — es la fuente real de `since` para
   la siguiente llamada, no algo que se derive de `clock`.
3. **`tv <epoch_min del último registro recibido>`** — siguiente página,
   repitiendo el paso 3 hasta que la respuesta sea `-` o venga más corta
   que la anterior (ring vaciado).

El muestreo arranca con el reloj interno de la XIAO esté o no sincronizado
con `clock sync` — no depende de eso (ver spec, sección "Desviaciones").

Para calcular un `epoch_min` para pruebas manuales sin depender de parsear
el texto de `clock` (frágil: no tiene cero-padding), usá el reloj de tu
propia máquina — es la misma base de tiempo con la que sincronizás el nodo:

```sh
echo $(( $(date -u +%s) / 60 ))
```

## Comportamiento conocido: `clock sync` invalida el historial previo

Un `clock sync` **válido** (`sender_timestamp > curr`, el reloj salta hacia
adelante) descarta de hecho todo lo muestreado antes del salto — no porque
el ring se borre, sino porque `valid()` exige `now_min - epoch_min < 1440`
(24 h), y un salto grande (típico: del default de `VolatileRTCClock`, 15
mayo 2024, al real de hoy) hace que esa resta sea enorme para *todos* los
registros pre-sync de una sola vez.

Paso a paso:

1. Antes de sincronizar, el reloj interno arranca en un valor fijo por
   defecto (15 may 2024). Como el muestreo **no depende de `clock_ok`**
   (ver más arriba), `sample_tick()` ya viene grabando con ese epoch
   "falso" desde el boot.
2. `clock sync <timestamp>` salta el reloj de golpe (`VolatileRTCClock` no
   interpola, reemplaza `base_time` directo).
3. Los bytes del ring **no se tocan** por el sync — pero todos los
   registros pre-sync fallan `valid()` contra el nuevo `now_min`.
   `tv 0` responde `-` justo después del salto, aunque el ring tenga bytes
   escritos.
4. Se recupera solo, casi al instante: el contador de "bucket" de
   `sample_tick()` es independiente del RTC, así que el primer `loop()`
   después del sync ve un bucket distinto al de antes y dispara una
   muestra nueva de inmediato, con el epoch ya sincronizado como anchor.

**Implicación práctica:** sincronizá el reloj (`clock sync`) apenas
bootea el nodo, antes de que el backend empiece a confiar en el
historial de `tv` — cualquier dato juntado antes de ese sync queda
inalcanzable en cuanto el sync se aplica.

## Compilar y flashear

```sh
pio run -e Xiao_nrf52_repeater_tv -t upload
```

## Ver también

- `docs/Spec telemetría vectorial MeshCore (XIAO nRF52).md` — spec
  completa: formato del vector, decisiones de diseño, decoder TypeScript
  de referencia.
- `tools/tv_decoder/tv_decoder.py` y `tools/tv_decoder/tv_decoder.ts` —
  decoders standalone (sin dependencias externas) para el backend, cada
  uno con un self-check ejecutable (`python3 tv_decoder.py` / `node
  tv_decoder.ts`).
- `README_I2C_TELEMETRY.md` — telemetría formal (`GetTelemetry`,
  CayenneLPP) para firmwares `companion_radio`; mecanismo distinto, no
  relacionado con `tv`.
