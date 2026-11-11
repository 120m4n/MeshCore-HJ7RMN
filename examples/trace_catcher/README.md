# Catcher (medidor de baliza CW)

XIAO nRF52840 + Wio-SX1262, o Faketec (ProMicro nRF52840 + SX1262). **No transmite nunca**: lee el RSSI instantáneo (0,5 dB) en la frecuencia fijada y devuelve la estadística filtrada de ráfagas ajenas. Lo usa la tarjeta **Baliza** de MeshAzimuth (se anuncia por BLE como `PingPong-CA-<8 hex>`).

## Compilar y flashear

```bash
pio run -e Xiao_nrf52_trace_catcher -t upload       # o ProMicro_trace_catcher
pio device monitor -b 115200
```

Flashear reemplaza el firmware de la placa. Si no encuentra el dispositivo: doble reset rápido (el LED pulsa lento = bootloader), cable USB **de datos**, y `--upload-port /dev/cu.usbmodemXXXX` si hay varios puertos.

Preset inicial: 915.800 MHz, BW 250, SF10, CR 5. La app fija la frecuencia de la baliza al conectar.

## Protocolo (115200 8N1 por USB, o NUS por BLE; una línea por comando)

```
HELLO                #HELLO fw=0.6.0 proto=2 role=catcher radio=SX1262 id=<6 hex>
CFG?                 #CFG freq=915.800 bw=250 sf=10 cr=5
SET freq|bw|sf|cr=…  #OK k=v   (!ERR unknown|range|syntax)
MEAS [n]             M,<n>,<kept>,<mean>,<sd>,<mediana>,<min>,<max>   (n = 16..256, por defecto 64)
```

La app exige `proto=2` y `fw ≥ 0.5.1`. Ya no existen `PING`, `GO`, `STOP`, `MON`, `SAVE` ni `PONG`; el tráfico TRACE vive en `examples/trace_pinger`.

## Tests (host, sin hardware)

```bash
./examples/trace_catcher/run_tests.sh      # config, LineCodec, estadística RSSI, FSM del catcher
```

Los fixtures de `tests/fixtures/lines/` son los mismos que usa el parser Swift de MeshAzimuth.
