#include "udp_link.h"

#include <ArduinoJson.h>
#include <ETH.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_system.h>
#include <lwip/sockets.h>

#include "config.h"
#include "mcp23017.h"
#include "mqtt.h"
#include "stepper_motor.h"
#include "udp_proto.h"
#include "udp_safety.h"

using namespace udpp;

extern void logMessage(const String& message);
extern void logPrintf(const char* fmt, ...);
extern bool ethConnected;
extern bool setOutput(int pin, int state);  // mqtt.cpp : ecriture materielle seule
extern void publishPinStatus(int pin, int state, const char* src);

#define FW_UDP_MAJOR 1
#define FW_UDP_MINOR 0
#define MAX_UDP_OUT 16
#define MAX_PENDING_EVENTS 8
#define RX_BUF 300

// ===== Configuration d'execution (NVS, espace "udp-link") =====
// La cle n'est JAMAIS dans le code ni renvoyee par l'API : ecriture seule.
struct UdpOutCfg {
  uint8_t pin;
  uint8_t safe;
  uint16_t deadmanMs;
};

struct UdpConfig {
  bool enabled;
  uint16_t port;
  char masterHost[64];
  uint8_t deviceId;
  char keyHex[65];
  bool allowInsecure;
  uint16_t statePeriodMs;
  uint16_t lossMs;
  udps::SafetyCfg safety;
  UdpOutCfg outs[MAX_UDP_OUT];
  uint8_t nOuts;
};

static UdpConfig cfg;
static Preferences prefs;
static SemaphoreHandle_t cfgMutex = nullptr;

static uint8_t masterKey[32];
static size_t masterLen = 0;
static bool secureMode = false;  // true = trames signees
static uint32_t masterIp = 0;    // 0 = toute source (signature seule)
static uint32_t lastResolveMs = 0;

// ===== Etat de session =====
struct PendingEvent {
  bool used;
  uint16_t counter;
  uint8_t index;
  uint8_t state;
  uint32_t tEdgeUs;
  uint32_t nextTxMs;
  uint8_t tries;
};

struct InputNote {
  uint8_t index;
  uint8_t state;
  uint32_t tEdgeUs;
};

struct OutNote {
  int16_t pin;
  uint8_t state;
  uint8_t srcCode;  // 1 = udp, 2 = watchdog
};

static QueueHandle_t inputQ = nullptr;
static QueueHandle_t outQ = nullptr;

static int sock = -1;
static TaskHandle_t udpTaskH = nullptr, guardTaskH = nullptr;

static bool sessOpen = false;
static bool sessPending = false;
static uint8_t keyActive[32], keyPending[32];
static uint32_t pendingPcNonce = 0, pendingEspNonce = 0;
static uint32_t pendingSince = 0;
static struct sockaddr_in peer;
static uint16_t rxSeq = 0;
static uint16_t txSeq = 0;
static uint16_t lastOp = 0;
static bool opValid = false;
static uint8_t lastOpStatus = 0;
static uint16_t hbPeriodMs = 50;
static uint16_t statePeriod = 100;
static uint32_t lastStateMs = 0, lastPingMs = 0, lastHelloMs = 0;
static uint16_t evCounter = 0;
static PendingEvent events[MAX_PENDING_EVENTS];
static uint32_t bootId = 0;
static uint32_t lastPingId = 0;
static uint32_t lastPingTxUs = 0;
static volatile uint32_t lastRttUs = 0;
static uint16_t statAuthFail = 0, statDrops = 0;
static uint32_t statRxOk = 0, statTx = 0, statReplays = 0;

static volatile uint32_t lastHbMs = 0;
static volatile bool armed = false;
static volatile uint32_t udpTick = 0;
static volatile uint64_t wdFiredMask = 0;
static volatile bool safetyAsserted = false;
static volatile bool resetRequest = false;
static volatile uint8_t linkState = udpp::L_INACTIVE;

// ===== Utilitaires =====
static const uint8_t* keyFor(const uint8_t* k) { return secureMode ? k : nullptr; }

static uint64_t buildInUseMask() {
  uint64_t m = 0;
  for (int i = 0; i < ioPinCount; i++)
    if (ioPins[i].pin < 64) m |= 1ULL << ioPins[i].pin;
  if (motorIsEnabled()) {
    m |= 1ULL << config.motorPulPin;
    m |= 1ULL << config.motorDirPin;
    m |= 1ULL << config.motorEnaPin;
  }
  if (config.useSerialBridge) {
    if (config.serialRxPin >= 0 && config.serialRxPin < 64) m |= 1ULL << config.serialRxPin;
    if (config.serialTxPin >= 0 && config.serialTxPin < 64) m |= 1ULL << config.serialTxPin;
  }
  return m;
}

