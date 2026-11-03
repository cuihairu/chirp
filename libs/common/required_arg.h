#pragma once

#include <cctype>
#include <string>
#include <string_view>

namespace chirp::common {

// True when the string is empty or contains only whitespace. Used for
// signing secrets: a whitespace-only value is just as useless as an empty
// one (HMAC over a blank key is a known-key signature), so fail-fast callers
// must reject it at the same boundary as the empty case.
inline bool IsBlankString(std::string_view s) {
  for (char c : s) {
    if (!std::isspace(static_cast<unsigned char>(c))) {
      return false;
    }
  }
  return true;
}

// Parse "--flag value" from argv with the same pairing as the per-service
// GetArg helpers, but with no fallback: returns false and fills `err` when
// the flag is missing, carries no value, or its value is empty. Use this for
// arguments where a shared built-in default would be a vulnerability (JWT
// signing secrets above all): the process must fail fast at startup instead
// of running on a secret every deployment shares.
inline bool RequireArg(int argc, char* argv[], const std::string& flag,
                       std::string* out, std::string* err) {
  for (int i = 1; i < argc; ++i) {
    if (argv[i] == flag && i + 1 < argc) {
      if (IsBlankString(argv[i + 1])) {
        if (err) {
          *err = flag + " must not be empty or whitespace-only (pass an explicit value)";
        }
        return false;
      }
      if (out) {
        *out = argv[i + 1];
      }
      return true;
    }
  }
  if (err) {
    *err = flag + " is required: there is no built-in default; pass an explicit value (for signing secrets use a random value of 32+ bytes)";
  }
  return false;
}

}  // namespace chirp::common
