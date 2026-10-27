#include "stepper_motor.h"
#include "config.h"
#include <math.h>

extern void logMessage(const String& message);
extern void logPrintf(const char* fmt, ...);
extern Config config;

static hw_timer_t *s_timer = NULL;

// Copie des réglages lus au boot (un changement de config impose un
// redémarrage) : évite que l'ISR lise une broche modifiée à chaud.
static bool s_enabled = false;
static uint8_t s_pulPin = MOTOR_PUL;
static uint8_t s_dirPin = MOTOR_DIR;
static uint8_t s_enaPin = MOTOR_ENA;

static volatile bool s_running = false;
static volatile bool s_pulseHigh = false;
static volatile uint32_t s_stepsDone = 0;
static volatile uint32_t s_stepsTarget = 0;

// Profil de vitesse trapézoïdal (rampe d'accélération/décélération),
// recalculé pas par pas depuis l'ISR.
static volatile float s_currentSpeed = 0;  // pas/s, vitesse instantanée
static float s_targetSpeed = 0;            // pas/s, vitesse de croisière demandée
static float s_accel = 0;                  // pas/s², accélération/décélération

static bool s_lastForward = true;
static uint32_t s_lastSpeed = 0;
static uint32_t s_lastAccel = 0;

// Vitesse maximale acceptée (pas/seconde). Limite arbitraire raisonnable
// pour un DM556 piloté depuis l'ESP32 ; à ajuster selon le moteur/la charge.
#define MOTOR_MAX_SPEED 20000

// Accélération par défaut si non précisée par l'appelant (pas/s²). Sans
// rampe, un saut instantané de 0 à quelques centaines/milliers de pas/s
// dépasse le couple de démarrage du moteur : il broute sur place au lieu de
// tourner. On démarre donc toujours à MOTOR_MIN_START_SPEED puis on
// accélère/décélère à cette valeur, ajustable via le paramètre "accel".
#define MOTOR_DEFAULT_ACCEL   2000
#define MOTOR_MIN_START_SPEED 50.0f // pas/s : vitesse de démarrage "à froid" typique d'un pas-à-pas

// Appelée 2x par pas (front montant = 1 pas, front descendant = creux du
// pulse). Le driver DM556 déclenche sur le front montant de PUL+. La période
// du timer est recalculée à chaque pas pour suivre la rampe de vitesse.
static void IRAM_ATTR onMotorTimer() {
  if (!s_running) return;
  s_pulseHigh = !s_pulseHigh;
  digitalWrite(s_pulPin, s_pulseHigh ? HIGH : LOW);
  if (!s_pulseHigh) return; // seul le front montant compte comme un pas

  s_stepsDone++;
  if (s_stepsDone >= s_stepsTarget) {
    s_running = false;
    digitalWrite(s_pulPin, LOW);
    timerAlarmDisable(s_timer);
    return;
  }

  // Vitesse maximale à laquelle on peut encore décélérer jusqu'à l'arrêt
  // exactement au dernier pas restant : v = sqrt(2 * accel * distance).
  uint32_t remaining = s_stepsTarget - s_stepsDone;
  float decelLimit = sqrtf(2.0f * s_accel * (float)remaining);
  float desired = fminf(s_targetSpeed, decelLimit);

  float dt = 1.0f / s_currentSpeed; // durée du pas qui vient de s'écouler
  float maxDelta = s_accel * dt;
  float speed = s_currentSpeed;
  if (speed < desired) {
    speed = fminf(desired, speed + maxDelta);
  } else if (speed > desired) {
    speed = fmaxf(desired, speed - maxDelta);
  }
  if (speed < 1.0f) speed = 1.0f; // garde-fou division par zéro
  s_currentSpeed = speed;

  uint64_t halfPeriodUs = (uint64_t)(500000.0f / speed);
  if (halfPeriodUs < 3) halfPeriodUs = 3; // largeur mini de pulse ~2.5µs pour le DM556
  timerAlarmWrite(s_timer, halfPeriodUs, true);
}

// GPIO de sortie exposés sur les headers du WT32-ETH01 et non utilisés par
// le firmware (Ethernet, UART0, I2C MCP23017 = IO32/IO33, LED = IO2).
// IO35/36/39 sont input-only.
static const uint8_t MOTOR_ALLOWED_PINS[] = { 4, 5, 12, 14, 15 };

bool motorValidatePins(int pul, int dir, int ena, String &errorMsg) {
  const int pins[3] = { pul, dir, ena };
  const char *names[3] = { "PUL+", "DIR+", "EN+" };
  for (int i = 0; i < 3; i++) {
    bool allowed = false;
    for (uint8_t p : MOTOR_ALLOWED_PINS) {
      if (pins[i] == p) { allowed = true; break; }
    }
    if (!allowed) {
      errorMsg = String("Broche ") + names[i] + " invalide (IO" + String(pins[i]) + ") : choisir parmi IO4, IO5, IO12, IO14, IO15";
      return false;
    }
  }
  if (pul == dir || pul == ena || dir == ena) {
    errorMsg = "Les broches PUL+, DIR+ et EN+ doivent être différentes";
    return false;
  }
  return true;
}

