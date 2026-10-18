#ifndef CHIRP_SERVICES_NOTIFICATION_DEVICE_STORE_H_
#define CHIRP_SERVICES_NOTIFICATION_DEVICE_STORE_H_

// File-backed persistence for the registered-device registry (audit 表一
// P1-5): devices_ used to live only in memory, so a restart dropped every
// registration until each client happened to re-register.
//
// Format: one line per device, text fields base64url-encoded and joined by
// tabs, so any byte sequence inside a token or an id stays on one row. The
// header line records the format version. Saves write a ".tmp" sibling and
// rename it into place, so a crash mid-write cannot truncate the previous
// snapshot.
//
// Persistence is opt-in: with no DeviceStore wired (the default, and what an
// empty --device_store_file flag yields) the service behaves exactly as
// before, memory-only.

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "base64.h"
#include "logger.h"
#include "notification_service.h"

namespace chirp {
namespace app_notification {

class DeviceStore {
 public:
  explicit DeviceStore(std::string path) : path_(std::move(path)) {}

  // Replaces the stored snapshot with the given devices. Returns false (with
  // an Error log) when the file cannot be written or renamed into place.
  bool SaveAll(const std::vector<DeviceRegistration>& devices) {
    const std::string tmp = path_ + ".tmp";
    {
      std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
      out << kHeader << "\n";
      for (const DeviceRegistration& device : devices) {
        out << EncodeRow(device) << "\n";
      }
      out.flush();
      if (!out) {
        common::Logger::Instance().Error(
            "device store cannot write " + tmp +
            " (missing directory, full disk or bad permissions)");
        return false;
      }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path_, ec);
    if (ec) {
      common::Logger::Instance().Error("device store rename to " + path_ +
                                       " failed: " + ec.message());
      std::filesystem::remove(tmp, ec);
      return false;
    }
    return true;
  }

  // Reads back a SaveAll snapshot. A missing file is an empty registry (first
  // boot); malformed rows are skipped with a Warn so one corrupt line cannot
  // take down the whole restore.
  std::vector<DeviceRegistration> LoadAll() {
    std::vector<DeviceRegistration> devices;
    std::ifstream in(path_, std::ios::binary);
    if (!in.is_open()) {
      return devices;
    }
    std::string line;
    while (std::getline(in, line)) {
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      if (line.empty() || line[0] == '#') {
        continue;
      }
      DeviceRegistration device;
      if (!DecodeRow(line, &device)) {
        continue;
      }
      devices.push_back(std::move(device));
    }
    return devices;
  }

  const std::string& path() const { return path_; }

 private:
  static constexpr const char* kHeader = "# chirp notification device store v1";
  // device_id, user_id, platform, app_version, os_version, registered_at,
  // is_active, apns_token, apns_environment, push_kit_token, fcm_token.
  // Field order is fixed by the v1 header.
  static constexpr int kFieldCount = 11;

  static std::string EncodeField(const std::string& value) {
    return common::Base64UrlEncode(reinterpret_cast<const uint8_t*>(value.data()),
                                   value.size());
  }

  static std::string EncodeRow(const DeviceRegistration& device) {
    std::ostringstream row;
    row << EncodeField(device.device_id)
        << "\t" << EncodeField(device.user_id)
        << "\t" << EncodeField(device.platform)
        << "\t" << EncodeField(device.app_version)
        << "\t" << EncodeField(device.os_version)
        << "\t" << device.registered_at
        << "\t" << (device.is_active ? "1" : "0")
        << "\t" << EncodeField(device.apns_token)
        << "\t" << EncodeField(device.apns_environment)
        << "\t" << EncodeField(device.push_kit_token)
        << "\t" << EncodeField(device.fcm_token);
    return row.str();
  }

  static bool DecodeField(const std::string& cell, std::string* value) {
    if (cell.empty()) {
      value->clear();
      return true;
    }
    return common::Base64UrlDecode(cell, value);
  }

  // Splits keeping trailing empty cells (std::getline would drop the last
  // one, which an empty fcm_token produces).
  static std::vector<std::string> SplitTabs(const std::string& line) {
    std::vector<std::string> cells;
    std::string cell;
    for (const char c : line) {
      if (c == '\t') {
        cells.push_back(cell);
        cell.clear();
      } else {
        cell.push_back(c);
      }
    }
    cells.push_back(cell);
    return cells;
  }

  bool DecodeRow(const std::string& line, DeviceRegistration* device) {
    const std::vector<std::string> cells = SplitTabs(line);
    if (static_cast<int>(cells.size()) != kFieldCount) {
      common::Logger::Instance().Warn("device store " + path_ +
                                      ": skipping row with wrong field count");
      return false;
    }
    if (!DecodeField(cells[0], &device->device_id) ||
        !DecodeField(cells[1], &device->user_id) ||
        !DecodeField(cells[2], &device->platform) ||
        !DecodeField(cells[3], &device->app_version) ||
        !DecodeField(cells[4], &device->os_version) ||
        !DecodeField(cells[7], &device->apns_token) ||
        !DecodeField(cells[8], &device->apns_environment) ||
        !DecodeField(cells[9], &device->push_kit_token) ||
        !DecodeField(cells[10], &device->fcm_token)) {
      common::Logger::Instance().Warn("device store " + path_ +
                                      ": skipping row with invalid base64");
      return false;
    }
    device->registered_at = std::strtoll(cells[5].c_str(), nullptr, 10);
    device->is_active = cells[6] == "1";
    return true;
  }

  std::string path_;
};

} // namespace app_notification
} // namespace chirp

#endif // CHIRP_SERVICES_NOTIFICATION_DEVICE_STORE_H_
