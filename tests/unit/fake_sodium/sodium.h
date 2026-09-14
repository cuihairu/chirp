// Minimal libsodium API subset for unit tests.
//
// password_hasher.cc / token_generator.cc call five sodium entry points.
// Linking real libsodium into the unit tests would drag a new dependency
// into every environment that runs them, so tests compile against this
// drop-in header plus the scripted implementation in fake_sodium.cc:
// hashing is reversible (the password is hex-encoded into the fake hash
// string) which keeps Hash/Verify round trips observable in tests.

#ifndef CHIRP_TEST_FAKE_SODIUM_SODIUM_H_
#define CHIRP_TEST_FAKE_SODIUM_SODIUM_H_

#include <cstddef>
#include <cstdint>

#define SODIUM_VERSION_STRING "fake"
#define crypto_pwhash_argon2id_STRBYTES 128
#define crypto_pwhash_argon2id_STRPREFIX "$fake$"

#ifdef __cplusplus
extern "C" {
#endif

int sodium_init(void);
void randombytes_buf(void* const buf, const size_t size);

int crypto_pwhash_str(char* const out,
                      const char* const passwd,
                      unsigned long long passwdlen,
                      unsigned int opslimit,
                      size_t memlimit);

int crypto_pwhash_argon2id_str_verify(const char* str,
                                      const char* const passwd,
                                      unsigned long long passwdlen);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CHIRP_TEST_FAKE_SODIUM_SODIUM_H_