void motorBegin() {
  s_enabled = config.motorEnabled;
  if (!s_enabled) {
    logMessage("Moteur pas-à-pas DM556 désactivé : broches PUL/DIR/EN libres pour les I/O.");
    return;
  }

  String err;
  if (!motorValidatePins(config.motorPulPin, config.motorDirPin, config.motorEnaPin, err)) {
    logPrintf("⚠️ Config moteur invalide (%s) : moteur désactivé.", err.c_str());
    s_enabled = false;
    return;
  }
  s_pulPin = config.motorPulPin;
  s_dirPin = config.motorDirPin;
  s_enaPin = config.motorEnaPin;

  pinMode(s_pulPin, OUTPUT);
  pinMode(s_dirPin, OUTPUT);
  pinMode(s_enaPin, OUTPUT);
  digitalWrite(s_pulPin, LOW);
  digitalWrite(s_dirPin, LOW);
  digitalWrite(s_enaPin, HIGH); // driver désactivé au repos (EN+ actif bas)

  s_timer = timerBegin(0, 80, true); // timer HW #0, prédiviseur 80 -> 1 tick = 1µs (APB 80MHz)
  timerAttachInterrupt(s_timer, &onMotorTimer, true);

  logPrintf("✓ Moteur pas-à-pas DM556 initialisé (PUL=IO%d, DIR=IO%d, EN=IO%d)", s_pulPin, s_dirPin, s_enaPin);
}

bool motorIsEnabled() { return s_enabled; }

bool motorIsPinUsed(int pin) {
  return s_enabled && (pin == s_pulPin || pin == s_dirPin || pin == s_enaPin);
}

bool motorStart(uint32_t steps, bool forward, uint32_t speedStepsPerSec, uint32_t accelStepsPerSec2, String &errorMsg) {
  if (!s_enabled) {
    errorMsg = "Moteur désactivé (voir Configuration I/O)";
    return false;
  }
  if (s_running) {
    errorMsg = "Un mouvement est déjà en cours";
    return false;
  }
  if (steps == 0) {
    errorMsg = "Nombre de pas invalide";
    return false;
  }
  if (speedStepsPerSec == 0 || speedStepsPerSec > MOTOR_MAX_SPEED) {
    errorMsg = "Vitesse invalide (1 à " + String(MOTOR_MAX_SPEED) + " pas/s)";
    return false;
  }

  digitalWrite(s_dirPin, forward ? HIGH : LOW);
  delayMicroseconds(10); // temps de setup DIR->PUL requis par le DM556 avant le premier pas
  digitalWrite(s_enaPin, LOW); // active le driver

  s_pulseHigh = false;
  digitalWrite(s_pulPin, LOW);
  s_stepsDone = 0;
  s_stepsTarget = steps;
  s_lastForward = forward;
  s_lastSpeed = speedStepsPerSec;
  s_lastAccel = (accelStepsPerSec2 > 0) ? accelStepsPerSec2 : MOTOR_DEFAULT_ACCEL;

  s_targetSpeed = (float)speedStepsPerSec;
  s_accel = (float)s_lastAccel;
  // Démarre directement à la vitesse cible si elle est déjà basse (pas
  // besoin de rampe), sinon à une vitesse de démarrage "à froid" sûre.
  s_currentSpeed = fminf(s_targetSpeed, MOTOR_MIN_START_SPEED);

  uint64_t halfPeriodUs = (uint64_t)(500000.0f / s_currentSpeed);
  if (halfPeriodUs < 3) halfPeriodUs = 3;

  s_running = true;
  timerAlarmWrite(s_timer, halfPeriodUs, true);
  timerAlarmEnable(s_timer);

  logPrintf("▶️ Moteur : %u pas, sens %s, vitesse %u pas/s, accel %u pas/s²",
            (unsigned)steps, forward ? "AVANT" : "ARRIERE", (unsigned)speedStepsPerSec, (unsigned)s_lastAccel);
  return true;
}

void motorStop() {
  if (!s_enabled) return;
  s_running = false;
  timerAlarmDisable(s_timer);
  digitalWrite(s_pulPin, LOW);
  digitalWrite(s_enaPin, HIGH);
  logMessage("⏹ Moteur arrêté.");
}

bool motorIsRunning() { return s_running; }
uint32_t motorGetStepsDone() { return s_stepsDone; }
uint32_t motorGetStepsTarget() { return s_stepsTarget; }
bool motorGetLastDirection() { return s_lastForward; }
uint32_t motorGetLastSpeed() { return s_lastSpeed; }
uint32_t motorGetLastAccel() { return s_lastAccel; }
