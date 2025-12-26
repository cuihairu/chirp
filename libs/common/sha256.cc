#include "common/sha256.h"

#include <array>
#include <cstring>

namespace chirp::common {
namespace {

constexpr uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

uint32_t RotR(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

uint32_t Ch(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (~x & z); }
uint32_t Maj(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (x & z) ^ (y & z); }
uint32_t BigSigma0(uint32_t x) { return RotR(x, 2) ^ RotR(x, 13) ^ RotR(x, 22); }
uint32_t BigSigma1(uint32_t x) { return RotR(x, 6) ^ RotR(x, 11) ^ RotR(x, 25); }
uint32_t SmallSigma0(uint32_t x) { return RotR(x, 7) ^ RotR(x, 18) ^ (x >> 3); }
uint32_t SmallSigma1(uint32_t x) { return RotR(x, 17) ^ RotR(x, 19) ^ (x >> 10); }

struct Sha256Ctx {
  uint64_t bit_len = 0;
  uint32_t state[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                       0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  uint8_t buf[64]{};
  size_t buf_len = 0;
};

void Transform(Sha256Ctx* ctx, const uint8_t block[64]) {
  uint32_t w[64];
  for (int i = 0; i < 16; ++i) {
    const int j = i * 4;
    w[i] = (static_cast<uint32_t>(block[j]) << 24) | (static_cast<uint32_t>(block[j + 1]) << 16) |
           (static_cast<uint32_t>(block[j + 2]) << 8) | static_cast<uint32_t>(block[j + 3]);
  }
  for (int i = 16; i < 64; ++i) {
    w[i] = SmallSigma1(w[i - 2]) + w[i - 7] + SmallSigma0(w[i - 15]) + w[i - 16];
  }

  uint32_t a = ctx->state[0];
  uint32_t b = ctx->state[1];
  uint32_t c = ctx->state[2];
  uint32_t d = ctx->state[3];
  uint32_t e = ctx->state[4];
  uint32_t f = ctx->state[5];
  uint32_t g = ctx->state[6];
  uint32_t h = ctx->state[7];

  for (int i = 0; i < 64; ++i) {
    const uint32_t t1 = h + BigSigma1(e) + Ch(e, f, g) + K[i] + w[i];
    const uint32_t t2 = BigSigma0(a) + Maj(a, b, c);
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }

  ctx->state[0] += a;
  ctx->state[1] += b;
  ctx->state[2] += c;
  ctx->state[3] += d;
  ctx->state[4] += e;
  ctx->state[5] += f;
  ctx->state[6] += g;
  ctx->state[7] += h;
}

void Update(Sha256Ctx* ctx, const uint8_t* data, size_t len) {
  ctx->bit_len += static_cast<uint64_t>(len) * 8;

  size_t i = 0;
  if (ctx->buf_len > 0) {
    const size_t n = (len < (64 - ctx->buf_len)) ? len : (64 - ctx->buf_len);
    std::memcpy(ctx->buf + ctx->buf_len, data, n);
    ctx->buf_len += n;
    i += n;
    if (ctx->buf_len == 64) {
      Transform(ctx, ctx->buf);
      ctx->buf_len = 0;
    }
  }

  for (; i + 64 <= len; i += 64) {
    Transform(ctx, data + i);
  }

  const size_t rem = len - i;
  if (rem > 0) {
    std::memcpy(ctx->buf, data + i, rem);
    ctx->buf_len = rem;
  }
}

std::array<uint8_t, 32> Final(Sha256Ctx* ctx) {
  const uint64_t orig_bit_len = ctx->bit_len;
  uint8_t pad[64]{0x80};
  const size_t pad_len = (ctx->buf_len < 56) ? (56 - ctx->buf_len) : (56 + 64 - ctx->buf_len);
  Update(ctx, pad, pad_len);

  uint8_t len_be[8];
  uint64_t bit_len = orig_bit_len;
  for (int i = 7; i >= 0; --i) {
    len_be[i] = static_cast<uint8_t>(bit_len & 0xFF);
    bit_len >>= 8;
  }
  Update(ctx, len_be, 8);

  std::array<uint8_t, 32> out{};
  for (int i = 0; i < 8; ++i) {
    out[i * 4] = static_cast<uint8_t>((ctx->state[i] >> 24) & 0xFF);
    out[i * 4 + 1] = static_cast<uint8_t>((ctx->state[i] >> 16) & 0xFF);
    out[i * 4 + 2] = static_cast<uint8_t>((ctx->state[i] >> 8) & 0xFF);
    out[i * 4 + 3] = static_cast<uint8_t>(ctx->state[i] & 0xFF);
  }
  return out;
}

} // namespace

std::array<uint8_t, 32> Sha256(std::string_view data) {
  Sha256Ctx ctx;
  Update(&ctx, reinterpret_cast<const uint8_t*>(data.data()), data.size());
  return Final(&ctx);
}

std::array<uint8_t, 32> HmacSha256(std::string_view key, std::string_view msg) {
  constexpr size_t kBlock = 64;
  uint8_t kopad[kBlock];
  uint8_t kipad[kBlock];
  uint8_t k0[kBlock]{};

  if (key.size() > kBlock) {
    auto d = Sha256(key);
    std::memcpy(k0, d.data(), d.size());
  } else {
    std::memcpy(k0, key.data(), key.size());
  }

  for (size_t i = 0; i < kBlock; ++i) {
    kipad[i] = static_cast<uint8_t>(k0[i] ^ 0x36);
    kopad[i] = static_cast<uint8_t>(k0[i] ^ 0x5c);
  }

  Sha256Ctx inner;
  Update(&inner, kipad, kBlock);
  Update(&inner, reinterpret_cast<const uint8_t*>(msg.data()), msg.size());
  auto inner_digest = Final(&inner);

  Sha256Ctx outer;
  Update(&outer, kopad, kBlock);
  Update(&outer, inner_digest.data(), inner_digest.size());
  return Final(&outer);
}

bool SecureEqual(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) {
    return false;
  }
  uint8_t diff = 0;
  for (size_t i = 0; i < a.size(); ++i) {
    diff |= static_cast<uint8_t>(a[i]) ^ static_cast<uint8_t>(b[i]);
  }
  return diff == 0;
}

} // namespace chirp::common
