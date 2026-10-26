#!/usr/bin/env bash
set -euo pipefail

# Find target ELF file
TARGET_ELF="${1:-}"
if [ -z "$TARGET_ELF" ]; then
    TARGET_ELF=$(find build -maxdepth 1 -name "*.elf" -print0 2>/dev/null | xargs -0 ls -t 2>/dev/null | head -n 1 || true)
fi

if [ -z "$TARGET_ELF" ] || [ ! -f "$TARGET_ELF" ]; then
    echo "Error: ELF file not found: ${TARGET_ELF:-<none>}"
    echo "Usage: $0 [path/to/binary.elf]"
    exit 1
fi

ROOT_DIR="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
CACHE_FILE="${ROOT_DIR}/build/CMakeCache.txt"

# 1. Parse CMake Configuration if available
PICO_BOARD_VAL=""
TRANSPORT_VAL=""
BUILD_TYPE_VAL=""
PICO_PLATFORM_VAL=""
VERSION_VAL=""

if [ -f "$CACHE_FILE" ]; then
    PICO_BOARD_VAL=$(grep "^PICO_BOARD:" "$CACHE_FILE" 2>/dev/null | cut -d'=' -f2 || true)
    TRANSPORT_VAL=$(grep "^A2PICO_NET_TRANSPORT:" "$CACHE_FILE" 2>/dev/null | cut -d'=' -f2 || true)
    BUILD_TYPE_VAL=$(grep "^CMAKE_BUILD_TYPE:" "$CACHE_FILE" 2>/dev/null | cut -d'=' -f2 || true)
    PICO_PLATFORM_VAL=$(grep "^PICO_PLATFORM:" "$CACHE_FILE" 2>/dev/null | cut -d'=' -f2 || true)
    VERSION_VAL=$(grep "^VERSION:" "$CACHE_FILE" 2>/dev/null | cut -d'=' -f2 || true)
fi

# 2. Parse ELF Metadata via picotool if available
TARGET_CHIP="RP2350"
BOARD_DEF="${PICO_BOARD_VAL:-unknown}"
IMAGE_TYPE="ARM Secure"
BOOT2_NAME="boot2_w25q080"

if command -v picotool >/dev/null 2>&1; then
    PICO_INFO=$(picotool info -a "$TARGET_ELF" 2>/dev/null || true)
    if [ -n "$PICO_INFO" ]; then
        CHIP_PARSED=$(echo "$PICO_INFO" | grep "target chip:" | head -n 1 | awk '{print $NF}')
        BOARD_PARSED=$(echo "$PICO_INFO" | grep "pico_board:" | head -n 1 | awk '{print $NF}')
        BOOT2_PARSED=$(echo "$PICO_INFO" | grep "boot2_name:" | head -n 1 | awk '{print $NF}')
        IMAGE_PARSED=$(echo "$PICO_INFO" | grep "image type:" | head -n 1 | sed 's/.*image type:[[:space:]]*//')
        
        [ -n "$CHIP_PARSED" ] && TARGET_CHIP="$CHIP_PARSED"
        [ -n "$BOARD_PARSED" ] && BOARD_DEF="$BOARD_PARSED"
        [ -n "$BOOT2_PARSED" ] && BOOT2_NAME="$BOOT2_PARSED"
        [ -n "$IMAGE_PARSED" ] && IMAGE_TYPE="$IMAGE_PARSED"
    fi
fi

# 3. Resolve MCU Module Description
MCU_MODULE_DESC="Raspberry Pi Pico 2 (RP2350A)"
BOARD_COMPAT_NOTE=""
if [ "$BOARD_DEF" = "a2pico2" ]; then
    MCU_MODULE_DESC="Raspberry Pi Pico 2 / Pico 2 W (RP2350 Unified)"
    BOARD_COMPAT_NOTE=" (Unified A2Pico2 / Lite W)"
elif [ "$BOARD_DEF" = "a2pico2litew" ]; then
    if [ "$TRANSPORT_VAL" = "ETHER" ]; then
        MCU_MODULE_DESC="Raspberry Pi Pico 2 / Pico 2 W (RP2350A)"
        BOARD_COMPAT_NOTE=" (Operable on both Pico 2 and Pico 2 W)"
    else
        MCU_MODULE_DESC="Raspberry Pi Pico 2 W (RP2350A + CYW43439)"
    fi
elif [ "$BOARD_DEF" = "pico2" ]; then
    MCU_MODULE_DESC="Raspberry Pi Pico 2 (RP2350A / Standard)"
elif [ "$BOARD_DEF" = "a2pico2-generic" ]; then
    MCU_MODULE_DESC="RP2350B (80-pin / Generic)"
elif [ "$TARGET_CHIP" = "RP2040" ]; then
    MCU_MODULE_DESC="Raspberry Pi Pico (RP2040)"
fi

# 4. Resolve Transport Description
TRANSPORT_DESC="Unknown"
if [ "$TRANSPORT_VAL" = "ETHER" ]; then
    TRANSPORT_DESC="Ethernet (TinyUSB Host + lwIP / Realtek RTL8152)"
elif [ "$TRANSPORT_VAL" = "WIFI" ]; then
    TRANSPORT_DESC="Wi-Fi (CYW43439 + lwIP)"
fi

# 5. Parse Symbols via nm
NM_CMD=""
for cmd in arm-none-eabi-nm llvm-nm nm; do
    if command -v "$cmd" >/dev/null 2>&1; then
        NM_CMD="$cmd"
        break
    fi
done

HAS_ADDR_INDIRECT=false
HAS_DEVSEL=false
HAS_IOSEL=false
HAS_IOSTRB=false