static uint16_t ioMapHash() {
  uint16_t crc = 0xFFFF;
  for (int i = 0; i < ioPinCount; i++) {
    crc = crc16((const uint8_t*)ioPins[i].name, strnlen(ioPins[i].name, sizeof(ioPins[i].name)), crc);
    uint8_t b[2] = {(uint8_t)ioPins[i].pin, ioPins[i].mode};
    crc = crc16(b, 2, crc);
  }
  return crc;
}

static uint64_t outputsMask() {
  uint64_t m = 0;
  for (int i = 0; i < ioPinCount && i < 64; i++)
    if (ioPins[i].mode == 2 && ioPins[i].state) m |= 1ULL << i;
  return m;
}

static uint64_t inputsMask() {
  uint64_t m = 0;
  for (int i = 0; i < ioPinCount && i < 64; i++)
    if (ioPins[i].mode == 1 && ioPins[i].state) m |= 1ULL << i;
  return m;
}

static const UdpOutCfg* outCfgForPin(int pin) {
  for (int i = 0; i < cfg.nOuts; i++)
    if (cfg.outs[i].pin == pin) return &cfg.outs[i];
  return nullptr;
}

// ===== Persistance =====
static void setDefaults() {
  memset(&cfg, 0, sizeof(cfg));
  cfg.enabled = false;
  cfg.port = DEFAULT_PORT;
  cfg.deviceId = 1;
  cfg.statePeriodMs = 100;
  cfg.lossMs = 1000;
  cfg.safety = udps::defaultSafetyCfg();
}

static void loadCfg() {
  setDefaults();
  prefs.begin("udp-link", true);
  cfg.enabled = prefs.getBool("en", false);
  cfg.port = prefs.getUShort("port", DEFAULT_PORT);
  prefs.getString("host", cfg.masterHost, sizeof(cfg.masterHost));
  cfg.deviceId = prefs.getUChar("devid", 1);
  prefs.getString("key", cfg.keyHex, sizeof(cfg.keyHex));
  cfg.allowInsecure = prefs.getBool("insec", false);
  cfg.statePeriodMs = prefs.getUShort("stper", 100);
  cfg.lossMs = prefs.getUShort("loss", 1000);
  cfg.safety.enabled = prefs.getBool("sfEn", false);
  cfg.safety.pin = (int8_t)prefs.getChar("sfPin", -1);
  cfg.safety.activeLevel = prefs.getUChar("sfLvl", 1);
  cfg.safety.mode = prefs.getUChar("sfMode", udps::MODE_FAULT);
  cfg.safety.delayMs = prefs.getUShort("sfDly", 200);
  cfg.safety.periodMs = prefs.getUShort("sfPer", 50);
  cfg.nOuts = prefs.getUChar("nOut", 0);
  if (cfg.nOuts > MAX_UDP_OUT) cfg.nOuts = 0;
  if (cfg.nOuts) prefs.getBytes("outs", cfg.outs, cfg.nOuts * sizeof(UdpOutCfg));
  prefs.end();
}

static void saveCfg() {
  prefs.begin("udp-link", false);
  prefs.putBool("en", cfg.enabled);
  prefs.putUShort("port", cfg.port);
  prefs.putString("host", cfg.masterHost);
  prefs.putUChar("devid", cfg.deviceId);
  prefs.putString("key", cfg.keyHex);
  prefs.putBool("insec", cfg.allowInsecure);
  prefs.putUShort("stper", cfg.statePeriodMs);
  prefs.putUShort("loss", cfg.lossMs);
  prefs.putBool("sfEn", cfg.safety.enabled);
  prefs.putChar("sfPin", cfg.safety.pin);
  prefs.putUChar("sfLvl", cfg.safety.activeLevel);
  prefs.putUChar("sfMode", cfg.safety.mode);
  prefs.putUShort("sfDly", cfg.safety.delayMs);
  prefs.putUShort("sfPer", cfg.safety.periodMs);
  prefs.putUChar("nOut", cfg.nOuts);
  prefs.putBytes("outs", cfg.outs, cfg.nOuts * sizeof(UdpOutCfg));
  prefs.end();
}

static void applyKeyAndHost() {
  masterLen = hexToBytes(cfg.keyHex, masterKey, sizeof(masterKey));
  secureMode = masterLen > 0;
  masterIp = 0;
  if (cfg.masterHost[0]) {
    IPAddress ip;
    if (ip.fromString(cfg.masterHost)) {
      masterIp = (uint32_t)ip;
    }
  }
  lastResolveMs = 0;
}

