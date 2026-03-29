"""
config.py — ESP32 Talking Agent server settings

Loaded from environment variables. A .env file in server/ or the project root
is parsed before the dataclass is constructed.
"""

from __future__ import annotations

import os
from dataclasses import dataclass
from pathlib import Path


def _load_dotenv() -> None:
    """Minimal .env parser — sets os.environ for keys not already present."""
    candidates = [
        Path(__file__).parent / ".env",
        Path(__file__).parent.parent / ".env",
    ]
    for path in candidates:
        if not path.is_file():
            continue
        with path.open() as fh:
            for raw in fh:
                line = raw.strip()
                if not line or line.startswith("#") or "=" not in line:
                    continue
                key, _, value = line.partition("=")
                os.environ.setdefault(key.strip(), value.strip().strip("\"'"))
        break


_load_dotenv()


@dataclass(frozen=True)
class Settings:
    # Required — no fallback; server will warn loudly if missing
    anthropic_api_key: str

    # Model selection
    whisper_model: str = "base.en"
    kokoro_voice:  str = "af_heart"

    # VAD thresholds — normalized RMS (0.0 – 1.0 of full int16 scale)
    # 0.02 ≈ 655 raw units  →  speech onset
    # 0.01 ≈ 328 raw units  →  silence floor
    speech_threshold:    float = 0.02
    silence_threshold:   float = 0.01
    silence_duration_ms: int   = 700

    # Network
    ws_port:   int = 8765
    http_port: int = 8080

    @classmethod
    def from_env(cls) -> "Settings":
        key = os.environ.get("ANTHROPIC_API_KEY", "")
        if not key:
            import warnings
            warnings.warn(
                "ANTHROPIC_API_KEY is not set — LLM calls will fail",
                stacklevel=2,
            )
        return cls(
            anthropic_api_key    = key,
            whisper_model        = os.environ.get("WHISPER_MODEL",        "base.en"),
            kokoro_voice         = os.environ.get("KOKORO_VOICE",         "af_heart"),
            speech_threshold     = float(os.environ.get("SPEECH_THRESHOLD",    "0.02")),
            silence_threshold    = float(os.environ.get("SILENCE_THRESHOLD",   "0.01")),
            silence_duration_ms  = int(  os.environ.get("SILENCE_DURATION_MS", "700")),
            ws_port              = int(  os.environ.get("WS_PORT",              "8765")),
            http_port            = int(  os.environ.get("HTTP_PORT",            "8080")),
        )


# Module-level singleton — import this everywhere.
settings = Settings.from_env()
