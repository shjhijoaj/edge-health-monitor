#!/usr/bin/env python3
"""Render a dependency-free SVG chart from telemetry, for documentation.

Accepts either a raw protocol frame capture (``*.ehraw``, as produced by the
simulator or by ``hexlog_to_raw.py``) or the CSV written by the gateway.

Usage::

    python tools/make_chart.py --input docs/evidence/watchdog-frames.ehraw --output docs/evidence/watchdog-telemetry.svg
    python tools/make_chart.py --input data/telemetry.csv --output docs/evidence/panel-telemetry.svg
"""

from __future__ import annotations

import argparse
import csv
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from serial_bridge import FrameReader, decode_frame  # noqa: E402

WIDTH = 900
HEIGHT = 420
MARGIN_LEFT = 64
MARGIN_RIGHT = 24
MARGIN_TOP = 28
MARGIN_BOTTOM = 42


def load_raw(path: Path) -> list[dict]:
    reader = FrameReader()
    frames = reader.feed(path.read_bytes())
    return [decode_frame(frame) for frame in frames]


def load_csv(path: Path) -> list[dict]:
    rows = []
    with path.open("r", encoding="utf-8", newline="") as stream:
        for raw in csv.DictReader(stream):
            try:
                rows.append(
                    {
                        "sequence": int(raw["sequence"]),
                        "temperature_c": int(raw["temperature_centi_c"]) / 100.0,
                        "current_ma": int(raw["current_ma"]),
                        "vibration_rms_mg": int(raw["vibration_rms_mg"]),
                    }
                )
            except (KeyError, TypeError, ValueError):
                continue
    return rows


def polyline(values: list[float], maximum: float, minimum: float = 0.0) -> str:
    if not values:
        return ""
    plot_width = WIDTH - MARGIN_LEFT - MARGIN_RIGHT
    plot_height = HEIGHT - MARGIN_TOP - MARGIN_BOTTOM
    span = max(maximum - minimum, 1e-9)
    step = plot_width / max(len(values) - 1, 1)
    points = []
    for index, value in enumerate(values):
        x = MARGIN_LEFT + index * step
        y = MARGIN_TOP + plot_height * (1.0 - (value - minimum) / span)
        points.append(f"{x:.1f},{y:.1f}")
    return " ".join(points)


def build_svg(rows: list[dict], title: str, source: str) -> str:
    temperatures = [row["temperature_c"] for row in rows]
    currents = [row["current_ma"] for row in rows]
    vibrations = [row["vibration_rms_mg"] for row in rows]

    plot_height = HEIGHT - MARGIN_TOP - MARGIN_BOTTOM
    grid = []
    for step in range(5):
        y = MARGIN_TOP + plot_height * step / 4
        value = 100 * (1 - step / 4)
        grid.append(f'<line x1="{MARGIN_LEFT}" y1="{y:.1f}" x2="{WIDTH - MARGIN_RIGHT}" y2="{y:.1f}" stroke="#26354d" stroke-width="1"/>')
        grid.append(f'<text x="{MARGIN_LEFT - 10}" y="{y + 4:.1f}" fill="#8fa6c4" font-size="11" text-anchor="end">{value:.0f}%</text>')

    temp_points = polyline(temperatures, max(temperatures + [1]), min(temperatures + [0]))
    current_points = polyline([value / max(currents + [1]) * max(temperatures + [1]) for value in currents],
                              max(temperatures + [1]), 0)
    vibration_points = polyline([value / max(vibrations + [1]) * max(temperatures + [1]) for value in vibrations],
                                max(temperatures + [1]), 0)

    threshold_y = MARGIN_TOP + plot_height * (1 - 80.0 / max(temperatures + [1]))

    return f'''<svg xmlns="http://www.w3.org/2000/svg" width="{WIDTH}" height="{HEIGHT}" viewBox="0 0 {WIDTH} {HEIGHT}" role="img">
  <rect width="{WIDTH}" height="{HEIGHT}" fill="#0d1526"/>
  <text x="{MARGIN_LEFT}" y="18" fill="#eaf2ff" font-size="14" font-family="Segoe UI, sans-serif">{title}</text>
  <text x="{WIDTH - MARGIN_RIGHT}" y="18" fill="#8fa6c4" font-size="11" text-anchor="end" font-family="Segoe UI, sans-serif">{source} · {len(rows)} samples</text>
  {''.join(grid)}
  <line x1="{MARGIN_LEFT}" y1="{threshold_y:.1f}" x2="{WIDTH - MARGIN_RIGHT}" y2="{threshold_y:.1f}" stroke="#f6c453" stroke-width="1" stroke-dasharray="6 4"/>
  <text x="{WIDTH - MARGIN_RIGHT}" y="{threshold_y - 6:.1f}" fill="#f6c453" font-size="11" text-anchor="end" font-family="Segoe UI, sans-serif">temperature limit 80.00 C</text>
  <polyline points="{temp_points}" fill="none" stroke="#3dd6c6" stroke-width="2"/>
  <polyline points="{current_points}" fill="none" stroke="#7aa2ff" stroke-width="1.5" opacity="0.85"/>
  <polyline points="{vibration_points}" fill="none" stroke="#fb7185" stroke-width="1.5" opacity="0.85"/>
  <g font-family="Segoe UI, sans-serif" font-size="12">
    <rect x="{MARGIN_LEFT}" y="{HEIGHT - 30}" width="10" height="10" fill="#3dd6c6"/>
    <text x="{MARGIN_LEFT + 16}" y="{HEIGHT - 21}" fill="#c9d8ee">temperature ({min(temperatures):.1f} - {max(temperatures):.1f} C)</text>
    <rect x="{MARGIN_LEFT + 260}" y="{HEIGHT - 30}" width="10" height="10" fill="#7aa2ff"/>
    <text x="{MARGIN_LEFT + 276}" y="{HEIGHT - 21}" fill="#c9d8ee">current (max {max(currents)} mA)</text>
    <rect x="{MARGIN_LEFT + 500}" y="{HEIGHT - 30}" width="10" height="10" fill="#fb7185"/>
    <text x="{MARGIN_LEFT + 516}" y="{HEIGHT - 21}" fill="#c9d8ee">vibration (max {max(vibrations)} mg)</text>
  </g>
</svg>
'''


def main() -> int:
    parser = argparse.ArgumentParser(description="Telemetry -> SVG chart")
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--title", default="Edge Health Monitor telemetry")
    args = parser.parse_args()

    if not args.input.exists():
        print(f"input not found: {args.input}", file=sys.stderr)
        return 1

    rows = load_raw(args.input) if args.input.suffix == ".ehraw" else load_csv(args.input)
    if not rows:
        print("no telemetry rows found", file=sys.stderr)
        return 2

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(build_svg(rows, args.title, args.input.name), encoding="utf-8")
    print(f"wrote {args.output} ({len(rows)} samples)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