if [ -n "$NM_CMD" ]; then
    SYMBOLS=$("$NM_CMD" "$TARGET_ELF" 2>/dev/null || true)
    echo "$SYMBOLS" | grep -q "addr_indirect_program" && HAS_ADDR_INDIRECT=true
    echo "$SYMBOLS" | grep -q "devsel_program" && HAS_DEVSEL=true
    echo "$SYMBOLS" | grep -q "iosel_program" && HAS_IOSEL=true
    echo "$SYMBOLS" | grep -q "iostrb_program" && HAS_IOSTRB=true
fi

# 6. Display Facts
echo "======================================================================"
echo " a2pico Firmware Hardware & Configuration Inspector"
echo " Target ELF : $TARGET_ELF"
echo "======================================================================"
echo "[MCU Module Configuration]"
echo "  Target Chip        : $TARGET_CHIP ($IMAGE_TYPE)"
echo "  Target Module      : $MCU_MODULE_DESC"
echo "  Board Definition   : $BOARD_DEF$BOARD_COMPAT_NOTE"
echo "  Boot Stage 2       : $BOOT2_NAME"
echo "  Build Type         : ${BUILD_TYPE_VAL:-Release}"
echo ""
echo "[Carrier Board & Bus Routing]"
if [ "$BOARD_DEF" = "a2pico2litew" ]; then
    echo "  Target Carrier     : A2Pico2Lite W (Lite W Series)"
    echo "  Bus Routing Mode   : Combined ENBL Tracking with External AND Gate"
    echo "  Apple II Signals   : GPIO26 = ENBL (AND gate output from /DEVSEL,/IOSEL,/IOSTRB)"
    echo "                       GPIO27 = /RESET (Apple II /RESET pin)"
    echo "                       GPIO28 = UART0 TX (Free pin, available for stdio/logging)"
    echo "  PIO Architecture   : 4 State Machines (PIO0 only)"
    echo "  Active PIO Programs: addr_program, read_program, write_program"
elif [ "$HAS_ADDR_INDIRECT" = true ]; then
    echo "  Target Carrier     : A2Pico2Lite (Lite Series, Non-W)"
    if [ "$TRANSPORT_VAL" = "ETHER" ]; then
        echo "  Board Note         : Operable on A2Pico2Lite in Ethernet mode"
    fi
    echo "  Bus Routing Mode   : Individual 3-Signal Tracking (Indirect Mode)"
    echo "  Apple II Signals   : GPIO26 = /DEVSEL (Direct Apple II pin)"
    echo "                       GPIO27 = /IOSEL  (Direct Apple II pin)"
    echo "                       GPIO28 = /IOSTRB (Direct Apple II pin)"
    echo "  PIO Architecture   : 7 State Machines (PIO0: 4 SMs, PIO1: 3 SMs)"
    echo "  Active PIO Programs: addr_indirect_program, devsel_program,"
    echo "                       iosel_program, iostrb_program"
else
    echo "  Target Carrier     : A2Pico Standard / A2Pico2 (RP2350B Direct / Non-Lite)"
    echo "  Bus Routing Mode   : Combined ENBL Tracking (Standard Mode)"
    echo "  Apple II Signals   : GPIO26 = ENBL (AND gate output from /DEVSEL,/IOSEL,/IOSTRB)"
    echo "                       GPIO27 = /RESET"
    echo "                       GPIO28 = LED"
    echo "  PIO Architecture   : 4 State Machines (PIO0 only)"
    echo "  Active PIO Programs: addr_program, read_program, write_program"
fi
echo ""
echo "[Build Configuration & Reproduction Command]"
TARGET_DIR=$(dirname "$TARGET_ELF")
REPRO_CMD="cmake -B $TARGET_DIR"
[ -n "$PICO_BOARD_VAL" ] && REPRO_CMD="$REPRO_CMD -DPICO_BOARD=$PICO_BOARD_VAL"
[ -n "$TRANSPORT_VAL" ] && REPRO_CMD="$REPRO_CMD -DA2PICO_NET_TRANSPORT=$TRANSPORT_VAL"
[ -n "$PICO_PLATFORM_VAL" ] && REPRO_CMD="$REPRO_CMD -DPICO_PLATFORM=$PICO_PLATFORM_VAL"
[ -n "$BUILD_TYPE_VAL" ] && REPRO_CMD="$REPRO_CMD -DCMAKE_BUILD_TYPE=$BUILD_TYPE_VAL"
[ -n "$VERSION_VAL" ] && REPRO_CMD="$REPRO_CMD -DVERSION=\"$VERSION_VAL\""

OPT_SUMMARY=""
[ -n "$TRANSPORT_VAL" ] && OPT_SUMMARY="$OPT_SUMMARY -DA2PICO_NET_TRANSPORT=$TRANSPORT_VAL"
[ -n "$PICO_BOARD_VAL" ] && OPT_SUMMARY="$OPT_SUMMARY -DPICO_BOARD=$PICO_BOARD_VAL"
[ -n "$PICO_PLATFORM_VAL" ] && OPT_SUMMARY="$OPT_SUMMARY -DPICO_PLATFORM=$PICO_PLATFORM_VAL"
[ -n "$BUILD_TYPE_VAL" ] && OPT_SUMMARY="$OPT_SUMMARY -DCMAKE_BUILD_TYPE=$BUILD_TYPE_VAL"
[ -n "$VERSION_VAL" ] && OPT_SUMMARY="$OPT_SUMMARY -DVERSION=\"$VERSION_VAL\""

echo "  Active Options     :${OPT_SUMMARY:- (default options)}"
echo "  Configure Command  : $REPRO_CMD"
echo "  Build Command      : cmake --build $TARGET_DIR"
echo "======================================================================"
