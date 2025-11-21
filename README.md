# ESP32-ETH01-WifiMQTT-Controller

Contrôleur générique d'I/O pour WT32-ETH01 avec Ethernet prioritaire et fallback WiFi.

## Description

Ce projet est basé sur le ESP32-WifiMQTTRelay avec les adaptations suivantes pour la board WT32-ETH01:
- **Ethernet par défaut**: Connexion Ethernet prioritaire pour une latence minimale
- **Fallback WiFi**: Bascule automatique sur WiFi si Ethernet échoue
- **MQTT**: Contrôle et monitoring via MQTT avec synchronisation temporelle microseconde
- **Interface Web**: Configuration complète via interface web (SPIFFS)
- **I/O configurables**: Jusqu'à 20 I/O configurables (entrées/sorties)
- **OTA**: Mise à jour Over-The-Air via ElegantOTA

## Caractéristiques

### Réseau
- **Ethernet WT32-ETH01 (LAN8720)** - Connexion prioritaire
- **WiFi** - Fallback automatique avec WiFiManager
- **IP statique ou DHCP** - Configurable
- **MQTT** - Contrôle temps réel avec précision microseconde
- **Synchronisation temporelle** - Via MQTT (précision μs)

### I/O
- **Entrées**: INPUT, INPUT_PULLUP, INPUT_PULLDOWN
- **Sorties**: Avec état par défaut configurable
- **Détection de changement**: Réactivité 1ms via tâche FreeRTOS
- **Commandes programmées**: Exécution avec précision microseconde

### Interface
- **Web UI**: Configuration complète depuis le navigateur
- **API REST**: Contrôle et statut en JSON
- **OTA**: Mises à jour firmware via web

### Pont Série (Serial Bridge)

- **Serial Bridge (RS232/RS485)**: Le contrôleur peut acheminer des messages série vers/depuis un port UART (utilise `Serial2`).
- **Usage principal**: Permet d'envoyer des messages série (ex. format RS232 pour KUKA VKRC2) et de consulter un journal des échanges depuis l'interface Web ou via l'API REST.
- **Pins configurables**: Broches RX/TX configurables depuis l'interface (par défaut `RX=4`, `TX=5`).
- **Paramètres**: Activation, `baudrate`, `RX`, `TX` sont persistés dans Preferences.

## Configuration Matérielle

### WT32-ETH01
- **Ethernet**: PHY LAN8720
  - MDC: GPIO 23
  - MDIO: GPIO 18
  - Power: GPIO 16
  - CLK: GPIO 0
- **LED Status**: GPIO 2
- **Bouton BOOT**: GPIO 0 (triple-press pour reset WiFi)

### GPIO Disponibles
Les GPIO suivants sont disponibles pour configuration:
- GPIO 4, 5, 12, 13, 14, 15, 17, 32, 33

**⚠️ GPIO Réservés:**
- 0, 2, 16, 18, 23 (Ethernet + LED)

## Installation

### PlatformIO

## API MQTT

La communication avec le module via MQTT est documentée dans le fichier [MQTT_API.md](MQTT_API.md).
```bash
# Cloner le projet
cd ESP32-ETH01-WifiMQTT-Controller

# Compiler et uploader
pio run -t upload

# Uploader le système de fichiers SPIFFS
pio run -t uploadfs

# Monitor série
pio device monitor -b 115200
```

## Configuration

### Première utilisation
1. Connecter le câble Ethernet
2. Le système démarre automatiquement en mode Ethernet
3. Accéder à l'interface web via l'IP DHCP ou IP statique configurée
4. Configurer les I/O, MQTT et autres paramètres

### Fallback WiFi
Si Ethernet échoue:
1. Le système bascule automatiquement sur WiFi
2. Un portail captif s'ouvre: **ESP32-Controller-Setup**
3. Connecter au WiFi et configurer les credentials
4. L'appareil redémarre et se connecte au WiFi

### Reset WiFi
Triple-appui sur le bouton BOOT dans les 5 secondes au démarrage pour effacer les credentials WiFi.

## API REST

### Statut
```http
GET /api/status
```
Retourne le statut complet du système (réseau, MQTT, I/O, heure)

### Contrôle I/O
```http
POST /api/io/set
Content-Type: application/json

{
  "name": "relay1",
  "state": true
}
```

