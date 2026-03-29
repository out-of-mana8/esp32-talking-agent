"""
main.py — ESP32 Talking Agent WebSocket + HTTP server

Ports
-----
WS_PORT   (8765)  WebSocket
    /device  →  handle_device(ws)   ESP32 audio stream
    /ui      →  handle_ui(ws)       browser monitor

HTTP_PORT (8080)  HTTP
    GET /    →  serves ui/index.html

Run
---
    python server/main.py
    # or via Makefile:
    make run-server
"""

from __future__ import annotations

import asyncio
import json
import logging
import os
from pathlib import Path

import websockets
from websockets.server import WebSocketServerProtocol

from config import settings
from pipeline import (
    VADBuffer,
    prewarm_models,
    run_stt,
    run_llm,
    run_tts,
    sentence_chunker,
)

# ── Logging ───────────────────────────────────────────────────
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(name)s] %(levelname)s %(message)s",
)
log = logging.getLogger("agent.server")

# ── Paths ─────────────────────────────────────────────────────
UI_FILE = Path(__file__).resolve().parent.parent / "ui" / "index.html"

# ── Global state ──────────────────────────────────────────────
ui_clients:   set[WebSocketServerProtocol] = set()
device_state: str = "disconnected"


def _set_device_state(state: str) -> None:
    global device_state
    device_state = state


# ── Broadcast ─────────────────────────────────────────────────

async def broadcast(event_dict: dict) -> None:
    """Serialise event_dict as JSON and send to every /ui client.

    Closed connections are removed from ui_clients so they never accumulate.
    """
    if not ui_clients:
        return

    data = json.dumps(event_dict)
    dead: set[WebSocketServerProtocol] = set()

    for ws in list(ui_clients):
        try:
            await ws.send(data)
        except websockets.ConnectionClosed:
            log.debug("broadcast: pruning closed /ui client %s", ws.remote_address)
            dead.add(ws)

    ui_clients.difference_update(dead)


# ── /ui handler ───────────────────────────────────────────────

async def handle_ui(websocket: WebSocketServerProtocol) -> None:
    """Browser monitor — receives no messages, only broadcasts."""
    addr = websocket.remote_address
    log.info("[ui] Connected    %s", addr)
    ui_clients.add(websocket)

    # Immediately push the current device state so the badge is correct
    # on page load even if the ESP32 connected before the browser did.
    await websocket.send(json.dumps({"event": "state", "state": device_state}))

    try:
        async for _ in websocket:
            pass
    except websockets.ConnectionClosed:
        pass
    finally:
        ui_clients.discard(websocket)
        log.info("[ui] Disconnected  %s  (remaining: %d)", addr, len(ui_clients))


# ── /device handler ───────────────────────────────────────────

async def handle_device(websocket: WebSocketServerProtocol) -> None:
    """ESP32 audio stream — full VAD → STT → LLM → TTS pipeline.

    Binary frames in:  16 kHz 16-bit mono PCM from ESP32 mic
    Binary frames out: 24 kHz 16-bit mono PCM TTS audio to ESP32 speaker

    Pipeline optimisation
    ---------------------
    LLM token generation and TTS synthesis are pipelined via an asyncio.Queue:
    - A collector task drives sentence_chunker(run_llm(...)) and enqueues each
      sentence as it becomes available.
    - The main loop dequeues sentences and runs TTS sequentially.
    - Because TTS uses asyncio.to_thread, the event loop is free to continue
      running the collector while TTS executes — so sentence N+1 is ready
      in the queue by the time TTS for sentence N finishes.
    """
    addr = websocket.remote_address
    log.info("[device] Connected  %s", addr)

    vad = VADBuffer()
    _set_device_state("listening")
    await broadcast({"event": "state", "state": "listening"})

    try:
        async for message in websocket:
            if not isinstance(message, bytes):
                continue

            utterance = vad.add(message)
            if utterance is None:
                continue

            log.info(
                "[device] Utterance  %.2f s  %d B",
                len(utterance) / (16_000 * 2),
                len(utterance),
            )

            try:
                # ── STT ───────────────────────────────────────
                _set_device_state("transcribing")
                await broadcast({"event": "state", "state": "transcribing"})

                transcript = await run_stt(utterance)
                if not transcript:
                    log.info("[device] Empty transcript — skipping")
                    _set_device_state("listening")
                    await broadcast({"event": "state", "state": "listening"})
                    vad.reset()
                    continue

                await broadcast({"event": "transcript", "text": transcript})

                # ── LLM + TTS (pipelined) ─────────────────────
                _set_device_state("thinking")
                await broadcast({"event": "state", "state": "thinking"})

                sentence_queue: asyncio.Queue[str | None] = asyncio.Queue()

                async def _collect_sentences() -> None:
                    try:
                        async for sentence in sentence_chunker(
                            run_llm(transcript, broadcast), broadcast
                        ):
                            await sentence_queue.put(sentence)
                    finally:
                        await sentence_queue.put(None)   # sentinel

                collector = asyncio.create_task(_collect_sentences())

                try:
                    while True:
                        sentence = await sentence_queue.get()
                        if sentence is None:
                            break

                        _set_device_state("speaking")
                        await broadcast({"event": "state", "state": "speaking"})

                        # TTS runs in a thread; the collector task continues
                        # generating + enqueuing the next sentence concurrently.
                        pcm = await run_tts(sentence)

                        try:
                            await websocket.send(pcm)
                        except websockets.ConnectionClosed:
                            log.info("[device] Disconnected during TTS  %s", addr)
                            collector.cancel()
                            return
                finally:
                    # Ensure collector is always cleaned up.
                    if not collector.done():
                        collector.cancel()
                    try:
                        await collector
                    except asyncio.CancelledError:
                        pass

                _set_device_state("listening")
                await broadcast({"event": "state", "state": "listening"})
                log.info("[device] Turn complete — listening")

            except Exception:
                log.exception("[device] Pipeline error")
                _set_device_state("listening")
                await broadcast({"event": "state", "state": "listening"})
            finally:
                vad.reset()

    except websockets.ConnectionClosed:
        pass
    finally:
        _set_device_state("disconnected")
        await broadcast({"event": "state", "state": "disconnected"})
        log.info("[device] Disconnected  %s", addr)


