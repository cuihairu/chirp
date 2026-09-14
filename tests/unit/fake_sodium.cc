// Scripted fake implementation of the libsodium subset declared in
// fake_sodium/sodium.h. See that header for the rationale.

#include "sodium.h"

#include <cstring>
#include <mutex>
#include <string>

namespace chirp_test {
namespace fake_sodium {

struct State {
  std::mutex mu;
  bool init_should_fail = false;
  bool pwhash_should_fail = false;
  uint64_t random_seed = 0x9e3779b97f4a7c15ull;  // xorshift state
};

inline State& state() {
  static State s;
  return s;
}

void Reset() {
  auto& s = state();
  std::lock_guard<std::mutex> lock(s.mu);
  s.init_should_fail = false;
  s.pwhash_should_fail = false;
  s.random_seed = 0x9e3779b97f4a7c15ull;
}

void SetInitShouldFail(bool fail) {
  state().init_should_fail = fail;
}

void SetPwhashShouldFail(bool fail) {
  state().pwhash_should_fail = fail;
}

inline std::string HexEncode(const unsigned char* data, size_t len) {
  static const char* kHex = "0123456789abcdef";
  std::string out;
  out.reserve(len * 2);
  for (size_t i = 0; i < len; ++i) {
    out.push_back(kHex[data[i] >> 4]);
    out.push_back(kHex[data[i] & 0x0F]);
  }
  return out;
}

}  // namespace fake_sodium
}  // namespace chirp_test

extern "C" {

int sodium_init(void) {
  return chirp_test::fake_sodium::state().init_should_fail ? -1 : 0;
}

void randombytes_buf(void* const buf, const size_t size) {
  auto& s = chirp_test::fake_sodium::state();
  std::lock_guard<std::mutex> lock(s.mu);
  auto* out = static_cast<uint8_t*>(buf);
  for (size_t i = 0; i < size; ++i) {
    // xorshift64*
    s.random_seed ^= s.random_seed >> 12;
    s.random_seed ^= s.random_seed << 25;
    s.random_seed ^= s.random_seed >> 27;
    out[i] = static_cast<uint8_t>((s.random_seed * 0x2545F4914F6CDD1Dull) & 0xFF);
  }
}

int crypto_pwhash_str(char* const out,
                      const char* const passwd,
                      unsigned long long passwdlen,
                      unsigned int,
                      size_t) {
  auto& s = chirp_test::fake_sodium::state();
  std::lock_guard<std::mutex> lock(s.mu);
  if (s.pwhash_should_fail) {
    return -1;
  }
  const std::string hex = chirp_test::fake_sodium::HexEncode(
      reinterpret_cast<const unsigned char*>(passwd), passwdlen);
  const std::string encoded = std::string(crypto_pwhash_argon2id_STRPREFIX) + hex + "$";
  if (encoded.size() + 1 > crypto_pwhash_argon2id_STRBYTES) {
    return -1;
  }
  std::memcpy(out, encoded.c_str(), encoded.size() + 1);
  return 0;
}

int crypto_pwhash_argon2id_str_verify(const char* str,
                                      const char* const passwd,
                                      unsigned long long passwdlen) {
  const std::string prefix = crypto_pwhash_argon2id_STRPREFIX;
  if (std::strncmp(str, prefix.c_str(), prefix.size()) != 0) {
    return -1;
  }
  const std::string expect = prefix + chirp_test::fake_sodium::HexEncode(
                                           reinterpret_cast<const unsigned char*>(passwd),
                                           passwdlen) +
                             "$";
  return expect == str ? 0 : -1;
}

}  // extern "C"