### Configuration I/O
```http
GET /api/ios
POST /api/ios
```

### Configuration Système
```http
GET /api/config
POST /api/config
```

La configuration système expose désormais les paramètres du pont série:

- `useSerialBridge` (bool): activer/désactiver le pont série
- `serialRxPin` (int): broche RX utilisée par `Serial2`
- `serialTxPin` (int): broche TX utilisée par `Serial2`
- `serialBaudRate` (int): vitesse en bauds

Exemple payload pour `POST /api/config` (JSON):

```json
{
  "useSerialBridge": true,
  "serialRxPin": 4,
  "serialTxPin": 5,
  "serialBaudRate": 9600
}
```

### Pont Série — Envoi de message
```http
POST /api/serial/send
Content-Type: application/json

{ "message": "...\n" }
```
Envoie la chaîne fournie au port série (ajoute un saut de ligne si nécessaire). Retourne JSON `{ "success": true }` en cas de succès.

### Pont Série — Récupération des logs
```http
GET /api/serial/logs
```
Retourne un tableau JSON de logs série (timestamp, direction `TX|RX`, message).

## MQTT

### Topics

#### Contrôle
```
{deviceName}/control/{ioName}/set
Payload: {"state": 1, "exec_at": 1234567890, "exec_at_us": 123456}
```

#### Statut
```
{deviceName}/status/{ioName}
Payload: {"state": 1, "timestamp": 1234567890, "us": 123456}
```

#### Synchronisation temporelle
```
esp32/time/sync
Payload: {"seconds": 1234567890, "us": 123456, "compensations": {...}}
```

#### Mesure latence
```
{deviceName}/ping
{deviceName}/pong
```

## Dépendances

- ESP32 Arduino Core
- ESPAsyncWebServer
- AsyncTCP
- ArduinoJson 7.x
- PubSubClient (MQTT)
- WiFiManager
- ElegantOTA
- NTPClient

## Notes sur la configuration du Pont Série

- Les préférences sont sauvegardées sous les clés: `useSerial` (bool), `serRx` (int), `serTx` (int), `serBaud` (long).
- Par défaut, si `useSerial` = `false`, le port série externe n'est pas initialisé.
- Le gestionnaire série utilise `Serial2`; vérifiez la compatibilité des broches sur votre WT32-ETH01 avant d'assigner des GPIO réservés (voir section GPIO Disponibles / Réservés).

## Différences avec ESP32-WifiMQTTRelay

1. **Ethernet prioritaire**: WT32-ETH01 démarre en mode Ethernet par défaut
2. **LED Status**: GPIO 2 au lieu de GPIO 23
3. **Nom par défaut**: `esp32-eth01` au lieu de `esp32`
4. **Config automatique**: Force le mode Ethernet au premier démarrage

## Licence

Ce projet est dérivé de ESP32-WifiMQTTRelay et adapté pour WT32-ETH01.

## Notes

