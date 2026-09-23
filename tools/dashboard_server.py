#!/usr/bin/env python3
"""Small local dashboard for the Edge Health Monitor telemetry CSV."""

from __future__ import annotations

import argparse
import csv
import json
import mimetypes
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse


DEFAULT_THRESHOLDS = {
    "temperature_c": 80.0,
    "current_ma": 2500,
    "vibration_rms_mg": 500,
}

DEFAULT_ONLINE_TIMEOUT_S = 5.0
DEFAULT_CONFIG_PATH = Path(__file__).resolve().parent.parent / "config" / "thresholds.cfg"


def load_thresholds(path: Path) -> tuple[dict, float]:
    """Read config/thresholds.cfg so the panel and the C++ gateway agree."""
    thresholds = dict(DEFAULT_THRESHOLDS)
    timeout_s = DEFAULT_ONLINE_TIMEOUT_S
    if not path.exists():
        return thresholds, timeout_s

    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.split("#", 1)[0].strip()
        if "=" not in line:
            continue
        key, value = (part.strip() for part in line.split("=", 1))
        try:
            number = int(value)
        except ValueError:
            continue
        if key == "temperature_max_centi_c":
            thresholds["temperature_c"] = number / 100.0
        elif key == "current_max_ma":
            thresholds["current_ma"] = number
        elif key == "vibration_max_mg":
            thresholds["vibration_rms_mg"] = number
        elif key == "online_timeout_s":
            timeout_s = float(number)
    return thresholds, timeout_s


class TelemetryStore:
    def __init__(self, csv_path: Path, thresholds: dict | None = None, online_timeout_s: float = DEFAULT_ONLINE_TIMEOUT_S) -> None:
        self.csv_path = csv_path
        self.thresholds = thresholds if thresholds is not None else dict(DEFAULT_THRESHOLDS)
        self.online_timeout_s = online_timeout_s
        self._lock = threading.Lock()

    def rows(self) -> list[dict]:
        if not self.csv_path.exists():
            return []
        result = []
        with self._lock, self.csv_path.open("r", encoding="utf-8", newline="") as stream:
            for raw in csv.DictReader(stream):
                try:
                    result.append(
                        {
                            "sequence": int(raw["sequence"]),
                            "temperature_c": int(raw["temperature_centi_c"]) / 100.0,
                            "humidity_pct": int(raw["humidity_centi_pct"]) / 100.0,
                            "current_ma": int(raw["current_ma"]),
                            "vibration_rms_mg": int(raw["vibration_rms_mg"]),
                            "status": int(raw["status"]),
                            "uptime_s": int(raw["uptime_s"]),
                        }
                    )
                except (KeyError, TypeError, ValueError):
                    continue
        return result

    def append(self, sample: dict) -> None:
        self.csv_path.parent.mkdir(parents=True, exist_ok=True)
        with self._lock, self.csv_path.open("a", encoding="utf-8", newline="") as stream:
            writer = csv.DictWriter(
                stream,
                fieldnames=[
                    "sequence",
                    "temperature_centi_c",
                    "humidity_centi_pct",
                    "current_ma",
                    "vibration_rms_mg",
                    "status",
                    "uptime_s",
                ],
            )
            if self.csv_path.stat().st_size == 0:
                writer.writeheader()
            writer.writerow(sample)

    @staticmethod
    def normalize_sample(payload: dict) -> dict:
        def number(name: str, fallback: object = None) -> object:
            value = payload.get(name, fallback)
            if value is None:
                raise ValueError(f"missing field: {name}")
            return value

        sequence = int(number("sequence"))
        temperature = payload.get("temperature_c")
        if temperature is None:
            temperature = int(number("temperature_centi_c")) / 100.0
        humidity = payload.get("humidity_pct")
        if humidity is None:
            humidity = int(number("humidity_centi_pct")) / 100.0
        sample = {
            "sequence": sequence,
            "temperature_centi_c": round(float(temperature) * 100),
            "humidity_centi_pct": round(float(humidity) * 100),
            "current_ma": int(number("current_ma")),
            "vibration_rms_mg": int(number("vibration_rms_mg")),
            "status": int(payload.get("status", 0)),
            "uptime_s": int(number("uptime_s", 0)),
        }
        if not 0 <= sequence <= 65535:
            raise ValueError("sequence must be between 0 and 65535")
        if sample["current_ma"] < 0 or sample["vibration_rms_mg"] < 0:
            raise ValueError("current and vibration cannot be negative")
        return sample

    def alerts(self, rows: list[dict]) -> list[dict]:
        alerts = []
        for row in rows:
            if row["temperature_c"] > self.thresholds["temperature_c"]:
                alerts.append({"sequence": row["sequence"], "level": "warning",
                               "message": f"温度超过 {self.thresholds['temperature_c']:.2f}°C"})
            if row["current_ma"] > self.thresholds["current_ma"]:
                alerts.append({"sequence": row["sequence"], "level": "critical",
                               "message": f"电流超过 {self.thresholds['current_ma']}mA"})
            if row["vibration_rms_mg"] > self.thresholds["vibration_rms_mg"]:
                alerts.append({"sequence": row["sequence"], "level": "critical",
                               "message": f"振动 RMS 超过 {self.thresholds['vibration_rms_mg']}mg"})
            if row["status"] != 0:
                alerts.append({"sequence": row["sequence"], "level": "critical", "message": "传感器状态异常"})
        return alerts

    def summary(self) -> dict:
        rows = self.rows()
        alerts = self.alerts(rows)
        latest = rows[-1] if rows else None
        fresh = False
        if latest and self.csv_path.exists():
            fresh = time.time() - self.csv_path.stat().st_mtime <= self.online_timeout_s
        return {
            "device": "EHM-DEV-001",
            "online": fresh,
            "last_seen_age_s": round(time.time() - self.csv_path.stat().st_mtime, 1) if latest and self.csv_path.exists() else None,
            "sample_count": len(rows),
            "alert_count": len(alerts),
            "thresholds": self.thresholds,
            "online_timeout_s": self.online_timeout_s,
            "latest": latest,
            "max_temperature_c": max((row["temperature_c"] for row in rows), default=None),
            "max_current_ma": max((row["current_ma"] for row in rows), default=None),
            "max_vibration_rms_mg": max((row["vibration_rms_mg"] for row in rows), default=None),
        }


