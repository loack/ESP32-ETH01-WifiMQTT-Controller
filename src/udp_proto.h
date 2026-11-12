// Protocole UDP direct OneMen (OMUDP v1) : trames, HMAC-SHA-256 tronque, session.
// Module PUR (aucune dependance Arduino) : compilable et testable sur PC.
// Specification : T-042-protocole-udp.md (depot OneMenCompany).
#ifndef UDP_PROTO_H
#define UDP_PROTO_H

#include <stddef.h>
#include <stdint.h>

namespace udpp {

constexpr uint8_t  MAGIC0 = 0x4F;  // 'O'
constexpr uint8_t  MAGIC1 = 0x4D;  // 'M'
constexpr uint8_t  VERSION = 1;
constexpr size_t   HEADER_LEN = 12;
constexpr size_t   TAG_LEN = 8;
constexpr size_t   MAX_DATAGRAM = 256;
constexpr size_t   MAX_PAYLOAD = MAX_DATAGRAM - HEADER_LEN - TAG_LEN;
constexpr uint16_t DEFAULT_PORT = 47100;

enum Type : uint8_t {
  T_HELLO = 0x01,
  T_WELCOME = 0x02,
  T_OPEN = 0x03,
  T_CLOSE = 0x04,
  T_SET = 0x10,
  T_ACK = 0x11,
  T_EVENT = 0x20,
  T_EVENT_ACK = 0x21,
  T_HB = 0x30,
  T_STATE = 0x31,
  T_PING = 0x40,
  T_PONG = 0x41,
};

enum Flags : uint8_t {
  F_ACKREQ = 0x01,
  F_RETX = 0x02,
  F_SIGNED = 0x04,
};

// Statuts d'ACK
enum AckStatus : uint8_t {
  ACK_OK = 0,
  ACK_BAD_INDEX = 1,
  ACK_NOT_OUTPUT = 2,
  ACK_REFUSED = 3,   // broche reservee (moteur) ou refus applicatif
  ACK_STALE = 4,     // op_id plus ancien que le dernier applique : non applique
};

// Etats de liaison (STATE.link), vue de l'ESP32
enum LinkState : uint8_t {
  L_INACTIVE = 0,    // UDP desactive ou pas de session
  L_PENDING = 1,     // WELCOME envoye, OPEN pas encore recu
  L_ESTABLISHED = 2, // session ouverte, HB frais
  L_DEGRADED = 3,    // HB en retard (> 2 periodes annoncees)
  L_LOST = 4,        // pas de HB depuis > loss_ms
};

// Bits de STATE.wd_flags
enum WdFlags : uint8_t {
  WD_ARMED = 0x01,       // surveillance active (OPEN recu puis HB recu)
  WD_DEADMAN_FIRED = 0x02,
  WD_SAFETY_ASSERTED = 0x04, // la broche de securite est en alarme
  WD_SAFETY_ENABLED = 0x08,
};

struct Header {
  uint8_t version;
  uint8_t type;
  uint8_t device_id;
  uint8_t flags;
  uint16_t seq;
  uint32_t t_us;
};

// --- Primitives ---
void sha256(const uint8_t* msg, size_t len, uint8_t out[32]);
void hmacSha256(const uint8_t* key, size_t klen, const uint8_t* msg, size_t mlen, uint8_t out[32]);
bool ctEqual(const uint8_t* a, const uint8_t* b, size_t n);

// Cle de session : HMAC(master, "OMUDPv1" || pc_nonce LE || esp_nonce LE)
void deriveSessionKey(const uint8_t* master, size_t mlen, uint32_t pcNonce, uint32_t espNonce, uint8_t out[32]);

// Comparaison de numeros de sequence 16 bits (arithmetique modulo 2^16) : a plus recent que b
inline bool seqNewer(uint16_t a, uint16_t b) { return (uint16_t)(a - b) != 0 && (uint16_t)(a - b) < 0x8000; }

// --- Little-endian ---
inline void put16(uint8_t* p, uint16_t v) { p[0] = v & 0xFF; p[1] = v >> 8; }
inline void put32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (v >> (8 * i)) & 0xFF; }
inline void put64(uint8_t* p, uint64_t v) { for (int i = 0; i < 8; i++) p[i] = (v >> (8 * i)) & 0xFF; }
inline uint16_t get16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
inline uint32_t get32(const uint8_t* p) { uint32_t v = 0; for (int i = 3; i >= 0; i--) v = (v << 8) | p[i]; return v; }
inline uint64_t get64(const uint8_t* p) { uint64_t v = 0; for (int i = 7; i >= 0; i--) v = (v << 8) | p[i]; return v; }

// --- Trames ---
// Construit une trame. key == nullptr : non signee (mode laboratoire). Sinon HMAC tronque ajoute
// et le drapeau F_SIGNED est force. Retourne la longueur totale, 0 si le tampon est trop petit.
size_t buildFrame(uint8_t* out, size_t cap, const Header& h, const uint8_t* payload, size_t plen,
                  const uint8_t* key, size_t klen);

enum ParseResult {
  P_OK = 0,
  P_TOO_SHORT,
  P_BAD_MAGIC,
  P_BAD_VERSION,
  P_BAD_TAG,        // signature fausse ou absente alors qu'une cle est exigee
  P_UNSIGNED_REFUSED,
  P_TOO_LONG,
};

// Analyse + verification. key == nullptr : accepte seulement les trames non signees.
ParseResult parseFrame(const uint8_t* in, size_t len, Header& h, const uint8_t** payload, size_t* plen,
                       const uint8_t* key, size_t klen);

// Decode une cle hexadecimale (64 caracteres = 32 octets). Retourne le nombre d'octets, 0 si invalide.
size_t hexToBytes(const char* hex, uint8_t* out, size_t cap);

// Hash court de la table des E/S (noms + broches) pour detecter un desaccord avec la config du PC.
uint16_t crc16(const uint8_t* data, size_t len, uint16_t crc = 0xFFFF);

}  // namespace udpp

#endif
