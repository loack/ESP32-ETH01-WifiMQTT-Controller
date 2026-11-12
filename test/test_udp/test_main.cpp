// Tests natifs : pio test -e native
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "udp_proto.h"
#include "udp_safety.h"

using namespace udpp;

void setUp() {}
void tearDown() {}

static void hexOf(const uint8_t* b, size_t n, char* out) {
  for (size_t i = 0; i < n; i++) sprintf(out + 2 * i, "%02x", b[i]);
  out[2 * n] = 0;
}

void test_hmac_rfc4231_like() {
  uint8_t mac[32];
  const char* msg = "what do ya want for nothing?";
  hmacSha256((const uint8_t*)"Jefe", 4, (const uint8_t*)msg, strlen(msg), mac);
  char h[65];
  hexOf(mac, 32, h);
  TEST_ASSERT_EQUAL_STRING("5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843", h);
}

void test_sha256_abc() {
  uint8_t d[32];
  sha256((const uint8_t*)"abc", 3, d);
  char h[65];
  hexOf(d, 32, h);
  TEST_ASSERT_EQUAL_STRING("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", h);
}

static void masterKey(uint8_t k[32]) {
  for (int i = 0; i < 32; i++) k[i] = i;
}

void test_session_key_vector() {
  uint8_t k[32], s[32];
  masterKey(k);
  deriveSessionKey(k, 32, 0x11223344, 0xAABBCCDD, s);
  char h[65];
  hexOf(s, 32, h);
  TEST_ASSERT_EQUAL_STRING("0d45d89a79df1262c9dc3c5d1af345f2a4c8a822de38513d59603d7f94237f3e", h);
}

void test_frame_vector_and_roundtrip() {
  uint8_t k[32];
  masterKey(k);
  Header h = {VERSION, T_HB, 7, 0, 513, 0xDEADBEEF};
  uint8_t pl[4];
  put32(pl, 42);
  uint8_t out[64];
  size_t n = buildFrame(out, sizeof(out), h, pl, 4, k, 32);
  TEST_ASSERT_EQUAL(24, n);
  char hx[100];
  hexOf(out, n, hx);
  TEST_ASSERT_EQUAL_STRING("4f4d013007040102efbeadde2a0000009262d2a859bbe2c6", hx);
  Header r;
  const uint8_t* p;
  size_t pl2;
  TEST_ASSERT_EQUAL(P_OK, parseFrame(out, n, r, &p, &pl2, k, 32));
  TEST_ASSERT_EQUAL(T_HB, r.type);
  TEST_ASSERT_EQUAL(513, r.seq);
  TEST_ASSERT_EQUAL(4, pl2);
  TEST_ASSERT_EQUAL_UINT32(42, get32(p));
  TEST_ASSERT_TRUE(r.flags & F_SIGNED);
}

void test_tamper_and_unsigned() {
  uint8_t k[32];
  masterKey(k);
  Header h = {VERSION, T_SET, 1, F_ACKREQ, 1, 5};
  uint8_t pl[5] = {1, 0, 1, 0, 1};
  uint8_t out[64];
  size_t n = buildFrame(out, sizeof(out), h, pl, 5, k, 32);
  Header r;
  const uint8_t* p;
  size_t l;
  out[HEADER_LEN + 4] ^= 1;  // altere la charge utile
  TEST_ASSERT_EQUAL(P_BAD_TAG, parseFrame(out, n, r, &p, &l, k, 32));
  out[HEADER_LEN + 4] ^= 1;
  uint8_t k2[32];
  memset(k2, 9, 32);
  TEST_ASSERT_EQUAL(P_BAD_TAG, parseFrame(out, n, r, &p, &l, k2, 32));
  // trame non signee refusee quand une cle est exigee
  uint8_t u[64];
  size_t nu = buildFrame(u, sizeof(u), h, pl, 5, nullptr, 0);
  TEST_ASSERT_EQUAL(P_UNSIGNED_REFUSED, parseFrame(u, nu, r, &p, &l, k, 32));
  TEST_ASSERT_EQUAL(P_OK, parseFrame(u, nu, r, &p, &l, nullptr, 0));
}

