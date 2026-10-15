#include "apns_jwt.h"

#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

#include <cstring>
#include <functional>
#include <fstream>
#include <memory>
#include <sstream>
#include <utility>

#include "base64.h"

namespace chirp {
namespace app_notification {
namespace {

using common::Base64UrlEncode;

// P-256 coordinate width: the JWS signature is exactly two of these.
constexpr int kP256IntBytes = 32;

std::string Base64Url(const std::string& text) {
  return Base64UrlEncode(reinterpret_cast<const uint8_t*>(text.data()),
                         text.size());
}

// The kid/iss values are provider-assigned ASCII, but escape defensively so
// a hostile config cannot break out of the JSON string.
std::string JsonEscape(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  for (const char c : text) {
    if (c == '"' || c == '\\') {
      out.push_back('\\');
    }
    out.push_back(c);
  }
  return out;
}

// ECDSA over SHA-256 with the given P-256 key; DER signature out.
bool SignSha256(EVP_PKEY* pkey, const std::string& message,
                std::string* der_out, std::string* error_out) {
  std::unique_ptr<EVP_MD_CTX, std::function<void(EVP_MD_CTX*)>> ctx(
      EVP_MD_CTX_new(),
      [](EVP_MD_CTX* c) {
        if (c != nullptr) {
          EVP_MD_CTX_free(c);
        }
      });
  if (ctx == nullptr) {
    // OOM-only: the key was fully validated before this point.
    *error_out = "md ctx alloc failed";
    return false;
  }
  size_t sig_len = 0;
  if (EVP_DigestSignInit(ctx.get(), nullptr, EVP_sha256(), nullptr, pkey) != 1 ||
      EVP_DigestSign(ctx.get(), nullptr, &sig_len,
                     reinterpret_cast<const uint8_t*>(message.data()),
                     message.size()) != 1) {
    *error_out = "ecdsa sign init failed";
    return false;
  }
  std::string der(static_cast<size_t>(sig_len), '\0');
  if (EVP_DigestSign(ctx.get(), reinterpret_cast<uint8_t*>(&der[0]), &sig_len,
                     reinterpret_cast<const uint8_t*>(message.data()),
                     message.size()) != 1) {
    *error_out = "ecdsa sign failed";
    return false;
  }
  der.resize(static_cast<size_t>(sig_len));
  *der_out = std::move(der);
  return true;
}

}  // namespace

bool DerEcdsaSigToRaw(const std::string& der_signature, std::string* raw_out) {
  if (raw_out == nullptr) {
    return false;
  }
  raw_out->clear();
  // d2i consumes the buffer, so parse from a mutable copy.
  std::string der = der_signature;
  const uint8_t* cursor = reinterpret_cast<const uint8_t*>(der.data());
  std::unique_ptr<ECDSA_SIG, std::function<void(ECDSA_SIG*)>> sig(
      der.empty()
          ? nullptr
          : d2i_ECDSA_SIG(nullptr, &cursor,
                          static_cast<long>(der.size())),
      [](ECDSA_SIG* s) {
        if (s != nullptr) {
          ECDSA_SIG_free(s);
        }
      });
  if (sig == nullptr) {
    return false;
  }
  // d2i_ECDSA_SIG only succeeds with both integers present, so r/s are
  // non-null here.
  const BIGNUM* r = ECDSA_SIG_get0_r(sig.get());
  const BIGNUM* s = ECDSA_SIG_get0_s(sig.get());
  std::string raw(static_cast<size_t>(kP256IntBytes) * 2, '\0');
  if (BN_bn2binpad(r, reinterpret_cast<uint8_t*>(&raw[0]), kP256IntBytes) !=
          kP256IntBytes ||
      BN_bn2binpad(s, reinterpret_cast<uint8_t*>(&raw[kP256IntBytes]),
                   kP256IntBytes) != kP256IntBytes) {
    // Integer wider than the P-256 field: the signature is not ES256.
    raw_out->clear();
    return false;
  }
  *raw_out = std::move(raw);
  return true;
}

bool BuildApnsProviderToken(const std::string& pem_private_key,
                            const std::string& key_id,
                            const std::string& team_id,
                            int64_t issued_at_epoch_seconds,
                            std::string* token_out,
                            std::string* error_out) {
  if (token_out == nullptr || error_out == nullptr) {
    return false;
  }
  token_out->clear();
  error_out->clear();
  if (key_id.empty() || team_id.empty() || pem_private_key.empty()) {
    *error_out = "provider token needs kid, team and private key";
    return false;
  }

  std::unique_ptr<BIO, std::function<void(BIO*)>> bio(
      BIO_new_mem_buf(pem_private_key.data(),
                      static_cast<int>(pem_private_key.size())),
      [](BIO* b) {
        if (b != nullptr) {
          BIO_free(b);
        }
      });
  std::unique_ptr<EVP_PKEY, std::function<void(EVP_PKEY*)>> pkey(
      bio == nullptr
          ? nullptr
          : PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr),
      [](EVP_PKEY* k) {
        if (k != nullptr) {
          EVP_PKEY_free(k);
        }
      });
  if (pkey == nullptr) {
    *error_out = "private key pem parse failed";
    return false;
  }
  if (EVP_PKEY_base_id(pkey.get()) != EVP_PKEY_EC) {
    *error_out = "provider key must be an EC key";
    return false;
  }
  char group_name[64] = {};
  EVP_PKEY_get_group_name(pkey.get(), group_name, sizeof(group_name), nullptr);
  if (std::strcmp(group_name, "prime256v1") != 0) {
    *error_out = "provider key must be an EC P-256 key";
    return false;
  }

  const std::string header_json =
      "{\"alg\":\"ES256\",\"kid\":\"" + JsonEscape(key_id) + "\"}";
  const std::string claims_json =
      "{\"iss\":\"" + JsonEscape(team_id) + "\",\"iat\":" +
      std::to_string(issued_at_epoch_seconds) + "}";
  const std::string signing_input =
      Base64Url(header_json) + "." + Base64Url(claims_json);

  std::string der_signature;
  if (!SignSha256(pkey.get(), signing_input, &der_signature, error_out)) {
    return false;
  }
  std::string raw_signature;
  if (!DerEcdsaSigToRaw(der_signature, &raw_signature)) {
    *error_out = "signature conversion failed";
    return false;
  }
  *token_out = signing_input + "." + Base64Url(raw_signature);
  return true;
}

std::string ReadPemFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    return "";
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

}  // namespace app_notification
}  // namespace chirp
