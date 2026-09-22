#!/bin/sh
# Copy the engine module into the benchmark sketch and compile it.
# Usage: sh tools/umax_bench.sh [extra arduino-cli compile args, e.g. -u -p COM8]
set -e
cd "$(dirname "$0")/.."
mkdir -p umaxbench/src/umax
cp src/umax/umax.h src/umax/umax.cpp src/umax/umax_keys.h umaxbench/src/umax/
"C:/Users/theco/AppData/Local/Programs/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe" \
  --config-file "C:/Users/theco/.arduinoIDE/arduino-cli.yaml" \
  compile --fqbn arduino:avr:mega umaxbench "$@"
