#pragma once

#include <algorithm>
#include <cctype>
#include <string>

namespace chirp::common {

/// @brief True when @p value is a safe bare SQL identifier: non-empty and
/// containing only letters, digits and underscores. Used to gate table names
/// interpolated into CREATE/INSERT/DELETE statements.
inline bool IsValidSqlIdentifier(const std::string& value) {
  if (value.empty()) {
    return false;
  }
  return std::all_of(value.begin(), value.end(), [](unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
  });
}

} // namespace chirp::common
