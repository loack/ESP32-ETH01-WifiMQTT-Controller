#include <Arduino.h>
#include <ETH.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>

// put function declarations here:
int myFunction(int, int);
void setupWebServer();
void controlRelay(int relayNum, bool state);
String getRelayStatusJSON();

// Network configuration
IPAddress local_IP(192, 168, 1, 11);  // Fixed IP address
IPAddress gateway(192, 168, 1, 1);     // Gateway IP  
IPAddress subnet(255, 255, 255, 224);  // Subnet mask /27 (192.168.1.224-255)
IPAddress primaryDNS(8, 8, 8, 8);      // Primary DNS (optional)
IPAddress secondaryDNS(8, 8, 4, 4);    // Secondary DNS (optional)

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

// Relay pins - using the correct pins for WT32-ETH01
int IN1 = 2;
int IN2 = 4;
int IN3 = 14;
int IN4 = 15;

// Relay states
bool relayStates[4] = {false, false, false, false};

// Ethernet event handler
void WiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      Serial.println("ETH Started");
      ETH.setHostname("esp32-ethernet");
      break;
    case ARDUINO_EVENT_ETH_CONNECTED:
      Serial.println("ETH Connected");
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      Serial.print("ETH MAC: ");
      Serial.print(ETH.macAddress());
      Serial.print(", IPv4: ");
      Serial.print(ETH.localIP());
      Serial.print(", GW: ");
      Serial.print(ETH.gatewayIP());
      Serial.print(", DNS: ");
      Serial.println(ETH.dnsIP());
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      Serial.println("ETH Disconnected");
      break;
    case ARDUINO_EVENT_ETH_STOP:
      Serial.println("ETH Stopped");
      break;
    default:
      break;
  }
}


void setup() {
  // Initialize serial communication
  Serial.begin(115200);
  
  // Wait for serial to be ready and send repeated startup messages
  for (int i = 0; i < 10; i++) {
    Serial.println("=== ESP32 ETHERNET RELAY CONTROL ===");
    Serial.print("Startup sequence: ");
    Serial.println(i + 1);
    delay(500);
  }
  
  Serial.println("Serial communication established!");
  Serial.println("System initializing...");
  Serial.println("NOTE: Using ACTIVE-LOW relay module (HIGH=OFF, LOW=ON)");
  
  // Initialize relay pins
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);

  // Turn off all relays initially (HIGH = OFF for active-low relay modules)
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, HIGH);
  
  // Initialize ethernet with fixed IP
  Serial.println("------------------------");
  Serial.println("ETHERNET INITIALIZATION");
  Serial.println("------------------------");
  WiFi.onEvent(WiFiEvent);
  
  Serial.println("Configuring Ethernet with static IP...");
  Serial.print("Target IP: "); Serial.println(local_IP);
  Serial.print("Gateway: "); Serial.println(gateway);
  Serial.print("Subnet: "); Serial.println(subnet);
  Serial.print("DNS1: "); Serial.println(primaryDNS);
  Serial.print("DNS2: "); Serial.println(secondaryDNS);
  
  // Configure static IP
  if (!ETH.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
    Serial.println("ERROR: Failed to configure static IP!");
  } else {
    Serial.println("Static IP configuration successful");
  }
  
  // Start ethernet
  ETH.begin();
  
  // Apply IP configuration again after ETH.begin() for better reliability
  delay(500);
  Serial.println("Re-applying IP configuration after ETH.begin()...");
  ETH.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS);
  
  // Wait for ethernet physical connection
  Serial.println("Waiting for Ethernet connection...");
  int connectionAttempts = 0;
  while (!ETH.linkUp()) {
    delay(100);
    Serial.print(".");
    connectionAttempts++;
    
    // Print heartbeat every 50 attempts (5 seconds)
    if (connectionAttempts % 50 == 0) {
      Serial.println();
      Serial.print("Still waiting... (");
      Serial.print(connectionAttempts / 10);
      Serial.println(" seconds)");
      Serial.print("Continuing");
    }
  }
  Serial.println("\nEthernet physically connected!");
  
  // Wait for IP configuration to complete
  Serial.println("Waiting for IP configuration...");
  unsigned long startTime = millis();
  int ipAttempts = 0;
  while (ETH.localIP() == IPAddress(0, 0, 0, 0)) {
    delay(100);
    Serial.print(".");
    ipAttempts++;
    
    // Print heartbeat every 50 attempts (5 seconds)
    if (ipAttempts % 50 == 0) {
      Serial.println();
      Serial.print("Still configuring IP... (");
      Serial.print((millis() - startTime) / 1000);
      Serial.println(" seconds)");
      Serial.print("Current IP: ");
      Serial.println(ETH.localIP());
      Serial.print("Continuing");
    }
    
    // Timeout after 30 seconds
    if (millis() - startTime > 30000) {
      Serial.println("\nIP configuration timeout!");
      Serial.println("Check your network settings and cable connection.");
      break;
    }
  }
  
  if (ETH.localIP() != IPAddress(0, 0, 0, 0)) {
    Serial.println("\nEthernet fully configured!");
    Serial.print("IP address: ");
    Serial.println(ETH.localIP());
    Serial.print("Gateway: ");
    Serial.println(ETH.gatewayIP());
    Serial.print("Subnet: ");
    Serial.println(ETH.subnetMask());
    Serial.print("DNS: ");
    Serial.println(ETH.dnsIP());
  } else {
    Serial.println("\nFailed to get IP address!");
    Serial.println("Forcing static IP configuration...");
    
    // Force static IP configuration again
    ETH.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS);
    delay(1000);
    
    // Check again
    if (ETH.localIP() != IPAddress(0, 0, 0, 0)) {
      Serial.print("Success! IP address: ");
      Serial.println(ETH.localIP());
    } else {
      Serial.println("ERROR: Unable to configure IP. Check network settings!");
      Serial.println("Current subnet /27 allows IPs: 192.168.1.0-31 or 192.168.1.224-255");
      Serial.print("Your IP (192.168.1.11) should work with /27 subnet");
      Serial.print("Configured IP: ");
      Serial.println(local_IP);
      Serial.print("Gateway: ");
      Serial.println(gateway);
      Serial.print("Subnet: ");
      Serial.println(subnet);
    }
  }
  
  // Setup web server routes
  setupWebServer();
  
  // Start server
  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  // Main loop - the web server handles requests asynchronously
  static unsigned long lastStatusPrint = 0;
  static int loopCounter = 0;
  
  loopCounter++;
  
  // Print status every 10 seconds
  if (millis() - lastStatusPrint > 10000) {
    Serial.println("=== STATUS UPDATE ===");
    Serial.print("Loop count: ");
    Serial.println(loopCounter);
    Serial.print("Uptime: ");
    Serial.print(millis() / 1000);
    Serial.println(" seconds");
    Serial.print("IP address: ");
    Serial.println(ETH.localIP());
    Serial.print("Link status: ");
    Serial.println(ETH.linkUp() ? "UP" : "DOWN");
    
    // Print relay states
    Serial.print("Relays: ");
    for (int i = 0; i < 4; i++) {
      Serial.print("R");
      Serial.print(i + 1);
      Serial.print(":");
      Serial.print(relayStates[i] ? "ON" : "OFF");
      if (i < 3) Serial.print(" ");
    }
    Serial.println();
    Serial.println("===================");
    
    lastStatusPrint = millis();
  }
  
  delay(1000);
}

