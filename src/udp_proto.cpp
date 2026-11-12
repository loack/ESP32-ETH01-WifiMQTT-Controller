#include "udp_proto.h"

#include <string.h>

namespace udpp {

// ===== SHA-256 (implementation compacte, FIPS 180-4) =====
namespace {
const uint32_t K256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

struct Sha {
  uint32_t h[8];
  uint8_t buf[64];
  size_t blen;
  uint64_t total;
};

void shaBlock(Sha& s, const uint8_t* p) {
  uint32_t w[64];
  for (int i = 0; i < 16; i++)
    w[i] = ((uint32_t)p[i * 4] << 24) | ((uint32_t)p[i * 4 + 1] << 16) | ((uint32_t)p[i * 4 + 2] << 8) | p[i * 4 + 3];
  for (int i = 16; i < 64; i++) {
    uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
    uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  uint32_t a = s.h[0], b = s.h[1], c = s.h[2], d = s.h[3], e = s.h[4], f = s.h[5], g = s.h[6], hh = s.h[7];
  for (int i = 0; i < 64; i++) {
    uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
    uint32_t ch = (e & f) ^ (~e & g);
    uint32_t t1 = hh + S1 + ch + K256[i] + w[i];
    uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
    uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
    uint32_t t2 = S0 + mj;
    hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
  }
  s.h[0] += a; s.h[1] += b; s.h[2] += c; s.h[3] += d; s.h[4] += e; s.h[5] += f; s.h[6] += g; s.h[7] += hh;
}

void shaInit(Sha& s) {
  static const uint32_t init[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                   0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  memcpy(s.h, init, sizeof(init));
  s.blen = 0;
  s.total = 0;
}

void shaUpdate(Sha& s, const uint8_t* p, size_t n) {
  s.total += n;
  while (n > 0) {
    size_t take = 64 - s.blen;
    if (take > n) take = n;
    memcpy(s.buf + s.blen, p, take);
    s.blen += take;
    p += take;
    n -= take;
    if (s.blen == 64) {
      shaBlock(s, s.buf);
      s.blen = 0;
    }
  }
}

void shaFinal(Sha& s, uint8_t out[32]) {
  uint64_t bits = s.total * 8;
  uint8_t pad = 0x80;
  shaUpdate(s, &pad, 1);
  uint8_t zero = 0;
  while (s.blen != 56) shaUpdate(s, &zero, 1);
  uint8_t lenb[8];
  for (int i = 0; i < 8; i++) lenb[i] = (uint8_t)(bits >> (56 - 8 * i));
  shaUpdate(s, lenb, 8);
  for (int i = 0; i < 8; i++) {
    out[i * 4] = s.h[i] >> 24;
    out[i * 4 + 1] = s.h[i] >> 16;
    out[i * 4 + 2] = s.h[i] >> 8;
    out[i * 4 + 3] = s.h[i];
  }
}
}  // namespace

void sha256(const uint8_t* msg, size_t len, uint8_t out[32]) {
  Sha s;
  shaInit(s);
  shaUpdate(s, msg, len);
  shaFinal(s, out);
}

void hmacSha256(const uint8_t* key, size_t klen, const uint8_t* msg, size_t mlen, uint8_t out[32]) {
  uint8_t k[64];
  memset(k, 0, sizeof(k));
  if (klen > 64) {
    sha256(key, klen, k);
  } else if (klen > 0) {
    memcpy(k, key, klen);
  }
  uint8_t ipad[64], opad[64];
  for (int i = 0; i < 64; i++) {
    ipad[i] = k[i] ^ 0x36;
    opad[i] = k[i] ^ 0x5c;
  }
  uint8_t inner[32];
  Sha s;
  shaInit(s);
  shaUpdate(s, ipad, 64);
  shaUpdate(s, msg, mlen);
  shaFinal(s, inner);
  shaInit(s);
  shaUpdate(s, opad, 64);
  shaUpdate(s, inner, 32);
  shaFinal(s, out);
}

bool ctEqual(const uint8_t* a, const uint8_t* b, size_t n) {
  uint8_t d = 0;
  for (size_t i = 0; i < n; i++) d |= a[i] ^ b[i];
  return d == 0;
}

void deriveSessionKey(const uint8_t* master, size_t mlen, uint32_t pcNonce, uint32_t espNonce, uint8_t out[32]) {
  uint8_t msg[7 + 8];
  memcpy(msg, "OMUDPv1", 7);
  put32(msg + 7, pcNonce);
  put32(msg + 11, espNonce);
  hmacSha256(master, mlen, msg, sizeof(msg), out);
}

size_t buildFrame(uint8_t* out, size_t cap, const Header& h, const uint8_t* payload, size_t plen,
                  const uint8_t* key, size_t klen) {
  if (plen > MAX_PAYLOAD) return 0;
  size_t total = HEADER_LEN + plen + (key ? TAG_LEN : 0);
  if (cap < total) return 0;
  out[0] = MAGIC0;
  out[1] = MAGIC1;
  out[2] = h.version ? h.version : VERSION;
  out[3] = h.type;
  out[4] = h.device_id;
  uint8_t flags = h.flags & (uint8_t)~F_SIGNED;
  if (key) flags |= F_SIGNED;
  out[5] = flags;
  put16(out + 6, h.seq);
  put32(out + 8, h.t_us);
  if (plen) memcpy(out + HEADER_LEN, payload, plen);
  if (key) {
    uint8_t mac[32];
    hmacSha256(key, klen, out, HEADER_LEN + plen, mac);
    memcpy(out + HEADER_LEN + plen, mac, TAG_LEN);
  }
  return total;
}

ParseResult parseFrame(const uint8_t* in, size_t len, Header& h, const uint8_t** payload, size_t* plen,
                       const uint8_t* key, size_t klen) {
  if (len > MAX_DATAGRAM) return P_TOO_LONG;
  if (len < HEADER_LEN) return P_TOO_SHORT;
  if (in[0] != MAGIC0 || in[1] != MAGIC1) return P_BAD_MAGIC;
  if (in[2] != VERSION) return P_BAD_VERSION;
  h.version = in[2];
  h.type = in[3];
  h.device_id = in[4];
  h.flags = in[5];
  h.seq = get16(in + 6);
  h.t_us = get32(in + 8);
  bool signedFrame = (h.flags & F_SIGNED) != 0;
  size_t body = len - HEADER_LEN;
  if (key) {
    if (!signedFrame) return P_UNSIGNED_REFUSED;
    if (body < TAG_LEN) return P_TOO_SHORT;
    size_t pl = body - TAG_LEN;
    uint8_t mac[32];
    hmacSha256(key, klen, in, HEADER_LEN + pl, mac);
    if (!ctEqual(mac, in + HEADER_LEN + pl, TAG_LEN)) return P_BAD_TAG;
    *payload = in + HEADER_LEN;
    *plen = pl;
  } else {
    if (signedFrame) return P_BAD_TAG;  // une cle est exigee pour verifier
    *payload = in + HEADER_LEN;
    *plen = body;
  }
  return P_OK;
}

size_t hexToBytes(const char* hex, uint8_t* out, size_t cap) {
  size_t n = strlen(hex);
  if (n == 0 || (n % 2) != 0 || n / 2 > cap) return 0;
  for (size_t i = 0; i < n / 2; i++) {
    int v[2];
    for (int j = 0; j < 2; j++) {
      char c = hex[i * 2 + j];
      if (c >= '0' && c <= '9') v[j] = c - '0';
      else if (c >= 'a' && c <= 'f') v[j] = c - 'a' + 10;
      else if (c >= 'A' && c <= 'F') v[j] = c - 'A' + 10;
      else return 0;
    }
    out[i] = (uint8_t)((v[0] << 4) | v[1]);
  }
  return n / 2;
}

uint16_t crc16(const uint8_t* data, size_t len, uint16_t crc) {
  for (size_t i = 0; i < len; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (int b = 0; b < 8; b++) crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
  }
  return crc;
}

}  // namespace udpp
