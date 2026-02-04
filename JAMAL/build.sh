#!/usr/bin/env bash
set -euo pipefail

mkdir -p build

clang -std=c11 -fobjc-arc \
  -framework Cocoa \
  -framework QuartzCore \
  -framework UniformTypeIdentifiers \
  -framework AudioToolbox \
  -framework CoreAudio \
  -framework CoreFoundation \
  -o build/livecode \
  src/main.m \
  src/meter_view.m \
  src/memory_map_view.m \
  src/audio_engine.c \
  src/dsl.c

echo "Built build/livecode"