// Function to control relays (active-low logic)
void controlRelay(int relayNum, bool state) {
  int pin;
  switch(relayNum) {
    case 1: pin = IN1; break;
    case 2: pin = IN2; break;
    case 3: pin = IN3; break;
    case 4: pin = IN4; break;
    default: return;
  }
  
  // Inverted logic for active-low relay modules
  // state=true (ON) -> digitalWrite LOW
  // state=false (OFF) -> digitalWrite HIGH
  digitalWrite(pin, state ? LOW : HIGH);
  relayStates[relayNum - 1] = state;
  
  Serial.print("Relay ");
  Serial.print(relayNum);
  Serial.print(" set to ");
  Serial.println(state ? "ON" : "OFF");
  Serial.print("  -> GPIO pin set to ");
  Serial.println(state ? "LOW" : "HIGH");
}

// Function to get relay status JSON
String getRelayStatusJSON() {
  String json = "{";
  json += "\"relay1\":" + String(relayStates[0] ? "true" : "false") + ",";
  json += "\"relay2\":" + String(relayStates[1] ? "true" : "false") + ",";
  json += "\"relay3\":" + String(relayStates[2] ? "true" : "false") + ",";
  json += "\"relay4\":" + String(relayStates[3] ? "true" : "false");
  json += "}";
  return json;
}

