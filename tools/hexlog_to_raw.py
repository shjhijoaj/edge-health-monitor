#!/usr/bin/env python3
"""Convert a text hex log of serial output into a replayable raw frame file.

Online Cortex-M simulators (Wokwi, Renode logs, Arduino serial monitors, or any
terminal capture) usually give you *text*. This tool turns that text back into
the binary frame stream that ``serial_bridge.py --replay`` understands, and
validates every frame's CRC while doing it.

Accepted input shapes, mixed freely in one file::

    A5 5A 01 01 01 00 16 00 ...      (space separated)
    A55A01010001001600...            (continuous hex)
    [12:00:01.001] TX: a5 5a 01 01  (with timestamps and prefixes)

Usage::

    python tools/hexlog_to_raw.py --input serial-log.txt --output data/capture.ehraw
    python tools/hexlog_to_raw.py --input serial-log.txt --dump
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from serial_bridge import FrameReader, decode_frame, parse_hex_line  # noqa: E402


def extract_bytes(text: str) -> tuple[bytes, int, int]:
    """Pull hex bytes out of frame dump lines, ignoring other log output."""
    stream = bytearray()
    consumed = 0
    skipped = 0
    for line in text.splitlines():
        # Only frame dump lines are parsed. Text lines such as "STAT frames=120"
        # are ignored, so a decimal number is never mistaken for a hex byte.
        chunk = parse_hex_line(line)
        if chunk is None:
            if line.strip():
                skipped += 1
            continue
        stream.extend(chunk)
        consumed += len(chunk)
    return bytes(stream), consumed, skipped


def main() -> int:
    parser = argparse.ArgumentParser(description="Hex serial log -> raw frame file")
    parser.add_argument("--input", type=Path, action="append", required=True,
                        help="log file, repeat the flag to merge several files")
    parser.add_argument("--output", type=Path, default=Path("data/capture.ehraw"))
    parser.add_argument("--dump", action="store_true",
                        help="also print the decoded samples")
    args = parser.parse_args()

    text = ""
    for path in args.input:
        try:
            text += path.read_text(encoding="utf-8", errors="replace") + "\n"
        except OSError as error:
            print(f"cannot read {path}: {error}", file=sys.stderr)
            return 1

    stream, consumed, skipped = extract_bytes(text)
    if not stream:
        print(f"no frame lines found in the input ({skipped} non-frame lines skipped)",
              file=sys.stderr)
        print("expected lines like:  FRAME A5 5A 01 01 ...", file=sys.stderr)
        return 2

    reader = FrameReader()
    frames = reader.feed(stream)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(b"".join(frames))

    print(f"frame lines      : {consumed // 32} (hex bytes {consumed})")
    print(f"other log lines  : {skipped}")
    print(f"frames recovered : {len(frames)}")
    print(f"bad frames       : {reader.bad_frames}")
    print(f"written          : {args.output} ({args.output.stat().st_size} bytes)")

    if args.dump:
        for frame in frames:
            sample = decode_frame(frame)
            print(
                f"#{sample['sequence']:<6} {sample['temperature_c']:>6.2f} C  "
                f"{sample['current_ma']:>5} mA  {sample['vibration_rms_mg']:>4} mg  "
                f"status={sample['status']}"
            )

    if not frames:
        print("hint: check the baud rate and that firmware prints the full frame", file=sys.stderr)
        return 3
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
