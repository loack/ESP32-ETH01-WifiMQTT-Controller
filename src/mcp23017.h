#ifndef MCP23017_H
#define MCP23017_H

#include <Arduino.h>

// ===== MCP23017 (module CJMCU-2317) — expandeur I/O I2C =====
// Port A (A0-A7) = SORTIES, Port B (B0-B7) = ENTRÉES (pull-up interne).
// SDA/SCL/INTA/INTB sont définis dans config.h.
//
// Pour brancher l'expandeur dans le pipeline générique existant (IOPin[],
// MQTT, WebSocket, toggles web) sans dupliquer tout ce code, chaque broche
// A0-A7 / B0-B7 est représentée comme une "broche virtuelle" avec un numéro
// de pin fictif >= MCP_VPIN_BASE. Il suffit de l'ajouter comme un I/O normal
// (page "I/O" de l'interface web) :
//   - Sorties : pin 100 à 107  → A0 à A7 (mode = Sortie)
//   - Entrées : pin 108 à 115  → B0 à B7 (mode = Entrée)
// applyIOPinModes()/handleIOs()/executeCommand() détectent ces numéros de
// pin via isMcpVirtualPin() et appellent mcpIoPinMode()/mcpIoWrite()/
// mcpIoRead() à la place des fonctions digitalRead/digitalWrite/pinMode
// natives — le reste (MQTT, WebSocket, logs, scheduler) fonctionne déjà tel
// quel puisqu'il ne connaît que le tableau ioPins[].

#define MCP_VPIN_BASE  100
#define MCP_VPIN_A0    (MCP_VPIN_BASE)       // 100
#define MCP_VPIN_A7    (MCP_VPIN_BASE + 7)   // 107
#define MCP_VPIN_B0    (MCP_VPIN_BASE + 8)   // 108
#define MCP_VPIN_B7    (MCP_VPIN_BASE + 15)  // 115

inline bool isMcpVirtualPin(int pin) { return pin >= MCP_VPIN_A0 && pin <= MCP_VPIN_B7; }
inline bool isMcpOutputPin(int pin)  { return pin >= MCP_VPIN_A0 && pin <= MCP_VPIN_A7; } // port A
inline bool isMcpInputPin(int pin)   { return pin >= MCP_VPIN_B0 && pin <= MCP_VPIN_B7; } // port B

// Initialise le bus I2C (Wire), configure IODIR/GPPU/interruptions du
// MCP23017 et attache l'ISR sur INTB. À appeler une fois dans setup(),
// après applyIOPinModes() pour que les états par défaut des sorties A0-A7
// (issus du tableau ioPins[]) soient déjà connus.
void mcpIoBegin();

// À appeler très régulièrement (tâche handleIOs, ~1ms). Ne fait une
// transaction I2C que si une interruption INTB est en attente ; sinon c'est
// un simple test de booléen, quasiment gratuit.
void mcpIoService();

// Équivalents de pinMode()/digitalWrite()/digitalRead() pour les broches
// virtuelles. Les appelants doivent tester isMcpVirtualPin(pin) avant de
// basculer vers les fonctions GPIO natives.
void mcpIoPinMode(int pin, uint8_t mode, uint8_t inputType);
void mcpIoWrite(int pin, bool state);
bool mcpIoRead(int pin);

// true si le MCP23017 a répondu sur le bus I2C au démarrage.
bool mcpIoIsPresent();

// Ajoute automatiquement les 16 IOPin virtuels (MCP_A0..A7 en sortie,
// MCP_B0..B7 en entrée) au premier démarrage suivant cette mise à jour, s'ils
// ne sont pas déjà présents. Ne fait rien ensuite (flag persistant).
void mcpEnsureDefaultIOsRegistered();

#endif // MCP23017_H
