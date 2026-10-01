# MeshCore TV Decoder (extensión de Edge)

Popup que decodifica la salida del comando CLI `tv 0` (formato v1) usando
`../tv_decoder/tv_decoder.ts`. Todo local, sin permisos ni red.

## Uso
1. `./build.sh` (genera `popup.js`; ya viene commiteado, solo hace falta si cambia el decoder o `popup.ts`).
2. Edge → `edge://extensions` → activar *Modo desarrollador* → *Cargar desempaquetada* → esta carpeta.
3. Pegar la respuesta de `tv 0` (una línea por página: `tv 0`, `tv <epoch>`, ...) y *Decodificar*.
   Muestra la tabla, el `tv <epoch_min>` siguiente y permite copiar CSV.

## Check
`node tools/tv_edge_ext/check.mjs` compara el decoder TS con `tv_decoder.py`.
