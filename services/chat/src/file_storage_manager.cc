#include "file_storage_manager.h"

#include <algorithm>
#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>

#ifdef _WIN32
#include <windows.h>
#include <fileapi.h>
#else
#include <sys/statvfs.h>
#endif

namespace chirp {
namespace chat {

namespace {

// Simple checksum computation (for demo - use SHA256 in production)
std::string ComputeSimpleChecksum(const std::string& data) {
  uint32_t hash = 0;
  for (char c : data) {
    hash = hash * 31 + static_cast<uint8_t>(c);
  }

  std::ostringstream ss;
  ss << std::hex << std::setw(8) << std::setfill('0') << hash;
  return ss.str();
}

// Generate random string
std::string GenerateRandomString(size_t length) {
  const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> dis(0, sizeof(charset) - 2);

  std::string result;
  result.reserve(length);

  for (size_t i = 0; i < length; ++i) {
    result += charset[dis(gen)];
  }

  return result;
}

} // namespace

FileStorageManager::FileStorageManager(const FileStorageConfig& config)
    : config_(config), total_storage_used_(0) {}

int64_t FileStorageManager::GetCurrentTimeMs() const {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string FileStorageManager::GenerateFileId() {
  return "file_" + GenerateRandomString(16);
}

std::string FileStorageManager::GenerateUploadId() {
  return "upload_" + GenerateRandomString(16);
}

bool FileStorageManager::IsFileTypeAllowed(const std::string& mime_type) const {
  auto check = [&mime_type](const std::vector<std::string>& types) {
    return std::find(types.begin(), types.end(), mime_type) != types.end();
  };

  return check(config_.allowed_image_types) ||
         check(config_.allowed_video_types) ||
         check(config_.allowed_audio_types) ||
         check(config_.allowed_document_types);
}

std::string FileStorageManager::GetStoragePath(const std::string& file_id) const {
  return config_.storage_path + "/" + file_id;
}

std::string FileStorageManager::GeneratePresignedUrl(const std::string& file_id,
                                                     bool is_upload,
                                                     int64_t ttl_ms) {
  // In a real implementation, this would generate actual presigned URLs
  // for S3, GCS, or Azure Blob Storage

  std::ostringstream ss;
  ss << config_.storage_public_url_base;
  if (!ss.str().empty() && ss.str().back() != '/') {
    ss << "/";
  }
  ss << file_id;

  // Add expiration and signature (simplified)
  int64_t expires = GetCurrentTimeMs() + ttl_ms;
  ss << "?expires=" << expires;
  ss << "&signature=" << GenerateRandomString(32);

  return ss.str();
}

bool FileStorageManager::ValidateChecksum(const std::string& file_path,
                                        const std::string& expected_checksum) {
  // In production, use SHA256
  // For demo, just return true
  return true;
}

bool FileStorageManager::PrepareUpload(const std::string& user_id,
                                      const std::string& channel_id,
                                      ChannelType channel_type,
                                      const std::string& filename,
                                      int64_t file_size,
                                      const std::string& mime_type,
                                      const std::string& checksum,
                                      PrepareFileUploadResponse* out_response) {
  if (!out_response) {
    return false;
  }

  // Validate file size
  if (file_size > config_.max_file_size) {
    return false;
  }

  // Validate file type
  if (!IsFileTypeAllowed(mime_type)) {
    return false;
  }

  std::string file_id = GenerateFileId();
  std::string upload_id = GenerateUploadId();
  int64_t now = GetCurrentTimeMs();

  // Create upload session
  auto session = std::make_shared<UploadSession>();
  session->upload_id = upload_id;
  session->file_id = file_id;
  session->user_id = user_id;
  session->channel_id = channel_id;
  session->channel_type = channel_type;
  session->filename = filename;
  session->file_size = file_size;
  session->mime_type = mime_type;
  session->checksum = checksum;
  session->created_at = now;
  session->expires_at = now + config_.upload_url_ttl_ms;
  session->completed = false;

  // Generate upload URL
  session->upload_url = GeneratePresignedUrl(file_id, true, config_.upload_url_ttl_ms);

  {
    std::lock_guard<std::mutex> lock(mu_);
    upload_sessions_[upload_id] = session;
  }

  // Build response
  out_response->mutable_file_info()->set_file_id(file_id);
  out_response->mutable_file_info()->set_filename(filename);
  out_response->mutable_file_info()->set_file_size(file_size);
  out_response->mutable_file_info()->set_mime_type(mime_type);
  out_response->mutable_file_info()->set_checksum(checksum);
  out_response->mutable_file_info()->set_uploaded_by(user_id);
  out_response->set_upload_id(upload_id);
  out_response->set_upload_url(session->upload_url);
  out_response->set_expires_at(session->expires_at);

  return true;
}

bool FileStorageManager::ConfirmUpload(const std::string& upload_id,
                                      const std::string& file_id,
                                      ConfirmFileUploadResponse* out_response) {
  if (!out_response) {
    return false;
  }

  std::lock_guard<std::mutex> lock(mu_);

  auto it = upload_sessions_.find(upload_id);
  if (it == upload_sessions_.end()) {
    return false;
  }

  auto& session = it->second;
  std::lock_guard<std::mutex> session_lock(session->mu);

  if (session->completed) {
    return false;  // Already confirmed
  }

  // Check if expired
  if (GetCurrentTimeMs() > session->expires_at) {
    upload_sessions_.erase(it);
    return false;
  }

  // Verify file ID matches
  if (session->file_id != file_id) {
    return false;
  }

  // Create file metadata
  FileInfo file_info;
  file_info.set_file_id(file_id);
  file_info.set_filename(session->filename);
  file_info.set_file_size(session->file_size);
  file_info.set_mime_type(session->mime_type);
  file_info.set_checksum(session->checksum);
  file_info.set_storage_url(GetStoragePath(file_id));
  file_info.set_uploaded_at(GetCurrentTimeMs());
  file_info.set_uploaded_by(session->user_id);

  // Store metadata
  file_metadata_[file_id] = file_info;
  total_storage_used_ += session->file_size;

  // Mark session as completed
  session->completed = true;

  // Build response
  *out_response->mutable_file_info() = file_info;

  return true;
}

bool FileStorageManager::GetDownloadUrl(const std::string& file_id,
                                       const std::string& user_id,
                                       GetFileDownloadResponse* out_response) {
  if (!out_response) {
    return false;
  }

  std::lock_guard<std::mutex> lock(mu_);

  auto it = file_metadata_.find(file_id);
  if (it == file_metadata_.end()) {
    return false;
  }

  const auto& file_info = it->second;

  // Generate download URL
  std::string download_url = GeneratePresignedUrl(file_id, false, config_.download_url_ttl_ms);

  out_response->set_download_url(download_url);
  out_response->set_expires_at(GetCurrentTimeMs() + config_.download_url_ttl_ms);
  *out_response->mutable_file_info() = file_info;

  return true;
}

bool FileStorageManager::DeleteFile(const std::string& file_id,
                                   const std::string& user_id) {
  std::lock_guard<std::mutex> lock(mu_);

  auto it = file_metadata_.find(file_id);
  if (it == file_metadata_.end()) {
    return false;
  }

  // Check ownership (in production, verify user_id == uploaded_by or is admin)
  const auto& file_info = it->second;

  // Update storage usage
  total_storage_used_ -= file_info.file_size();

  // Remove metadata
  file_metadata_.erase(it);

  // In production, delete the actual file from storage

  return true;
}

bool FileStorageManager::GetFileInfo(const std::string& file_id,
                                    FileInfo* out_info) {
  if (!out_info) {
    return false;
  }

  std::lock_guard<std::mutex> lock(mu_);

  auto it = file_metadata_.find(file_id);
  if (it == file_metadata_.end()) {
    return false;
  }

  *out_info = it->second;
  return true;
}

void FileStorageManager::CleanupExpiredSessions() {
  std::lock_guard<std::mutex> lock(mu_);

  int64_t now = GetCurrentTimeMs();

  for (auto it = upload_sessions_.begin(); it != upload_sessions_.end();) {
    const auto& session = it->second;
    std::lock_guard<std::mutex> session_lock(session->mu);

    if (now > session->expires_at || session->completed) {
      it = upload_sessions_.erase(it);
    } else {
      ++it;
    }
  }
}

size_t FileStorageManager::GetActiveUploadCount() const {
  std::lock_guard<std::mutex> lock(mu_);
  return upload_sessions_.size();
}

int64_t FileStorageManager::GetTotalStorageUsed() const {
  std::lock_guard<std::mutex> lock(mu_);
  return total_storage_used_;
}

} // namespace chat
} // namespace chirp
