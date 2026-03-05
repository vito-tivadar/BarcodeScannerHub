# BarcodeScannerHub

An ESP32 firmware that bridges a Bluetooth LE barcode scanner to any HTTP/HTTPS webhook endpoint. Configuration is done entirely through a built-in web interface — no recompilation needed.

Working with [NETUM L8BL](https://doc1.netum.net/L8BL/en/)

---

## Features

- **BLE auto-connect** — scans for a device by advertised name and reconnects automatically on disconnect
- **Webhook delivery** — POSTs each scanned barcode as JSON to a configurable URL (HTTP or HTTPS)
- **Browser-based configuration** — WiFi credentials, target device name, and webhook URL are all set through a web form and persisted in NVS flash
- **Captive AP fallback** — if no WiFi credentials are stored (or connection fails), the ESP32 starts its own access point so you can configure it immediately
- **Safe concurrency** — BLE notification callbacks only enqueue barcodes; HTTP POSTs happen exclusively in `loop()`, avoiding stack/heap issues from callbacks
- **Reusable BLE library** — scanner logic is isolated in `lib/BLEScanner` and can be used independently

---

## Hardware Requirements

- Any ESP32 development board (tested on `esp32dev`)
- A BLE barcode scanner that advertises itself by a fixed device name and streams scan results as newline-terminated ASCII over a notify characteristic (e.g. NETUM NT scanner LE)

---

## Installation

### Prerequisites

- [PlatformIO](https://platformio.org/) (IDE plugin or CLI)
- ESP32 Arduino platform (`espressif32`)

### Build & Flash

```bash
# Clone the repository
git clone https://github.com/your-username/BarcodeScannerHub.git
cd BarcodeScannerHub

# Build and upload (replace /dev/ttyUSB0 with your port)
pio run --target upload --upload-port /dev/ttyUSB0

# Open serial monitor
pio device monitor --baud 115200
```

---

## First-Time Setup

1. Power on the ESP32. Because no WiFi is configured yet, it starts an access point:
   - **SSID:** `ESP32-SCANNER-SETUP`
   - **Password:** `12345678`

2. Connect your phone or laptop to that AP, then open `http://192.168.4.1`.

3. Fill in:
   - **Target name** — the BLE advertised name of your scanner (default: `NT scanner LE`)
   - **POST URL** — the webhook endpoint that receives scans (e.g. `https://your-server.example.com/api/scan`)
   - **WiFi SSID / Password** — your network credentials

4. Click **Save & Reboot**. The device joins your WiFi and is ready.

> **Change the AP password** before deploying in any shared environment. Edit `AP_PASS` in [`src/main.cpp`](src/main.cpp).

---

## Usage

Once connected to WiFi, the device is accessible at its DHCP-assigned IP (printed to the serial monitor on boot).

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/`      | GET    | Configuration web form |
| `/save`  | POST   | Save settings and reboot |
| `/status`| GET    | JSON status (WiFi, IP, target, URL) |

### Webhook payload

Each successful scan sends:

```json
{ "barcode": "0123456789012" }
```

HTTPS is supported; TLS certificate verification is currently disabled (`setInsecure()`). To enable proper verification, replace the `tlsClient.setInsecure()` call with `tlsClient.setCACert(...)` and supply the server's CA certificate.

---

## Configuration Reference

All settings are persisted in NVS (survives power cycles). They can be updated at any time through the web interface.

| Setting | NVS key | Default | Description |
|---------|---------|---------|-------------|
| BLE target name | `target` | `NT scanner LE` | Advertised name of the scanner to connect to |
| Webhook URL | `postUrl` | _(empty)_ | HTTP or HTTPS endpoint that receives scan POSTs |
| WiFi SSID | `ssid` | _(empty)_ | Station-mode network name |
| WiFi password | `pass` | _(empty)_ | Station-mode network password |

---

## Project Structure

```
BarcodeScannerHub/
├── src/
│   └── main.cpp          # App entry point: WiFi, web server, HTTP POST, scan queue
├── lib/
│   └── BLEScanner/
│       ├── BLEScanner.h  # Public API for the BLE scanner library
│       └── BLEScanner.cpp# BLE scan, connect, service discovery, notify handling
├── platformio.ini        # PlatformIO build configuration
└── LICENSE
```

### BLEScanner library API

```cpp
#include <BLEScanner.h>

BLEScanner scanner;

// Call after NimBLEDevice::init()
scanner.begin("My Scanner Name", [](const String& barcode) {
    // called from loop() context via update()
    Serial.println(barcode);
});

// Call every loop() iteration
scanner.update();

// Optionally change the target at runtime
scanner.setTargetName("Other Scanner");

// Query connection state
bool connected = scanner.isConnected();
```

---

## Development

Dependencies are managed by PlatformIO and declared in `platformio.ini`:

```ini
lib_deps =
    h2zero/NimBLE-Arduino@^2.3.7
```

`WebServer`, `WiFi`, `HTTPClient`, `WiFiClientSecure`, and `Preferences` are all part of the ESP32 Arduino framework — no extra libraries needed.

### Building

```bash
pio run
```

### Running tests

No automated tests are included yet. Manual testing is done via the serial monitor and a local webhook receiver (e.g. `python3 -m http.server` or [webhook.site](https://webhook.site)).

---

## Contributing

Contributions are welcome. Please:

1. Fork the repository and create a branch from `main`
2. Keep changes focused — one feature or fix per pull request
3. Test on hardware before submitting
4. Follow the existing code style (no Arduino `String` in library hot-paths where avoidable)

---

## License

[GPL-3.0 license](LICENSE)
