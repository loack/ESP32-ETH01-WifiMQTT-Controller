// Liaison UDP directe PC maitre <-> ESP32 (en plus de MQTT, qui reste inchange).
// Specification : T-042-protocole-udp.md. Desactivee par defaut (udpEnabled = false).
#ifndef UDP_LINK_H
#define UDP_LINK_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

// A appeler une fois dans setup(), apres setupWebServer() et setupMQTT().
void udpLinkBegin();

// Routes REST /api/udp (GET lecture, POST ecriture protegee par l'en-tete X-Admin-Password).
// A appeler depuis setupWebServer().
void udpLinkRegisterRoutes(AsyncWebServer& srv);

// Depuis la tache d'entrees : un front est survenu sur l'E/S d'indice ioIndex (non bloquant).
void udpLinkNotifyInput(int ioIndex, bool state);

// Depuis loop() : publie sur MQTT les statuts des sorties changees par UDP ou par le deadman.
void udpLinkDrainMqtt();

bool udpLinkEnabled();

#endif
