#!/usr/bin/env bash
# run.sh — start the ESP32 Talking Agent server
# Usage: bash server/run.sh  (from project root)
#        ./run.sh            (from server/)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VENV_DIR="$SCRIPT_DIR/.venv"

# Activate virtualenv
if [[ -f "$VENV_DIR/bin/activate" ]]; then
    source "$VENV_DIR/bin/activate"
elif [[ -f "$VENV_DIR/Scripts/activate" ]]; then
    source "$VENV_DIR/Scripts/activate"
else
    echo "[run.sh] Warning: virtualenv not found at $VENV_DIR" >&2
    echo "[run.sh] Run 'make install' first." >&2
fi

export ENV=development

cd "$SCRIPT_DIR"
exec python main.py "$@"