// L'UDP ne demarre que si une cle est configuree, ou si le mode non signe a ete autorise explicitement.
static bool linkUsable() { return cfg.enabled && (secureMode || cfg.allowInsecure); }

// ===== Emission =====
static uint32_t nowUs() { return (uint32_t)micros(); }

static void sendRaw(const uint8_t* buf, size_t n, const struct sockaddr_in& to) {
  if (sock < 0 || n == 0) return;
  sendto(sock, buf, n, 0, (const struct sockaddr*)&to, sizeof(to));
  statTx++;
}

// seq : -1 = utiliser txSeq de la session ; sinon valeur imposee (WELCOME : 0)
static void sendFrame(uint8_t type, uint8_t flags, const uint8_t* payload, size_t plen, const uint8_t* key,
                      const struct sockaddr_in& to, int seqOverride = -1) {
  uint8_t out[MAX_DATAGRAM];
  Header h;
  h.version = VERSION;
  h.type = type;
  h.device_id = cfg.deviceId;
  h.flags = flags;
  h.seq = seqOverride >= 0 ? (uint16_t)seqOverride : ++txSeq;
  h.t_us = nowUs();
  size_t n = buildFrame(out, sizeof(out), h, payload, plen, keyFor(key), 32);
  sendRaw(out, n, to);
}

static uint8_t currentLink() {
  uint32_t now = millis();
  uint32_t age = armed ? now - lastHbMs : 0;
  return udps::classifyLink(sessOpen, sessPending, armed, age, hbPeriodMs, cfg.lossMs);
}

static void sendState(const struct sockaddr_in& to) {
  uint8_t p[43];
  uint32_t now = millis();
  uint32_t age = armed ? now - lastHbMs : 0;
  put64(p, outputsMask());
  put64(p + 8, inputsMask());
  put64(p + 16, wdFiredMask);
  put32(p + 24, now / 1000);
  put32(p + 28, lastRttUs);
  put16(p + 32, statAuthFail);
  put16(p + 34, statDrops);
  put16(p + 36, age > 65535 ? 65535 : (uint16_t)age);
  p[38] = currentLink();
  uint8_t wf = 0;
  if (armed) wf |= WD_ARMED;
  if (wdFiredMask) wf |= WD_DEADMAN_FIRED;
  if (safetyAsserted) wf |= WD_SAFETY_ASSERTED;
  if (cfg.safety.enabled) wf |= WD_SAFETY_ENABLED;
  p[39] = wf;
  p[40] = (config.useEthernet && ethConnected) ? 0 : 1;
  p[41] = (uint8_t)esp_reset_reason();
  p[42] = (config.useEthernet && ethConnected) ? 0 : (uint8_t)(int8_t)WiFi.RSSI();
  sendFrame(T_STATE, 0, p, sizeof(p), keyActive, to);
  lastStateMs = now;
}

static void sendAck(uint16_t op, uint16_t ackSeq, uint8_t status, uint32_t tRxUs, const struct sockaddr_in& to) {
  uint8_t p[21];
  put16(p, op);
  put16(p + 2, ackSeq);
  p[4] = status;
  put64(p + 5, outputsMask());
  put32(p + 13, tRxUs);
  put32(p + 17, nowUs());
  sendFrame(T_ACK, 0, p, sizeof(p), keyActive, to);
}

static void sendEvent(PendingEvent& e, const struct sockaddr_in& to) {
  uint8_t p[8];
  put16(p, e.counter);
  p[2] = e.index;
  p[3] = e.state;
  put32(p + 4, e.tEdgeUs);
  sendFrame(T_EVENT, e.tries ? F_RETX : 0, p, sizeof(p), keyActive, to);
}

// ===== Session =====
static void closeSession(const char* why) {
  if (sessOpen) logPrintf("UDP: session fermee (%s)", why);
  sessOpen = false;
  sessPending = false;
  armed = false;
  for (auto& e : events) e.used = false;
}

static void applySafeStates(const char* why) {
  // Utilise aussi par CLOSE : met les sorties dotees d'un deadman_ms en etat sur.
  for (int i = 0; i < cfg.nOuts; i++) {
    const UdpOutCfg& o = cfg.outs[i];
    if (o.deadmanMs == 0) continue;
    for (int k = 0; k < ioPinCount; k++) {
      if (ioPins[k].pin == o.pin && ioPins[k].mode == 2) {
        if (setOutput(o.pin, o.safe)) {
          wdFiredMask |= 1ULL << k;
          OutNote n = {(int16_t)o.pin, o.safe, 2};
          if (outQ) xQueueSend(outQ, &n, 0);
        }
      }
    }
  }
  logPrintf("UDP: etats surs appliques (%s)", why);
}

