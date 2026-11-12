#include "udp_safety.h"

namespace udps {

const char* verdictText(PinVerdict v) {
  switch (v) {
    case PIN_OK: return "ok";
    case PIN_RANGE: return "GPIO hors plage (0-39)";
    case PIN_ETH: return "GPIO reserve a l'Ethernet (RMII/PHY) du WT32-ETH01";
    case PIN_FLASH: return "GPIO reserve a la memoire flash (6-11)";
    case PIN_STRAP: return "GPIO de strapping au demarrage (0, 2, 5, 12, 15)";
    case PIN_UART: return "GPIO de l'UART0 console/programmation (1, 3)";
    case PIN_INPUT_ONLY: return "GPIO en entree seulement (34-39)";
    case PIN_I2C: return "GPIO du bus I2C du MCP23017 (32, 33)";
    case PIN_VIRTUAL: return "broche virtuelle MCP23017 : pas un GPIO";
    case PIN_NOT_ALLOWED: return "GPIO hors liste blanche des sorties libres (4, 13, 14)";
    case PIN_IN_USE: return "GPIO deja utilise par une E/S, le moteur ou le pont serie";
  }
  return "inconnu";
}

PinVerdict validateSafetyPin(int pin, uint64_t inUseMask) {
  if (pin >= 100) return PIN_VIRTUAL;
  if (pin < 0 || pin > 39) return PIN_RANGE;
  // Ethernet WT32-ETH01 (LAN8720 RMII) : REF_CLK 0, MDIO 18, TXD0 19, TX_EN 21, TXD1 22, MDC 23,
  // RXD0 25, RXD1 26, CRS_DV 27 ; PHY power 16 ; 17 par prudence (commentaire du depot : conflit).
  switch (pin) {
    case 0: case 16: case 17: case 18: case 19: case 21: case 22: case 23: case 25: case 26: case 27:
      return PIN_ETH;
    default: break;
  }
  if (pin >= 6 && pin <= 11) return PIN_FLASH;
  if (pin == 2 || pin == 5 || pin == 12 || pin == 15) return PIN_STRAP;
  if (pin == 1 || pin == 3) return PIN_UART;
  if (pin >= 34) return PIN_INPUT_ONLY;
  if (pin == 32 || pin == 33) return PIN_I2C;
  if (pin != 4 && pin != 13 && pin != 14) return PIN_NOT_ALLOWED;
  if (inUseMask & (1ULL << pin)) return PIN_IN_USE;
  return PIN_OK;
}

SafetyCfg defaultSafetyCfg() {
  SafetyCfg c;
  c.enabled = false;
  c.pin = -1;
  c.activeLevel = 1;
  c.mode = MODE_FAULT;
  c.delayMs = 200;
  c.periodMs = 50;
  return c;
}

const char* validateSafetyCfg(const SafetyCfg& c, uint64_t inUseMask) {
  if (!c.enabled) return nullptr;  // une config desactivee n'est pas contrainte
  PinVerdict v = validateSafetyPin(c.pin, inUseMask);
  if (v != PIN_OK) return verdictText(v);
  if (c.activeLevel > 1) return "niveau actif : 0 ou 1";
  if (c.mode > MODE_HEARTBEAT) return "mode : 0 (defaut) ou 1 (battement)";
  if (c.delayMs < DELAY_MIN || c.delayMs > DELAY_MAX) return "delai : 10 a 60000 ms";
  if (c.mode == MODE_HEARTBEAT && (c.periodMs < PERIOD_MIN || c.periodMs > PERIOD_MAX)) return "periode : 10 a 5000 ms";
  return nullptr;
}

int safetyLevel(const SafetyCfg& c, const GuardInput& in, uint32_t nowMs, bool* asserted) {
  if (!c.enabled || c.pin < 0) {
    if (asserted) *asserted = false;
    return -1;
  }
  uint8_t active = c.activeLevel ? 1 : 0;
  bool healthy = in.armed && in.taskAlive && in.hbAgeMs <= c.delayMs;
  if (c.mode == MODE_FAULT) {
    // Alarme seulement si la surveillance est armee et que le HB manque (ou que la tache est figee).
    bool fault = in.armed && (!in.taskAlive || in.hbAgeMs > c.delayMs);
    if (asserted) *asserted = fault;
    return fault ? active : (active ? 0 : 1);
  }
  // Battement : signal carre tant que sain ; sinon fige au niveau actif (= pas d'impulsions).
  if (asserted) *asserted = !healthy;
  if (!healthy) return active;
  uint32_t half = c.periodMs / 2;
  if (half == 0) half = 1;
  uint8_t phase = (nowMs / half) & 1;
  return phase ? 1 : 0;
}

bool deadmanExpired(const GuardInput& in, uint16_t deadmanMs) {
  return in.armed && deadmanMs > 0 && in.hbAgeMs > deadmanMs;
}

bool validDeadman(uint32_t ms) { return ms == 0 || (ms >= 10 && ms <= 60000); }

uint8_t classifyLink(bool sessionOpen, bool pending, bool armed, uint32_t hbAgeMs, uint32_t hbPeriodMs, uint32_t lossMs) {
  if (!sessionOpen) return pending ? 1 : 0;      // PENDING / INACTIVE
  if (!armed) return 2;                           // ouverte, premier HB attendu
  if (hbAgeMs > lossMs) return 4;                 // LOST
  if (hbPeriodMs > 0 && hbAgeMs > 2 * hbPeriodMs) return 3;  // DEGRADED
  return 2;                                       // ESTABLISHED
}

}  // namespace udps
