#!/usr/bin/env python3
"""Decode Edge Health Monitor frames from a serial port, socket or capture file.

Three input modes are supported:

* ``--replay FILE`` reads a raw frame capture produced by
  ``edge_health_sim --raw FILE``. This works without any hardware.
* ``--port COM3 --baud 115200`` reads live bytes from a serial adapter. This
  mode needs ``pyserial`` (``pip install pyserial``).
* ``--tcp HOST:PORT`` reads live bytes from a socket, which is how an emulator
  or a network serial server can be connected without any hardware.

Decoded samples are sent to the local dashboard through ``POST /api/ingest``.
Live sources reconnect automatically when the cable is unplugged, the adapter
is reset, or the emulator is restarted.
"""

from __future__ import annotations

import argparse
import json
import re
import socket
import struct
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

SYNC = b"\xA5\x5A"
VERSION = 1
FRAME_TYPE_SAMPLE = 0x01
HEADER_SIZE = 8
PAYLOAD_SIZE = 22
FRAME_SIZE = HEADER_SIZE + PAYLOAD_SIZE + 2

# Firmware prints frames as hex text ("FRAME A5 5A 01 01 ..."), so a live text
# source needs the same parsing rules the offline log converter uses.
TIMESTAMP = r"(?:\[[^\]]*\]\s*)?"
LABEL = r"(?:FRAME|TX|RX|UART)?\s*:?\s*"
SPACED_HEX = re.compile(rf"^{TIMESTAMP}{LABEL}((?:[0-9A-Fa-f]{{2}}[ \t]+)+[0-9A-Fa-f]{{2}})\s*$")
CONTINUOUS_HEX = re.compile(rf"^{TIMESTAMP}{LABEL}((?:[0-9A-Fa-f]{{2}}){{8,}})\s*$")


