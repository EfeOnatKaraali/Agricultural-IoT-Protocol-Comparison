"""Local agricultural IoT receiver. Run: py iot_http_receiver.py

Uses only Python's standard library. GET / displays receiver status.
POST /sensor accepts JSON and appends validated readings to a CSV file.
This is the first connectivity test, not a protocol benchmark harness.
"""

import argparse
import csv
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import math
from pathlib import Path
import threading


FIELDS = [
    "received_at_utc", "protocol", "device_id", "sequence", "uptime_ms",
    "temperature_c", "humidity_pct", "pressure_hpa",
]
MAX_BODY_BYTES = 65536


def validate_reading(data):
    if not isinstance(data, dict):
        raise ValueError("JSON bir nesne olmali.")
    device_id = data.get("device_id")
    if not isinstance(device_id, str) or not device_id.strip() or len(device_id) > 64:
        raise ValueError("device_id: 1-64 karakterlik bir metin olmali.")
    for field in ("sequence", "uptime_ms"):
        if type(data.get(field)) is not int or data[field] < 0:
            raise ValueError(f"{field}: negatif olmayan tam sayi olmali.")
    for field in ("temperature_c", "humidity_pct", "pressure_hpa"):
        value = data.get(field)
        if type(value) not in (int, float) or not math.isfinite(value):
            raise ValueError(f"{field}: gecerli bir sayi olmali.")
    if not 0 <= data["humidity_pct"] <= 100:
        raise ValueError("humidity_pct: 0 ile 100 arasinda olmali.")


class ReceiverServer(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, address, output):
        self.output = Path(output).resolve()
        self.output.parent.mkdir(parents=True, exist_ok=True)
        if self.output.exists() and self.output.stat().st_size:
            with self.output.open(newline="", encoding="utf-8") as stream:
                if next(csv.reader(stream), None) != FIELDS:
                    raise ValueError("Mevcut CSV basliklari uyusmuyor; farkli --output sec.")
        else:
            with self.output.open("w", newline="", encoding="utf-8") as stream:
                csv.DictWriter(stream, fieldnames=FIELDS).writeheader()
        self.state_lock = threading.Lock()
        self.received_count = 0
        self.last_reading = None
        super().__init__(address, ReceiverHandler)

    def store_reading(self, data):
        row = {
            "received_at_utc": datetime.now(timezone.utc).isoformat(timespec="milliseconds"),
            "protocol": "HTTP",
            **{field: data[field] for field in FIELDS[2:]},
        }
        with self.state_lock:
            with self.output.open("a", newline="", encoding="utf-8") as stream:
                csv.DictWriter(stream, fieldnames=FIELDS).writerow(row)
            self.received_count += 1
            self.last_reading = row
        return row


class ReceiverHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def respond(self, code, content, content_type="application/json"):
        if content_type == "application/json":
            body = json.dumps(content, ensure_ascii=False, allow_nan=False).encode("utf-8")
        else:
            body = content.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", content_type + "; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("Connection", "close")
        self.end_headers()
        self.close_connection = True
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def do_GET(self):
        if self.path not in ("/", "/health"):
            self.respond(404, {"ok": False, "error": "Adres bulunamadi."})
            return
        with self.server.state_lock:
            status = {
                "status": "ready", "received_this_run": self.server.received_count,
                "last_reading": self.server.last_reading,
            }
        if self.path == "/health":
            self.respond(200, status)
            return
        last = json.dumps(status["last_reading"], ensure_ascii=False, indent=2)
        message = (
            "HTTP alicisi calisiyor.\n"
            f"Bu oturumda alinan mesaj: {status['received_this_run']}\n"
            "ESP32 veri adresi: /sensor (POST)\n\n"
            + ("Henuz sensor verisi gelmedi.\n" if status["last_reading"] is None
               else "Son alinan veri:\n" + last + "\n")
        )
        self.respond(200, message, "text/plain")

    def do_POST(self):
        if self.path != "/sensor":
            self.respond(404, {"ok": False, "error": "POST adresi /sensor olmali."})
            return
        if self.headers.get_content_type() != "application/json":
            self.respond(415, {"ok": False, "error": "Content-Type application/json olmali."})
            return
        if self.headers.get("Content-Length") is None:
            self.respond(411, {"ok": False, "error": "Content-Length gerekli."})
            return
        try:
            length = int(self.headers["Content-Length"])
        except ValueError:
            self.respond(400, {"ok": False, "error": "Gecersiz Content-Length."})
            return
        if length < 1 or length > MAX_BODY_BYTES:
            self.respond(413, {"ok": False, "error": "Mesaj boyutu gecersiz."})
            return
        self.connection.settimeout(10)
        try:
            body = self.rfile.read(length)
            if len(body) != length:
                raise ValueError("Eksik mesaj govdesi.")
            data = json.loads(body)
        except (ValueError, UnicodeError, TimeoutError) as exc:
            self.respond(400, {"ok": False, "error": str(exc)})
            return
        try:
            validate_reading(data)
        except ValueError as exc:
            self.respond(422, {"ok": False, "error": str(exc)})
            return
        try:
            row = self.server.store_reading(data)
        except OSError as exc:
            print(f"CSV yazilamadi: {exc}", flush=True)
            self.respond(500, {"ok": False, "error": "CSV kaydi basarisiz."})
            return
        print(
            f"ALINDI | cihaz={row['device_id']} | sira={row['sequence']} | "
            f"sicaklik={row['temperature_c']} C | nem={row['humidity_pct']} % | "
            f"basinc={row['pressure_hpa']} hPa", flush=True,
        )
        self.respond(200, {"ok": True, "sequence": data["sequence"]})

    def log_message(self, format, *args):
        # Accepted readings are printed above; keep browser health checks quiet.
        pass


def main():
    parser = argparse.ArgumentParser(description="ESP32 HTTP sensor alicisi")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=5000)
    parser.add_argument("--output", type=Path,
                        default=Path(__file__).resolve().parent / "data" / "http_readings.csv")
    args = parser.parse_args()
    try:
        server = ReceiverServer((args.host, args.port), args.output)
    except (OSError, ValueError) as exc:
        print(f"Sunucu baslatilamadi: {exc}")
        return 1
    print(f"HTTP alicisi hazir. Dinlenen port: {server.server_address[1]}", flush=True)
    print(f"Tarayici testi: http://127.0.0.1:{server.server_address[1]}/", flush=True)
    print(f"CSV dosyasi: {server.output}", flush=True)
    print("Bu pencere acik kalsin. Durdurmak icin Ctrl+C.", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nAlici durduruldu.")
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
