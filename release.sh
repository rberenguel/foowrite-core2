#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD="$SCRIPT_DIR/build"
VERSION_H="$SCRIPT_DIR/main/version.h"

# Pull version from version.h
VERSION=$(grep -o '"[^"]*"' "$VERSION_H" | tr -d '"')
OUT="$HOME/Downloads/foowrite-core2-${VERSION}.bin"

# Sanity checks
if [ ! -d "$BUILD" ]; then
    echo "error: build/ not found — run 'idf.py build' first" >&2
    exit 1
fi
for f in "$BUILD/bootloader/bootloader.bin" \
          "$BUILD/partition_table/partition-table.bin" \
          "$BUILD/foowrite_core2.bin"; do
    if [ ! -f "$f" ]; then
        echo "error: missing $f" >&2
        exit 1
    fi
done

esptool.py --chip esp32 merge_bin \
    --flash_mode dio --flash_freq 80m --flash_size 16MB \
    -o "$OUT" \
    0x1000  "$BUILD/bootloader/bootloader.bin" \
    0x8000  "$BUILD/partition_table/partition-table.bin" \
    0x10000 "$BUILD/foowrite_core2.bin"

echo "Created: $OUT"
echo ""
echo "Flash with:"
echo "  esptool.py --chip esp32 --port /dev/cu.usbserial-XXXX --baud 921600 write_flash 0x0 $(basename "$OUT")"
