#!/usr/bin/env bash
set -euo pipefail

# ANSI Color Codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
BOLD='\033[1m'
NC='\033[0m' # No Color

ROOT_DIR="$(git -C "$(dirname "${BASH_SOURCE[0]}")" rev-parse --show-toplevel 2>/dev/null || pwd)"
SCRIPT_DIR="${ROOT_DIR}/tools"

echo -e "${BOLD}======================================================================${NC}"
echo -e "${BOLD}  a2pico Firmware Build Configuration & Integrity Validator           ${NC}"
echo -e "${BOLD}======================================================================${NC}"

HAS_ERROR=0

# -----------------------------------------------------------------------------
# Check 1: CMakeCache.txt configuration (if build/ directory exists)
# -----------------------------------------------------------------------------
CACHE_FILE="${ROOT_DIR}/build/CMakeCache.txt"
if [ -f "$CACHE_FILE" ]; then
    echo -e "${BLUE}▶ Checking CMake Configuration (build/CMakeCache.txt)...${NC}"

    PICO_BOARD_VAL=$(grep "^PICO_BOARD:" "$CACHE_FILE" 2>/dev/null | cut -d'=' -f2 || true)
    BUILD_TYPE_VAL=$(grep "^CMAKE_BUILD_TYPE:" "$CACHE_FILE" 2>/dev/null | cut -d'=' -f2 || true)

    # 1. PICO_BOARD check
    if [ "$PICO_BOARD_VAL" = "a2pico2" ]; then
        echo -e "  ${GREEN}✔ PICO_BOARD         :${NC} ${PICO_BOARD_VAL} (Valid)"
    else
        echo -e "  ${RED}✘ PICO_BOARD         :${NC} ${PICO_BOARD_VAL:-<not set>} ${RED}(CRITICAL: MUST be 'a2pico2')${NC}"
        HAS_ERROR=1
    fi

    # 2. CMAKE_BUILD_TYPE check
    if [ "$BUILD_TYPE_VAL" = "Release" ]; then
        echo -e "  ${GREEN}✔ CMAKE_BUILD_TYPE   :${NC} ${BUILD_TYPE_VAL} (Valid)"
    else
        echo -e "  ${RED}✘ CMAKE_BUILD_TYPE   :${NC} ${BUILD_TYPE_VAL:-<not set>} ${RED}(CRITICAL: MUST be 'Release')${NC}"
        HAS_ERROR=1
    fi
else
    echo -e "${YELLOW}ℹ build/CMakeCache.txt not found. Skipping cache check.${NC}"
fi

# -----------------------------------------------------------------------------
# Check 2: Binary UF2 inspection using picotool
# -----------------------------------------------------------------------------
echo -e "${BLUE}▶ Inspecting UF2 Firmware Binary...${NC}"

TARGET_UF2="${1:-}"
if [ -z "$TARGET_UF2" ]; then
    # Automatically pick the newest .uf2 in build/ (excluding backup files)
    TARGET_UF2=$(find "${ROOT_DIR}/build" -maxdepth 1 -name "*.uf2" ! -name "*bkup*" -print0 2>/dev/null | xargs -0 ls -t 2>/dev/null | head -n 1 || true)
fi

if [ -z "$TARGET_UF2" ] || [ ! -f "$TARGET_UF2" ]; then
    echo -e "  ${YELLOW}⚠ No target UF2 file found to inspect.${NC}"
    if [ "$HAS_ERROR" -ne 0 ]; then
        exit 1
    fi
    exit 0
fi

echo -e "  Target file: ${BOLD}${TARGET_UF2}${NC}"

if ! command -v picotool >/dev/null 2>&1; then
    echo -e "  ${YELLOW}⚠ picotool not found in PATH. Skipping deep metadata inspection.${NC}"
else
    PICO_INFO=$(picotool info -a "$TARGET_UF2" 2>/dev/null || true)

    CHIP=$(echo "$PICO_INFO" | grep "target chip:" | head -n 1 | awk '{print $NF}')
    BOARD=$(echo "$PICO_INFO" | grep "pico_board:" | head -n 1 | awk '{print $NF}')
    ATTRS=$(echo "$PICO_INFO" | grep "build attributes:" | head -n 1 | awk '{print $NF}')
    FILE_SIZE=$(stat -f "%z" "$TARGET_UF2" 2>/dev/null || stat -c "%s" "$TARGET_UF2" 2>/dev/null || echo 0)

    # Chip check
    if [ "$CHIP" = "RP2350" ]; then
        echo -e "  ${GREEN}✔ Target Chip        :${NC} ${CHIP} (Valid)"
    else
        echo -e "  ${RED}✘ Target Chip        :${NC} ${CHIP:-<unknown>} ${RED}(MUST be RP2350)${NC}"
        HAS_ERROR=1
    fi

    # Board check
    if [ "$BOARD" = "a2pico2" ]; then
        echo -e "  ${GREEN}✔ Board Definition   :${NC} ${BOARD} (Valid: Unified A2Pico2 / Lite W)"
    else
        echo -e "  ${RED}✘ Board Definition   :${NC} ${BOARD:-<unknown>} ${RED}(CRITICAL: MUST be 'a2pico2')${NC}"
        echo -e "    ${RED}↳ Reason: Board definition MUST be 'a2pico2' for unified firmware!${NC}"
        HAS_ERROR=1
    fi

    # Build attributes check
    if [ "$ATTRS" = "Release" ]; then
        echo -e "  ${GREEN}✔ Build Attributes   :${NC} ${ATTRS} (Valid)"
    else
        echo -e "  ${RED}✘ Build Attributes   :${NC} ${ATTRS:-<unknown>} ${RED}(CRITICAL: MUST be 'Release')${NC}"
        echo -e "    ${RED}↳ Reason: Debug builds cause severe bus timing failures on Apple II slot accesses!${NC}"
        HAS_ERROR=1
    fi

    # Size check (~846 KB expected for Release; 841 KB indicates wrong board/Debug build)
    if [ "$FILE_SIZE" -gt 845000 ]; then
        echo -e "  ${GREEN}✔ Binary Size        :${NC} ${FILE_SIZE} bytes (Expected range)"
    else
        echo -e "  ${RED}✘ Binary Size        :${NC} ${FILE_SIZE} bytes ${RED}(Suspiciously small, matches broken build size ~841KB)${NC}"
        HAS_ERROR=1
    fi
fi

echo -e "${BOLD}======================================================================${NC}"

if [ "$HAS_ERROR" -ne 0 ]; then
    echo -e "${RED}${BOLD}🚨 BUILD VALIDATION FAILED!${NC}"
    echo -e "${RED}このバイナリはハードウェアと互換性がなく、書き込むと起動不能（フリーズ）になります。${NC}"
    echo -e "正しいビルド手順で再構成してください:"
    echo -e "  ${BOLD}cmake -B build -DPICO_BOARD=a2pico2 -DCMAKE_BUILD_TYPE=Release${NC}"
    echo -e "  ${BOLD}cmake --build build -j8${NC}"
    echo -e "${BOLD}======================================================================${NC}"
    exit 1
else
    echo -e "${GREEN}${BOLD}✅ BUILD VALIDATION PASSED!${NC}"
    echo -e "${GREEN}ファームウェアは a2pico2 / Release 構成で正常に生成されています。${NC}"
    echo -e "${BOLD}======================================================================${NC}"
    exit 0
fi
