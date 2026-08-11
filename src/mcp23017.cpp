#include "mcp23017.h"
#include <Wire.h>
#include <Preferences.h>
#include "config.h"

// ===== Registres MCP23017 (IOCON.BANK = 0, réglage d'usine) =====
#define MCP_REG_IODIRA   0x00
#define MCP_REG_IODIRB   0x01
#define MCP_REG_GPINTENB 0x05
#define MCP_REG_DEFVALB  0x07
#define MCP_REG_INTCONB  0x09
#define MCP_REG_GPPUB    0x0D
#define MCP_REG_INTCAPB  0x11
#define MCP_REG_GPIOB    0x13
#define MCP_REG_OLATA    0x14

static const uint8_t MCP_I2C_ADDR = 0x20; // CJMCU-2317 : A0/A1/A2 reliés à GND par défaut

static uint8_t s_olatA = 0x00;  // cache de l'état écrit sur le port A (sorties)
static uint8_t s_gpioB = 0x00;  // cache de la dernière lecture du port B (entrées)
static bool s_present = false;

static volatile bool s_intPending = false;
static void IRAM_ATTR mcpIsr() { s_intPending = true; }

extern void logMessage(const String& message);
extern void logPrintf(const char* fmt, ...);
extern Preferences preferences;
extern IOPin ioPins[];
extern int ioPinCount;
extern void saveIOs();

static bool mcpWriteReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(MCP_I2C_ADDR);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

static bool mcpReadReg(uint8_t reg, uint8_t &value) {
  Wire.beginTransmission(MCP_I2C_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false; // repeated start, pas de STOP
  if (Wire.requestFrom(MCP_I2C_ADDR, (uint8_t)1) != 1) return false;
  value = Wire.read();
  return true;
}

void mcpIoBegin() {
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);

  pinMode(MCP23017_INTB, INPUT); // GPIO39 est input-only, sans pull possible sur l'ESP32

  uint8_t probe;
  s_present = mcpReadReg(MCP_REG_IODIRA, probe);
  if (!s_present) {
    logPrintf("⚠️ MCP23017 introuvable sur le bus I2C (addr 0x%02X, SDA=IO%d, SCL=IO%d). Les broches MCP_A*/MCP_B* resteront inactives.",
              MCP_I2C_ADDR, I2C_SDA, I2C_SCL);
    return;
  }

  // Port A (A0-A7) = sorties, Port B (B0-B7) = entrées avec pull-up interne
  mcpWriteReg(MCP_REG_IODIRA, 0x00);
  mcpWriteReg(MCP_REG_IODIRB, 0xFF);
  mcpWriteReg(MCP_REG_GPPUB, 0xFF);
  mcpWriteReg(MCP_REG_OLATA, s_olatA);

  // Interruption sur changement d'état, sur les 8 broches du port B,
  // comparée à la valeur précédente (pas à une valeur de référence fixe).
  mcpWriteReg(MCP_REG_DEFVALB, 0x00);
  mcpWriteReg(MCP_REG_INTCONB, 0x00);
  mcpWriteReg(MCP_REG_GPINTENB, 0xFF);

  uint8_t initial;
  if (mcpReadReg(MCP_REG_GPIOB, initial)) {
    s_gpioB = initial;
  }

  attachInterrupt(digitalPinToInterrupt(MCP23017_INTB), mcpIsr, FALLING);

  logPrintf("✓ MCP23017 détecté (0x%02X) — Port A = sorties (A0-A7), Port B = entrées (B0-B7).", MCP_I2C_ADDR);
}

bool mcpIoIsPresent() { return s_present; }

void mcpIoService() {
  if (!s_present || !s_intPending) return;
  s_intPending = false;

  // La lecture de GPIOB donne la valeur courante et efface l'interruption
  // (comme le ferait une lecture de INTCAPB, mais GPIOB reflète l'état réel
  // même si plusieurs changements sont arrivés entre deux passages).
  uint8_t value;
  if (mcpReadReg(MCP_REG_GPIOB, value)) {
    s_gpioB = value;
  }
}

void mcpIoPinMode(int pin, uint8_t mode, uint8_t inputType) {
  // Le sens (entrée/sortie) est fixé une fois pour toutes par le câblage
  // (port A = sorties, port B = entrées) et configuré dans mcpIoBegin().
  // Cette fonction existe juste pour que les appelants traitent les broches
  // virtuelles de façon uniforme avec les GPIO natifs.
  (void)pin; (void)mode; (void)inputType;
}

void mcpIoWrite(int pin, bool state) {
  if (!s_present || !isMcpOutputPin(pin)) return;
  uint8_t bit = pin - MCP_VPIN_A0;
  if (state) s_olatA |= (1 << bit);
  else       s_olatA &= ~(1 << bit);
  mcpWriteReg(MCP_REG_OLATA, s_olatA);
}

bool mcpIoRead(int pin) {
  if (!s_present) return false;
  if (isMcpInputPin(pin)) {
    uint8_t bit = pin - MCP_VPIN_B0;
    return (s_gpioB >> bit) & 0x01;
  }
  if (isMcpOutputPin(pin)) {
    uint8_t bit = pin - MCP_VPIN_A0;
    return (s_olatA >> bit) & 0x01;
  }
  return false;
}

void mcpEnsureDefaultIOsRegistered() {
  if (preferences.getBool("mcpIosAdded", false)) return;

  bool added = false;
  for (int n = 0; n < 8 && ioPinCount < MAX_IOS; n++) {
    int pin = MCP_VPIN_A0 + n;
    bool exists = false;
    for (int i = 0; i < ioPinCount; i++) if (ioPins[i].pin == pin) { exists = true; break; }
    if (exists) continue;

    IOPin &io = ioPins[ioPinCount++];
    snprintf(io.name, sizeof(io.name), "MCP_A%d", n);
    io.pin = pin;
    io.mode = 2; // OUTPUT
    io.inputType = 0;
    io.state = false;
    io.defaultState = false;
    added = true;
  }
  for (int n = 0; n < 8 && ioPinCount < MAX_IOS; n++) {
    int pin = MCP_VPIN_B0 + n;
    bool exists = false;
    for (int i = 0; i < ioPinCount; i++) if (ioPins[i].pin == pin) { exists = true; break; }
    if (exists) continue;

    IOPin &io = ioPins[ioPinCount++];
    snprintf(io.name, sizeof(io.name), "MCP_B%d", n);
    io.pin = pin;
    io.mode = 1; // INPUT
    io.inputType = 1; // pull-up (géré côté MCP23017, pas côté ESP32)
    io.state = false;
    io.defaultState = false;
    added = true;
  }

  if (added) {
    saveIOs();
    logMessage("✓ 16 I/O virtuels MCP23017 (MCP_A0-A7, MCP_B0-B7) ajoutés à la configuration.");
  }
  preferences.putBool("mcpIosAdded", true);
}
