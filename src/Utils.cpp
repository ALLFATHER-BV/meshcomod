#include "Utils.h"
#include <AES.h>
#include <SHA256.h>

#ifdef ARDUINO
  #include <Arduino.h>
#endif

namespace mesh {

uint32_t RNG::nextInt(uint32_t _min, uint32_t _max) {
  uint32_t num;
  random((uint8_t *) &num, sizeof(num));
  return (num % (_max - _min)) + _min;
}

void Utils::sha256(uint8_t *hash, size_t hash_len, const uint8_t* msg, int msg_len) {
  SHA256 sha;
  sha.update(msg, msg_len);
  sha.finalize(hash, hash_len);
}

void Utils::sha256(uint8_t *hash, size_t hash_len, const uint8_t* frag1, int frag1_len, const uint8_t* frag2, int frag2_len) {
  SHA256 sha;
  sha.update(frag1, frag1_len);
  sha.update(frag2, frag2_len);
  sha.finalize(hash, hash_len);
}

// ---- Self-contained AES-128 ECB ----
// Reference implementation, no external dependencies. Used in place of
// arduinolibs/Crypto AES128 because that library has a deterministic bug on
// this ESP32-S3 toolchain where every encrypt() call after the first one
// produces all-zero ciphertext (msg 2+ on the wire ended up `49 B6 26 16 00…`,
// hash stuck at a constant value across reboots). Writing AES from scratch
// removes the entire library — and the bug — from the equation.

static const uint8_t MC_AES_SBOX[256] = {
  0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
  0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
  0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
  0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
  0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
  0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
  0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
  0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
  0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
  0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
  0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
  0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
  0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
  0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
  0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
  0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

static const uint8_t MC_AES_INVSBOX[256] = {
  0x52,0x09,0x6a,0xd5,0x30,0x36,0xa5,0x38,0xbf,0x40,0xa3,0x9e,0x81,0xf3,0xd7,0xfb,
  0x7c,0xe3,0x39,0x82,0x9b,0x2f,0xff,0x87,0x34,0x8e,0x43,0x44,0xc4,0xde,0xe9,0xcb,
  0x54,0x7b,0x94,0x32,0xa6,0xc2,0x23,0x3d,0xee,0x4c,0x95,0x0b,0x42,0xfa,0xc3,0x4e,
  0x08,0x2e,0xa1,0x66,0x28,0xd9,0x24,0xb2,0x76,0x5b,0xa2,0x49,0x6d,0x8b,0xd1,0x25,
  0x72,0xf8,0xf6,0x64,0x86,0x68,0x98,0x16,0xd4,0xa4,0x5c,0xcc,0x5d,0x65,0xb6,0x92,
  0x6c,0x70,0x48,0x50,0xfd,0xed,0xb9,0xda,0x5e,0x15,0x46,0x57,0xa7,0x8d,0x9d,0x84,
  0x90,0xd8,0xab,0x00,0x8c,0xbc,0xd3,0x0a,0xf7,0xe4,0x58,0x05,0xb8,0xb3,0x45,0x06,
  0xd0,0x2c,0x1e,0x8f,0xca,0x3f,0x0f,0x02,0xc1,0xaf,0xbd,0x03,0x01,0x13,0x8a,0x6b,
  0x3a,0x91,0x11,0x41,0x4f,0x67,0xdc,0xea,0x97,0xf2,0xcf,0xce,0xf0,0xb4,0xe6,0x73,
  0x96,0xac,0x74,0x22,0xe7,0xad,0x35,0x85,0xe2,0xf9,0x37,0xe8,0x1c,0x75,0xdf,0x6e,
  0x47,0xf1,0x1a,0x71,0x1d,0x29,0xc5,0x89,0x6f,0xb7,0x62,0x0e,0xaa,0x18,0xbe,0x1b,
  0xfc,0x56,0x3e,0x4b,0xc6,0xd2,0x79,0x20,0x9a,0xdb,0xc0,0xfe,0x78,0xcd,0x5a,0xf4,
  0x1f,0xdd,0xa8,0x33,0x88,0x07,0xc7,0x31,0xb1,0x12,0x10,0x59,0x27,0x80,0xec,0x5f,
  0x60,0x51,0x7f,0xa9,0x19,0xb5,0x4a,0x0d,0x2d,0xe5,0x7a,0x9f,0x93,0xc9,0x9c,0xef,
  0xa0,0xe0,0x3b,0x4d,0xae,0x2a,0xf5,0xb0,0xc8,0xeb,0xbb,0x3c,0x83,0x53,0x99,0x61,
  0x17,0x2b,0x04,0x7e,0xba,0x77,0xd6,0x26,0xe1,0x69,0x14,0x63,0x55,0x21,0x0c,0x7d
};

static const uint8_t MC_AES_RCON[11] = {
  0x00,0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36
};

static inline uint8_t mc_xtime(uint8_t x) {
  return (uint8_t)((x << 1) ^ ((x & 0x80) ? 0x1b : 0x00));
}

// Galois field multiplication helper for inverse MixColumns.
static inline uint8_t mc_gmul(uint8_t a, uint8_t b) {
  uint8_t r = 0;
  for (int i = 0; i < 8; ++i) {
    if (b & 1) r ^= a;
    uint8_t hi = a & 0x80;
    a <<= 1;
    if (hi) a ^= 0x1b;
    b >>= 1;
  }
  return r;
}

// Expand 16-byte key into 176-byte round key schedule.
static void mc_aes128_expand_key(const uint8_t key[16], uint8_t rk[176]) {
  memcpy(rk, key, 16);
  for (int i = 16, rc = 1; i < 176; i += 4) {
    uint8_t t0 = rk[i-4], t1 = rk[i-3], t2 = rk[i-2], t3 = rk[i-1];
    if ((i % 16) == 0) {
      // RotWord + SubWord + Rcon
      uint8_t a = MC_AES_SBOX[t1] ^ MC_AES_RCON[rc++];
      uint8_t b = MC_AES_SBOX[t2];
      uint8_t c = MC_AES_SBOX[t3];
      uint8_t d = MC_AES_SBOX[t0];
      t0 = a; t1 = b; t2 = c; t3 = d;
    }
    rk[i+0] = rk[i-16+0] ^ t0;
    rk[i+1] = rk[i-16+1] ^ t1;
    rk[i+2] = rk[i-16+2] ^ t2;
    rk[i+3] = rk[i-16+3] ^ t3;
  }
}

// Encrypt one 16-byte block in place: state <- AES(state).
static void mc_aes128_encrypt_block(const uint8_t rk[176], uint8_t state[16]) {
  // AddRoundKey(0)
  for (int i = 0; i < 16; ++i) state[i] ^= rk[i];

  // 9 main rounds
  for (int round = 1; round <= 9; ++round) {
    // SubBytes + ShiftRows
    uint8_t s[16];
    s[ 0] = MC_AES_SBOX[state[ 0]]; s[ 1] = MC_AES_SBOX[state[ 5]];
    s[ 2] = MC_AES_SBOX[state[10]]; s[ 3] = MC_AES_SBOX[state[15]];
    s[ 4] = MC_AES_SBOX[state[ 4]]; s[ 5] = MC_AES_SBOX[state[ 9]];
    s[ 6] = MC_AES_SBOX[state[14]]; s[ 7] = MC_AES_SBOX[state[ 3]];
    s[ 8] = MC_AES_SBOX[state[ 8]]; s[ 9] = MC_AES_SBOX[state[13]];
    s[10] = MC_AES_SBOX[state[ 2]]; s[11] = MC_AES_SBOX[state[ 7]];
    s[12] = MC_AES_SBOX[state[12]]; s[13] = MC_AES_SBOX[state[ 1]];
    s[14] = MC_AES_SBOX[state[ 6]]; s[15] = MC_AES_SBOX[state[11]];

    // MixColumns
    for (int c = 0; c < 4; ++c) {
      uint8_t a0 = s[c*4+0], a1 = s[c*4+1], a2 = s[c*4+2], a3 = s[c*4+3];
      uint8_t t = a0 ^ a1 ^ a2 ^ a3;
      state[c*4+0] = a0 ^ t ^ mc_xtime(a0 ^ a1);
      state[c*4+1] = a1 ^ t ^ mc_xtime(a1 ^ a2);
      state[c*4+2] = a2 ^ t ^ mc_xtime(a2 ^ a3);
      state[c*4+3] = a3 ^ t ^ mc_xtime(a3 ^ a0);
    }

    // AddRoundKey
    for (int i = 0; i < 16; ++i) state[i] ^= rk[round*16 + i];
  }

  // Final round: SubBytes + ShiftRows + AddRoundKey (no MixColumns)
  uint8_t s[16];
  s[ 0] = MC_AES_SBOX[state[ 0]]; s[ 1] = MC_AES_SBOX[state[ 5]];
  s[ 2] = MC_AES_SBOX[state[10]]; s[ 3] = MC_AES_SBOX[state[15]];
  s[ 4] = MC_AES_SBOX[state[ 4]]; s[ 5] = MC_AES_SBOX[state[ 9]];
  s[ 6] = MC_AES_SBOX[state[14]]; s[ 7] = MC_AES_SBOX[state[ 3]];
  s[ 8] = MC_AES_SBOX[state[ 8]]; s[ 9] = MC_AES_SBOX[state[13]];
  s[10] = MC_AES_SBOX[state[ 2]]; s[11] = MC_AES_SBOX[state[ 7]];
  s[12] = MC_AES_SBOX[state[12]]; s[13] = MC_AES_SBOX[state[ 1]];
  s[14] = MC_AES_SBOX[state[ 6]]; s[15] = MC_AES_SBOX[state[11]];
  for (int i = 0; i < 16; ++i) state[i] = s[i] ^ rk[10*16 + i];
}

// Decrypt one 16-byte block in place using the standard AES-128 inverse path.
static void mc_aes128_decrypt_block(const uint8_t rk[176], uint8_t state[16]) {
  // Initial AddRoundKey with last round key
  for (int i = 0; i < 16; ++i) state[i] ^= rk[10*16 + i];

  // 9 inverse rounds (rounds 9 down to 1)
  for (int round = 9; round >= 1; --round) {
    // InvShiftRows + InvSubBytes
    uint8_t s[16];
    s[ 0] = MC_AES_INVSBOX[state[ 0]]; s[ 5] = MC_AES_INVSBOX[state[ 1]];
    s[10] = MC_AES_INVSBOX[state[ 2]]; s[15] = MC_AES_INVSBOX[state[ 3]];
    s[ 4] = MC_AES_INVSBOX[state[ 4]]; s[ 9] = MC_AES_INVSBOX[state[ 5]];
    s[14] = MC_AES_INVSBOX[state[ 6]]; s[ 3] = MC_AES_INVSBOX[state[ 7]];
    s[ 8] = MC_AES_INVSBOX[state[ 8]]; s[13] = MC_AES_INVSBOX[state[ 9]];
    s[ 2] = MC_AES_INVSBOX[state[10]]; s[ 7] = MC_AES_INVSBOX[state[11]];
    s[12] = MC_AES_INVSBOX[state[12]]; s[ 1] = MC_AES_INVSBOX[state[13]];
    s[ 6] = MC_AES_INVSBOX[state[14]]; s[11] = MC_AES_INVSBOX[state[15]];
    memcpy(state, s, 16);

    // AddRoundKey (inverse rounds use round keys in reverse)
    for (int i = 0; i < 16; ++i) state[i] ^= rk[round*16 + i];

    // InvMixColumns: [0e 0b 0d 09; 09 0e 0b 0d; 0d 09 0e 0b; 0b 0d 09 0e]
    uint8_t out[16];
    for (int c = 0; c < 4; ++c) {
      uint8_t a0 = state[c*4+0], a1 = state[c*4+1],
              a2 = state[c*4+2], a3 = state[c*4+3];
      out[c*4+0] = mc_gmul(a0,0x0e) ^ mc_gmul(a1,0x0b) ^ mc_gmul(a2,0x0d) ^ mc_gmul(a3,0x09);
      out[c*4+1] = mc_gmul(a0,0x09) ^ mc_gmul(a1,0x0e) ^ mc_gmul(a2,0x0b) ^ mc_gmul(a3,0x0d);
      out[c*4+2] = mc_gmul(a0,0x0d) ^ mc_gmul(a1,0x09) ^ mc_gmul(a2,0x0e) ^ mc_gmul(a3,0x0b);
      out[c*4+3] = mc_gmul(a0,0x0b) ^ mc_gmul(a1,0x0d) ^ mc_gmul(a2,0x09) ^ mc_gmul(a3,0x0e);
    }
    memcpy(state, out, 16);
  }

  // Final inverse round: InvShiftRows + InvSubBytes + AddRoundKey(rk[0..15])
  uint8_t s[16];
  s[ 0] = MC_AES_INVSBOX[state[ 0]]; s[ 5] = MC_AES_INVSBOX[state[ 1]];
  s[10] = MC_AES_INVSBOX[state[ 2]]; s[15] = MC_AES_INVSBOX[state[ 3]];
  s[ 4] = MC_AES_INVSBOX[state[ 4]]; s[ 9] = MC_AES_INVSBOX[state[ 5]];
  s[14] = MC_AES_INVSBOX[state[ 6]]; s[ 3] = MC_AES_INVSBOX[state[ 7]];
  s[ 8] = MC_AES_INVSBOX[state[ 8]]; s[13] = MC_AES_INVSBOX[state[ 9]];
  s[ 2] = MC_AES_INVSBOX[state[10]]; s[ 7] = MC_AES_INVSBOX[state[11]];
  s[12] = MC_AES_INVSBOX[state[12]]; s[ 1] = MC_AES_INVSBOX[state[13]];
  s[ 6] = MC_AES_INVSBOX[state[14]]; s[11] = MC_AES_INVSBOX[state[15]];
  for (int i = 0; i < 16; ++i) state[i] = s[i] ^ rk[i];
}

int Utils::decrypt(const uint8_t* shared_secret, uint8_t* dest, const uint8_t* src, int src_len) {
  uint8_t rk[176];
  mc_aes128_expand_key(shared_secret, rk);

  uint8_t* dp = dest;
  const uint8_t* sp = src;
  while (sp - src < src_len) {
    uint8_t state[16];
    memcpy(state, sp, 16);
    mc_aes128_decrypt_block(rk, state);
    memcpy(dp, state, 16);
    dp += 16; sp += 16;
  }
  return sp - src;  // will always be multiple of 16
}

int Utils::encrypt(const uint8_t* shared_secret, uint8_t* dest, const uint8_t* src, int src_len) {
  uint8_t rk[176];
  mc_aes128_expand_key(shared_secret, rk);

  uint8_t* dp = dest;
  while (src_len > 0) {
    uint8_t state[16];
    memset(state, 0, 16);
    int n = src_len >= 16 ? 16 : src_len;
    memcpy(state, src, n);
    mc_aes128_encrypt_block(rk, state);
    memcpy(dp, state, 16);
    dp += 16; src += n; src_len -= n;
  }
  return dp - dest;  // multiple of 16
}

// Self-test against NIST FIPS-197 Appendix B AES-128 test vector.
// Returns true if our self-contained AES is byte-perfect standard AES-128
// AND decrypt is the exact inverse of encrypt. Called from main.cpp setup()
// to give boot-time confidence the crypto is correct.
bool Utils::selfTestAES() {
  static const uint8_t key[16] = {
    0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
    0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f
  };
  static const uint8_t pt[16] = {
    0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,
    0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff
  };
  static const uint8_t expected[16] = {
    0x69,0xc4,0xe0,0xd8,0x6a,0x7b,0x04,0x30,
    0xd8,0xcd,0xb7,0x80,0x70,0xb4,0xc5,0x5a
  };
  uint8_t rk[176];
  mc_aes128_expand_key(key, rk);
  uint8_t state[16];
  memcpy(state, pt, 16);
  mc_aes128_encrypt_block(rk, state);
  if (memcmp(state, expected, 16) != 0) return false;
  mc_aes128_decrypt_block(rk, state);
  return memcmp(state, pt, 16) == 0;
}

int Utils::encryptThenMAC(const uint8_t* shared_secret, uint8_t* dest, const uint8_t* src, int src_len) {
  int enc_len = encrypt(shared_secret, dest + CIPHER_MAC_SIZE, src, src_len);

  SHA256 sha;
  sha.resetHMAC(shared_secret, PUB_KEY_SIZE);
  sha.update(dest + CIPHER_MAC_SIZE, enc_len);
  sha.finalizeHMAC(shared_secret, PUB_KEY_SIZE, dest, CIPHER_MAC_SIZE);

  return CIPHER_MAC_SIZE + enc_len;
}

int Utils::MACThenDecrypt(const uint8_t* shared_secret, uint8_t* dest, const uint8_t* src, int src_len) {
  if (src_len <= CIPHER_MAC_SIZE) return 0;  // invalid src bytes

  uint8_t hmac[CIPHER_MAC_SIZE];
  {
    SHA256 sha;
    sha.resetHMAC(shared_secret, PUB_KEY_SIZE);
    sha.update(src + CIPHER_MAC_SIZE, src_len - CIPHER_MAC_SIZE);
    sha.finalizeHMAC(shared_secret, PUB_KEY_SIZE, hmac, CIPHER_MAC_SIZE);
  }
  if (memcmp(hmac, src, CIPHER_MAC_SIZE) == 0) {
    return decrypt(shared_secret, dest, src + CIPHER_MAC_SIZE, src_len - CIPHER_MAC_SIZE);
  }
  return 0; // invalid HMAC
}

static const char hex_chars[] = "0123456789ABCDEF";

void Utils::toHex(char* dest, const uint8_t* src, size_t len) {
  while (len > 0) {
    uint8_t b = *src++;
    *dest++ = hex_chars[b >> 4];
    *dest++ = hex_chars[b & 0x0F];
    len--;
  }
  *dest = 0;
}

void Utils::printHex(Stream& s, const uint8_t* src, size_t len) {
  while (len > 0) {
    uint8_t b = *src++;
    s.print(hex_chars[b >> 4]);
    s.print(hex_chars[b & 0x0F]);
    len--;
  }
}

static uint8_t hexVal(char c) {
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= '0' && c <= '9') return c - '0';
  return 0;
}

bool Utils::isHexChar(char c) {
  return c == '0' || hexVal(c) > 0;
}

bool Utils::fromHex(uint8_t* dest, int dest_size, const char *src_hex) {
  int len = strlen(src_hex);
  if (len != dest_size*2) return false;  // incorrect length

  uint8_t* dp = dest;
  while (dp - dest < dest_size) {
    char ch = *src_hex++;
    char cl = *src_hex++;
    *dp++ = (hexVal(ch) << 4) | hexVal(cl);
  }
  return true;
}

int Utils::parseTextParts(char* text, const char* parts[], int max_num, char separator) {
  int num = 0;
  char* sp = text;
  while (*sp && num < max_num) {
    parts[num++] = sp;
    while (*sp && *sp != separator) sp++;
    if (*sp) {
       *sp++ = 0;  // replace the seperator with a null, and skip past it
    }
  }
  // if we hit the maximum parts, make sure LAST entry does NOT have separator
  while (*sp && *sp != separator) sp++;
  if (*sp) {
    *sp = 0;  // replace the separator with null
  }
  return num;
}

}
