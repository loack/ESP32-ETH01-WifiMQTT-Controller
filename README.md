# ESP32 WT32-ETH01 Relay Controller

A web-based relay control system for the WT32-ETH01 ESP32 board with Ethernet connectivity. This project provides a modern web interface and REST API to control 4 relay channels over Ethernet.

## Features

- ✅ **Ethernet Connectivity**: Fixed IP configuration with WT32-ETH01
- ✅ **Web Interface**: Modern, responsive HTML interface
- ✅ **REST API**: RESTful endpoints for automation integration
- ✅ **Real-time Status**: Live status updates every 2 seconds
- ✅ **4-Channel Control**: Independent control of 4 relay channels
- ✅ **Async Operation**: Non-blocking web server implementation

## Hardware Requirements

- **WT32-ETH01** ESP32 board with Ethernet
- **4-Channel Relay Module** (5V or 3.3V compatible)
- **Ethernet Cable** and network connection
- **Power Supply** (5V for relay module, 3.3V for ESP32)

## Pin Configuration

| Relay | ESP32 Pin | GPIO |
|-------|-----------|------|
| IN1   | GPIO2     | 2    |
| IN2   | GPIO4     | 4    |
| IN3   | GPIO14    | 14   |
| IN4   | GPIO15    | 15   |

## Network Configuration

Default network settings (modify in `src/main.cpp`):

```cpp
IPAddress local_IP(192, 168, 1, 184);    // Fixed IP address
IPAddress gateway(192, 168, 1, 1);       // Gateway IP
IPAddress subnet(255, 255, 255, 0);      // Subnet mask
IPAddress primaryDNS(8, 8, 8, 8);        // Primary DNS
IPAddress secondaryDNS(8, 8, 4, 4);      // Secondary DNS
```

## Installation & Setup

### 1. Prerequisites

- [PlatformIO](https://platformio.org/) installed
- VS Code with PlatformIO extension (recommended)

### 2. Clone and Build

```bash
git clone <your-repo-url>
cd ESP32_ETHRELAY
pio run
```

### 3. Upload to Board

```bash
pio run --target upload
```

### 4. Monitor Serial Output

```bash
pio device monitor
```

## Web Interface

After uploading and connecting the Ethernet cable, access the web interface at:

**http://192.168.1.184/**

### Interface Features

- **Visual Status Indicators**: Green (ON) / Red (OFF) status for each relay
- **Control Buttons**: START/STOP buttons for each relay
- **Auto-refresh**: Status updates automatically every 2 seconds
- **Responsive Design**: Works on desktop and mobile devices

## REST API Endpoints

### Control Endpoints

| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | `/relay1/start` | Turn Relay 1 ON |
| POST | `/relay1/stop` | Turn Relay 1 OFF |
| POST | `/relay2/start` | Turn Relay 2 ON |
| POST | `/relay2/stop` | Turn Relay 2 OFF |
| POST | `/relay3/start` | Turn Relay 3 ON |
| POST | `/relay3/stop` | Turn Relay 3 OFF |
| POST | `/relay4/start` | Turn Relay 4 ON |
| POST | `/relay4/stop` | Turn Relay 4 OFF |

### Status Endpoint

| Method | Endpoint | Description | Response |
|--------|----------|-------------|----------|
| GET | `/status` | Get all relay states | JSON |

### Example API Usage

```bash
# Turn relay 1 ON
curl -X POST http://192.168.1.184/relay1/start

# Turn relay 1 OFF
curl -X POST http://192.168.1.184/relay1/stop

# Get status of all relays
curl http://192.168.1.184/status
```

### Status Response Format

```json
{
  "relay1": true,
  "relay2": false,
  "relay3": true,
  "relay4": false
}
```

## Libraries Used

- **ESP Async WebServer** (v1.2.3): Asynchronous web server
- **AsyncTCP** (v1.1.1): Asynchronous TCP library for ESP32
- **ETH** (built-in): Ethernet support for ESP32

## Wiring Diagram

```
WT32-ETH01          Relay Module
-----------         ------------
GPIO2        -----> IN1
GPIO4        -----> IN2  
GPIO14       -----> IN3
GPIO15       -----> IN4
GND          -----> GND
3.3V         -----> VCC (if 3.3V relay)
```

**Note**: If using a 5V relay module, you may need a logic level converter or use 5V from an external power supply.

## Customization

### Changing Network Settings

Edit the network configuration in `src/main.cpp`:

```cpp
// Network configuration
IPAddress local_IP(192, 168, 1, 184);  // Change this IP
IPAddress gateway(192, 168, 1, 1);     // Your router IP
IPAddress subnet(255, 255, 255, 0);    // Subnet mask
```

### Changing Relay Pins

Modify the pin assignments in `src/main.cpp`:

```cpp
// Relay pins
int IN1 = 2;   // Change to desired GPIO
int IN2 = 4;   // Change to desired GPIO
int IN3 = 14;  // Change to desired GPIO
int IN4 = 15;  // Change to desired GPIO
```

### Relay Logic

Current implementation uses **ACTIVE HIGH** logic:
- `HIGH` = Relay ON
- `LOW` = Relay OFF

For **ACTIVE LOW** relays, change the `controlRelay()` function:

```cpp
digitalWrite(pin, state ? LOW : HIGH);  // Inverted logic
```

## Troubleshooting

### Common Issues

1. **"setupWebServer was not declared"**
   - Ensure function declarations are at the top of main.cpp
   - Verify the function is defined later in the file

2. **Ethernet not connecting**
   - Check cable connection
   - Verify network settings match your network
   - Check if IP address is available (not used by another device)

3. **Relay not responding**
   - Verify wiring connections
   - Check if relay module requires 5V instead of 3.3V
   - Test with multimeter for signal output

4. **Web page not loading**
   - Verify IP address in browser
   - Check serial monitor for connection status
   - Ensure firewall is not blocking port 80

### Serial Monitor Output

Normal startup sequence:
```
ETH Started
ETH Connected
ETH MAC: XX:XX:XX:XX:XX:XX, IPv4: 192.168.1.184, GW: 192.168.1.1, DNS: 8.8.8.8
Ethernet connected!
IP address: 192.168.1.184
HTTP server started
```

## Contributing

Feel free to submit issues and pull requests to improve this project.

## License

This project is open source. Use and modify as needed for your projects.

## Version History

- **v1.0**: Initial release with basic relay control
- **v1.1**: Added web interface and REST API
- **v1.2**: Improved error handling and documentation

---

**Author**: Created for WT32-ETH01 ESP32 relay control applications
**Date**: July 2025