static uint8_t applySet(const uint8_t* p, size_t plen, uint16_t op) {
  uint8_t count = p[2];
  if (plen != (size_t)(3 + 2 * count)) return ACK_BAD_INDEX;
  // Validation complete avant toute ecriture : atomique
  for (int i = 0; i < count; i++) {
    uint8_t idx = p[3 + 2 * i];
    if (idx >= ioPinCount) return ACK_BAD_INDEX;
    if (ioPins[idx].mode != 2) return ACK_NOT_OUTPUT;
    if (motorIsPinUsed(ioPins[idx].pin)) return ACK_REFUSED;
  }
  for (int i = 0; i < count; i++) {
    uint8_t idx = p[3 + 2 * i];
    uint8_t st = p[4 + 2 * i] ? 1 : 0;
    int pin = ioPins[idx].pin;
    setOutput(pin, st);  // ecriture materielle d'abord ; MQTT ensuite (udpLinkDrainMqtt)
    wdFiredMask &= ~(1ULL << idx);
    OutNote n = {(int16_t)pin, st, 1};
    if (outQ) xQueueSend(outQ, &n, 0);
  }
  (void)op;
  return ACK_OK;
}

static bool sourceAllowed(const struct sockaddr_in& from) {
  return masterIp == 0 || from.sin_addr.s_addr == masterIp;
}

static void handleDatagram(const uint8_t* buf, size_t n, const struct sockaddr_in& from, uint32_t tRxUs) {
  if (n < HEADER_LEN || buf[0] != MAGIC0 || buf[1] != MAGIC1) { statDrops++; return; }
  if (!sourceAllowed(from)) { statDrops++; return; }
  if (buf[4] != cfg.deviceId) { statDrops++; return; }
  uint8_t type = buf[3];
  Header h;
  const uint8_t* pl;
  size_t plen;
  uint32_t now = millis();

  if (type == T_HELLO) {
    if (now - lastHelloMs < 200) { statDrops++; return; }  // limitation de debit
    lastHelloMs = now;
    ParseResult r = parseFrame(buf, n, h, &pl, &plen, keyFor(masterKey), masterLen);
    if (r != P_OK || plen < 4) { statAuthFail++; return; }
    pendingPcNonce = get32(pl);
    pendingEspNonce = esp_random();
    pendingSince = now;
    if (secureMode) deriveSessionKey(masterKey, masterLen, pendingPcNonce, pendingEspNonce, keyPending);
    sessPending = true;
    uint8_t p[19];
    put32(p, pendingPcNonce);
    put32(p + 4, pendingEspNonce);
    put32(p + 8, bootId);
    p[12] = FW_UDP_MAJOR;
    p[13] = FW_UDP_MINOR;
    uint16_t caps = (cfg.safety.enabled ? 1 : 0) | (cfg.nOuts ? 2 : 0) | (motorIsEnabled() ? 4 : 0);
    put16(p + 14, caps);
    p[16] = (uint8_t)ioPinCount;
    put16(p + 17, ioMapHash());
    // WELCOME : signe avec la cle maitre, seq 0, hors session
    sendFrame(T_WELCOME, 0, p, sizeof(p), masterKey, from, 0);
    return;
  }

  // Autres types : verification avec la cle de session active, ou en attente (OPEN seulement)
  bool viaPending = false;
  ParseResult r = P_BAD_TAG;
  if (type == T_OPEN && sessPending && now - pendingSince < 10000) {
    r = parseFrame(buf, n, h, &pl, &plen, keyFor(keyPending), 32);
    viaPending = (r == P_OK);
  }
  if (r != P_OK && sessOpen) r = parseFrame(buf, n, h, &pl, &plen, keyFor(keyActive), 32);
  if (r != P_OK) { statAuthFail++; return; }

  if (viaPending) {
    memcpy(keyActive, keyPending, 32);
    sessOpen = true;
    sessPending = false;
    rxSeq = h.seq;
    txSeq = 0;
    opValid = false;
    wdFiredMask = 0;
    for (auto& e : events) e.used = false;
    armed = false;
    lastHbMs = now;
    hbPeriodMs = plen >= 2 ? get16(pl) : 50;
    if (hbPeriodMs < 5 || hbPeriodMs > 5000) hbPeriodMs = 50;
    uint16_t sp = plen >= 4 ? get16(pl + 2) : 0;
    statePeriod = sp ? sp : cfg.statePeriodMs;
    if (statePeriod < 10) statePeriod = 10;
    peer = from;
    logPrintf("UDP: session ouverte (hb=%u ms, state=%u ms)", hbPeriodMs, statePeriod);
    sendState(peer);
    statRxOk++;
    return;
  }

  // Anti-rejeu : numero de sequence strictement croissant (modulo 2^16)
  if (!seqNewer(h.seq, rxSeq)) { statReplays++; return; }
  rxSeq = h.seq;
  peer = from;
  statRxOk++;

  switch (type) {
    case T_HB:
      lastHbMs = now;
      armed = true;
      break;
    case T_SET: {
      if (plen < 3) { statDrops++; break; }
      uint16_t op = get16(pl);
      uint8_t status;
      if (opValid && !seqNewer(op, lastOp)) {
        status = (op == lastOp) ? lastOpStatus : ACK_STALE;  // doublon : re-ACK sans reappliquer
      } else {
        status = applySet(pl, plen, op);
        lastOp = op;
        opValid = true;
        lastOpStatus = status;
      }
      if (h.flags & F_ACKREQ) sendAck(op, h.seq, status, tRxUs, peer);
      break;
    }
    case T_EVENT_ACK:
      if (plen >= 2) {
        uint16_t c = get16(pl);
        for (auto& e : events)
          if (e.used && e.counter == c) e.used = false;
      }
      break;
    case T_PING:
      if (plen >= 4) {
        uint8_t p[16];
        put32(p, get32(pl));
        put32(p + 4, h.t_us);
        put32(p + 8, tRxUs);
        put32(p + 12, nowUs());
        sendFrame(T_PONG, 0, p, sizeof(p), keyActive, peer);
      }
      break;
    case T_PONG:
      if (plen >= 16 && get32(pl) == lastPingId) {
        uint32_t rtt = tRxUs - lastPingTxUs;
        uint32_t proc = get32(pl + 12) - get32(pl + 8);
        lastRttUs = rtt > proc ? rtt - proc : rtt;
      }
      break;
    case T_OPEN:  // OPEN rejoue par le maitre (STATE perdu) : on renvoie l'etat
      sendState(peer);
      break;
    case T_CLOSE:
      applySafeStates("CLOSE");
      closeSession("CLOSE du maitre");
      break;
    default:
      statDrops++;
      break;
  }
}

