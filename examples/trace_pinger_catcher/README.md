# Pinger + catcher en una sola tarjeta

XIAO nRF52840 + Wio-SX1262 o Faketec (ProMicro nRF52840 + SX1262). Un solo firmware, dos modos que se cambian por BLE/serie con `MODE`. **Arranca siempre en `catcher`** (no transmite); el modo no se guarda con `SAVE`.

| Modo | Hace | Comandos |
|---|---|---|
| `catcher` | mide la baliza CW, **no transmite** | `HELLO`, `CFG?`, `SET`, `STOP`, `MEAS [n]`, `MEAS?`, `ID`, `MODE` |
| `pinger` | TRACE DIRECT a un repetidor | `HELLO`, `CFG?`, `SET`, `SAVE`, `PING [n]`, `GO`, `STOP`, `MON`, `RF`, `LAST?`, `PONG`, `ID`, `MODE` |

Lo que no es del modo activo responde `!ERR mode`. `MODE catcher|pinger` → `#OK mode=…`; con un ping en vuelo responde `!ERR state busy` (hacer `STOP` antes).

`#HELLO fw=0.7.0 proto=3 role=<modo> radio=SX1262 id=<6 hex>`. Nombre BLE: `PingPong-<8 hex>`.

## Compilar y flashear

```bash
pio run -e Xiao_nrf52_trace_pinger_catcher -t upload      # o ProMicro_trace_pinger_catcher
```

Doble reset rápido si no entra en el bootloader; cable USB de datos.

## Tests (host)

```bash
./examples/trace_pinger_catcher/run_tests.sh
```

`test_mode` cubre el arranque en catcher, el bloqueo de comandos de TX y el cambio de modo.

## Pendiente

`PONG`, `RF`, `LAST?` y `wait=` son del laboratorio de ponger; MeshAzimuth no los usa. Candidatos a borrar tras la prueba de campo.
