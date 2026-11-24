"""Receive the existing ESP32 MQTT JSON messages and append them to CSV.

Install: py -m pip install "paho-mqtt>=2,<3"
Run:     py iot_mqtt_receiver.py
Keep the Mosquitto broker running on port 1884.

This initial functional test uses MQTT 3.1.1 and subscription QoS 0.
The current ESP32 firmware publishes QoS 0, retained=false.
No application acknowledgement or performance metric is implemented here.
"""

import argparse
import csv
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import sys
import uuid


FIELDS = [
    "received_at_utc", "protocol", "device_id", "sequence", "uptime_ms",
    "temperature_c", "humidity_pct", "pressure_hpa",
]
DEFAULT_TOPIC = "agriculture/sensors/esp32_01"
MAX_BODY_BYTES = 65536


def validate_reading(data):
    if not isinstance(data, dict):
        raise ValueError("JSON bir nesne olmali.")
    if data.get("device_id") != "esp32_01":
        raise ValueError("device_id esp32_01 olmali.")
    for field in ("sequence", "uptime_ms"):
        if type(data.get(field)) is not int or data[field] < 0:
            raise ValueError(f"{field}: negatif olmayan tam sayi olmali.")
    for field in ("temperature_c", "humidity_pct", "pressure_hpa"):
        value = data.get(field)
        if type(value) not in (int, float) or not math.isfinite(value):
            raise ValueError(f"{field}: gecerli bir sayi olmali.")
    if not 0 <= data["humidity_pct"] <= 100:
        raise ValueError("humidity_pct 0 ile 100 arasinda olmali.")


class CsvRecorder:
    def __init__(self, output):
        self.output = Path(output).resolve()
        self.output.parent.mkdir(parents=True, exist_ok=True)
        self.count = 0
        if self.output.exists() and self.output.stat().st_size:
            with self.output.open(newline="", encoding="utf-8") as stream:
                if next(csv.reader(stream), None) != FIELDS:
                    raise ValueError("CSV basliklari uyusmuyor; farkli --output sec.")
        else:
            with self.output.open("w", newline="", encoding="utf-8") as stream:
                csv.DictWriter(stream, fieldnames=FIELDS).writeheader()

    def store(self, payload, received_at):
        if len(payload) > MAX_BODY_BYTES:
            raise ValueError("Mesaj cok buyuk.")
        data = json.loads(payload.decode("utf-8"))
        validate_reading(data)
        row = {
            "received_at_utc": received_at,
            "protocol": "MQTT",
            **{field: data[field] for field in FIELDS[2:]},
        }
        with self.output.open("a", newline="", encoding="utf-8") as stream:
            csv.DictWriter(stream, fieldnames=FIELDS).writerow(row)
        self.count += 1
        return row


def main(argv=None):
    parser = argparse.ArgumentParser(description="ESP32 MQTT olcumlerini CSV'ye kaydet.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=1884)
    parser.add_argument("--topic", default=DEFAULT_TOPIC)
    parser.add_argument("--output", type=Path,
                        default=Path(__file__).resolve().parent / "data" / "mqtt_readings.csv")
    args = parser.parse_args(argv)
    if not 1 <= args.port <= 65535:
        parser.error("Port 1 ile 65535 arasinda olmali.")
    try:
        import paho.mqtt.client as mqtt
    except ImportError:
        print('Kutuphane eksik. CMD\'de calistir: py -m pip install "paho-mqtt>=2,<3"')
        return 1
    if not hasattr(mqtt, "CallbackAPIVersion"):
        print('Paho 2.x gerekli: py -m pip install --upgrade "paho-mqtt>=2,<3"')
        return 1
    try:
        recorder = CsvRecorder(args.output)
    except (OSError, ValueError) as exc:
        print(f"CSV hazirlanamadi: {exc}")
        return 1

    state = {"fatal": False}
    client = mqtt.Client(
        callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
        client_id="iot_csv_" + uuid.uuid4().hex[:12],
        clean_session=True,
        protocol=mqtt.MQTTv311,
    )
    client.reconnect_delay_set(min_delay=1, max_delay=10)

    def on_connect(client, userdata, flags, reason_code, properties):
        if reason_code.is_failure:
            print(f"MQTT baglantisi reddedildi: {reason_code}", flush=True)
            return
        result, _ = client.subscribe(args.topic, qos=0)
        if result != mqtt.MQTT_ERR_SUCCESS:
            print(f"Abonelik istegi basarisiz: {result}", flush=True)
            state["fatal"] = True
            client.disconnect()
        else:
            print("MQTT baglandi. Abonelik onayi bekleniyor...", flush=True)

    def on_subscribe(client, userdata, mid, reason_code_list, properties):
        if not reason_code_list or any(code.is_failure for code in reason_code_list):
            print("MQTT aboneligi reddedildi.", flush=True)
            state["fatal"] = True
            client.disconnect()
            return
        print(f"KAYIT HAZIR | konu={args.topic} | QoS=0", flush=True)
        print("Bundan sonra ulasan gecerli olcumler CSV'ye eklenecek.", flush=True)

    def on_message(client, userdata, message):
        received_at = datetime.now(timezone.utc).isoformat(timespec="milliseconds")
        if message.retain:
            print("Eski retained mesaj atlandi.", flush=True)
            return
        try:
            row = recorder.store(message.payload, received_at)
        except (ValueError, UnicodeError, OverflowError) as exc:
            print(f"Gecersiz mesaj kaydedilmedi: {exc}", flush=True)
            return
        except OSError as exc:
            print(f"CSV YAZMA HATASI: {exc}. Dosya Excel'de aciksa kapat.", flush=True)
            state["fatal"] = True
            client.disconnect()
            return
        print(
            f"CSV KAYDEDILDI | bu_oturum={recorder.count} | sira={row['sequence']} | "
            f"sicaklik={row['temperature_c']} C | nem={row['humidity_pct']} % | "
            f"basinc={row['pressure_hpa']} hPa", flush=True,
        )

    def on_connect_fail(client, userdata):
        print("Broker'a ulasilamadi. Mosquitto ve 1884 portunu kontrol et; yeniden deneniyor...", flush=True)

    def on_disconnect(client, userdata, disconnect_flags, reason_code, properties):
        if reason_code.is_failure and not state["fatal"]:
            print(f"MQTT baglantisi kesildi: {reason_code}. Yeniden baglanilacak.", flush=True)

    client.on_connect = on_connect
    client.on_subscribe = on_subscribe
    client.on_message = on_message
    client.on_connect_fail = on_connect_fail
    client.on_disconnect = on_disconnect
    print(f"MQTT CSV alicisi | broker={args.host}:{args.port}", flush=True)
    print(f"CSV dosyasi: {recorder.output}", flush=True)
    print("Durdurmak icin Ctrl+C. Var olan CSV'nin sonuna eklenir.", flush=True)
    try:
        client.connect_async(args.host, args.port, keepalive=30)
        client.loop_forever(retry_first_connection=True)
    except KeyboardInterrupt:
        print("\nAlici durduruldu.")
    except (OSError, ValueError) as exc:
        print(f"Alici hatasi: {exc}")
        state["fatal"] = True
    finally:
        client.disconnect()
        print(f"Bu oturumda kaydedilen olcum: {recorder.count}")
        print(f"CSV dosyasi: {recorder.output}")
    return 1 if state["fatal"] else 0


if __name__ == "__main__":
    sys.exit(main())
