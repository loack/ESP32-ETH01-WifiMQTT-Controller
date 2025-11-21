#ifndef SERIAL_MANAGER_H
#define SERIAL_MANAGER_H

#include <Arduino.h>
#include <vector>

struct SerialLog {
    String timestamp;
    String direction; // "TX" or "RX"
    String message;
};

class SerialManager {
public:
    SerialManager();
    void begin();
    void loop();
    void send(String message);
    std::vector<SerialLog> getLogs();
    void clearLogs();

private:
    HardwareSerial* _serial;
    std::vector<SerialLog> _logs;
    void addLog(String direction, String message);
};

extern SerialManager serialManager;

#endif