# ── WebSocket router ──────────────────────────────────────────

async def _ws_router(websocket: WebSocketServerProtocol) -> None:
    path = websocket.request.path
    if path == "/device":
        await handle_device(websocket)
    elif path == "/ui":
        await handle_ui(websocket)
    else:
        log.warning("ws: unknown path %r — rejecting", path)
        await websocket.close(1008, f"Unknown path: {path}")


# ── HTTP server (serves ui/index.html) ────────────────────────

async def _http_handler(
    reader: asyncio.StreamReader,
    writer: asyncio.StreamWriter,
) -> None:
    peer = writer.get_extra_info("peername")
    try:
        req = await asyncio.wait_for(reader.readline(), timeout=5.0)
        # Drain the remaining headers
        while True:
            line = await asyncio.wait_for(reader.readline(), timeout=5.0)
            if line in (b"\r\n", b"\n", b""):
                break

        parts  = req.decode(errors="replace").split()
        method = parts[0] if parts else "GET"
        path   = parts[1] if len(parts) > 1 else "/"
        log.info("[http] %s %s  %s", method, path, peer)

        if method != "GET" or path not in ("/", "/index.html"):
            body = b"Not Found"
            writer.write(
                b"HTTP/1.1 404 Not Found\r\n"
                b"Content-Type: text/plain\r\n"
                b"Content-Length: " + str(len(body)).encode() + b"\r\n"
                b"Connection: close\r\n\r\n" + body
            )
            return

        if not UI_FILE.is_file():
            log.error("[http] ui/index.html not found at %s", UI_FILE)
            body = b"ui/index.html not found"
            writer.write(
                b"HTTP/1.1 500 Internal Server Error\r\n"
                b"Content-Type: text/plain\r\n"
                b"Content-Length: " + str(len(body)).encode() + b"\r\n"
                b"Connection: close\r\n\r\n" + body
            )
            return

        body = UI_FILE.read_bytes()
        writer.write(
            b"HTTP/1.1 200 OK\r\n"
            b"Content-Type: text/html; charset=utf-8\r\n"
            b"Content-Length: " + str(len(body)).encode() + b"\r\n"
            b"Connection: close\r\n\r\n" + body
        )

    except (asyncio.TimeoutError, ConnectionResetError) as exc:
        log.debug("[http] %s  %s", exc, peer)
    finally:
        try:
            await writer.drain()
        except Exception:
            pass
        writer.close()


# ── Entry point ───────────────────────────────────────────────

async def main() -> None:
    log.info("Pre-warming STT and TTS models …")
    await asyncio.to_thread(prewarm_models)
    log.info("Models ready")

    ws_server   = await websockets.serve(_ws_router, "0.0.0.0", settings.ws_port)
    http_server = await asyncio.start_server(_http_handler, "0.0.0.0", settings.http_port)

    log.info("WebSocket  ws://0.0.0.0:%d  (/device · /ui)", settings.ws_port)
    log.info("HTTP       http://0.0.0.0:%d  →  ui/index.html", settings.http_port)

    async with ws_server, http_server:
        await asyncio.gather(
            ws_server.wait_closed(),
            http_server.serve_forever(),
        )


if __name__ == "__main__":
    asyncio.run(main())
