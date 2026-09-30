# Spec: telemetría vectorial MeshCore (XIAO nRF52)

> **Nota:** el formato de vector descrito acá (`T,H,t;dT,dH,dt;...`, "v0") y
> el encoder/decoder embebidos fueron reemplazados por el formato compacto
> v1 (base64url), y el intervalo de muestreo pasó de 15 a 30 min (48 slots)
> para que las 24 h entren en una sola consulta. Ver `README_TV_TELEMETRY.md`
> y `src/helpers/tv_telemetry.h`. El formato v0 queda en el tag git `tv-v0`.
> El resto del documento (ring, flujo del backend) sigue vigente.

Sep 28, 2026 · @roman

## Resumen

El repetidor MeshCore (XIAO nRF52) guarda 24 h de temperatura y humedad en un ring en RAM y las entrega como vectores de texto cuando el backend consulta con `tv <since>`.

| Decisión | Valor |
| --- | --- |
| Muestreo | cada 15 min (96 slots por día) |
| Retención | 1 día, ring en RAM, \~768 bytes |
| Consulta | `tv <since>`, una respuesta = un paquete, backend cada hora (máx. 24 por día) |
| Formato | enteros separados por comas; anchor absoluto + deltas contra el anchor |
| Tiempo | epoch en minutos, UTC |
| Persistencia | ninguna; un reinicio borra el buffer |
| Firmware | solo enteros: sin floats ni filesystem |

## Formato del vector

Un vector es una lista de registros `T,H,t` separados por `;`: el primero (anchor) lleva valores absolutos y los demás son deltas contra el anchor.

```
vector   = registro *( ";" registro )
registro = T "," H "," t
```

| Campo | Anchor (1.er registro) | Deltas (siguientes) |
| --- | --- | --- |
| `T` | décimas de °C (`330` = 33.0 °C) | Δ décimas de °C, con signo |
| `H` | %RH entero | Δ %RH, con signo |
| `t` | epoch en minutos, UTC | Δ minutos, siempre ≥ 0 |

Reglas:

- Los tres campos son obligatorios en todos los registros; no hay valores por defecto.
- El delta es siempre contra el anchor, nunca contra el registro anterior: el error no se acumula y cada registro se decodifica solo con el anchor.
- Los negativos llevan `-`; los positivos no llevan `+`. Como el anchor es el único absoluto, un anchor negativo (`-52,…`) no es ambiguo.
- El orden de los registros es cronológico ascendente.
- Un vector nunca se parte entre paquetes. Si no caben todos los registros, la respuesta trae los más antiguos que quepan y el cliente vuelve a preguntar (ver Consulta).

Ejemplo, con anchor a las 07:13 UTC:

```
330,65,29840113;-12,3,61;-25,8,76
```

| Registro | Temp (°C) | HR (%) | Hora (UTC) |
| --- | --- | --- | --- |
| anchor | 33.0 | 65 | 07:13 |
| `-12,3,61` | 31.8 | 68 | 08:14 |
| `-25,8,76` | 30.5 | 73 | 08:29 |

Rangos que asume el formato: `T` cabe en `int16` (−3276.8 a 3276.7 °C, de sobra) y `H` en 0–100. El tamaño por registro es 6–9 bytes en el caso típico; el anchor pesa \~15.

## Almacenamiento en el nodo

El nodo guarda las últimas 24 h en un array de 96 slots en RAM (\~768 bytes); no toca la flash, así que no hay desgaste.

- **Slot por hora:** `slot = (epoch_min / 15) % 96`. No hay puntero de cabeza ni contador: cada muestra sobrescribe la de hace 24 h.
- **Validez:** un slot es válido si `now_min - epoch_min < 1440`. Eso descarta slots vacíos y viejos sin limpiarlos.
- **Reloj:** mientras no haya `clock sync`, no se muestrea. Un epoch inválido corrompería el ring.
- **Reinicio o corte de energía:** el buffer se pierde (hasta 1 día de datos). Es un trade-off aceptado en esta versión.
- **Sensor:** la lectura entra como décimas de °C (`int16`) y %RH (`uint8`); la conversión del driver a enteros queda fuera de esta spec.

Si más adelante la persistencia importa, la extensión mínima es un append a una sola página de flash (511 registros de 8 bytes por página de 4 KB ≈ 5 días a 96 muestras/día), sin cambiar formato ni protocolo.

## Consulta: `tv <since>`

El backend lleva el estado (`since`); el nodo no sabe qué se consultó, así que perder una consulta nunca deja al nodo inconsistente.

- **Petición:** `tv <since>`, con `since` = epoch en minutos del último registro que ya tiene el backend; `0` pide todo lo que haya.
- **Respuesta:** un solo paquete con un vector que contiene los slots válidos con `epoch_min > since`, en orden ascendente, hasta llenar el payload.
- **Sin datos nuevos:** la respuesta es el literal `-`, para no enviar payloads de largo cero.

