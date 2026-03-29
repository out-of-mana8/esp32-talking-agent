"""
test_pipeline.py — unit tests for pipeline.py and main.py

Run:
    cd esp32-talking-agent
    pytest server/tests/ -v

No API keys or downloaded models required — Anthropic and faster-whisper
are patched out with unittest.mock.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path
from unittest.mock import AsyncMock, MagicMock, patch

import numpy as np
import pytest

# conftest.py inserts server/ into sys.path; guard for standalone runs.
_SERVER = Path(__file__).parent.parent
if str(_SERVER) not in sys.path:
    sys.path.insert(0, str(_SERVER))

import main as server_main
from pipeline import VADBuffer, sentence_chunker, MIC_SAMPLE_RATE

# ── Audio helpers ─────────────────────────────────────────────

CHUNK_SAMPLES = 512
CHUNK_BYTES   = CHUNK_SAMPLES * 2   # int16


def _speech_chunk() -> bytes:
    """512-sample 440 Hz sine at amplitude 8 000.

    Normalised RMS ≈ 0.173 — well above SPEECH_THRESHOLD (0.02).
    """
    t = np.linspace(
        0,
        2 * np.pi * 440 * CHUNK_SAMPLES / MIC_SAMPLE_RATE,
        CHUNK_SAMPLES,
        endpoint=False,
    )
    return (np.sin(t) * 8_000).astype(np.int16).tobytes()


def _silence_chunk() -> bytes:
    """512 samples of zeros — normalised RMS = 0.0 < SILENCE_THRESHOLD (0.01)."""
    return np.zeros(CHUNK_SAMPLES, dtype=np.int16).tobytes()


def _feed(vad: VADBuffer, chunk: bytes, n: int) -> list[bytes]:
    """Feed n copies of chunk; collect any emitted utterances."""
    out = []
    for _ in range(n):
        u = vad.add(chunk)
        if u is not None:
            out.append(u)
    return out


async def _noop(event: dict) -> None:
    """No-op broadcast stub."""


# ── Test helpers — ceiling division ──────────────────────────

def _chunks_for_ms(ms: int) -> int:
    """Number of CHUNK_SAMPLES chunks that cover at least ms milliseconds."""
    samples = int(ms / 1000 * MIC_SAMPLE_RATE)
    return -(-samples // CHUNK_SAMPLES)   # ⌈samples / CHUNK_SAMPLES⌉


# ══════════════════════════════════════════════════════════════
# 1. VADBuffer — emits utterance after sufficient trailing silence
# ══════════════════════════════════════════════════════════════

def test_vad_emits_on_silence():
    """2 s of speech + 800 ms silence → exactly one utterance returned."""
    # Use explicit thresholds so the test is immune to config changes.
    vad = VADBuffer(
        speech_threshold=0.02,
        silence_threshold=0.01,
        silence_duration_ms=700,
    )
    silence_budget = int(0.700 * MIC_SAMPLE_RATE)   # 11 200 samples

    speech_n  = _chunks_for_ms(2_000)
    silence_n = _chunks_for_ms(800)

    assert silence_n * CHUNK_SAMPLES >= silence_budget, (
        "Precondition: 800 ms must exceed the 700 ms silence budget"
    )

    results  = _feed(vad, _speech_chunk(),  speech_n)
    results += _feed(vad, _silence_chunk(), silence_n)

    assert len(results) == 1, f"Expected 1 utterance, got {len(results)}"
    assert len(results[0]) > 0


# ══════════════════════════════════════════════════════════════
# 2. VADBuffer — no emit when silence is too short
# ══════════════════════════════════════════════════════════════

def test_vad_no_emit_short_silence():
    """2 s of speech + 300 ms silence → no utterance (silence budget not met)."""
    vad = VADBuffer(
        speech_threshold=0.02,
        silence_threshold=0.01,
        silence_duration_ms=700,
    )
    silence_budget = int(0.700 * MIC_SAMPLE_RATE)

    speech_n  = _chunks_for_ms(2_000)
    # Use floor so we stay strictly under the 700 ms budget.
    silence_n = int(0.3 * MIC_SAMPLE_RATE / CHUNK_SAMPLES)

    assert silence_n * CHUNK_SAMPLES < silence_budget, (
        "Precondition: 300 ms must not reach the 700 ms silence budget"
    )

    _feed(vad, _speech_chunk(),  speech_n)
    results = _feed(vad, _silence_chunk(), silence_n)

    assert len(results) == 0, (
        f"Expected no utterance after only 300 ms silence, got {len(results)}"
    )


# ══════════════════════════════════════════════════════════════
# 3. sentence_chunker — splits token stream into sentences
# ══════════════════════════════════════════════════════════════

@pytest.mark.asyncio
async def test_sentence_chunker_splits():
    """Token stream forming two sentences yields exactly two sentences."""

    tokens = ["Hello", " there.", " How", " are", " you?"]

    async def _fake_tokens():
        for t in tokens:
            yield t

    received = []
    async for sentence in sentence_chunker(_fake_tokens(), _noop):
        received.append(sentence)

    assert len(received) == 2, f"Expected 2 sentences, got {received}"
    assert received[0] == "Hello there."
    assert received[1] == "How are you?"


# ══════════════════════════════════════════════════════════════
# 4. broadcast — reaches every connected ui_client
# ══════════════════════════════════════════════════════════════

@pytest.mark.asyncio
async def test_broadcast_reaches_all():
    """broadcast() delivers serialised JSON to all three mock clients."""
    import websockets

    ws1, ws2, ws3 = MagicMock(), MagicMock(), MagicMock()
    ws1.send = AsyncMock()
    ws2.send = AsyncMock()
    ws3.send = AsyncMock()

    original = set(server_main.ui_clients)
    server_main.ui_clients.clear()
    server_main.ui_clients.update({ws1, ws2, ws3})

    try:
        event = {"event": "state", "state": "listening"}
        await server_main.broadcast(event)
    finally:
        server_main.ui_clients.clear()
        server_main.ui_clients.update(original)

    payload = json.dumps(event)
    ws1.send.assert_called_once_with(payload)
    ws2.send.assert_called_once_with(payload)
    ws3.send.assert_called_once_with(payload)


@pytest.mark.asyncio
async def test_broadcast_prunes_closed_client():
    """broadcast() removes a client whose send() raises ConnectionClosed."""
    import websockets

    ws_ok   = MagicMock()
    ws_dead = MagicMock()
    ws_ok.send   = AsyncMock()
    ws_dead.send = AsyncMock(
        side_effect=websockets.exceptions.ConnectionClosedOK(None, None)
    )

    server_main.ui_clients.clear()
    server_main.ui_clients.update({ws_ok, ws_dead})

    try:
        await server_main.broadcast({"event": "state", "state": "idle"})
    finally:
        server_main.ui_clients.discard(ws_ok)
        server_main.ui_clients.discard(ws_dead)

    ws_ok.send.assert_called_once()
    assert ws_dead not in server_main.ui_clients