- La LED sur GPIO 2 indique le statut (clignotements pendant l'initialisation)
- Ethernet est prioritaire sur WiFi pour une latence minimale
- Le fallback WiFi permet une haute disponibilité
- La synchronisation temporelle MQTT permet une précision microseconde
- Les commandes programmées sont exécutées avec précision microseconde
# ESP32-ETH01-WifiMQTT-Controller

Contrôleur générique d'I/O pour WT32-ETH01 avec Ethernet prioritaire et fallback WiFi.

## Description

Ce projet est basé sur le ESP32-WifiMQTTRelay avec les adaptations suivantes pour la board WT32-ETH01:
- **Ethernet par défaut**: Connexion Ethernet prioritaire pour une latence minimale
- **Fallback WiFi**: Bascule automatique sur WiFi si Ethernet échoue
- **MQTT**: Contrôle et monitoring via MQTT avec synchronisation temporelle microseconde
- **Interface Web**: Configuration complète via interface web (SPIFFS)
- **I/O configurables**: Jusqu'à 20 I/O configurables (entrées/sorties)
- **OTA**: Mise à jour Over-The-Air via ElegantOTA

## Caractéristiques

### Réseau
- **Ethernet WT32-ETH01 (LAN8720)** - Connexion prioritaire
- **WiFi** - Fallback automatique avec WiFiManager
- **IP statique ou DHCP** - Configurable
- **MQTT** - Contrôle temps réel avec précision microseconde
- **Synchronisation temporelle** - Via MQTT (précision μs)

### I/O
- **Entrées**: INPUT, INPUT_PULLUP, INPUT_PULLDOWN
- **Sorties**: Avec état par défaut configurable
- **Détection de changement**: Réactivité 1ms via tâche FreeRTOS
- **Commandes programmées**: Exécution avec précision microseconde

### Interface
- **Web UI**: Configuration complète depuis le navigateur
- **API REST**: Contrôle et statut en JSON
- **OTA**: Mises à jour firmware via web

## Configuration Matérielle

### WT32-ETH01
- **Ethernet**: PHY LAN8720
  - MDC: GPIO 23
  - MDIO: GPIO 18
  - Power: GPIO 16
  - CLK: GPIO 0
- **LED Status**: GPIO 2
- **Bouton BOOT**: GPIO 0 (triple-press pour reset WiFi)

### GPIO Disponibles
Les GPIO suivants sont disponibles pour configuration:
- GPIO 4, 5, 12, 13, 14, 15, 17, 32, 33

**⚠️ GPIO Réservés:**
- 0, 2, 16, 18, 23 (Ethernet + LED)

## Installation

### PlatformIO

## API MQTT

La communication avec le module via MQTT est documentée dans le fichier [MQTT_API.md](MQTT_API.md).
```bash
# Cloner le projet
cd ESP32-ETH01-WifiMQTT-Controller

# Compiler et uploader
pio run -t upload

# Uploader le système de fichiers SPIFFS
pio run -t uploadfs

# Monitor série
pio device monitor -b 115200
```

## Configuration

### Première utilisation
1. Connecter le câble Ethernet
2. Le système démarre automatiquement en mode Ethernet
3. Accéder à l'interface web via l'IP DHCP ou IP statique configurée
4. Configurer les I/O, MQTT et autres paramètres

### Fallback WiFi
Si Ethernet échoue:
1. Le système bascule automatiquement sur WiFi
2. Un portail captif s'ouvre: **ESP32-Controller-Setup**
3. Connecter au WiFi et configurer les credentials
4. L'appareil redémarre et se connecte au WiFi

### Reset WiFi
Triple-appui sur le bouton BOOT dans les 5 secondes au démarrage pour effacer les credentials WiFi.

## API REST

### Statut
```http
GET /api/status
```
Retourne le statut complet du système (réseau, MQTT, I/O, heure)

### Contrôle I/O
```http
POST /api/io/set
Content-Type: application/json

{
  "name": "relay1",
  "state": true
}
```

### Configuration I/O
```http
GET /api/ios
POST /api/ios
```

### Configuration Système
```http
GET /api/config
POST /api/config
```

## MQTT

### Topics

#### Contrôle
```
{deviceName}/control/{ioName}/set
Payload: {"state": 1, "exec_at": 1234567890, "exec_at_us": 123456}
```

#### Statut
```
{deviceName}/status/{ioName}
Payload: {"state": 1, "timestamp": 1234567890, "us": 123456}
```

#### Synchronisation temporelle
```
esp32/time/sync
Payload: {"seconds": 1234567890, "us": 123456, "compensations": {...}}
```

#### Mesure latence
```
{deviceName}/ping
{deviceName}/pong
```

## Dépendances

- ESP32 Arduino Core
- ESPAsyncWebServer
- AsyncTCP
- ArduinoJson 7.x
- PubSubClient (MQTT)
- WiFiManager
- ElegantOTA
- NTPClient

## Différences avec ESP32-WifiMQTTRelay

1. **Ethernet prioritaire**: WT32-ETH01 démarre en mode Ethernet par défaut
2. **LED Status**: GPIO 2 au lieu de GPIO 23
3. **Nom par défaut**: `esp32-eth01` au lieu de `esp32`
4. **Config automatique**: Force le mode Ethernet au premier démarrage

## Licence

Ce projet est dérivé de ESP32-WifiMQTTRelay et adapté pour WT32-ETH01.

## Notes

- La LED sur GPIO 2 indique le statut (clignotements pendant l'initialisation)
- Ethernet est prioritaire sur WiFi pour une latence minimale
- Le fallback WiFi permet une haute disponibilité
- La synchronisation temporelle MQTT permet une précision microseconde
- Les commandes programmées sont exécutées avec précision microseconde
