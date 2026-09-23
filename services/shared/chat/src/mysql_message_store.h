#ifndef CHIRP_CHAT_MYSQL_MESSAGE_STORE_H_
#define CHIRP_CHAT_MYSQL_MESSAGE_STORE_H_

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <mysql/mysql.h>

#include "message_store.h"
#include "proto/chat.pb.h"

namespace chirp {
namespace chat {

// MySQL connection wrapper
class MySQLConnection {
public:
  MySQLConnection(const std::string& host, uint16_t port,
                 const std::string& database, const std::string& user,
                 const std::string& password);
  ~MySQLConnection();

  bool Connect();
  void Disconnect();
  bool IsConnected() const { return connected_; }

  // Execute query (SELECT)
  bool Query(const std::string& query);

  // Execute statement (INSERT, UPDATE, DELETE)
  bool Execute(const std::string& query);

  // Get result set
  std::vector<std::vector<std::string>> FetchResults();

  // Get last insert ID
  uint64_t LastInsertId();

  // Get affected rows
  uint64_t AffectedRows();

  // Escape string
  std::string Escape(const std::string& str);

  MYSQL* GetMySQL() { return mysql_; }

private:
  std::string host_;
  uint16_t port_;
  std::string database_;
  std::string user_;
  std::string password_;

  MYSQL* mysql_;
  MYSQL_RES* result_;
  bool connected_;
};

// Connection pool for MySQL
class MySQLConnectionPool {
public:
  MySQLConnectionPool(size_t pool_size,
                     const std::string& host, uint16_t port,
                     const std::string& database, const std::string& user,
                     const std::string& password);

  ~MySQLConnectionPool();

  std::unique_ptr<MySQLConnection> GetConnection();
  void ReturnConnection(std::unique_ptr<MySQLConnection> conn);

  size_t GetPoolSize() const { return pool_size_; }
  size_t GetAvailableCount() const;

private:
  size_t pool_size_;
  std::string host_;
  uint16_t port_;
  std::string database_;
  std::string user_;
  std::string password_;

  std::vector<std::unique_ptr<MySQLConnection>> connections_;
  std::mutex mu_;
};

// MySQL message store (the archive backend behind the MessageStore interface;
// MySQL dialect SQL lives here)
class MySQLMessageStore final : public MessageStore {
public:
  MySQLMessageStore(std::shared_ptr<MySQLConnectionPool> pool);
  ~MySQLMessageStore() override = default;

  // Initialize database schema
  bool Initialize() override;

  // Store message
  bool StoreMessage(const StoredMessage& message) override;

  // Get message history
  std::vector<StoredMessage> GetHistory(const std::string& channel_id,
                                        int channel_type,
                                        int64_t before_timestamp,
                                        int32_t limit) override;

  // 消息引用（P1）：按 channel_id + message_id 判定消息是否仍在库
  bool MessageExists(const std::string& channel_id,
                     const std::string& message_id) override;

  // Get offline messages for a user
  std::vector<StoredMessage> GetOfflineMessages(const std::string& user_id) override;

  // Delete offline messages for a user
  bool ClearOfflineMessages(const std::string& user_id) override;

  // Store read receipt
  bool StoreReadReceipt(const std::string& message_id, const std::string& user_id, int64_t read_at) override;

  // Get read receipts for a message
  std::vector<ReadReceiptData> GetReadReceipts(const std::string& message_id) override;

  // Mark messages as read
  bool MarkAsRead(const std::string& user_id, const std::string& channel_id,
                 int channel_type, const std::string& message_id, int64_t read_at) override;

  // Get unread count
  int32_t GetUnreadCount(const std::string& user_id) override;

  // Get all unread counts per channel
  std::vector<std::pair<std::string, int32_t>> GetAllUnread(const std::string& user_id) override;

private:
  std::shared_ptr<MySQLConnectionPool> pool_;
};

} // namespace chat
} // namespace chirp

#endif // CHIRP_CHAT_MYSQL_MESSAGE_STORE_H_
