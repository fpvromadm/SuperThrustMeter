# Thrust Test Stand for Brushless Motors

ESP32-powered thrust measurement stand for testing brushless motors with real-time data visualization and a built-in web interface.

## Features
- ESP32 microcontroller with Wi-Fi interface
- Load cell sensor (HX711) for thrust measurement
- Web interface with real-time graphing (Chart.js + custom CSS)
- Tare function (WebSocket command)
- CSV export (client-side) and saved results download (`/api/results/latest`)
- ESC control via PWM (configurable pin; default GPIO 27)
- ESC telemetry voltage/current support
- Configuration stored on the ESP32 (`/board.cfg`, no recompilation for changes)

## Hardware
- ESP32 WROOM-32 Dev Board
- HX711 + load cell (5 kg or suitable range)
- ESC telemetry wire (for voltage/current)
- ESC (e.g., FlyingRC AM32 75A with BEC)
- RC motor, propeller, and power supply

## Web Interface
- Hosted by the ESP32
- Accessible via local IP (e.g., `http://192.168.1.xxx`)
- Real-time thrust chart
- Control buttons: Start/Stop, Reset
- Export: CSV (from Test Summary)
- Status indicators: WebSocket connection + status log
- Settings modal: test info, scale calibration, board config editor, reboot
- Simulation toggle (Live/Sim) with reboot-to-apply notice

## Project Structure
```
src/
  main.cpp            # ESP32 logic: sensors, Wi-Fi, web server
data/
  index.html          # Web UI (Chart.js)
  wifi_setup.html     # Wi-Fi provisioning UI
test/
  sample_log.csv      # Example output data
README.md
```

## Configuration
- Wi-Fi SSID and password (provisioned via the setup UI, stored in NVS)
- ESC control pin: configurable (default GPIO 27)
- Config stored in LittleFS as `/board.cfg`
- Tare happens at startup or via WebSocket command
- Wi-Fi credentials are stored in NVS (survive firmware + LittleFS reflash)

## Simulation Mode (Safe UI/Backend Testing)
- Enable in `board.cfg` under `[sim]` with `SIM_ENABLED = 1`
- Simulates thrust, voltage, and current without driving the ESC or reading hardware sensors
- Use to verify UI, WebSocket flow, logging, and safety logic before live runs

## Security / Threat Model
- Wi-Fi credentials are stored in NVS; legacy `wifi.json` on LittleFS may exist if provisioned that way. Physical access or firmware extraction can reveal them.
- Intended for trusted LAN/lab environments unless additional auth/encryption is added.
- Auth is optional. Set `/board.cfg` `[security] AUTH_TOKEN` to enable; leave empty for no-login access.
- If enabled, open the UI with `http://<ip>/?token=YOUR_TOKEN` and the web app will pass the token on subsequent requests.
- Optional AP password can be set in `/board.cfg` under `[wifi] WIFI_AP_PASSWORD` (8+ chars enables WPA2).

## Build and Upload
1. Flash ESP32 with PlatformIO or Arduino IDE.
2. Upload the web interface to LittleFS (`data/` folder).
3. Power on and connect to Wi-Fi.
4. Open the web interface in a browser.
5. Start a test, observe the graph, export results.

## TODO (Not Implemented)
- Telegram bot integration for test reports
- Pause detection and filtering
- Graph image export
- Matek Hall (analog) current sensor support
- Temperature sensors (motor and battery)
- Pressure sensors (inlet and nozzle)
- ADXL accelerometer for vibration measurement
- Motor RPM measurement

## Roadmap (Near-Term Improvements)
- Refine calibration workflow (guided steps + validation)
- Improve config editor UX (schema hints + diff feedback)
- Expand status/error reporting for test runs