Flujo del backend, cada hora:

1. Enviar `tv <since>` con el `since` guardado.
2. Decodificar el vector y guardar los registros; actualizar `since` con el `epoch_min` del último.
3. Si la respuesta llenó el paquete, repetir desde el paso 1 con el nuevo `since`.
4. Parar cuando la respuesta sea `-` o traiga menos registros que la capacidad del paquete.

| Caso | Qué pasa |
| --- | --- |
| Consulta horaria normal | \~4 registros, 1 ida y vuelta |
| Consultas perdidas | el backend sigue con su último `since`; peor caso 96 registros, \~8 idas y vueltas |
| Más de 24 h sin consultar | se pierden los registros más viejos, que el ring ya sobrescribió |
| Reinicio del nodo | el buffer empieza vacío; el backend recibe `-` hasta la siguiente muestra |
| Reloj sin sincronizar | el nodo no muestrea y responde `-` |

El tamaño máximo del vector (`cap` en el encoder) depende del payload útil de respuesta de la versión de MeshCore usada; hay que confirmarlo contra el firmware antes de fijarlo.

## Encoder C++ (firmware)

Un solo header, sin floats ni dependencias más allá de `snprintf`, con el ring, el muestreo y la codificación. Los puntos de integración con MeshCore (handler del comando y reloj) son ganchos: la firma exacta depende de la versión del repeater y hay que adaptarla.

```cpp
// tv_telemetry.h — ring de 24 h + encoder de vectores "T,H,t;dT,dH,dt;..."
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace tv {

constexpr uint32_t INTERVAL_MIN = 15;
constexpr uint32_t SLOTS = 1440 / INTERVAL_MIN;  // 96

struct Rec {
  uint32_t epoch_min;  // 0 = slot vacío
  int16_t  t;          // décimas de °C
  uint8_t  h;          // %RH
};

static Rec ring[SLOTS];  // en BSS: arranca en cero

inline void put(uint32_t now_min, int16_t t, uint8_t h) {
  ring[(now_min / INTERVAL_MIN) % SLOTS] = { now_min, t, h };
}

inline bool valid(const Rec& r, uint32_t now_min) {
  return r.epoch_min != 0 && r.epoch_min <= now_min && (now_min - r.epoch_min) < 1440;
}

// Gancho del sensor: el driver devuelve enteros.
extern int16_t read_temp_dC();   // décimas de °C
extern uint8_t read_hum_pct();   // %RH

// Llamar desde loop(). No muestrea si el reloj no está sincronizado.
inline void sample_tick(uint32_t now_min, bool clock_ok) {
  static uint32_t last_bucket = 0;
  if (!clock_ok) return;
  uint32_t b = now_min / INTERVAL_MIN;
  if (b == last_bucket) return;
  last_bucket = b;
  put(now_min, read_temp_dC(), read_hum_pct());
}

// Escribe en `out` (terminado en NUL) los registros con epoch_min > since,
// del más viejo al más nuevo, sin partir ningún registro. `cap` incluye el NUL
// y debe ser >= 32. Sin datos nuevos escribe "-". Devuelve el largo escrito.
inline size_t encode(uint32_t now_min, uint32_t since, char* out, size_t cap) {
  if (cap < 32) return 0;
  size_t n = 0;
  bool first = true;
  int16_t t0 = 0;
  uint8_t h0 = 0;
  uint32_t e0 = 0;
  const uint32_t start = (now_min / INTERVAL_MIN + 1) % SLOTS;  // slot más viejo

  for (uint32_t i = 0; i < SLOTS; i++) {
    const Rec& r = ring[(start + i) % SLOTS];
    if (!valid(r, now_min) || r.epoch_min <= since) continue;

    char buf[32];
    int len;
    if (first) {
      len = snprintf(buf, sizeof buf, "%d,%u,%lu",
                     (int)r.t, (unsigned)r.h, (unsigned long)r.epoch_min);
    } else {
      len = snprintf(buf, sizeof buf, ";%d,%d,%lu",
                     (int)r.t - (int)t0, (int)r.h - (int)h0,
                     (unsigned long)(r.epoch_min - e0));
    }
    if (len <= 0 || n + (size_t)len >= cap) break;  // no cabe: el resto va en la próxima consulta

    memcpy(out + n, buf, (size_t)len);
    n += (size_t)len;
    if (first) { t0 = r.t; h0 = r.h; e0 = r.epoch_min; first = false; }
  }

  if (first) { out[0] = '-'; out[1] = '\0'; return 1; }
  out[n] = '\0';
  return n;
}

// Gancho del comando remoto "tv <since>"; `args` apunta a lo que sigue a "tv ".
inline void handle_tv(const char* args, uint32_t now_min, char* reply, size_t cap) {
  uint32_t since = (uint32_t)strtoul(args, nullptr, 10);  // vacío o 0 = todo
  encode(now_min, since, reply, cap);
}

}  // namespace tv
```

