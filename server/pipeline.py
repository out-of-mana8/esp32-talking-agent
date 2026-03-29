"""
pipeline.py — ESP32 Talking Agent audio pipeline

Stages
------
PCM bytes → VADBuffer → run_stt → run_llm → sentence_chunker → run_tts → PCM bytes

Latency budget (target):
  VAD silence detection  ~700 ms
  faster-whisper STT     ~300 ms  (beam_size=1, base.en)
  Claude TTFT            ~500 ms
  First TTS sentence     ~400 ms
  ─────────────────────────────
  Total to first audio   ~1.9 s
"""

from __future__ import annotations

import asyncio
import logging
from pathlib import Path
from typing import AsyncIterator, Callable, Coroutine

import numpy as np

from config import settings

log = logging.getLogger("agent.pipeline")

# ── Type alias ────────────────────────────────────────────────
Broadcast = Callable[[dict], Coroutine]

# ── Audio constants ───────────────────────────────────────────
MIC_SAMPLE_RATE = 16_000   # Hz — ESP32 capture rate
SPK_SAMPLE_RATE = 24_000   # Hz — kokoro-onnx output rate

# ── Lazy model handles ────────────────────────────────────────
_whisper_model = None
_kokoro_model  = None


def _load_whisper():
    global _whisper_model
    if _whisper_model is None:
        from faster_whisper import WhisperModel
        log.info("Loading Whisper '%s' …", settings.whisper_model)
        _whisper_model = WhisperModel(
            settings.whisper_model,
            device="cpu",
            compute_type="int8",
        )
        log.info("Whisper ready")
    return _whisper_model


def _load_kokoro():
    global _kokoro_model
    if _kokoro_model is None:
        from kokoro_onnx import Kokoro
        log.info("Loading Kokoro TTS …")
        _here = Path(__file__).parent
        _kokoro_model = Kokoro(
            str(_here / "kokoro-v1.0.onnx"),
            str(_here / "voices-v1.0.bin"),
        )
        log.info("Kokoro ready")
    return _kokoro_model


def prewarm_models() -> None:
    """Load both models into memory at startup to eliminate first-call latency."""
    _load_whisper()
    _load_kokoro()


# ── RMS helper ────────────────────────────────────────────────

def _rms_norm(chunk: bytes) -> float:
    """Return RMS amplitude of a 16-bit PCM chunk, normalised to [0, 1]."""
    if len(chunk) < 2:
        return 0.0
    samples = np.frombuffer(chunk, dtype=np.int16).astype(np.float32)
    return float(np.sqrt(np.mean(samples ** 2))) / 32_768.0


# ── 1. VADBuffer ──────────────────────────────────────────────

class VADBuffer:
    """Accumulates 16 kHz 16-bit mono PCM and emits complete utterances.

    Detection criteria
    ------------------
    An utterance is emitted once:
    1. RMS has exceeded ``speech_threshold`` at least once (onset), AND
    2. RMS then stays below ``silence_threshold`` for ``silence_duration_ms`` ms
       (offset).

    Pre-speech chunks are discarded; trailing silence is included so Whisper
    sees a clean boundary.

    Parameters
    ----------
    speech_threshold, silence_threshold:
        Normalised RMS values in [0, 1].  Defaults come from config.
    silence_duration_ms:
        Milliseconds of continuous silence required to end an utterance.
    """

    def __init__(
        self,
        speech_threshold:    float = settings.speech_threshold,
        silence_threshold:   float = settings.silence_threshold,
        silence_duration_ms: int   = settings.silence_duration_ms,
    ) -> None:
        self._speech_thr  = speech_threshold
        self._silence_thr = silence_threshold
        # Silence budget in samples (int16 samples, not bytes)
        self._silence_budget = int(silence_duration_ms / 1000 * MIC_SAMPLE_RATE)

        self._buf:          list[bytes] = []
        self._has_speech:   bool        = False
        self._silence_accum: int        = 0   # samples accumulated in post-speech silence

    def add(self, chunk: bytes) -> bytes | None:
        """Feed one PCM chunk.  Returns the utterance bytes when complete, else None."""
        rms = _rms_norm(chunk)

        if rms >= self._speech_thr:
            self._has_speech    = True
            self._silence_accum = 0
            self._buf.append(chunk)

        elif self._has_speech:
            self._buf.append(chunk)
            self._silence_accum += len(chunk) // 2   # 2 bytes per int16 sample

            if self._silence_accum >= self._silence_budget:
                utterance = b"".join(self._buf)
                log.debug(
                    "VAD: utterance  %.2f s  %d B",
                    len(utterance) / (MIC_SAMPLE_RATE * 2),
                    len(utterance),
                )
                self.reset()
                return utterance

        # silence before speech onset — discard
        return None

    def reset(self) -> None:
        """Discard all buffered audio and return to idle state."""
        self._buf.clear()
        self._has_speech    = False
        self._silence_accum = 0