static void serviceTimers(uint32_t now) {
  if (!sessOpen) return;
  if (now - lastStateMs >= statePeriod) sendState(peer);
  if (now - lastPingMs >= 2000) {
    lastPingMs = now;
    lastPingId++;
    lastPingTxUs = nowUs();
    uint8_t p[4];
    put32(p, lastPingId);
    sendFrame(T_PING, 0, p, 4, keyActive, peer);
  }
  // Retransmission des evenements : +2, +5, +10, +25, +50 ms puis toutes les 100 ms (3 s max)
  static const uint16_t backoff[] = {2, 5, 10, 25, 50};
  for (auto& e : events) {
    if (!e.used || (int32_t)(now - e.nextTxMs) < 0) continue;
    if (e.tries >= 35) { e.used = false; continue; }
    e.tries++;
    sendEvent(e, peer);
    e.nextTxMs = now + (e.tries <= 5 ? backoff[e.tries - 1] : 100);
  }
}

static void resolveMasterIfNeeded(uint32_t now) {
  if (masterIp != 0 || !cfg.masterHost[0] || sessOpen) return;
  if (lastResolveMs != 0 && now - lastResolveMs < 300000) return;
  lastResolveMs = now ? now : 1;
  IPAddress ip;
  if (WiFi.hostByName(cfg.masterHost, ip) == 1) {
    masterIp = (uint32_t)ip;
    logPrintf("UDP: hote maitre %s resolu en %s", cfg.masterHost, ip.toString().c_str());
  }
}

static bool openSocket() {
  sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock < 0) return false;
  struct sockaddr_in a;
  memset(&a, 0, sizeof(a));
  a.sin_family = AF_INET;
  a.sin_port = htons(cfg.port);
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(sock, (struct sockaddr*)&a, sizeof(a)) < 0) {
    lwip_close(sock);
    sock = -1;
    return false;
  }
  struct timeval tv = {0, 3000};
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  return true;
}

