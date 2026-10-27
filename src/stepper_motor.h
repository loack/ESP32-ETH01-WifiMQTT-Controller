#ifndef STEPPER_MOTOR_H
#define STEPPER_MOTOR_H

#include <Arduino.h>

// ===== Driver moteur pas-à-pas DM556 (optionnel) =====
// Activé/désactivé via config.motorEnabled. Broches configurables
// (config.motorPulPin / motorDirPin / motorEnaPin), par défaut
// PUL+ = IO4, DIR+ = IO14, EN+ = IO15 (actif bas).
// Les broches PUL-/DIR-/EN- du DM556 sont câblées sur le commun (GND ou 5V
// selon le schéma de la carte).
//
// Moteur désactivé : motorBegin() ne configure aucune broche ni timer, les
// broches restent disponibles pour des I/O génériques, et motorStart()
// refuse toute commande.
//
// Le train de pulses PUL est généré par un timer matériel (non bloquant) :
// un déplacement se lance via motorStart() et se termine seul après le
// nombre de pas demandé, sans bloquer loop() ni la tâche I/O.

// Lit la configuration (config.motorEnabled + broches) et initialise le
// driver uniquement si le moteur est activé.
void motorBegin();

// true si le moteur est activé dans la configuration (et donc initialisé).
bool motorIsEnabled();

// true si `pin` est occupée par le moteur (moteur activé uniquement).
bool motorIsPinUsed(int pin);

// Vérifie qu'un jeu de broches est utilisable pour le DM556 sur le
// WT32-ETH01 (GPIO de sortie, non réservé Ethernet/UART0/I2C, pas de doublon).
bool motorValidatePins(int pul, int dir, int ena, String &errorMsg);

// Démarre un déplacement de `steps` pas à `speedStepsPerSec` pas/seconde
// (vitesse de croisière), dans le sens `forward` (true) ou inverse (false).
// `accelStepsPerSec2` est l'accélération/décélération de la rampe trapézoïdale
// en pas/s² ; 0 = utiliser la valeur par défaut du firmware. Retourne false
// (et remplit errorMsg) si le moteur est désactivé, si un mouvement est déjà
// en cours ou si les paramètres sont invalides ; sinon le mouvement démarre
// immédiatement.
bool motorStart(uint32_t steps, bool forward, uint32_t speedStepsPerSec, uint32_t accelStepsPerSec2, String &errorMsg);

// Arrêt immédiat : coupe les pulses et désactive le driver (EN+ = HIGH).
// Sans effet si le moteur est désactivé.
void motorStop();

bool motorIsRunning();
uint32_t motorGetStepsDone();
uint32_t motorGetStepsTarget();
bool motorGetLastDirection();      // true = forward
uint32_t motorGetLastSpeed();      // pas/seconde (vitesse de croisière) du dernier mouvement lancé
uint32_t motorGetLastAccel();      // pas/s² utilisée pour le dernier mouvement lancé

#endif // STEPPER_MOTOR_H