# ── 2. STT ────────────────────────────────────────────────────

def _transcribe_sync(audio: np.ndarray) -> str:
    """Blocking Whisper transcription — runs in a thread pool."""
    model = _load_whisper()
    # beam_size=1 is the single largest speedup vs the default of 5.
    # vad_filter=True removes internal silence, reducing transcript length.
    # condition_on_previous_text=False avoids carrying state between calls.
    segments, _ = model.transcribe(
        audio,
        language="en",
        beam_size=1,
        best_of=1,
        temperature=0.0,
        vad_filter=True,
        condition_on_previous_text=False,
    )
    # Consume the lazy generator inside the thread.
    return " ".join(seg.text.strip() for seg in segments).strip()


async def run_stt(audio_bytes: bytes) -> str:
    """Transcribe a complete utterance.

    Parameters
    ----------
    audio_bytes:
        Raw 16 kHz 16-bit mono PCM.

    Returns
    -------
    str
        Recognised text, or empty string if nothing intelligible was heard.
    """
    log.info("STT: %.2f s of audio", len(audio_bytes) / (MIC_SAMPLE_RATE * 2))
    audio = np.frombuffer(audio_bytes, dtype=np.int16).astype(np.float32) / 32_768.0
    transcript = await asyncio.to_thread(_transcribe_sync, audio)
    log.info("STT: %r", transcript)
    return transcript


# ── 3. LLM ────────────────────────────────────────────────────

_SYSTEM_PROMPT = (
    "You are a voice assistant called ESP32 Talking Agent. "
    "Respond concisely in 1-3 spoken sentences. "
    "No markdown, no lists, no special characters, "
    "natural conversational language only."
)


async def run_llm(transcript: str, broadcast: Broadcast) -> AsyncIterator[str]:
    """Stream a Claude response token by token.

    Broadcasts ``{"event": "llm_token", "token": t}`` for each token.
    Yields each token so ``sentence_chunker`` can process the stream
    incrementally and start TTS as soon as the first sentence is complete.
    """
    import anthropic

    log.info("LLM: %d-char transcript", len(transcript))
    client = anthropic.AsyncAnthropic(api_key=settings.anthropic_api_key)
    count  = 0

    async with client.messages.stream(
        model="claude-sonnet-4-5",
        max_tokens=300,
        system=_SYSTEM_PROMPT,
        messages=[{"role": "user", "content": transcript}],
    ) as stream:
        async for token in stream.text_stream:
            await broadcast({"event": "llm_token", "token": token})
            count += 1
            yield token

    log.info("LLM: %d tokens streamed", count)


# ── 4. Sentence chunker ───────────────────────────────────────

async def sentence_chunker(
    token_iterator: AsyncIterator[str],
    broadcast: Broadcast,
) -> AsyncIterator[str]:
    """Buffer LLM tokens and yield complete sentences.

    A sentence boundary is any ``.``, ``!``, or ``?`` character.
    The remaining fragment after the last boundary is flushed when the
    token stream ends (handles responses without terminal punctuation).

    Broadcasts ``{"event": "llm_sentence", "text": sentence}`` on each yield.

    Yielding sentences one at a time lets the caller pipeline TTS for
    sentence N while the LLM is still generating sentence N+1.
    """
    buf = ""

    async for token in token_iterator:
        buf += token

        # Drain all complete sentences that have accumulated.
        while True:
            end = next((i for i, ch in enumerate(buf) if ch in ".!?"), -1)
            if end == -1:
                break
            sentence = buf[: end + 1].strip()
            buf      = buf[end + 1 :].lstrip()
            if sentence:
                log.debug("sentence: %r", sentence)
                await broadcast({"event": "llm_sentence", "text": sentence})
                yield sentence

    remainder = buf.strip()
    if remainder:
        log.debug("sentence (flush): %r", remainder)
        await broadcast({"event": "llm_sentence", "text": remainder})
        yield remainder


# ── 5. TTS ────────────────────────────────────────────────────

def _synthesise_sync(text: str) -> bytes:
    """Blocking kokoro synthesis — runs in a thread pool."""
    kokoro = _load_kokoro()
    samples, _ = kokoro.create(
        text,
        voice=settings.kokoro_voice,
        speed=1.0,
        lang="en-us",
    )
    # float32 [-1, 1] → int16 little-endian PCM
    pcm = (np.clip(samples, -1.0, 1.0) * 32_767).astype(np.int16)
    return pcm.tobytes()


async def run_tts(text: str) -> bytes:
    """Synthesise one sentence to 24 kHz 16-bit mono PCM.

    Returns
    -------
    bytes
        Raw little-endian int16 PCM at 24 kHz.
    """
    log.info("TTS: %d chars", len(text))
    pcm = await asyncio.to_thread(_synthesise_sync, text)
    log.debug("TTS: %d B  (%.2f s)", len(pcm), len(pcm) / (SPK_SAMPLE_RATE * 2))
    return pcm
