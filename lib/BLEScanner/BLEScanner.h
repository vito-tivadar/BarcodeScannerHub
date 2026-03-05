#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <functional>

// Forward-declare callback shim classes
class BLEScannerClientCB;
class BLEScannerScanCB;

class BLEScanner {
public:
    using BarcodeCallback = std::function<void(const String&)>;

    BLEScanner();
    ~BLEScanner();

    // Call once from setup(). Power must already be initialised via
    // NimBLEDevice::init() before calling begin().
    void begin(const String& targetName, BarcodeCallback onBarcode);

    // Change the advertised name to search for at runtime (triggers re-scan).
    void setTargetName(const String& name);

    // Call every loop() iteration.
    void update();

    bool isConnected() const { return _isConnected; }

private:
    // ---- BLE helpers ----
    void startScan();
    void stopScanClean();
    void connectIfPending();
    void dumpServicesAndSubscribe();
    void processIncomingBytes(const uint8_t* data, size_t len);

    // ---- Called by shim callbacks ----
    void onDeviceFound(const NimBLEAdvertisedDevice* dev);
    void onConnected(NimBLEClient* pClient);
    void onDisconnected(NimBLEClient* pClient, int reason);
    void onNotify(NimBLERemoteCharacteristic* chr, uint8_t* data, size_t len);

    // ---- State ----
    String          _targetName;
    String          _lineBuf;
    BarcodeCallback _onBarcode;

    NimBLEClient*   _client       = nullptr;
    NimBLEScan*     _scan         = nullptr;

    bool            _isConnecting = false;
    bool            _isConnected  = false;
    bool            _targetPending = false;
    NimBLEAddress   _targetAddr;

    // Shim callback objects that hold a back-pointer to this instance
    BLEScannerClientCB* _clientCB = nullptr;
    BLEScannerScanCB*   _scanCB   = nullptr;

    friend class BLEScannerClientCB;
    friend class BLEScannerScanCB;
};
