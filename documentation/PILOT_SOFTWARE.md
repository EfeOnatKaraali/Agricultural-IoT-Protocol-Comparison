# Agricultural IoT pilot software
These sketches and Windows receivers archive the ESP32 connectivity and sensor pilots. Open each Arduino sketch from its own same-named folder. Select ESP32 Dev Module and the board's USB serial port. Use a serial monitor speed of 115200 baud.

| Directory | Firmware | Receiver and output | Observed status |
|---|---|---|---|
| hardware | Prototype_Sensor_Test | Arduino serial monitor | BME280 readings and soil-probe response to watering observed. Soil readings are raw ADC values. |
| http | ESP32_HTTP_Test | iot_http_receiver.py, http_readings.csv | BME280 readings reached the PC via HTTP POST and were logged. |
| mqtt | ESP32_MQTT_Test | iot_mqtt_receiver.py, mqtt_readings.csv | The Mosquitto broker delivered sensor messages to the Python subscriber, which logged them. |
| coap | ESP32_CoAP_Test | iot_coap_receiver.py, coap_readings.csv | The one-shot pilot logged measurements and the ESP32 displayed a matching 2.04 response. |
| coap | ESP32_CoAP_100 | Same CoAP receiver | A 100-attempt pilot has been prepared. Completion on the physical board is unverified. |

## Local settings
The BME280 uses I2C on GPIO21 (SDA) and GPIO22 (SCL). The soil probe's analog output uses GPIO34 in the standalone sensor sketch. Network sketches send temperature, relative humidity and pressure. Configure the Wi-Fi name and password in a local working copy. Replace the example PC IPv4 address in each sketch with the active Wi-Fi adapter address on the receiver PC. Keep credentials out of commits.

## Receiver startup on Windows
Run commands from the repository root. Install aiocoap and paho-mqtt for the respective receivers. The HTTP receiver uses the Python standard library.

- HTTP: run `py http/iot_http_receiver.py`. The default endpoint is POST /sensor on TCP port 5000.
- MQTT: start the existing Mosquitto broker with the project's local configuration on port 1884. Install the receiver dependency with `py -m pip install "paho-mqtt>=2,<3"` and run `py mqtt/iot_mqtt_receiver.py`. The topic is `agriculture/sensors/esp32_01`.
- CoAP: install the pilot version with `py -m pip install aiocoap==0.4.17` and run `py coap/iot_coap_receiver.py`. Enter the PC Wi-Fi IPv4 address when prompted. The receiver listens on UDP port 5683 at /sensor.

Start the receiver before uploading or resetting the ESP32. The default CSV location is the data subfolder beside the relevant receiver script. Keep earlier sketches in separate folders to avoid duplicate setup() or loop() definitions. Install the Adafruit BME280 library with its dependencies, plus PubSubClient for MQTT and CoAP simple library for CoAP.

## Scope
The source snapshot contains the files prepared for the pilots. The original local Mosquitto configuration, local edits, measured CSV files and Excel copies are separate. The controlled three-protocol benchmark remains planned.
