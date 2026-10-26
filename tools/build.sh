#!/usr/bin/env bash
set -euo pipefail

# ANSI Color Codes
GREEN='\033[0;32m'
BLUE='\033[0;34m'
BOLD='\033[1m'
NC='\033[0m'

ROOT_DIR="$(git -C "$(dirname "${BASH_SOURCE[0]}")" rev-parse --show-toplevel 2>/dev/null || pwd)"
SCRIPT_DIR="${ROOT_DIR}/tools"
cd "$ROOT_DIR"

VERSION="${1:-}"

echo -e "${BLUE}${BOLD}===> [1/3] Configuring CMake (enforcing a2pico2litew / Release)...${NC}"
CMAKE_ARGS=(
    -B build
    -DPICO_BOARD=a2pico2litew
    -DCMAKE_BUILD_TYPE=Release
)

if [ -n "$VERSION" ]; then
    CMAKE_ARGS+=("-DVERSION=${VERSION}")
fi

cmake "${CMAKE_ARGS[@]}"

echo -e "${BLUE}${BOLD}===> [2/3] Building firmware target...${NC}"
NPROC=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
cmake --build build -j"$NPROC"

echo -e "${BLUE}${BOLD}===> [3/3] Running automated post-build verification...${NC}"
"${SCRIPT_DIR}/verify_build.sh"

echo -e "${GREEN}${BOLD}🎉 Build and verification completed successfully!${NC}"
