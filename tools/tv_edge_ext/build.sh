#!/bin/sh
# Empaqueta popup.ts (+ decodeVector) en popup.js (npx descarga esbuild si falta).
# Los --define neutralizan el guard de CLI de tv_decoder.ts (process/import.meta no existen en el navegador).
cd "$(dirname "$0")" && npx --yes esbuild popup.ts --bundle --format=iife --outfile=popup.js \
  --define:process.argv='[]' --define:import.meta.url='""'
