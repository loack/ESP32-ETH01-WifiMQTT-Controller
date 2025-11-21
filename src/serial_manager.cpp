#include "serial_manager.h"
#include "config.h"
#include <time.h>

extern Config config;

SerialManager serialManager;

SerialManager::SerialManager() {
    _serial = &Serial2; // Use Serial2 for external communication
}

void SerialManager::begin() {
    if (config.useSerialBridge) {
        // Default pins if not configured (check available pins on WT32-ETH01)
        // Using 4 and 5 as default if 0
        int rx = config.serialRxPin != 0 ? config.serialRxPin : 4;
        int tx = config.serialTxPin != 0 ? config.serialTxPin : 5;
        long baud = config.serialBaudRate > 0 ? config.serialBaudRate : 9600;

        _serial->begin(baud, SERIAL_8N1, rx, tx);
        Serial.printf("Serial Bridge started on RX:%d, TX:%d at %ld baud\n", rx, tx, baud);
    }
}

void SerialManager::loop() {
    if (!config.useSerialBridge) return;

    if (_serial->available()) {
        String msg = _serial->readStringUntil('\n');
        msg.trim();
        if (msg.length() > 0) {
            addLog("RX", msg);
            Serial.println("Serial Bridge RX: " + msg);
        }
    }
}

void SerialManager::send(String message) {
    if (!config.useSerialBridge) return;
    
    _serial->println(message);
    addLog("TX", message);
    Serial.println("Serial Bridge TX: " + message);
}

void SerialManager::addLog(String direction, String message) {
    SerialLog log;
    
    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    char timeStr[20];
    strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);
    
    log.timestamp = String(timeStr);
    log.direction = direction;
    log.message = message;
    
    _logs.push_back(log);
    
    // Keep only last 50 logs
    if (_logs.size() > 50) {
        _logs.erase(_logs.begin());
    }
}

std::vector<SerialLog> SerialManager::getLogs() {
    return _logs;
}

void SerialManager::clearLogs() {
    _logs.clear();
}
