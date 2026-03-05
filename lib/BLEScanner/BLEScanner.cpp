#include "BLEScanner.h"

// ---------------------------------------------------------------------------
// Shim: client callbacks
// ---------------------------------------------------------------------------
class BLEScannerClientCB : public NimBLEClientCallbacks {
public:
    explicit BLEScannerClientCB(BLEScanner* owner) : _owner(owner) {}

    void onConnect(NimBLEClient* pClient) override {
        _owner->onConnected(pClient);
    }

    void onDisconnect(NimBLEClient* pClient, int reason) override {
        _owner->onDisconnected(pClient, reason);
    }

private:
    BLEScanner* _owner;
};

// ---------------------------------------------------------------------------
// Shim: scan callbacks
// ---------------------------------------------------------------------------
class BLEScannerScanCB : public NimBLEScanCallbacks {
public:
    explicit BLEScannerScanCB(BLEScanner* owner) : _owner(owner) {}

    void onResult(const NimBLEAdvertisedDevice* dev) override {
        _owner->onDeviceFound(dev);
    }

private:
    BLEScanner* _owner;
};

// ---------------------------------------------------------------------------
// BLEScanner implementation
// ---------------------------------------------------------------------------

BLEScanner::BLEScanner()
    : _targetAddr("", 0) {}

BLEScanner::~BLEScanner() {
    delete _clientCB;
    delete _scanCB;
}

void BLEScanner::begin(const String& targetName, BarcodeCallback onBarcode) {
    _targetName = targetName;
    _onBarcode  = onBarcode;

    _clientCB = new BLEScannerClientCB(this);
    _scanCB   = new BLEScannerScanCB(this);

    _scan = NimBLEDevice::getScan();
    _scan->setActiveScan(true);
    _scan->setInterval(100);
    _scan->setWindow(99);
    _scan->setScanCallbacks(_scanCB, false);

    startScan();
}

void BLEScanner::setTargetName(const String& name) {
    _targetName = name;
}

void BLEScanner::update() {
    connectIfPending();
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void BLEScanner::startScan() {
    if (!_scan) return;
    Serial.println("🔍 [BLEScanner] Starting BLE scan...");
    _scan->start(0, true, true);
}

void BLEScanner::stopScanClean() {
    if (!_scan) return;
    _scan->stop();
    delay(250);
}

void BLEScanner::connectIfPending() {
    if (!_targetPending)  return;
    if (_isConnected || _isConnecting) return;

    _targetPending = false;
    _isConnecting  = true;

    if (_client) {
        NimBLEDevice::deleteClient(_client);
        _client = nullptr;
    }

    _client = NimBLEDevice::createClient();
    _client->setClientCallbacks(_clientCB, false);
    _client->setConnectTimeout(10);

    Serial.printf("📌 [BLEScanner] Connecting to: %s\n", _targetAddr.toString().c_str());

    if (!_client->connect(_targetAddr)) {
        Serial.println("❌ [BLEScanner] Connect failed");
        NimBLEDevice::deleteClient(_client);
        _client = nullptr;
        _isConnecting = false;
        startScan();
    }
}

void BLEScanner::dumpServicesAndSubscribe() {
    if (!_client || !_client->isConnected()) return;

    Serial.println("🔎 [BLEScanner] Discovering services...");
    _client->discoverAttributes();

    std::vector<NimBLERemoteService*> services = _client->getServices();
    Serial.printf("🧩 [BLEScanner] Services found: %d\n", (int)services.size());

    for (auto* svc : services) {
        if (!svc) continue;
        Serial.printf("  SERVICE: %s\n", svc->getUUID().toString().c_str());

        std::vector<NimBLERemoteCharacteristic*> chars = svc->getCharacteristics();
        for (auto* chr : chars) {
            if (!chr) continue;
            Serial.printf("    CHAR: %s  [N:%d]\n",
                          chr->getUUID().toString().c_str(),
                          chr->canNotify());

            if (chr->canNotify()) {
                Serial.println("      🔔 Subscribing...");
                chr->subscribe(true, [this](NimBLERemoteCharacteristic* c,
                                            uint8_t* data, size_t len, bool notify) {
                    onNotify(c, data, len);
                });
            }
        }
    }
}

void BLEScanner::processIncomingBytes(const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        char c = (char)data[i];
        if (c == '\r' || c == '\n') {
            if (_lineBuf.length() > 0) {
                String barcode = _lineBuf;
                _lineBuf = "";

                Serial.print("🏷️ [BLEScanner] SCAN: ");
                Serial.println(barcode);

                if (_onBarcode) _onBarcode(barcode);
            }
        } else {
            _lineBuf += c;
            if (_lineBuf.length() > 256) _lineBuf = "";
        }
    }
}

// ---------------------------------------------------------------------------
// Callbacks (called by shim classes)
// ---------------------------------------------------------------------------

void BLEScanner::onDeviceFound(const NimBLEAdvertisedDevice* dev) {
    String name = dev->getName().length() ? dev->getName().c_str() : "<no name>";

    if (_isConnected || _isConnecting || _targetPending) return;
    if (name != _targetName) return;

    Serial.printf("🎯 [BLEScanner] Target found: %s [%s] type=%d\n",
                  name.c_str(),
                  dev->getAddress().toString().c_str(),
                  (int)dev->getAddressType());

    _targetAddr    = NimBLEAddress(dev->getAddress().toString(), dev->getAddressType());
    _targetPending = true;

    stopScanClean();
}

void BLEScanner::onConnected(NimBLEClient* pClient) {
    Serial.println("✅ [BLEScanner] Connected");
    _isConnected  = true;
    _isConnecting = false;
    dumpServicesAndSubscribe();
}

void BLEScanner::onDisconnected(NimBLEClient* pClient, int reason) {
    Serial.printf("⚠️ [BLEScanner] Disconnected (reason=%d)\n", reason);
    _isConnected  = false;
    _isConnecting = false;

    if (_client) {
        NimBLEDevice::deleteClient(_client);
        _client = nullptr;
    }

    _targetPending = false;
    startScan();
}

void BLEScanner::onNotify(NimBLERemoteCharacteristic* chr, uint8_t* data, size_t len) {
    processIncomingBytes(data, len);
}
