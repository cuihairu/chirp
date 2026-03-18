#ifndef CHIRP_SERVICES_CHAT_FILE_STORAGE_MANAGER_H_
#define CHIRP_SERVICES_CHAT_FILE_STORAGE_MANAGER_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "proto/chat.pb.h"

namespace chirp {
namespace chat {

// File upload session
struct UploadSession {
  std::string upload_id;
  std::string file_id;
  std::string user_id;
  std::string channel_id;
  ChannelType channel_type;
  std::string filename;
  int64_t file_size = 0;
  std::string mime_type;
  std::string checksum;
  std::string upload_url;
  int64_t expires_at = 0;
  int64_t created_at = 0;
  bool completed = false;

  mutable std::mutex mu;
};

// Configuration for file storage
struct FileStorageConfig {
  int64_t max_file_size = 100 * 1024 * 1024;  // 100MB default
  int64_t upload_url_ttl_ms = 3600000;         // 1 hour
  int64_t download_url_ttl_ms = 86400000;      // 24 hours

  // Allowed file types
  std::vector<std::string> allowed_image_types = {"image/jpeg", "image/png", "image/gif", "image/webp"};
  std::vector<std::string> allowed_video_types = {"video/mp4", "video/webm", "video/quicktime"};
  std::vector<std::string> allowed_audio_types = {"audio/mpeg", "audio/ogg", "audio/wav"};
  std::vector<std::string> allowed_document_types = {"application/pdf"};

  // Storage backend configuration
  std::string storage_backend = "local";        // local, s3, gcs, azure
  std::string storage_path = "/tmp/chirp_files";
  std::string storage_bucket = "";              // For cloud storage
  std::string storage_region = "";
  std::string storage_access_key = "";
  std::string storage_secret_key = "";
  std::string storage_public_url_base = "";     // For generating public URLs
};

// Callback for virus scanning (optional)
using VirusScanCallback = std::function<void(const std::string& file_id,
                                            const std::string& file_path,
                                            std::function<void(bool clean)> result)>;

// Manages file uploads and downloads
class FileStorageManager {
public:
  explicit FileStorageManager(const FileStorageConfig& config = FileStorageConfig());
  ~FileStorageManager() = default;

  // Prepare upload (generate presigned URL)
  bool PrepareUpload(const std::string& user_id,
                    const std::string& channel_id,
                    ChannelType channel_type,
                    const std::string& filename,
                    int64_t file_size,
                    const std::string& mime_type,
                    const std::string& checksum,
                    PrepareFileUploadResponse* out_response);

  // Confirm upload (after client uploads)
  bool ConfirmUpload(const std::string& upload_id,
                    const std::string& file_id,
                    ConfirmFileUploadResponse* out_response);

  // Get download URL
  bool GetDownloadUrl(const std::string& file_id,
                     const std::string& user_id,
                     GetFileDownloadResponse* out_response);

  // Delete file
  bool DeleteFile(const std::string& file_id, const std::string& user_id);

  // Get file info
  bool GetFileInfo(const std::string& file_id, FileInfo* out_info);

  // Set virus scan callback
  void SetVirusScanCallback(VirusScanCallback callback) {
    virus_scan_callback_ = std::move(callback);
  }

  // Cleanup expired upload sessions
  void CleanupExpiredSessions();

  // Get statistics
  size_t GetActiveUploadCount() const;
  int64_t GetTotalStorageUsed() const;

  // Validate file type
  bool IsFileTypeAllowed(const std::string& mime_type) const;

  // Generate file ID
  std::string GenerateFileId();

  // Generate upload ID
  std::string GenerateUploadId();

private:
  std::string GeneratePresignedUrl(const std::string& file_id, bool is_upload, int64_t ttl_ms);
  std::string GetStoragePath(const std::string& file_id) const;
  bool ValidateChecksum(const std::string& file_path, const std::string& expected_checksum);

  int64_t GetCurrentTimeMs() const;

  FileStorageConfig config_;
  mutable std::mutex mu_;

  // Upload sessions
  std::unordered_map<std::string, std::shared_ptr<UploadSession>> upload_sessions_;

  // File metadata storage
  std::unordered_map<std::string, FileInfo> file_metadata_;

  // Storage usage tracking
  int64_t total_storage_used_ = 0;

  // Virus scan callback
  VirusScanCallback virus_scan_callback_;
};

} // namespace chat
} // namespace chirp

#endif // CHIRP_SERVICES_CHAT_FILE_STORAGE_MANAGER_H_
