// Logique de securite de la liaison UDP : validation de la broche de securite (WT32-ETH01),
// decision du niveau de sortie, expiration du deadman, classement de l'etat de liaison.
// Module PUR (sans Arduino), teste sur PC. Cette broche N'EST PAS une fonction de securite
// certifiee : elle ne remplace pas le circuit cable independant (T-027).
#ifndef UDP_SAFETY_H
#define UDP_SAFETY_H

#include <stdint.h>

namespace udps {

enum PinVerdict {
  PIN_OK = 0,
  PIN_RANGE,        // hors 0..39
  PIN_ETH,          // Ethernet RMII / alimentation PHY du WT32-ETH01
  PIN_FLASH,        // flash SPI (6..11)
  PIN_STRAP,        // broche de strapping au demarrage (0, 2, 5, 12, 15)
  PIN_UART,         // UART0 console/programmation (1, 3)
  PIN_INPUT_ONLY,   // 34..39 : entrees seulement
  PIN_I2C,          // bus I2C du MCP23017 (32, 33)
  PIN_VIRTUAL,      // broche virtuelle MCP23017 (>= 100)
  PIN_NOT_ALLOWED,  // pas dans la liste blanche des sorties libres
  PIN_IN_USE,       // deja utilisee (E/S configuree, moteur, pont serie)
};

const char* verdictText(PinVerdict v);

// Liste blanche des GPIO de sortie utilisables : 4, 13, 14 (voir la specification, section 9).
// inUseMask : bit n = GPIO n deja utilise par la configuration courante.
PinVerdict validateSafetyPin(int pin, uint64_t inUseMask);

enum SafetyMode : uint8_t {
  MODE_FAULT = 0,      // niveau actif = alarme, sinon niveau inactif
  MODE_HEARTBEAT = 1,  // signal carre tant que tout va bien ; fige au niveau actif sur defaut
};

struct SafetyCfg {
  bool enabled;
  int8_t pin;            // -1 = aucune
  uint8_t activeLevel;   // niveau (0/1) qui signifie ALARME
  uint8_t mode;          // SafetyMode
  uint16_t delayMs;      // delai sans HB avant alarme (10..60000)
  uint16_t periodMs;     // periode du signal carre en mode battement (10..5000)
};

constexpr uint16_t DELAY_MIN = 10, DELAY_MAX = 60000, PERIOD_MIN = 10, PERIOD_MAX = 5000;

// Valeurs par defaut : desactive, aucune broche.
SafetyCfg defaultSafetyCfg();

// Valide une configuration complete ; retourne nullptr si OK, sinon un message.
const char* validateSafetyCfg(const SafetyCfg& c, uint64_t inUseMask);

struct GuardInput {
  bool armed;          // OPEN recu et au moins un HB recu
  uint32_t hbAgeMs;    // age du dernier HB
  bool taskAlive;      // la tache UDP a tourne recemment
};

// Niveau a appliquer sur la broche ; asserted = l'alarme est declaree. -1 si desactive.
int safetyLevel(const SafetyCfg& c, const GuardInput& in, uint32_t nowMs, bool* asserted);

// Faux si deadman_ms == 0 (pas de watchdog) ou si la surveillance n'est pas armee.
bool deadmanExpired(const GuardInput& in, uint16_t deadmanMs);

// Validation d'un deadman_ms : 0 (inactif) ou 10..60000.
bool validDeadman(uint32_t ms);

// Etat de liaison vu de l'ESP32 (valeurs de udpp::LinkState).
uint8_t classifyLink(bool sessionOpen, bool pending, bool armed, uint32_t hbAgeMs, uint32_t hbPeriodMs, uint32_t lossMs);

}  // namespace udps

#endif
