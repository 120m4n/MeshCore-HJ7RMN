# Telemetría vectorial `tv` (XIAO nRF52840 + Wio-SX1262, repeater)

Variante del firmware **repeater** de MeshCore que guarda 24 h de
temperatura (y humedad o presión, según el sensor) en un ring en RAM, una
muestra cada **30 min** (48 slots), y las entrega como vectores de texto
compactos (formato **v1**, base64url, ver abajo) con el comando
`tv <since>`: el día completo entra en una sola consulta. Pensado para un
backend que hace polling periódico (cada hora, per la spec), no para
lectura instantánea por la malla.

- **Target de hardware**: Seeed XIAO nRF52840 + módulo LoRa Wio-SX1262
  (entorno PlatformIO `variants/xiao_nrf52`), mismo bus I2C (`D6`/`D7`) que
  las demás variantes I2C de este directorio.
- **Firmware**: `Xiao_nrf52_repeater_tv` — extiende `Xiao_nrf52_repeater`
  agregando `-D WITH_TV_TELEMETRY=1`. El repeater plano
  (`Xiao_nrf52_repeater`) no incluye nada de esto: todo el código nuevo
  está detrás de ese flag y no toca ningún otro build.
- **Spec original**: `docs/Spec telemetría vectorial MeshCore (XIAO nRF52).md`
  — decisiones de diseño del ring/muestreo. Su formato de vector
  (`T,H,t;dT,dH,dt;...`, "v0") ya fue reemplazado por el v1 descrito acá.
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

El header del vector (primer carácter) indica cuál de los dos es, así que
los decoders muestran %RH o hPa automáticamente. `tv sensor` queda solo como
diagnóstico.

## Comandos CLI

| Comando | Respuesta | Notas |
| --- | --- | --- |
| `tv sensor` | `BME280`, `BMP280` o `none` | Diagnóstico: qué detectó el firmware al boot. |
| `tv <since>` | Un vector v1 (ej. `FAFBx1KKIuA3CCCBDC.C!BFA8`) o `-` | `since=0` pide todo lo que haya; ver paginación abajo. |
| `clock` | Fecha/hora UTC legible (`HH:MM - D/M/AAAA UTC`) | **No** da epoch en minutos crudo — ver la receta de uso más abajo. |

El reply de `tv` está limitado a `TV_REPLY_CAP = 150` bytes (el buffer de
reply real de `MyMesh::onPeerDataRecv` es de 161 bytes; se deja margen).

## Formato del vector (v1)

Alfabeto base64url (`A–Z a–z 0–9 - _`, 6 bits por carácter), sin
separadores. Pensado para enlaces débiles: menos bytes = menos airtime y
menos round trips.

```
V NN EEEEE TT HH | dT dH | .k | !TTHH | ...
```

| Token | Chars | Contenido |
| --- | --- | --- |
| `V` | 1 | `(versión 1 << 2) \| sensor`: `F` = BME280, `G` = BMP280 |
| `NN` | 2 | cantidad de registros del vector (el decoder detecta truncamiento) |
| `EEEEE` | 5 | `epoch_min` exacto del anchor (primer registro) |
| `TT` | 2 | temperatura del anchor en décimas de °C, zigzag |
| `HH` | 2 | campo `H` del anchor (0-255) |
| `dT dH` | 2 | registro en el slot siguiente: 1 char zigzag c/u (−32..+31), delta vs el registro **anterior** |
| `.k` | 2 | saltar `k` slots vacíos (1-63; se repite si el hueco es mayor) |
| `!TTHH` | 5 | registro en el slot siguiente con T/H absolutos (el delta no entraba en 1 char) |

- **Signos**: zigzag (`0,−1,1,−2,2… → 0,1,2,3,4…`), 1 bit por valor.
- **Tiempo implícito**: después del anchor, cada registro es el inicio de su
  bucket de 30 min (`bucket × 30`, cae en :00 o :30). El firmware muestrea
  al entrar al bucket, así que en régimen normal coincide con el minuto
  real; si el `loop()` se atrasó, el timestamp decodificado puede
  adelantarse hasta 29 min.
- **`since` se compara por bucket** (`epoch_min / 30`), así que el backend
  puede mandar tal cual el `epoch_min` decodificado del último registro.
- Sin datos: `-`.

## Paginación: qué pasa cuando el ring está lleno

El reply está limitado a 150 bytes (149 útiles + NUL): header de 12 chars
+ 2 por registro → **hasta 69 registros por página** con deltas chicos. El
intervalo de 30 min se eligió para que el día completo (48 registros = 106
bytes) salga en **1 sola consulta**, con 43 bytes de margen para ~14 saltos
grandes (`!`, +3 c/u) o huecos (`.k`, +2 c/u). Con 20 min (72 registros) ya
no entraba; con 24 min entraba con solo 19 bytes de margen y timestamps
desalineados de la hora.

No es una garantía: un día con más saltos que ese margen sale en 2
páginas. `encode()` corta limpio — nunca parte un registro — y la
paginación sigue funcionando igual.

**Ejemplo concreto**, lecturas estables (`T=280` → 28.0 °C, `H=155`),
anchor en epoch `29840010`, 18 registros (9 h):

```
FASBx1KKIwCbAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
```

46 bytes (el formato viejo necesitaba 146 para lo mismo). `S` = 18
registros, `AA` = delta 0,0.

Si hubo más de una página: `tv <epoch_min del último registro decodificado>`. El
primer registro que quedó afuera pasa a ser el nuevo anchor. El backend para
cuando recibe `-` o una página que no llenó el paquete.

Si el backend hace polling una vez al día en vez de cada hora (recomendado
por la spec), corre el riesgo de quedar justo en el borde de perder las
muestras más viejas: el ring solo retiene 24 h, así que cualquier atraso
extra ya las sobrescribió.

## Receta de uso manual (CLI serial/mesh)

1. **`clock`** — confirma que el reloj interno del nodo esté en un valor
   razonable (fecha/hora UTC legible). Es solo una referencia humana: no
   devuelve epoch en minutos, así que no sirve para calcular `since` a
   mano.
2. **`tv 0`** — primer paquete. Decodificalo (`python3
   tools/tv_decoder/tv_decoder.py '<vector>'`): el `epoch_min` del último
   registro es la fuente de `since` para la siguiente llamada, no algo que
   se derive de `clock`.
3. **`tv <epoch_min del último registro decodificado>`** — siguiente
   página, repitiendo el paso 3 hasta que la respuesta sea `-`.

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
  decoders v1 standalone (sin dependencias externas), mismo CLI:
  `python3 tv_decoder.py '<vector>'` / `node tv_decoder.ts '<vector>'`.
  Leen el sensor del header y muestran %RH o hPa (offset 800, el de
  `tv_sensor.cpp`) sin flags.
- `tools/tv_decoder/test_tv_roundtrip.py` — compila el encoder real en host
  (`g++`), pagina un día completo y casos borde (huecos, re-anchor,
  negativos, truncamiento) y los decodifica: `python3
  tools/tv_decoder/test_tv_roundtrip.py`.
- **Vectores del formato viejo v0** (`T,H,t;dT,dH,dt;...`, firmware previo
  a este cambio): los decoders v1 los rechazan y apuntan al decoder
  anterior en git:
  `git show tv-v0:tools/tv_decoder/tv_decoder.py > tv_decoder_v0.py`
  (o `.ts`).
- `README_I2C_TELEMETRY.md` — telemetría formal (`GetTelemetry`,
  CayenneLPP) para firmwares `companion_radio`; mecanismo distinto, no
  relacionado con `tv`.
