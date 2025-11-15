# Changelog

## Version 1.0 - 2025-11-15

### Migration depuis ESP32-WifiMQTTRelay

#### Nouveautés WT32-ETH01
- ✅ **Ethernet prioritaire**: Connexion Ethernet par défaut avec LAN8720
- ✅ **Fallback WiFi automatique**: Bascule sur WiFi si Ethernet échoue
- ✅ **LED Status GPIO 2**: Adaptation pour la LED intégrée WT32-ETH01
- ✅ **Configuration automatique**: Force Ethernet au premier démarrage

#### Fonctionnalités héritées
- ✅ **MQTT**: Contrôle temps réel avec précision microseconde
- ✅ **Synchronisation temporelle**: Via MQTT avec compensation de latence
- ✅ **I/O configurables**: Jusqu'à 20 GPIO configurables
- ✅ **FreeRTOS**: Tâche dédiée pour détection d'entrées (1ms)
- ✅ **Commandes programmées**: Exécution avec précision microseconde
- ✅ **Interface Web**: Configuration complète via SPIFFS
- ✅ **OTA**: Mise à jour Over-The-Air via ElegantOTA
- ✅ **API REST**: Contrôle et configuration en JSON

#### Fichiers copiés/adaptés
- `src/config.h` - Structure de configuration (inchangé)
- `src/mqtt.h` - Interface MQTT (inchangé)
- `src/mqtt.cpp` - Logique MQTT complète (inchangé)
- `src/web_server.h` - Interface serveur web (inchangé)
- `src/web_server.cpp` - Logique serveur web (inchangé)
- `src/main.cpp` - Adapté pour WT32-ETH01
- `data/index.html` - Interface web complète (inchangé)

#### Modifications main.cpp
1. **STATUS_LED**: GPIO 23 → GPIO 2
2. **deviceName par défaut**: "esp32" → "esp32-eth01"
3. **useEthernet par défaut**: false → true
4. **Démarrage**: WiFi conditionnel → Ethernet prioritaire
5. **pinMode(STATUS_LED)**: Ajouté explicitement dans setup()

#### Configuration PlatformIO
- Ajout de toutes les dépendances (MQTT, JSON, WiFiManager, ElegantOTA)
- Flags de build pour ElegantOTA et debug
- Compatibilité soft pour librairies

#### Documentation
- README.md complet avec spécificités WT32-ETH01
- GPIO réservés documentés
- API REST documentée
- Topics MQTT documentés

### Notes techniques

#### Pinout WT32-ETH01
```
Ethernet (LAN8720):
- GPIO 0  : CLK_MODE
- GPIO 16 : PHY_POWER
- GPIO 18 : MDIO
- GPIO 23 : MDC

Status:
- GPIO 2  : LED intégrée

Disponibles pour I/O:
- GPIO 4, 5, 12, 13, 14, 15, 17, 32, 33
```

#### Comportement réseau
1. Tentative Ethernet (10 secondes timeout)
2. Si échec → Fallback WiFi automatique
3. WiFi utilise WiFiManager (portail captif si pas de config)
4. Triple-press BOOT pour reset WiFi credentials

#### MQTT et temps réel
- Synchronisation temporelle par MQTT (précision μs)
- Compensation automatique de latence réseau
- Commandes programmées avec timestamp μs
- Publication d'état avec timestamp μs

### Migration future

Pour revenir sur ESP32 WiFi standard:
1. Changer `board = wt32-eth01` → `board = esp32dev`
2. Changer `STATUS_LED 2` → `STATUS_LED 23`
3. Changer `useEthernet default true` → `false`

### Compatibilité

- **Hardware**: WT32-ETH01 exclusivement
- **Software**: ESP32 Arduino Core 2.x+
- **MQTT Broker**: Mosquitto, HiveMQ, etc.
- **Navigateurs**: Chrome, Firefox, Edge, Safari