static void udpTask(void*) {
  static uint8_t rx[RX_BUF];
  for (;;) {
    uint32_t now = millis();
    udpTick = now;
    if (resetRequest) {
      resetRequest = false;
      if (sock >= 0) { lwip_close(sock); sock = -1; }
      closeSession("reconfiguration");
    }
    if (!linkUsable()) {
      if (sock >= 0) { lwip_close(sock); sock = -1; closeSession("UDP desactive"); }
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    bool netOk = config.useEthernet ? ethConnected : (WiFi.status() == WL_CONNECTED);
    if (!netOk) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }
    if (sock < 0 && !openSocket()) { vTaskDelay(pdMS_TO_TICKS(500)); continue; }
    resolveMasterIfNeeded(now);

    struct sockaddr_in from;
    socklen_t fl = sizeof(from);
    int n = recvfrom(sock, rx, sizeof(rx), 0, (struct sockaddr*)&from, &fl);
    uint32_t tRxUs = nowUs();
    if (n > 0) {
      if (n <= (int)MAX_DATAGRAM) handleDatagram(rx, (size_t)n, from, tRxUs);
      else statDrops++;
    }
    // Entrees : evenements a emettre (file alimentee par la tache d'entrees)
    InputNote note;
    while (inputQ && xQueueReceive(inputQ, &note, 0) == pdTRUE) {
      if (!sessOpen) continue;
      PendingEvent* slot = nullptr;
      for (auto& e : events) if (!e.used) { slot = &e; break; }
      if (!slot) slot = &events[0];  // plein : on ecrase (STATE rattrape)
      slot->used = true;
      slot->counter = ++evCounter;
      slot->index = note.index;
      slot->state = note.state;
      slot->tEdgeUs = note.tEdgeUs;
      slot->tries = 0;
      sendEvent(*slot, peer);
      slot->nextTxMs = millis() + 2;
    }
    serviceTimers(millis());
    linkState = currentLink();
    if (sessOpen && !sessPending && currentLink() == L_LOST) { /* reste ouverte : le PC refera HELLO */ }
  }
}

// Tache de garde : 1 ms, independante de la tache UDP. Deadman par sortie + broche de securite.
static void guardTask(void*) {
  static bool firedOnce[MAX_IOS];
  int8_t drivenPin = -1;
  for (;;) {
    uint32_t now = millis();
    udps::GuardInput in;
    in.armed = armed;
    in.hbAgeMs = armed ? now - lastHbMs : 0;
    in.taskAlive = (now - udpTick) < 150;
    // Deadman : une fois par expiration (les commandes suivantes, MQTT comprises, l'emportent)
    if (armed) {
      for (int i = 0; i < cfg.nOuts; i++) {
        const UdpOutCfg& o = cfg.outs[i];
        bool expired = udps::deadmanExpired(in, o.deadmanMs);
        for (int k = 0; k < ioPinCount && k < MAX_IOS; k++) {
          if (ioPins[k].pin != o.pin || ioPins[k].mode != 2) continue;
          if (expired && !firedOnce[k]) {
            firedOnce[k] = true;
            if (setOutput(o.pin, o.safe)) {
              wdFiredMask |= 1ULL << k;
              OutNote nt = {(int16_t)o.pin, o.safe, 2};
              if (outQ) xQueueSend(outQ, &nt, 0);
            }
          } else if (!expired) {
            firedOnce[k] = false;
          }
        }
      }
    } else {
      for (int k = 0; k < MAX_IOS; k++) firedOnce[k] = false;
    }
    // Broche de securite
    udps::SafetyCfg sc = cfg.safety;
    if (sc.enabled && sc.pin >= 0) {
      if (drivenPin != sc.pin) {
        pinMode(sc.pin, OUTPUT);
        drivenPin = sc.pin;
      }
      bool asserted = false;
      int lvl = udps::safetyLevel(sc, in, now, &asserted);
      if (lvl >= 0) digitalWrite(sc.pin, lvl);
      safetyAsserted = asserted;
    } else {
      if (drivenPin >= 0) { pinMode(drivenPin, INPUT); drivenPin = -1; }
      safetyAsserted = false;
    }
    vTaskDelay(1);
  }
}

// ===== API publique =====
void udpLinkNotifyInput(int ioIndex, bool state) {
  if (!inputQ || !cfg.enabled || !sessOpen || ioIndex < 0 || ioIndex > 255) return;
  InputNote n = {(uint8_t)ioIndex, (uint8_t)(state ? 1 : 0), nowUs()};
  xQueueSend(inputQ, &n, 0);
}

void udpLinkDrainMqtt() {
  if (!outQ) return;
  OutNote n;
  int guard = 8;
  while (guard-- > 0 && xQueueReceive(outQ, &n, 0) == pdTRUE) {
    publishPinStatus(n.pin, n.state, n.srcCode == 1 ? "udp" : "watchdog");
  }
}

bool udpLinkEnabled() { return cfg.enabled; }

