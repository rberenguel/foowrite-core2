#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VERSION_H="$SCRIPT_DIR/main/version.h"

# Pull version from version.h
VERSION=$(grep -o '"[^"]*"' "$VERSION_H" | tr -d '"')

# Board selection
BOARD="${1:-all}"
case "$BOARD" in
    core2|waveshare349|all) ;;
    *)
        echo "Usage: $0 [core2|waveshare349|all]"
        echo "  core2        – build/package only the Core2 variant"
        echo "  waveshare349 – build/package only the Waveshare variant"
        echo "  all          – build/package both variants (default)"
        exit 1
        ;;
esac

# Build + package a single variant.
#   $1 = board name (core2 | waveshare349)
#   $2 = esptool --chip value (esp32 | esp32s3)
package_variant() {
    local board=$1
    local chip=$2
    local build_dir="$SCRIPT_DIR/build${board:+_${board}}"
    [ "$board" = "core2" ] && build_dir="$SCRIPT_DIR/build"

    local bin_core="$build_dir/foowrite_core2.bin"
    local bin_boot="$build_dir/bootloader/bootloader.bin"
    local bin_part="$build_dir/partition_table/partition-table.bin"

    # If the build dir exists, verify it was built for the right chip.
    # Wrong target → wipe and rebuild.  Right target but missing binary
    # → build.  Right target with binary → reuse.
    local need_build=false
    if [ -d "$build_dir" ] && [ -f "$build_dir/project_description.json" ]; then
        local cached_target
        cached_target=$(python3 -c "import sys,json; d=json.load(open('$build_dir/project_description.json')); print(d.get('target',''))" 2>/dev/null || true)
        if [ "$cached_target" != "$chip" ]; then
            echo "Target mismatch in $build_dir ($cached_target != $chip), cleaning..."
            rm -rf "$build_dir"
            need_build=true
        elif [ ! -f "$bin_core" ]; then
            need_build=true
        fi
    else
        need_build=true
    fi

    if [ "$need_build" = false ]; then
        echo "Using existing build: $build_dir"
    else
        echo ""
        echo "=== Building $board variant ==="

        # ESP-IDF stores the target inside sdkconfig.  We must remove it
        # so CMake generates a fresh one for $chip.  Save the developer's
        # copy first and restore it after the build.
        local sdkconfig_backup=""
        if [ -f "$SCRIPT_DIR/sdkconfig" ]; then
            sdkconfig_backup=$(mktemp)
            cp "$SCRIPT_DIR/sdkconfig" "$sdkconfig_backup"
            rm -f "$SCRIPT_DIR/sdkconfig"
        fi

        # Ensure cleanup runs even if the build fails.
        restore_sdkconfig() {
            if [ -n "${sdkconfig_backup:-}" ] && [ -f "$sdkconfig_backup" ]; then
                mv "$sdkconfig_backup" "$SCRIPT_DIR/sdkconfig"
            fi
        }
        trap restore_sdkconfig EXIT

        # Set target first (regenerates sdkconfig + bootloader for this chip)
        idf.py -B "$build_dir" set-target "$chip"

        # Build with board-specific defines
        local cmake_args=""
        [ "$board" = "waveshare349" ] && cmake_args="-DFOOWRITE_BOARD=waveshare349"
        idf.py -B "$build_dir" $cmake_args build

        # Save the generated sdkconfig for future release builds of this board.
        if [ -f "$SCRIPT_DIR/sdkconfig" ]; then
            cp "$SCRIPT_DIR/sdkconfig" "$SCRIPT_DIR/sdkconfig.$board"
        fi

        restore_sdkconfig
        trap - EXIT
    fi

    # Sanity checks
    for f in "$bin_boot" "$bin_part" "$bin_core"; do
        if [ ! -f "$f" ]; then
            echo "error: missing $f — build may have failed" >&2
            exit 1
        fi
    done

    local out="$HOME/Downloads/foowrite-core2-${VERSION}-${board}.bin"

    esptool.py --chip "$chip" merge_bin \
        --flash_mode dio --flash_freq 80m --flash_size 16MB \
        -o "$out" \
        0x1000  "$bin_boot" \
        0x8000  "$bin_part" \
        0x10000 "$bin_core"

    echo "Created: $out"
    echo "Flash with:"
    echo "  esptool.py --chip $chip --port /dev/cu.usbserial-XXXX --baud 921600 write_flash 0x0 $(basename "$out")"
}

if [ "$BOARD" = "all" ] || [ "$BOARD" = "core2" ]; then
    package_variant core2 esp32
fi

if [ "$BOARD" = "all" ] || [ "$BOARD" = "waveshare349" ]; then
    package_variant waveshare349 esp32s3
fi

echo ""
echo "Done."
