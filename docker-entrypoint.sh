#!/usr/bin/env bash
set -e

ACTION="${1:-build}"

if [ "$ACTION" = "build" ]; then
    echo "=========================================================="
    echo "Building ESP32 firmware with PlatformIO inside Docker..."
    echo "=========================================================="
    pio run

    BUILD_DIR=".pio/build/esp32-c6-devkitm-1"
    if [ -f "$BUILD_DIR/firmware.bin" ]; then
        echo ""
        echo "=========================================================="
        echo "Merging binaries into a single flashable file..."
        echo "=========================================================="
        
        MERGE_ARGS=()
        MERGE_ARGS+=(--chip esp32c6 merge_bin)
        MERGE_ARGS+=(-o "$BUILD_DIR/firmware-merged.bin")
        MERGE_ARGS+=(--flash_mode dio)
        MERGE_ARGS+=(--flash_size 8MB)

        if [ -f "$BUILD_DIR/bootloader.bin" ]; then
            MERGE_ARGS+=(0x0000 "$BUILD_DIR/bootloader.bin")
        fi

        if [ -f "$BUILD_DIR/partitions.bin" ]; then
            MERGE_ARGS+=(0x8000 "$BUILD_DIR/partitions.bin")
        fi

        # Check for boot_app0 if present
        if [ -f "$BUILD_DIR/boot_app0.bin" ]; then
            MERGE_ARGS+=(0xe000 "$BUILD_DIR/boot_app0.bin")
        elif [ -f "/root/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin" ]; then
            MERGE_ARGS+=(0xe000 "/root/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin")
        fi

        MERGE_ARGS+=(0x10000 "$BUILD_DIR/firmware.bin")

        python3 -m esptool "${MERGE_ARGS[@]}"

        # Copy to the root firmware folder (overwrite if old one exists)
        mkdir -p firmware
        cp -f "$BUILD_DIR/firmware-merged.bin" "firmware/firmware-merged.bin"

        echo ""
        echo " Build & Merge Succeeded!"
        echo " Flashable Unified Binary: firmware/firmware-merged.bin (Flash at offset 0x0)"
        echo " Component Binaries:       $BUILD_DIR/"
        echo "=========================================================="
    else
        echo "ERROR: Expected firmware binary not found at $BUILD_DIR/firmware.bin" >&2
        exit 1
    fi
elif [ "$ACTION" = "clean" ]; then
    pio run -t clean
else
    exec "$@"
fi

