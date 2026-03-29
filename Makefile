# ESP32 Talking Agent — project task runner
#
# Usage:
#   make install      create server/.venv and install Python dependencies
#   make run-server   start the WebSocket + HTTP server
#   make flash        build and flash firmware to the ESP32-S3
#   make monitor      open serial monitor at 115200 baud
#   make open-ui      open the UI in the default browser
#   make test         run pytest suite
#   make all          install + run-server

.PHONY: install run-server flash monitor open-ui test all

# ── OS detection ──────────────────────────────────────────────
ifeq ($(OS),Windows_NT)
    VENV_BIN = server/.venv/Scripts
    PYTHON   = python
else
    VENV_BIN = server/.venv/bin
    PYTHON   = python3
endif

# ── Python setup ──────────────────────────────────────────────
install:
	@echo "→ Creating server/.venv"
	$(PYTHON) -m venv server/.venv
	@echo "→ Installing server/requirements.txt"
	$(VENV_BIN)/pip install --upgrade pip --quiet
	$(VENV_BIN)/pip install -r server/requirements.txt
	@echo "→ Done.  Run 'make run-server' to start."

# ── Server ────────────────────────────────────────────────────
run-server:
	bash server/run.sh

# ── Tests ─────────────────────────────────────────────────────
test:
	cd server && $(VENV_BIN)/pytest tests/ -v

# ── Firmware ──────────────────────────────────────────────────
flash:
	pio run --environment voicebox --target upload

monitor:
	pio device monitor --baud 115200

# ── Browser ───────────────────────────────────────────────────
# macOS: open  |  Linux: xdg-open  |  Windows: start
open-ui:
	open http://localhost:8080 2>/dev/null \
	  || xdg-open http://localhost:8080 2>/dev/null \
	  || start http://localhost:8080

# ── Convenience ───────────────────────────────────────────────
all: install run-server
