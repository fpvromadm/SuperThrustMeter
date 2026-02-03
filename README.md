# Thrust Test Stand for Brushless Motors

ESP32-powered thrust measurement stand for testing brushless motors with real-time data visualization, web interface, and automated reports.

## Features
- ESP32 microcontroller with Wi-Fi interface
- Load cell sensor (HX711) for thrust measurement
- Web interface with real-time graphing (Chart.js + Tailwind CSS)
- Tare function, pause detection, and filtering
- CSV export and graph image export
- Telegram bot integration for test reports
- ESC control via PWM (GPIO 17)
- Current sensor support (Matek Hall sensor or ESC telemetry)
- Temperature sensors (motor and battery)
- Pressure sensors (inlet and nozzle)
- ADXL accelerometer for vibration measurement
- Configuration stored on the ESP32 (no recompilation for changes)

## Hardware
- ESP32 WROOM-32 Dev Board
- HX711 + load cell (5 kg or suitable range)
- Matek Hall current sensor or ESC telemetry wire
- Temperature sensors (NTC or digital)
- Pressure sensors (analog or I2C)
- ADXL345 (or similar) accelerometer
- ESC (e.g., FlyingRC AM32 75A with BEC)
- RC motor, propeller, and power supply

## Web Interface
- Hosted by the ESP32
- Accessible via local IP (e.g., `http://192.168.1.xxx`)
- Real-time thrust chart
- Control buttons: Start, Stop, Tare
- Export: CSV, PNG
- Status indicators (sensor status, pause, errors)

## Project Structure
```
src/
  main.cpp            # ESP32 logic: sensors, Wi-Fi, web server
  config.json         # Editable settings (e.g., Wi-Fi, sensor calibration)
  web/                # HTML/JS/CSS interface with Chart.js & Tailwind
test/
  sample_log.csv      # Example output data
README.md
```

## Configuration
- Wi-Fi SSID and password
- ESC control pin: GPIO 17
- Config stored in SPIFFS as `config.json`
- Tare happens at startup or via button
- Wi-Fi credentials are stored in NVS (survive firmware + LittleFS reflash)

## Simulation Mode (Safe UI/Backend Testing)
- Enable in `board.cfg` under `[sim]` with `SIM_ENABLED = 1`
- Simulates thrust, voltage, and current without driving the ESC or reading hardware sensors
- Use to verify UI, WebSocket flow, logging, and safety logic before live runs

## Security / Threat Model
- Wi-Fi credentials are stored in plaintext on LittleFS; physical access or firmware extraction can reveal them.
- Intended for trusted LAN/lab environments unless additional auth/encryption is added.
- Auth is optional. Set `/board.cfg` `[security] AUTH_TOKEN` to enable; leave empty for no-login access.
- If enabled, open the UI with `http://<ip>/?token=YOUR_TOKEN` and the web app will pass the token on subsequent requests.
- Optional AP password can be set in `/board.cfg` under `[wifi] WIFI_AP_PASSWORD` (8+ chars enables WPA2).

## Telegram Bot
- Sends test results at the end
- Includes CSV file and chart image
- Token and chat ID are configured in `config.json`

## Build and Upload
1. Flash ESP32 with PlatformIO or Arduino IDE.
2. Upload the web interface to SPIFFS (`data/` folder).
3. Power on and connect to Wi-Fi.
4. Open the web interface in a browser.
5. Start a test, observe the graph, export results.

## Roadmap
- Add calibration UI for sensors
- Add config editor in web interface
- Improve pause detection logic
- Add motor RPM measurement
