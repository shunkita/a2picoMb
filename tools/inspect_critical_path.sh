#!/usr/bin/env bash
set -euo pipefail

# ==============================================================================
# a2picoMb Core 1 Bus Read Critical Path Cycle Analyzer Wrapper
# ==============================================================================

ROOT_DIR="$(git -C "$(dirname "${BASH_SOURCE[0]}")" rev-parse --show-toplevel 2>/dev/null || pwd)"
SCRIPT_DIR="${ROOT_DIR}/tools"
PYTHON_SCRIPT="${SCRIPT_DIR}/inspect_critical_path.py"

if [ ! -f "$PYTHON_SCRIPT" ]; then
    echo "Error: Python analyzer script not found: ${PYTHON_SCRIPT}" >&2
    exit 1
fi

if ! command -v python3 >/dev/null 2>&1; then
    echo "Error: python3 is required to run critical path analysis." >&2
    exit 1
fi

exec python3 "$PYTHON_SCRIPT" "$@"