class DashboardHandler(BaseHTTPRequestHandler):
    store: TelemetryStore
    dashboard_dir: Path

    def log_message(self, format: str, *args: object) -> None:
        # Keep the terminal readable while the dashboard refreshes every two seconds.
        return

    def _json(self, payload: object, status: int = 200) -> None:
        data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def _file(self, path: Path) -> None:
        try:
            data = path.read_bytes()
        except OSError:
            self._json({"error": "file not found"}, 404)
            return
        content_type = mimetypes.guess_type(path.name)[0] or "application/octet-stream"
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self) -> None:  # noqa: N802 - required by BaseHTTPRequestHandler
        route = urlparse(self.path).path
        if route == "/api/health":
            self._json({"status": "ok", "service": "edge-health-dashboard"})
            return
        if route == "/api/summary":
            self._json(self.store.summary())
            return
        if route == "/api/telemetry":
            rows = self.store.rows()
            self._json({"items": rows[-100:], "alerts": self.store.alerts(rows)[-100:]})
            return

        filename = "index.html" if route == "/" else route.lstrip("/")
        candidate = (self.dashboard_dir / filename).resolve()
        if self.dashboard_dir not in candidate.parents and candidate != self.dashboard_dir:
            self._json({"error": "invalid path"}, 400)
            return
        self._file(candidate)

    def do_POST(self) -> None:  # noqa: N802 - required by BaseHTTPRequestHandler
        route = urlparse(self.path).path
        if route != "/api/ingest":
            self._json({"error": "route not found"}, 404)
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
            payload = json.loads(self.rfile.read(length).decode("utf-8"))
            if not isinstance(payload, dict):
                raise ValueError("JSON body must be an object")
            sample = self.store.normalize_sample(payload)
            self.store.append(sample)
        except (ValueError, TypeError, json.JSONDecodeError) as error:
            self._json({"error": str(error)}, 400)
            return
        self._json({"accepted": True, "sample": sample, "summary": self.store.summary()}, 201)


def main() -> None:
    parser = argparse.ArgumentParser(description="Serve the local Edge Health Monitor dashboard")
    parser.add_argument("--csv", type=Path, default=Path("telemetry.csv"), help="telemetry CSV path")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--config", type=Path, default=DEFAULT_CONFIG_PATH,
                        help="thresholds configuration file")
    args = parser.parse_args()

    thresholds, online_timeout_s = load_thresholds(args.config)
    store = TelemetryStore(args.csv.resolve(), thresholds, online_timeout_s)
    handler = type("ConfiguredDashboardHandler", (DashboardHandler,), {})
    handler.store = store
    handler.dashboard_dir = (Path(__file__).resolve().parent.parent / "dashboard").resolve()
    server = ThreadingHTTPServer((args.host, args.port), handler)
    print(f"Edge Health Monitor dashboard: http://{args.host}:{args.port}/")
    print(f"Telemetry source: {store.csv_path}")
    print(f"Thresholds: {args.config} -> temperature {thresholds['temperature_c']} C, "
          f"current {thresholds['current_ma']} mA, vibration {thresholds['vibration_rms_mg']} mg, "
          f"offline after {online_timeout_s:g} s")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nDashboard stopped")
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
