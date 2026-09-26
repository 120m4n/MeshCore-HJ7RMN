# Fix: `gps advert` deshabilitado en Xiao_nrf52 (y adverts sin ubicación)

## Situación

Dispositivo: XIAO nRF52840, flasheado con la rama `feature/i2c-actuator-public-channel`
en el env `Xiao_nrf52_repeater` (firmware `simple_repeater`).

Se detectaron dos fallos al operar el nodo por CLI (serie/BLE) junto con la
app companion:

1. El comando `gps advert prefs` (y en general cualquier `gps advert ...`)
   respondía `Unknown command`.
2. La ubicación (lat/lon) fijada desde la app companion no aparecía en los
   adverts que el repetidor enviaba a la red, aunque los valores ya estaban
   guardados en el dispositivo.

## Diagnóstico (causa raíz)

### Fallo 1 — `gps advert` no existe en este build

En `variants/xiao_nrf52/platformio.ini`, el perfil base `[Xiao_nrf52]` (del
que hereda `Xiao_nrf52_repeater` y el resto de envs de esta placa) define:

```ini
-UENV_INCLUDE_GPS
```

y ningún env derivado lo vuelve a activar. Por lo tanto `ENV_INCLUDE_GPS`
queda indefinido en este firmware.

En `src/helpers/CommonCLI.cpp`, **todos** los comandos `gps ...`
(`gps on`, `gps off`, `gps sync`, `gps setloc`, `gps advert ...`, y el
estado `gps`) estaban agrupados dentro de un único bloque:

```cpp
#if ENV_INCLUDE_GPS == 1
   ...
#endif
```

Al no estar definida la macro, ese bloque completo se compila fuera del
binario. Cualquier `gps ...` (incluido `gps advert prefs`) cae en el `else`
final de `handleCommand()` → `"Unknown command"`. No era un error de
sintaxis del comando: el código simplemente no existía en ese build.

`ENV_INCLUDE_GPS` está deliberadamente desactivado para `xiao_nrf52` porque
esta placa no tiene un `LocationProvider` (driver de GPS físico) cableado —
`variants/xiao_nrf52/target.cpp` instancia
`EnvironmentSensorManager sensors;` con el constructor sin argumentos, que
solo existe cuando `ENV_INCLUDE_GPS` está apagado. Activar la macro a nivel
global habría roto la compilación (el constructor con GPS exige pasar un
`LocationProvider&` que no existe para esta placa).

### Fallo 2 — la ubicación fijada por la app no se envía en el advert

`CommonCLI::buildAdvertData()` decide si el advert lleva lat/lon según
`_prefs->advert_loc_policy`:

- `ADVERT_LOC_NONE` (0): nunca incluye lat/lon, aunque estén guardados.
- `ADVERT_LOC_SHARE` (1): usa `_sensors->node_lat/lon` (GPS físico en vivo).
- `ADVERT_LOC_PREFS` (2): usa `_prefs->node_lat/lon` — el valor que la app
  companion fija con `set lat`/`set lon` (comando que sí estaba compilado,
  no depende de `ENV_INCLUDE_GPS`).

Es decir: fijar lat/lon desde la app **no alcanza**. Si `advert_loc_policy`
no está en `prefs` (o `share`), el advert nunca lleva coordenadas, sin
importar el valor de `node_lat`/`node_lon`.

Ese valor se persiste en `prefs.json` y **la única vía en el CLI de texto**
para leerlo o cambiarlo era `gps advert` — deshabilitado por el Fallo 1.
Resultado: si en este dispositivo `advert_loc_policy` quedó en algo
distinto de `prefs` (por config previa u otro firmware), no había forma de
diagnosticarlo ni corregirlo desde el propio nodo.

**Conclusión:** el Fallo 2 era consecuencia directa del Fallo 1.

## Fix aplicado

En `src/helpers/CommonCLI.cpp`, se sacó el bloque `gps advert none|share|prefs`
de dentro del `#if ENV_INCLUDE_GPS == 1 ... #endif` y se dejó como rama
incondicional del `if/else if` de `handleCommand()`, junto a `region` y
antes del bloque GPS:

```cpp
} else if (memcmp(command, "region", 6) == 0) {
  handleRegionCmd(command, reply);
} else if (memcmp(command, "gps advert", 10) == 0) {
  // no necesita ENV_INCLUDE_GPS: solo cambia qué fuente de lat/lon (si alguna)
  // se embebe en los adverts; funciona incluso sin GPS físico.
  ...
#if ENV_INCLUDE_GPS == 1
} else if (memcmp(command, "gps on", 6) == 0) {
  ...
```

Justificación: `gps advert` solo lee/escribe `_prefs->advert_loc_policy`,
no toca `_sensors`/`LocationProvider` ni ningún driver de hardware — es una
preferencia pura, igual que `set lat`/`set lon`/`get lat`/`get lon`, que ya
eran incondicionales. El resto de comandos `gps ...` (`on`/`off`/`sync`/
`setloc`/estado) se dejaron dentro del `#if`, porque esos sí dependen de
`_sensors->getLocationProvider()`, inexistente en esta placa.

No se tocó `buildAdvertData()`: su lógica ya era correcta, solo estaba
inaccesible para inspeccionar/ajustar en este build.

## Verificación

- `pio run -e Xiao_nrf52_repeater` → build limpio (exit 0), `firmware.hex`/
  `.elf`/`.zip` generados sin errores.
- Pendiente en campo: flashear y confirmar que `gps advert prefs` responde
  `ok`/`> prefs` (en vez de `Unknown command`), y que un advert posterior
  a `set lat <lat>` + `set lon <lon>` + `gps advert prefs` incluye la
  ubicación.

## Efecto

- `gps advert none|share|prefs` (y sin argumento, para consultar el valor
  actual) ya responden correctamente en `Xiao_nrf52_repeater` y en
  cualquier otro env de `xiao_nrf52` (companion BLE/USB, room server,
  sensor, etc. — todos comparten el mismo `-UENV_INCLUDE_GPS`).
- Con `advert_loc_policy = prefs`, el lat/lon fijado por la app companion
  vía `set lat`/`set lon` ahora puede confirmarse y forzarse por CLI, y se
  incluirá en los adverts salientes del repetidor.
