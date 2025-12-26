#include "common/jwt.h"

#include <cctype>
#include <string>

#include "common/base64.h"
#include "common/sha256.h"

namespace chirp::common {
namespace {

std::string JsonEscape(std::string_view s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (char c : s) {
    switch (c) {
    case '\\':
      out += "\\\\";
      break;
    case '"':
      out += "\\\"";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      out.push_back(c);
      break;
    }
  }
  return out;
}

bool ExtractJsonString(std::string_view json, std::string_view key, std::string* out) {
  if (!out) {
    return false;
  }

  const std::string needle = "\"" + std::string(key) + "\"";
  size_t pos = json.find(needle);
  if (pos == std::string::npos) {
    return false;
  }
  pos += needle.size();

  while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) {
    pos++;
  }
  if (pos >= json.size() || json[pos] != ':') {
    return false;
  }
  pos++;
  while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) {
    pos++;
  }
  if (pos >= json.size() || json[pos] != '"') {
    return false;
  }
  pos++;

  std::string val;
  while (pos < json.size()) {
    char c = json[pos++];
    if (c == '"') {
      *out = std::move(val);
      return true;
    }
    if (c == '\\' && pos < json.size()) {
      char esc = json[pos++];
      switch (esc) {
      case '"':
        val.push_back('"');
        break;
      case '\\':
        val.push_back('\\');
        break;
      case 'n':
        val.push_back('\n');
        break;
      case 'r':
        val.push_back('\r');
        break;
      case 't':
        val.push_back('\t');
        break;
      default:
        val.push_back(esc);
        break;
      }
      continue;
    }
    val.push_back(c);
  }
  return false;
}

bool ExtractJsonInt64(std::string_view json, std::string_view key, int64_t* out) {
  if (!out) {
    return false;
  }

  const std::string needle = "\"" + std::string(key) + "\"";
  size_t pos = json.find(needle);
  if (pos == std::string::npos) {
    return false;
  }
  pos += needle.size();

  while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) {
    pos++;
  }
  if (pos >= json.size() || json[pos] != ':') {
    return false;
  }
  pos++;
  while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) {
    pos++;
  }

  bool neg = false;
  if (pos < json.size() && json[pos] == '-') {
    neg = true;
    pos++;
  }
  if (pos >= json.size() || !std::isdigit(static_cast<unsigned char>(json[pos]))) {
    return false;
  }

  int64_t v = 0;
  while (pos < json.size() && std::isdigit(static_cast<unsigned char>(json[pos]))) {
    v = v * 10 + (json[pos] - '0');
    pos++;
  }
  *out = neg ? -v : v;
  return true;
}

} // namespace

std::string JwtSignHS256(std::string_view subject, int64_t issued_at, std::string_view secret) {
  const std::string header_json = R"({"alg":"HS256","typ":"JWT"})";
  const std::string payload_json =
      std::string(R"({"sub":")") + JsonEscape(subject) + R"(","iat":)" + std::to_string(issued_at) + "}";

  const std::string header_b64 =
      Base64UrlEncode(reinterpret_cast<const uint8_t*>(header_json.data()), header_json.size());
  const std::string payload_b64 =
      Base64UrlEncode(reinterpret_cast<const uint8_t*>(payload_json.data()), payload_json.size());

  const std::string signing_input = header_b64 + "." + payload_b64;
  auto sig = HmacSha256(secret, signing_input);
  const std::string sig_b64 = Base64UrlEncode(sig.data(), sig.size());
  return signing_input + "." + sig_b64;
}

bool JwtVerifyHS256(std::string_view token, std::string_view secret, JwtClaims* out, std::string* err) {
  if (err) {
    err->clear();
  }
  if (!out) {
    if (err) {
      *err = "null out";
    }
    return false;
  }

  const size_t dot1 = token.find('.');
  if (dot1 == std::string::npos) {
    if (err) {
      *err = "missing dot";
    }
    return false;
  }
  const size_t dot2 = token.find('.', dot1 + 1);
  if (dot2 == std::string::npos) {
    if (err) {
      *err = "missing second dot";
    }
    return false;
  }

  const std::string_view header_b64 = token.substr(0, dot1);
  const std::string_view payload_b64 = token.substr(dot1 + 1, dot2 - dot1 - 1);
  const std::string_view sig_b64 = token.substr(dot2 + 1);

  const std::string signing_input = std::string(header_b64) + "." + std::string(payload_b64);
  auto expected = HmacSha256(secret, signing_input);
  const std::string expected_sig = Base64UrlEncode(expected.data(), expected.size());
  std::string sig_norm(sig_b64);
  while (!sig_norm.empty() && sig_norm.back() == '=') {
    sig_norm.pop_back();
  }
  if (!SecureEqual(expected_sig, sig_norm)) {
    if (err) {
      *err = "bad signature";
    }
    return false;
  }

  std::string header_json;
  if (!Base64UrlDecode(header_b64, &header_json)) {
    if (err) {
      *err = "bad header b64";
    }
    return false;
  }
  std::string alg;
  if (!ExtractJsonString(header_json, "alg", &alg) || alg != "HS256") {
    if (err) {
      *err = "unsupported alg";
    }
    return false;
  }

  std::string payload_json;
  if (!Base64UrlDecode(payload_b64, &payload_json)) {
    if (err) {
      *err = "bad payload b64";
    }
    return false;
  }

  JwtClaims claims;
  if (!ExtractJsonString(payload_json, "sub", &claims.subject)) {
    if (err) {
      *err = "missing sub";
    }
    return false;
  }
  ExtractJsonInt64(payload_json, "iat", &claims.issued_at);

  *out = std::move(claims);
  return true;
}

} // namespace chirp::common
