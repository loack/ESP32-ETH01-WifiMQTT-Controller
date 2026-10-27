#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

#define MAX_IOS 40  // 20 IOs natifs + 16 broches virtuelles MCP23017 (A0-A7 / B0-B7) + marge
#define MAX_LOG_LINES 64


// ===== CONFIGURATION PINS — WT32-ETH01 =====
// GPIO réservés par le LAN8720A (Ethernet RMII) — NE PAS UTILISER :
//   IO0 (REF_CLK), IO17 (TX_EN), IO19 (TXD0), IO22 (TXD1),
//   IO21 (CRS_DV), IO25 (RXD0), IO26 (RXD1), IO18 (MDIO), IO23 (MDC)
// GPIO réservés UART0 (prog/debug) : IO1 (TX), IO3 (RX)

// --- Modules relais (via module optocoupleur/relais AliExpress) ---
// IO16 n'est pas exposé sur les headers du WT32-ETH01 → remplacé par IO4
// IO17 conflicte avec Ethernet TX_EN → remplacé par IO5
#define RELAY_K1        4   // IO4  — sortie relais K1
#define RELAY_K2        5   // IO5  — sortie relais K2 (pull-up interne au boot, inoffensif)

// --- Driver moteur pas-à-pas DM556 (optionnel) ---
// Le moteur s'active/désactive depuis l'interface web (onglet "Configuration
// I/O"). Les broches ci-dessous ne sont que les valeurs par défaut : elles
// sont modifiables et persistées dans Config (motorPulPin/DirPin/EnaPin).
// Moteur désactivé = le firmware ne touche à aucune de ces broches, elles
// restent libres pour des I/O génériques.
#define MOTOR_DEFAULT_ENABLED true // conserve le comportement des cartes déjà câblées
#define MOTOR_PUL       4   // IO4  — signal PUL+ (pas)
#define MOTOR_DIR      14   // IO14 — signal DIR+ (direction)
#define MOTOR_ENA      15   // IO15 — signal EN+ (enable, actif-bas) — pull-up au boot = moteur désactivé ✓

// --- I²C vers module CJMCU-2317 (MCP23017) ---
#define I2C_SDA        33   // IO33 — SDA
#define I2C_SCL        32   // IO32 — SCL
#define MCP23017_INTA  36   // IO36 — interruption port A (non utilisée : port A = sorties)
#define MCP23017_INTB  39   // IO39 — interruption port B (input-only, change d'état sur les entrées B0-B7)

// ===== STRUCTURES =====
// Structure for a single configurable I/O pin
struct IOPin {
  uint8_t pin;
  char name[32];
  uint8_t mode; // 0 = DISABLED, 1 = INPUT, 2 = OUTPUT
  uint8_t inputType; // For inputs: 0 = INPUT, 1 = INPUT_PULLUP, 2 = INPUT_PULLDOWN
  bool state;   // Current state (for outputs) or last read state (for inputs)
  bool defaultState; // Default state at boot for outputs
};


struct AccessLog {
  char timestamp[25];
  char ip[16];
  char resource[50];
};

// Maximum number of scheduled commands
#define MAX_SCHEDULED_COMMANDS 10

struct ScheduledCommand {
  bool active;
  int pin;
  int state;
  uint32_t exec_at_sec;  // Unix timestamp en secondes
  uint32_t exec_at_us;   // Microsecondes (0-999999)
};


// Main configuration structure
struct Config {
  char deviceName[32];
  char adminPassword[32];
  
  // Network settings
  bool useEthernet;      // true = Ethernet, false = WiFi
  char ethernetType[16]; // "WT32-ETH01" ou autres types futurs
  bool useStaticIP;
  char staticIP[16];
  char staticGateway[16];
  char staticSubnet[16];

  // MQTT Settings
  char mqttServer[64];
  int mqttPort;
  char mqttUser[32];
  char mqttPassword[32];
  char mqttTopic[32];

  // NTP Settings
  char ntpServer[64];
  long gmtOffset_sec;
  int daylightOffset_sec;

  // Serial Bridge Settings
  bool useSerialBridge;
  int serialRxPin;
  int serialTxPin;
  long serialBaudRate;

  // Stepper motor (DM556) settings — optionnel
  bool motorEnabled;
  uint8_t motorPulPin;
  uint8_t motorDirPin;
  uint8_t motorEnaPin;

  bool initialized;
};

#endif // CONFIG_H
