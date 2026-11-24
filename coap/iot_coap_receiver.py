"""Single-board BME280 connectivity pilot. Run from a terminal with py."""
import asyncio
import csv
import ipaddress
import json
import math
from datetime import datetime, timezone
from pathlib import Path

import aiocoap
from aiocoap import resource

CSV_PATH = Path(__file__).resolve().parent / "data" / "coap_readings.csv"
FIELDS = ("device_id", "sequence", "uptime_ms", "temperature_c", "humidity_pct", "pressure_hpa")


class Sensor(resource.Resource):
    async def render_post(self, request):
        try:
            sample = json.loads(request.payload)
            if not isinstance(sample, dict):
                raise ValueError
            row = [sample[key] for key in FIELDS]
            if not isinstance(row[0], str) or not row[0].strip():
                raise ValueError
            if any(type(x) is not int or x < 0 for x in row[1:3]):
                raise ValueError
            if any(type(x) not in (int, float) or not math.isfinite(x) for x in row[3:]):
                raise ValueError
        except (ValueError, KeyError, OverflowError):
            return aiocoap.Message(code=aiocoap.BAD_REQUEST, payload=b"Invalid measurement")
        try:
            CSV_PATH.parent.mkdir(parents=True, exist_ok=True)
            with CSV_PATH.open("a", newline="", encoding="utf-8") as stream:
                writer = csv.writer(stream)
                if stream.tell() == 0:
                    writer.writerow(["received_at_utc", "protocol", *FIELDS])
                writer.writerow([datetime.now(timezone.utc).isoformat(), "CoAP", *row])
                stream.flush()
        except OSError as error:
            print("CSV yazma hatasi:", error, flush=True)
            return aiocoap.Message(code=aiocoap.INTERNAL_SERVER_ERROR)
        print(f"CSV KAYDEDILDI | sira={row[1]} | sicaklik={row[3]} C", flush=True)
        reply = json.dumps({"ok": True, "device_id": row[0], "sequence": row[1]}).encode()
        return aiocoap.Message(code=aiocoap.CHANGED, payload=reply, content_format=50)


async def main():
    address = str(ipaddress.IPv4Address(input("PC Wi-Fi IPv4 adresi: ").strip()))
    site = resource.Site()
    site.add_resource(["sensor"], Sensor())
    server = await aiocoap.Context.create_server_context(
        site, bind=(address, 5683), transports=["simplesocketserver"]
    )
    print(f"CoAP alicisi hazir | coap://{address}:5683/sensor", flush=True)
    print("CSV:", CSV_PATH)
    try:
        await asyncio.Future()
    finally:
        await server.shutdown()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("Alici durduruldu.")