void test_malformed() {
  Header r;
  const uint8_t* p;
  size_t l;
  uint8_t b[300] = {0};
  TEST_ASSERT_EQUAL(P_TOO_SHORT, parseFrame(b, 5, r, &p, &l, nullptr, 0));
  TEST_ASSERT_EQUAL(P_BAD_MAGIC, parseFrame(b, 12, r, &p, &l, nullptr, 0));
  b[0] = MAGIC0;
  b[1] = MAGIC1;
  b[2] = 2;
  TEST_ASSERT_EQUAL(P_BAD_VERSION, parseFrame(b, 12, r, &p, &l, nullptr, 0));
  TEST_ASSERT_EQUAL(P_TOO_LONG, parseFrame(b, 300, r, &p, &l, nullptr, 0));
  // tampon de sortie trop petit / charge trop grande
  Header h = {VERSION, T_HB, 1, 0, 1, 0};
  uint8_t o[16];
  TEST_ASSERT_EQUAL(0, buildFrame(o, sizeof(o), h, b, 4, (const uint8_t*)"k", 1));
  TEST_ASSERT_EQUAL(0, buildFrame(b, 300, h, b, MAX_PAYLOAD + 1, nullptr, 0));
}

void test_seq_wraparound() {
  TEST_ASSERT_TRUE(seqNewer(5, 4));
  TEST_ASSERT_FALSE(seqNewer(4, 5));
  TEST_ASSERT_FALSE(seqNewer(5, 5));
  TEST_ASSERT_TRUE(seqNewer(0, 65535));
  TEST_ASSERT_TRUE(seqNewer(3, 65530));
  TEST_ASSERT_FALSE(seqNewer(65530, 3));
  TEST_ASSERT_FALSE(seqNewer(0x9000, 0x0000));  // plus d une demi-plage : considere ancien
}

void test_hex_crc() {
  uint8_t b[32];
  char hex[65];
  for (int i = 0; i < 32; i++) sprintf(hex + 2 * i, "%02X", i);
  TEST_ASSERT_EQUAL(32, hexToBytes(hex, b, 32));
  TEST_ASSERT_EQUAL(31, b[31]);
  TEST_ASSERT_EQUAL(0, hexToBytes("zz", b, 32));
  TEST_ASSERT_EQUAL(0, hexToBytes("abc", b, 32));
  const uint8_t m[8] = {'K', '1', 4, 2, 'K', '2', 5, 2};
  TEST_ASSERT_EQUAL_HEX16(0xB67E, crc16(m, 8));
}

void test_pin_validation() {
  using namespace udps;
  TEST_ASSERT_EQUAL(PIN_OK, validateSafetyPin(14, 0));
  TEST_ASSERT_EQUAL(PIN_OK, validateSafetyPin(4, 0));
  TEST_ASSERT_EQUAL(PIN_IN_USE, validateSafetyPin(14, 1ULL << 14));
  int eth[] = {0, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27};
  for (int p : eth) TEST_ASSERT_EQUAL_MESSAGE(PIN_ETH, validateSafetyPin(p, 0), "ethernet");
  for (int p = 6; p <= 11; p++) TEST_ASSERT_EQUAL(PIN_FLASH, validateSafetyPin(p, 0));
  int strap[] = {2, 5, 12, 15};
  for (int p : strap) TEST_ASSERT_EQUAL(PIN_STRAP, validateSafetyPin(p, 0));
  TEST_ASSERT_EQUAL(PIN_UART, validateSafetyPin(1, 0));
  TEST_ASSERT_EQUAL(PIN_UART, validateSafetyPin(3, 0));
  for (int p = 34; p <= 39; p++) TEST_ASSERT_EQUAL(PIN_INPUT_ONLY, validateSafetyPin(p, 0));
  TEST_ASSERT_EQUAL(PIN_I2C, validateSafetyPin(32, 0));
  TEST_ASSERT_EQUAL(PIN_I2C, validateSafetyPin(33, 0));
  TEST_ASSERT_EQUAL(PIN_VIRTUAL, validateSafetyPin(100, 0));
  TEST_ASSERT_EQUAL(PIN_RANGE, validateSafetyPin(-1, 0));
  TEST_ASSERT_EQUAL(PIN_RANGE, validateSafetyPin(40, 0));
}

void test_cfg_validation() {
  using namespace udps;
  SafetyCfg c = defaultSafetyCfg();
  TEST_ASSERT_FALSE(c.enabled);  // desactive par defaut
  TEST_ASSERT_NULL(validateSafetyCfg(c, 0));
  c.enabled = true;
  TEST_ASSERT_NOT_NULL(validateSafetyCfg(c, 0));  // pas de broche
  c.pin = 14;
  TEST_ASSERT_NULL(validateSafetyCfg(c, 0));
  c.delayMs = 5;
  TEST_ASSERT_NOT_NULL(validateSafetyCfg(c, 0));
  c.delayMs = 200;
  c.mode = MODE_HEARTBEAT;
  c.periodMs = 3;
  TEST_ASSERT_NOT_NULL(validateSafetyCfg(c, 0));
  c.periodMs = 40;
  TEST_ASSERT_NULL(validateSafetyCfg(c, 0));
  c.pin = 18;
  TEST_ASSERT_NOT_NULL(validateSafetyCfg(c, 0));
  TEST_ASSERT_TRUE(validDeadman(0));
  TEST_ASSERT_FALSE(validDeadman(5));
  TEST_ASSERT_TRUE(validDeadman(100));
  TEST_ASSERT_FALSE(validDeadman(70000));
}