void udpLinkBegin() {
  loadCfg();
  applyKeyAndHost();
  bootId = esp_random();
  inputQ = xQueueCreate(16, sizeof(InputNote));
  outQ = xQueueCreate(32, sizeof(OutNote));
  cfgMutex = xSemaphoreCreateMutex();
  memset(events, 0, sizeof(events));
  // Validation de la broche de securite au demarrage : une config invalide la desactive
  const char* err = udps::validateSafetyCfg(cfg.safety, buildInUseMask());
  if (err) {
    logPrintf("UDP: broche de securite refusee (%s), desactivee", err);
    cfg.safety.enabled = false;
  }
  xTaskCreatePinnedToCore(udpTask, "UdpTask", 6144, nullptr, 3, &udpTaskH, 1);
  xTaskCreatePinnedToCore(guardTask, "UdpGuard", 3072, nullptr, 5, &guardTaskH, 1);
  logPrintf("UDP: module charge (actif=%d, port=%u, signe=%d, securite=%d)", cfg.enabled, cfg.port, secureMode,
            cfg.safety.enabled);
}

// ===== REST =====
static void fillStatus(JsonDocument& doc) {
  JsonObject st = doc["status"].to<JsonObject>();
  uint32_t now = millis();
  st["link"] = currentLink();
  st["sessionOpen"] = sessOpen;
  st["armed"] = (bool)armed;
  st["hbAgeMs"] = armed ? now - lastHbMs : 0;
  st["rttUs"] = lastRttUs;
  st["rxOk"] = statRxOk;
  st["tx"] = statTx;
  st["authFail"] = statAuthFail;
  st["replays"] = statReplays;
  st["drops"] = statDrops;
  st["deadmanFiredMask"] = (uint32_t)wdFiredMask;
  st["safetyAsserted"] = (bool)safetyAsserted;
  st["usable"] = linkUsable();
}

static void sendCfgJson(AsyncWebServerRequest* req) {
  JsonDocument doc;
  doc["enabled"] = cfg.enabled;
  doc["port"] = cfg.port;
  doc["masterHost"] = cfg.masterHost;
  doc["deviceId"] = cfg.deviceId;
  doc["keySet"] = secureMode;  // jamais la cle
  doc["allowInsecure"] = cfg.allowInsecure;
  doc["statePeriodMs"] = cfg.statePeriodMs;
  doc["lossMs"] = cfg.lossMs;
  doc["mapHash"] = ioMapHash();
  JsonObject sf = doc["safety"].to<JsonObject>();
  sf["enabled"] = cfg.safety.enabled;
  sf["pin"] = cfg.safety.pin;
  sf["activeLevel"] = cfg.safety.activeLevel;
  sf["mode"] = cfg.safety.mode;
  sf["delayMs"] = cfg.safety.delayMs;
  sf["periodMs"] = cfg.safety.periodMs;
  JsonArray outs = doc["outputs"].to<JsonArray>();
  for (int i = 0; i < cfg.nOuts; i++) {
    JsonObject o = outs.add<JsonObject>();
    o["pin"] = cfg.outs[i].pin;
    o["safe"] = cfg.outs[i].safe;
    o["deadmanMs"] = cfg.outs[i].deadmanMs;
  }
  JsonArray map = doc["ioMap"].to<JsonArray>();
  for (int i = 0; i < ioPinCount; i++) {
    JsonObject m = map.add<JsonObject>();
    m["index"] = i;
    m["name"] = ioPins[i].name;
    m["pin"] = ioPins[i].pin;
    m["mode"] = ioPins[i].mode;
  }
  fillStatus(doc);
  String out;
  serializeJson(doc, out);
  req->send(200, "application/json", out);
}

static void restReply(AsyncWebServerRequest* req, int code, const char* msg) {
  JsonDocument d;
  d["success"] = (code == 200);
  d["message"] = msg;
  String s;
  serializeJson(d, s);
  req->send(code, "application/json", s);
}

