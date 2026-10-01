# MeshCore TV Decoder (extensión de Edge)

Popup que decodifica la salida del comando CLI `tv 0` (formato v1) usando
`../tv_decoder/tv_decoder.ts`. Todo local, sin permisos ni red.

## Uso
1. `./build.sh` (genera `popup.js`; ya viene commiteado, solo hace falta si cambia el decoder o `popup.ts`).
2. Edge → `edge://extensions` → activar *Modo desarrollador* → *Cargar desempaquetada* → esta carpeta.
3. Pegar la respuesta de `tv 0` (una línea por página: `tv 0`, `tv <epoch>`, ...) y *Decodificar*. Cada línea válida crea su propia pestaña (`L<n> · <registros>`, máx. 6; navegable con ←/→); las líneas inválidas se listan en un banner sin anular las demás. Si hay ≥2 líneas del mismo sensor, se añade una pestaña **Todas** que las junta ordenadas y sin duplicados (útil al paginar `tv 0` → `tv N`).
   Muestra dos gráficas (T y %RH/hPa, mismo eje X, línea de promedio, hover con tooltip; la línea se corta donde faltan muestras, marcas de mín/máx), la tabla, el `tv <epoch_min>` siguiente permite copiar CSV y apagar las gráficas con el botón *Gráficas: on/off* (con menos de 2 puntos no se grafica y avisa).

## Check
`node tools/tv_edge_ext/check.mjs` compara el decoder TS con `tv_decoder.py` y prueba `niceTicks`/`mean`/`segments` de `chart.ts`.

Los toggles *Gráficas* y *hora local* se recuerdan entre aperturas (`localStorage`). Iconos en `icons/`.