// HTML page for relay control
const char* htmlPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>ESP32 Relay Control</title>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { 
            font-family: Arial, sans-serif; 
            margin: 20px; 
            background-color: #f0f0f0; 
        }
        .container { 
            max-width: 600px; 
            margin: 0 auto; 
            background-color: white; 
            padding: 20px; 
            border-radius: 10px; 
            box-shadow: 0 4px 6px rgba(0,0,0,0.1); 
        }
        h1 { 
            text-align: center; 
            color: #333; 
        }
        .relay-control { 
            margin: 15px 0; 
            padding: 15px; 
            border: 1px solid #ddd; 
            border-radius: 5px; 
            background-color: #f9f9f9; 
        }
        .relay-name { 
            font-weight: bold; 
            margin-bottom: 10px; 
        }
        .status { 
            display: inline-block; 
            padding: 5px 10px; 
            border-radius: 3px; 
            color: white; 
            font-weight: bold; 
            margin-right: 10px; 
        }
        .status.on { background-color: #4CAF50; }
        .status.off { background-color: #f44336; }
        button { 
            padding: 10px 20px; 
            margin: 5px; 
            border: none; 
            border-radius: 5px; 
            cursor: pointer; 
            font-size: 14px; 
        }
        .start-btn { 
            background-color: #4CAF50; 
            color: white; 
        }
        .stop-btn { 
            background-color: #f44336; 
            color: white; 
        }
        .start-btn:hover { background-color: #45a049; }
        .stop-btn:hover { background-color: #da190b; }
    </style>
</head>
<body>
    <div class="container">
        <h1>ESP32 Relay Control Panel</h1>
        
        <div class="relay-control">
            <div class="relay-name">Relay 1</div>
            <span class="status" id="status1">OFF</span>
            <button class="start-btn" onclick="controlRelay(1, 'start')">START</button>
            <button class="stop-btn" onclick="controlRelay(1, 'stop')">STOP</button>
        </div>
        
        <div class="relay-control">
            <div class="relay-name">Relay 2</div>
            <span class="status" id="status2">OFF</span>
            <button class="start-btn" onclick="controlRelay(2, 'start')">START</button>
            <button class="stop-btn" onclick="controlRelay(2, 'stop')">STOP</button>
        </div>
        
        <div class="relay-control">
            <div class="relay-name">Relay 3</div>
            <span class="status" id="status3">OFF</span>
            <button class="start-btn" onclick="controlRelay(3, 'start')">START</button>
            <button class="stop-btn" onclick="controlRelay(3, 'stop')">STOP</button>
        </div>
        
        <div class="relay-control">
            <div class="relay-name">Relay 4</div>
            <span class="status" id="status4">OFF</span>
            <button class="start-btn" onclick="controlRelay(4, 'start')">START</button>
            <button class="stop-btn" onclick="controlRelay(4, 'stop')">STOP</button>
        </div>
    </div>

    <script>
        function controlRelay(relayNum, action) {
            fetch('/relay' + relayNum + '/' + action, {method: 'POST'})
                .then(response => response.text())
                .then(data => {
                    console.log('Success:', data);
                    updateStatus();
                })
                .catch((error) => {
                    console.error('Error:', error);
                });
        }
        
        function updateStatus() {
            fetch('/status')
                .then(response => response.json())
                .then(data => {
                    document.getElementById('status1').textContent = data.relay1 ? 'ON' : 'OFF';
                    document.getElementById('status1').className = 'status ' + (data.relay1 ? 'on' : 'off');
                    
                    document.getElementById('status2').textContent = data.relay2 ? 'ON' : 'OFF';
                    document.getElementById('status2').className = 'status ' + (data.relay2 ? 'on' : 'off');
                    
                    document.getElementById('status3').textContent = data.relay3 ? 'ON' : 'OFF';
                    document.getElementById('status3').className = 'status ' + (data.relay3 ? 'on' : 'off');
                    
                    document.getElementById('status4').textContent = data.relay4 ? 'ON' : 'OFF';
                    document.getElementById('status4').className = 'status ' + (data.relay4 ? 'on' : 'off');
                });
        }
        
        // Update status every 2 seconds
        setInterval(updateStatus, 2000);
        
        // Initial status update
        updateStatus();
    </script>
</body>
</html>
)rawliteral";

// Setup web server routes
void setupWebServer() {
  // Serve the main HTML page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/html", htmlPage);
  });
  
  // Status endpoint
  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "application/json", getRelayStatusJSON());
  });
  
  // Relay control endpoints
  server.on("/relay1/start", HTTP_POST, [](AsyncWebServerRequest *request){
    controlRelay(1, true);
    request->send(200, "text/plain", "Relay 1 ON");
  });
  
  server.on("/relay1/stop", HTTP_POST, [](AsyncWebServerRequest *request){
    controlRelay(1, false);
    request->send(200, "text/plain", "Relay 1 OFF");
  });
  
  server.on("/relay2/start", HTTP_POST, [](AsyncWebServerRequest *request){
    controlRelay(2, true);
    request->send(200, "text/plain", "Relay 2 ON");
  });
  
  server.on("/relay2/stop", HTTP_POST, [](AsyncWebServerRequest *request){
    controlRelay(2, false);
    request->send(200, "text/plain", "Relay 2 OFF");
  });
  
  server.on("/relay3/start", HTTP_POST, [](AsyncWebServerRequest *request){
    controlRelay(3, true);
    request->send(200, "text/plain", "Relay 3 ON");
  });
  
  server.on("/relay3/stop", HTTP_POST, [](AsyncWebServerRequest *request){
    controlRelay(3, false);
    request->send(200, "text/plain", "Relay 3 OFF");
  });
  
  server.on("/relay4/start", HTTP_POST, [](AsyncWebServerRequest *request){
    controlRelay(4, true);
    request->send(200, "text/plain", "Relay 4 ON");
  });
  
  server.on("/relay4/stop", HTTP_POST, [](AsyncWebServerRequest *request){
    controlRelay(4, false);
    request->send(200, "text/plain", "Relay 4 OFF");
  });
  
  // Handle 404
  server.onNotFound([](AsyncWebServerRequest *request){
    request->send(404, "text/plain", "Not found");
  });
}

// put function definitions here:
int myFunction(int x, int y) {
  return x + y;
}