void test_safety_fault_mode() {
  using namespace udps;
  SafetyCfg c = defaultSafetyCfg();
  c.enabled = true;
  c.pin = 14;
  c.activeLevel = 1;
  c.delayMs = 200;
  bool a;
  GuardInput in = {false, 0, true};
  TEST_ASSERT_EQUAL(0, safetyLevel(c, in, 1000, &a));  // non arme : pas d alarme
  TEST_ASSERT_FALSE(a);
  in = {true, 50, true};
  TEST_ASSERT_EQUAL(0, safetyLevel(c, in, 1000, &a));
  in = {true, 201, true};
  TEST_ASSERT_EQUAL(1, safetyLevel(c, in, 1000, &a));  // HB perdu : alarme
  TEST_ASSERT_TRUE(a);
  in = {true, 10, false};
  TEST_ASSERT_EQUAL(1, safetyLevel(c, in, 1000, &a));  // tache UDP figee
  c.activeLevel = 0;
  in = {true, 500, true};
  TEST_ASSERT_EQUAL(0, safetyLevel(c, in, 1000, &a));
  in = {true, 10, true};
  TEST_ASSERT_EQUAL(1, safetyLevel(c, in, 1000, &a));
  c.enabled = false;
  TEST_ASSERT_EQUAL(-1, safetyLevel(c, in, 1000, &a));
}

void test_safety_heartbeat_mode() {
  using namespace udps;
  SafetyCfg c = defaultSafetyCfg();
  c.enabled = true;
  c.pin = 14;
  c.activeLevel = 1;
  c.mode = MODE_HEARTBEAT;
  c.periodMs = 40;
  c.delayMs = 200;
  bool a;
  GuardInput ok = {true, 20, true};
  int l0 = safetyLevel(c, ok, 0, &a);
  int l1 = safetyLevel(c, ok, 20, &a);
  TEST_ASSERT_FALSE(a);
  TEST_ASSERT_NOT_EQUAL(l0, l1);  // le signal bascule toutes les demi-periodes
  TEST_ASSERT_EQUAL(l0, safetyLevel(c, ok, 40, &a));
  GuardInput bad = {true, 300, true};
  TEST_ASSERT_EQUAL(1, safetyLevel(c, bad, 0, &a));
  TEST_ASSERT_EQUAL(1, safetyLevel(c, bad, 20, &a));  // fige au niveau actif
  TEST_ASSERT_TRUE(a);
  GuardInput unarmed = {false, 0, true};
  TEST_ASSERT_EQUAL(1, safetyLevel(c, unarmed, 20, &a));  // pas d impulsion tant que non arme
}

void test_deadman_and_link() {
  using namespace udps;
  GuardInput in = {true, 150, true};
  TEST_ASSERT_FALSE(deadmanExpired(in, 0));  // 0 = pas de watchdog
  TEST_ASSERT_FALSE(deadmanExpired(in, 200));
  TEST_ASSERT_TRUE(deadmanExpired(in, 100));
  in.armed = false;
  TEST_ASSERT_FALSE(deadmanExpired(in, 100));  // non arme : jamais
  TEST_ASSERT_EQUAL(L_INACTIVE, classifyLink(false, false, false, 0, 50, 1000));
  TEST_ASSERT_EQUAL(L_PENDING, classifyLink(false, true, false, 0, 50, 1000));
  TEST_ASSERT_EQUAL(L_ESTABLISHED, classifyLink(true, false, true, 40, 50, 1000));
  TEST_ASSERT_EQUAL(L_DEGRADED, classifyLink(true, false, true, 120, 50, 1000));
  TEST_ASSERT_EQUAL(L_LOST, classifyLink(true, false, true, 1500, 50, 1000));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_sha256_abc);
  RUN_TEST(test_hmac_rfc4231_like);
  RUN_TEST(test_session_key_vector);
  RUN_TEST(test_frame_vector_and_roundtrip);
  RUN_TEST(test_tamper_and_unsigned);
  RUN_TEST(test_malformed);
  RUN_TEST(test_seq_wraparound);
  RUN_TEST(test_hex_crc);
  RUN_TEST(test_pin_validation);
  RUN_TEST(test_cfg_validation);
  RUN_TEST(test_safety_fault_mode);
  RUN_TEST(test_safety_heartbeat_mode);
  RUN_TEST(test_deadman_and_link);
  return UNITY_END();
}