def crc16_modbus(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def parse_hex_line(line: str) -> bytes | None:
    """Return the bytes from a hex dump line, or None if the line is not one."""
    match = SPACED_HEX.match(line) or CONTINUOUS_HEX.match(line)
    if match is None:
        return None
    return bytes.fromhex(match.group(1).replace(" ", "").replace("\t", ""))


def decode_frame(frame: bytes) -> dict:
    if len(frame) != FRAME_SIZE:
        raise ValueError(f"frame must be {FRAME_SIZE} bytes, got {len(frame)}")
    if frame[0:2] != SYNC:
        raise ValueError("bad sync bytes")
    if frame[2] != VERSION:
        raise ValueError(f"unsupported protocol version {frame[2]}")
    if frame[3] != FRAME_TYPE_SAMPLE:
        raise ValueError(f"unsupported message type {frame[3]}")
    length = struct.unpack_from("<H", frame, 6)[0]
    if length != PAYLOAD_SIZE:
        raise ValueError(f"unexpected payload length {length}")
    expected_crc = struct.unpack_from("<H", frame, FRAME_SIZE - 2)[0]
    actual_crc = crc16_modbus(frame[:-2])
    if expected_crc != actual_crc:
        raise ValueError(f"CRC mismatch: expected 0x{expected_crc:04X}, got 0x{actual_crc:04X}")

    sequence = struct.unpack_from("<H", frame, 4)[0]
    temperature, humidity, current, vibration = struct.unpack_from("<iiii", frame, 8)
    status = struct.unpack_from("<H", frame, 24)[0]
    uptime = struct.unpack_from("<I", frame, 26)[0]
    return {
        "sequence": sequence,
        "temperature_c": round(temperature / 100.0, 2),
        "humidity_pct": round(humidity / 100.0, 2),
        "current_ma": current,
        "vibration_rms_mg": vibration,
        "status": status,
        "uptime_s": uptime,
    }


class FrameReader:
    """Incremental frame splitter that tolerates noise before the sync bytes."""

    def __init__(self) -> None:
        self.buffer = bytearray()
        self.bad_frames = 0

    def feed(self, chunk: bytes) -> list[bytes]:
        self.buffer.extend(chunk)
        frames = []
        while True:
            start = self.buffer.find(SYNC)
            if start < 0:
                if len(self.buffer) > 1:
                    del self.buffer[:-1]
                break
            if start > 0:
                del self.buffer[:start]
            if len(self.buffer) < FRAME_SIZE:
                break
            candidate = bytes(self.buffer[:FRAME_SIZE])
            del self.buffer[:FRAME_SIZE]
            if crc16_modbus(candidate[:-2]) != struct.unpack_from("<H", candidate, FRAME_SIZE - 2)[0]:
                self.bad_frames += 1
                continue
            frames.append(candidate)
        return frames


class HexTextReader:
    """Frame extractor for sources that deliver hex text instead of raw bytes."""

    def __init__(self) -> None:
        self.buffer = bytearray()
        self.bad_frames = 0

    def feed(self, chunk: bytes) -> list[bytes]:
        self.buffer.extend(chunk)
        frames: list[bytes] = []
        while True:
            index = self.buffer.find(b"\n")
            if index < 0:
                break
            line = bytes(self.buffer[:index]).decode("ascii", "replace")
            del self.buffer[: index + 1]
            candidate = parse_hex_line(line)
            if candidate is None or len(candidate) < FRAME_SIZE:
                continue
            for offset in range(0, len(candidate) - FRAME_SIZE + 1, FRAME_SIZE):
                window = candidate[offset:offset + FRAME_SIZE]
                if window[0:2] != SYNC:
                    continue
                if crc16_modbus(window[:-2]) == struct.unpack_from("<H", window, FRAME_SIZE - 2)[0]:
                    frames.append(window)
                else:
                    self.bad_frames += 1
        # A stream without newlines must not grow without bound.
        if len(self.buffer) > 8192:
            del self.buffer[:-256]
        return frames

    @staticmethod
    def looks_like_text(chunk: bytes) -> bool:
        return all(byte in (9, 10, 13) or 0x20 <= byte < 0x7F for byte in chunk[:64])


def post_sample(endpoint: str, sample: dict) -> dict:
    body = json.dumps(sample).encode("utf-8")
    request = urllib.request.Request(
        endpoint,
        data=body,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    with urllib.request.urlopen(request, timeout=5) as response:
        return json.loads(response.read().decode("utf-8"))


def replay(path: Path, endpoint: str, delay: float) -> int:
    reader = FrameReader()
    sent = 0
    data = path.read_bytes()
    for frame in reader.feed(data):
        sample = decode_frame(frame)
        result = post_sample(endpoint, sample)
        sent += 1
        print(
            f"#{sample['sequence']:<6} {sample['temperature_c']:>6.2f} C  "
            f"{sample['current_ma']:>5} mA  {sample['vibration_rms_mg']:>4} mg  "
            f"status={sample['status']}  alerts={result['summary']['alert_count']}"
        )
        if delay:
            time.sleep(delay)
    print(f"replayed {sent} frames, bad frames: {reader.bad_frames}")
    return 0 if sent else 1


def describe(sample: dict) -> str:
    return (
        f"#{sample['sequence']:<6} {sample['temperature_c']:>6.2f} C  "
        f"{sample['current_ma']:>5} mA  {sample['vibration_rms_mg']:>4} mg  "
        f"status={sample['status']}"
    )


def run_live(read, source_name: str, endpoint: str, reconnect_delay: float,
             max_seconds: float | None, data_format: str = "auto") -> int:
    """Consume bytes from a live source, reconnecting until interrupted."""
    reader = None
    frames = 0
    deadline = time.time() + max_seconds if max_seconds else None
    try:
        while deadline is None or time.time() < deadline:
            try:
                chunk = read()
            except Exception as error:  # serial or socket errors
                print(f"[{source_name}] connection lost ({error}); "
                      f"retrying in {reconnect_delay:g} s", file=sys.stderr, flush=True)
                time.sleep(reconnect_delay)
                continue
            if not chunk:
                continue
            if reader is None:
                if data_format == "hex":
                    reader = HexTextReader()
                elif data_format == "binary":
                    reader = FrameReader()
                else:  # auto-detect from the first bytes of the stream
                    reader = HexTextReader() if HexTextReader.looks_like_text(chunk) else FrameReader()
                    print(f"[{source_name}] detected {'hex text' if isinstance(reader, HexTextReader) else 'binary'} stream",
                          flush=True)
            for frame in reader.feed(chunk):
                sample = decode_frame(frame)
                post_sample(endpoint, sample)
                frames += 1
                print(describe(sample), flush=True)
    except KeyboardInterrupt:
        print("interrupted")
    bad = reader.bad_frames if reader is not None else 0
    print(f"stopped after {frames} frames, bad frames: {bad}")
    return 0 if frames else 1


def stream_serial(port: str, baud: int, endpoint: str, reconnect_delay: float,
                  max_seconds: float | None, data_format: str = "auto") -> int:
    try:
        import serial  # type: ignore
    except ImportError:
        print("pyserial is required for live serial mode: pip install pyserial", file=sys.stderr)
        return 2

    state = {"device": None}

    def read() -> bytes:
        if state["device"] is None:
            state["device"] = serial.Serial(port, baud, timeout=1)
            print(f"opened {port} at {baud} baud", flush=True)
        device = state["device"]
        try:
            return device.read(256) or b""
        except Exception:
            device.close()
            state["device"] = None
            raise

    return run_live(read, port, endpoint, reconnect_delay, max_seconds, data_format)


def stream_tcp(address: str, endpoint: str, reconnect_delay: float,
               max_seconds: float | None, data_format: str = "auto") -> int:
    if ":" not in address:
        print("--tcp expects HOST:PORT", file=sys.stderr)
        return 2
    host, port_text = address.rsplit(":", 1)
    port = int(port_text)

    state = {"socket": None}

    def read() -> bytes:
        if state["socket"] is None:
            state["socket"] = socket.create_connection((host, port), timeout=5)
            state["socket"].settimeout(1)
            print(f"connected to tcp {host}:{port}", flush=True)
        connection = state["socket"]
        try:
            chunk = connection.recv(256)
        except socket.timeout:
            return b""
        except Exception:
            connection.close()
            state["socket"] = None
            raise
        if chunk == b"":
            connection.close()
            state["socket"] = None
            raise OSError("peer closed the connection")
        return chunk

    return run_live(read, f"tcp {address}", endpoint, reconnect_delay, max_seconds, data_format)


def main() -> int:
    parser = argparse.ArgumentParser(description="Feed device frames into the local dashboard")
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--replay", type=Path, help="raw frame capture file")
    source.add_argument("--port", help="serial port, for example COM3")
    source.add_argument("--tcp", help="live byte stream as HOST:PORT")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--endpoint", default="http://127.0.0.1:8765/api/ingest")
    parser.add_argument("--delay", type=float, default=0.0, help="seconds between replayed frames")
    parser.add_argument("--reconnect-delay", type=float, default=3.0,
                        help="seconds to wait before reopening a live source")
    parser.add_argument("--max-seconds", type=float, default=None,
                        help="stop a live source after this many seconds")
    parser.add_argument("--format", choices=("auto", "hex", "binary"), default="auto",
                        help="live stream format; 'hex' matches the firmware's hex dump output")
    args = parser.parse_args()

    try:
        if args.replay:
            return replay(args.replay, args.endpoint, args.delay)
        if args.tcp:
            return stream_tcp(args.tcp, args.endpoint, args.reconnect_delay, args.max_seconds,
                              args.format)
        return stream_serial(args.port, args.baud, args.endpoint, args.reconnect_delay,
                             args.max_seconds, args.format)
    except urllib.error.URLError as error:
        print(f"cannot reach dashboard at {args.endpoint}: {error}", file=sys.stderr)
        return 3
    except ValueError as error:
        print(f"protocol error: {error}", file=sys.stderr)
        return 4


if __name__ == "__main__":
    raise SystemExit(main())