Notas de integración:

- `now_min` sale de `epoch_segundos / 60` con el reloj de MeshCore; `clock_ok` es falso hasta el primer `clock sync`.
- El ring se declara `static` en el header, así que debe incluirse desde una sola unidad de compilación.
- `snprintf` con `%d`, `%u` y `%lu` funciona con newlib-nano sin `-u _printf_float`, porque no hay floats.

## Decoder TypeScript

El decoder valida cada registro (tres enteros, humedad en 0–100, tiempo estrictamente creciente) y falla con `TvParseError` ante cualquier desviación. `pullAll` implementa el flujo de consulta y es independiente del transporte: recibe una función `send` que manda el comando y devuelve el texto de respuesta.

```ts
// tv-decoder.ts — decodifica vectores "T,H,t;dT,dH,dt;..." (spec v1)

export interface TvSample {
  epochMin: number; // minutos desde epoch, UTC
  date: Date;       // el mismo instante como Date
  tempC: number;    // °C con 1 decimal
  hum: number;      // %RH
}

export class TvParseError extends Error {}

const INT = /^-?\d+$/;

export function decodeVector(s: string): TvSample[] {
  const v = s.trim();
  if (v === "" || v === "-") return [];

  const recs = v.split(";").map((r, i) => {
    const f = r.split(",");
    if (f.length !== 3 || !f.every((x) => INT.test(x))) {
      throw new TvParseError(`registro ${i} inválido: "${r}"`);
    }
    return f.map(Number) as [number, number, number];
  });

  const [t0, h0, e0] = recs[0]; // anchor
  let prevE = 0;

  return recs.map(([t, h, e], i) => {
    const T = i === 0 ? t : t0 + t;
    const H = i === 0 ? h : h0 + h;
    const E = i === 0 ? e : e0 + e;
    if (H < 0 || H > 100) throw new TvParseError(`registro ${i}: humedad fuera de rango (${H})`);
    if (E <= prevE) throw new TvParseError(`registro ${i}: tiempo no creciente`);
    prevE = E;
    return { epochMin: E, date: new Date(E * 60_000), tempC: T / 10, hum: H };
  });
}

const MAX_REC_LEN = 18; // ";-32768,-100,1440"

// Pide todo lo posterior a `since`, repitiendo mientras el paquete venga lleno.
// `cap` = el mismo tamaño de buffer (con NUL) que usa el encoder del nodo.
export async function pullAll(
  send: (cmd: string) => Promise<string>,
  since: number,
  cap: number,
  maxTrips = 12, // 96 registros caben en ~8 idas y vueltas; margen para reintentos
): Promise<TvSample[]> {
  const out: TvSample[] = [];
  for (let i = 0; i < maxTrips; i++) {
    const raw = await send(`tv ${since}`);
    const batch = decodeVector(raw);
    if (batch.length === 0) break;
    out.push(...batch);
    since = batch[batch.length - 1].epochMin; // próximo `since` = último registro recibido
    if (raw.trim().length + MAX_REC_LEN < cap) break; // cupo de sobra: no hay más datos
  }
  return out;
}
```

Prueba de referencia (el ejemplo de la sección Formato):

```ts
decodeVector("330,65,29840113;-12,3,61;-25,8,76");
// [ { epochMin: 29840113, tempC: 33,   hum: 65 },
//   { epochMin: 29840174, tempC: 31.8, hum: 68 },
//   { epochMin: 29840189, tempC: 30.5, hum: 73 } ]
```

El `since` inicial de cada consulta horaria sale del último `epochMin` guardado en el backend (`0` en la primera). La conversión a hora local se hace en el frontend a partir de `date`.

## Pendientes

Lo que falta para cerrar la spec es el transporte sobre MeshCore y dos datos que hay que confirmar contra el firmware.

- [ ] **Transporte:** la respuesta estándar de telemetría de MeshCore es CayenneLPP, que no tiene tipo string, así que el vector no cabe ahí sin romper el parser de meshcore.js. Opciones: un request type custom con respuesta en texto, o un comando CLI `tv <since>` sobre la sesión con login (encaja con el flujo con y sin password).
- [ ] **Payload máximo:** fijar `cap` (tamaño del buffer de respuesta) según la versión de MeshCore usada; el encoder y `pullAll` deben usar el mismo valor.
- [ ] **Sincronía de reloj:** confirmar cómo expone el repeater el epoch y cuándo se considera sincronizado (`clock_ok`).
- [ ] **Driver del sensor:** implementar `read_temp_dC()` y `read_hum_pct()` para el sensor elegido.
- [ ] **Pruebas:** vector de referencia en ambos lados (el ejemplo de Formato), más casos límite: ring vacío, ring lleno, cruce de medianoche, paquete lleno y consulta con `since` posterior al último registro.
- [ ] **Persistencia (opcional):** append a una página de flash si un reinicio con pérdida de hasta 24 h resulta inaceptable.
