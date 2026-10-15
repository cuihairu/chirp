#ifndef CHIRP_SERVICES_NOTIFICATION_APNS_JWT_H_
#define CHIRP_SERVICES_NOTIFICATION_APNS_JWT_H_

#include <cstdint>
#include <string>

namespace chirp {
namespace app_notification {

// Builds the APNs provider token (ES256 JWT): header {"alg":"ES256",
// "kid":<key_id>}, claims {"iss":<team_id>, "iat":<issued_at>}, signature =
// ECDSA P-256 (SHA-256) over the base64url(header).base64url(claims) ASCII
// with the DER signature converted to the JWS raw r||s form. pem_private_key
// accepts the .p8 "PRIVATE KEY" (PKCS#8) or the legacy "EC PRIVATE KEY"
// (SEC1) PEM encoding. Returns false and fills error_out when any input is
// missing, the PEM fails to parse, or the key is not an EC P-256 key.
bool BuildApnsProviderToken(const std::string& pem_private_key,
                            const std::string& key_id,
                            const std::string& team_id,
                            int64_t issued_at_epoch_seconds,
                            std::string* token_out,
                            std::string* error_out);

// Reads a PEM file from disk (the --apns-key path feeds this). Returns an
// empty string when the file cannot be read; callers surface the config gap.
std::string ReadPemFile(const std::string& path);

// DER ECDSA signature -> JWS raw r||s fixed-width form (two 32-byte halves
// for P-256). Exposed for unit tests only: the parse arms are otherwise only
// reachable with malformed DER, which the signing path never produces.
bool DerEcdsaSigToRaw(const std::string& der_signature, std::string* raw_out);

}  // namespace app_notification
}  // namespace chirp

#endif  // CHIRP_SERVICES_NOTIFICATION_APNS_JWT_H_
