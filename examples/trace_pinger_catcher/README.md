# Pinger + catcher en una sola tarjeta

XIAO nRF52840 + Wio-SX1262 o Faketec (ProMicro nRF52840 + SX1262). Un solo firmware, dos modos que la app cambia con `MODE`. **Arranca siempre en `catcher`** (no transmite); el modo no se guarda y la configuración tampoco: la app fija hash, frecuencia y modo en cada conexión.

| Modo | Hace | Comandos |
|---|---|---|
| `catcher` | solo escucha y mide dBm (RSSI instantáneo, 0,5 dB) | `HELLO`, `CFG?`, `SET freq\|bw\|sf\|cr`, `STOP`, `MEAS [n]`, `ID`, `MODE` |
| `pinger` | TRACE DIRECT a un repetidor (12 o 15 bytes, nada más sale por la antena) | `HELLO`, `CFG?`, `SET`, `PING [n]`, `GO`, `STOP`, `ID`, `MODE` |

Lo que no es del modo activo responde `!ERR mode`; lo que ya no existe (`SAVE`, `MON`, `RF`, `PONG`, `LAST?`, `wait=`, `MEAS?`), `!ERR unknown`. `MODE catcher|pinger` responde `#OK mode=…`; con un ping en vuelo, `!ERR state busy` (hacer `STOP` antes).

`#HELLO fw=0.8.0 proto=3 role=<modo> radio=SX1262 id=<6 hex>`. Nombre BLE: `MeshAz-<8 hex>` (solo este firmware lo usa, las placas anteriores no aparecen en la app).

`MEAS [n]` → `M,<n>,<kept>,<mean>,<sd>,<mediana>,<min>,<max>` (n = 16..256, por defecto 64).

## Compilar y flashear

```bash
pio run -e Xiao_nrf52_trace_pinger_catcher -t upload      # o ProMicro_trace_pinger_catcher
```

Doble reset rápido si no entra en el bootloader; cable USB de datos.

## Tests (host)

```bash
./examples/trace_pinger_catcher/run_tests.sh
```
