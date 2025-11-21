#include "serial_manager.h"
#include "config.h"
#include "mqtt.h"
#include <time.h>
#include <ArduinoJson.h>

extern Config config;
extern bool mqttEnabled;

SerialManager serialManager;

SerialManager::SerialManager() {
    _serial = &Serial2; // Use Serial2 for external communication
}

void SerialManager::begin() {
    if (config.useSerialBridge) {
        const int rxPin = 5;
        const int txPin = 17;
        long baud = config.serialBaudRate > 0 ? config.serialBaudRate : 9600;

        _serial->begin(baud, SERIAL_8N1, rxPin, txPin);
        Serial.printf("Serial Bridge started on RX:%d, TX:%d at %ld baud\n", rxPin, txPin, baud);
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
            publish(msg);
        }
    }
}

void SerialManager::send(String message) {
    if (!config.useSerialBridge) return;

    _serial->println(message);
    addLog("TX", message);
    Serial.println("Serial Bridge TX: " + message);
}

void SerialManager::publish(String message) {
    if (!config.useSerialBridge) return;

    // Publish received message to MQTT
    if (mqttEnabled && mqttClient.connected()) {
        char topic[128];
        snprintf(topic, sizeof(topic), "%s/serial/receive", config.deviceName);

        JsonDocument doc;
        doc["message"] = message;
        
        time_t now;
        time(&now);
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        char timeStr[25];
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
        doc["timestamp"] = timeStr;

        char payload[256];
        serializeJson(doc, payload);
        
        Serial.printf("Publishing serial RX to topic [%s]: %s\n", topic, payload);
        publishMQTT(topic, payload);
    } else {
        Serial.println("MQTT not connected or disabled - serial message not published");
    }
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