void udpLinkRegisterRoutes(AsyncWebServer& srv) {
  srv.on("/api/udp", HTTP_GET, [](AsyncWebServerRequest* req) { sendCfgJson(req); });

  srv.on("/api/udp", HTTP_POST, [](AsyncWebServerRequest* req) {}, NULL,
         [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
           if (index != 0 || len != total) { restReply(req, 400, "corps en un seul bloc attendu"); return; }
           // Ecriture protegee : le champ adminPassword existe deja dans la configuration
           const AsyncWebHeader* ah = req->getHeader("X-Admin-Password");
           if (!ah || ah->value() != String(config.adminPassword)) { restReply(req, 401, "X-Admin-Password requis"); return; }
           JsonDocument doc;
           if (deserializeJson(doc, data, len) != DeserializationError::Ok) { restReply(req, 400, "JSON invalide"); return; }

           UdpConfig n = cfg;  // copie de travail : rien n'est applique tant que tout n'est pas valide
           if (doc["enabled"].is<bool>()) n.enabled = doc["enabled"];
           if (doc["port"].is<int>()) {
             int p = doc["port"];
             if (p < 1024 || p > 65535) { restReply(req, 400, "port : 1024 a 65535"); return; }
             n.port = p;
           }
           if (doc["masterHost"].is<const char*>()) strlcpy(n.masterHost, doc["masterHost"], sizeof(n.masterHost));
           if (doc["deviceId"].is<int>()) {
             int d = doc["deviceId"];
             if (d < 1 || d > 255) { restReply(req, 400, "deviceId : 1 a 255"); return; }
             n.deviceId = d;
           }
           if (doc["key"].is<const char*>()) {
             const char* k = doc["key"];
             uint8_t tmp[32];
             if (strlen(k) == 0) {
               n.keyHex[0] = 0;
             } else if (strlen(k) != 64 || hexToBytes(k, tmp, 32) != 32) {
               restReply(req, 400, "key : 64 caracteres hexadecimaux (32 octets)");
               return;
             } else {
               strlcpy(n.keyHex, k, sizeof(n.keyHex));
             }
           }
           if (doc["allowInsecure"].is<bool>()) n.allowInsecure = doc["allowInsecure"];
           if (doc["statePeriodMs"].is<int>()) {
             int v = doc["statePeriodMs"];
             if (v < 10 || v > 5000) { restReply(req, 400, "statePeriodMs : 10 a 5000"); return; }
             n.statePeriodMs = v;
           }
           if (doc["lossMs"].is<int>()) {
             int v = doc["lossMs"];
             if (v < 50 || v > 60000) { restReply(req, 400, "lossMs : 50 a 60000"); return; }
             n.lossMs = v;
           }
           if (doc["safety"].is<JsonObject>()) {
             JsonObject s = doc["safety"];
             if (s["enabled"].is<bool>()) n.safety.enabled = s["enabled"];
             if (s["pin"].is<int>()) n.safety.pin = (int8_t)(int)s["pin"];
             if (s["activeLevel"].is<int>()) n.safety.activeLevel = (int)s["activeLevel"] ? 1 : 0;
             if (s["mode"].is<int>()) n.safety.mode = (int)s["mode"];
             if (s["delayMs"].is<int>()) n.safety.delayMs = (int)s["delayMs"];
             if (s["periodMs"].is<int>()) n.safety.periodMs = (int)s["periodMs"];
           }
           const char* err = udps::validateSafetyCfg(n.safety, buildInUseMask());
           if (err) { restReply(req, 400, err); return; }
           if (doc["outputs"].is<JsonArray>()) {
             JsonArray a = doc["outputs"];
             if (a.size() > MAX_UDP_OUT) { restReply(req, 400, "outputs : 16 maximum"); return; }
             uint8_t cnt = 0;
             for (JsonObject o : a) {
               int pin = o["pin"] | -1, safe = o["safe"] | 0;
               uint32_t dm = o["deadmanMs"] | 0;
               bool found = false;
               for (int i = 0; i < ioPinCount; i++) if (ioPins[i].pin == pin && ioPins[i].mode == 2) found = true;
               if (!found) { restReply(req, 400, "outputs : broche inconnue ou pas une sortie"); return; }
               if (!udps::validDeadman(dm)) { restReply(req, 400, "deadmanMs : 0 ou 10 a 60000"); return; }
               n.outs[cnt].pin = pin;
               n.outs[cnt].safe = safe ? 1 : 0;
               n.outs[cnt].deadmanMs = dm;
               cnt++;
             }
             n.nOuts = cnt;
           }
           bool sec = strlen(n.keyHex) == 64;
           if (n.enabled && !sec && !n.allowInsecure) {
             restReply(req, 400, "cle absente : fournir key ou autoriser allowInsecure (banc uniquement)");
             return;
           }
           // Tout est valide : on applique
           int8_t oldPin = cfg.safety.pin;
           cfg = n;
           saveCfg();
           applyKeyAndHost();
           if (oldPin >= 0 && oldPin != cfg.safety.pin) pinMode(oldPin, INPUT);
           resetRequest = true;  // la tache UDP ferme la session et le socket (rebind sur le nouveau port)
           restReply(req, 200, "configuration UDP enregistree et appliquee");
         });
}
