#include "mysql_message_store.h"

#include <cstring>

namespace chirp {
namespace chat {

// MySQLConnection implementation
MySQLConnection::MySQLConnection(const std::string& host, uint16_t port,
                                const std::string& database, const std::string& user,
                                const std::string& password)
    : host_(host), port_(port), database_(database), user_(user), password_(password),
      mysql_(nullptr), result_(nullptr), connected_(false) {
  mysql_ = mysql_init(nullptr);
}

MySQLConnection::~MySQLConnection() {
  Disconnect();
  if (mysql_) {
    mysql_close(mysql_);
  }
}

bool MySQLConnection::Connect() {
  if (connected_) {
    return true;
  }

  if (!mysql_) {
    return false;
  }

  my_bool reconnect = 1;
  mysql_options(mysql_, MYSQL_OPT_RECONNECT, &reconnect);

  if (!mysql_real_connect(mysql_, host_.c_str(), user_.c_str(), password_.c_str(),
                         database_.c_str(), port_, nullptr, CLIENT_MULTI_STATEMENTS)) {
    return false;
  }

  connected_ = true;
  return true;
}

void MySQLConnection::Disconnect() {
  if (result_) {
    mysql_free_result(result_);
    result_ = nullptr;
  }
  connected_ = false;
}

bool MySQLConnection::Query(const std::string& query) {
  if (!connected_) {
    return false;
  }

  if (result_) {
    mysql_free_result(result_);
    result_ = nullptr;
  }

  if (mysql_query(mysql_, query.c_str()) != 0) {
    return false;
  }

  result_ = mysql_store_result(mysql_);
  return true;
}

bool MySQLConnection::Execute(const std::string& query) {
  if (!connected_) {
    return false;
  }

  return mysql_query(mysql_, query.c_str()) == 0;
}

std::vector<std::vector<std::string>> MySQLConnection::FetchResults() {
  std::vector<std::vector<std::string>> rows;

  if (!result_) {
    return rows;
  }

  MYSQL_ROW row;
  while ((row = mysql_fetch_row(result_))) {
    std::vector<std::string> cols;
    unsigned int num_fields = mysql_num_fields(result_);
    for (unsigned int i = 0; i < num_fields; i++) {
      cols.push_back(row[i] ? row[i] : "NULL");
    }
    rows.push_back(std::move(cols));
  }

  return rows;
}

uint64_t MySQLConnection::LastInsertId() {
  return mysql_insert_id(mysql_);
}

uint64_t MySQLConnection::AffectedRows() {
  return mysql_affected_rows(mysql_);
}

std::string MySQLConnection::Escape(const std::string& str) {
  std::vector<char> escaped(str.size() * 2 + 1);
  mysql_real_escape_string(mysql_, escaped.data(), str.c_str(), str.size());
  return std::string(escaped.data());
}

// MySQLConnectionPool implementation
MySQLConnectionPool::MySQLConnectionPool(size_t pool_size,
                                        const std::string& host, uint16_t port,
                                        const std::string& database, const std::string& user,
                                        const std::string& password)
    : pool_size_(pool_size), host_(host), port_(port), database_(database),
      user_(user), password_(password) {
  for (size_t i = 0; i < pool_size_; ++i) {
    auto conn = std::make_unique<MySQLConnection>(host_, port_, database_, user_, password_);
    if (conn->Connect()) {
      connections_.push_back(std::move(conn));
    }
  }
}

MySQLConnectionPool::~MySQLConnectionPool() = default;

std::unique_ptr<MySQLConnection> MySQLConnectionPool::GetConnection() {
  std::lock_guard<std::mutex> lock(mu_);
  if (connections_.empty()) {
    // Create new connection on demand
    auto conn = std::make_unique<MySQLConnection>(host_, port_, database_, user_, password_);
    if (conn->Connect()) {
      return conn;
    }
    return nullptr;
  }

  auto conn = std::move(connections_.back());
  connections_.pop_back();
  return conn;
}

void MySQLConnectionPool::ReturnConnection(std::unique_ptr<MySQLConnection> conn) {
  std::lock_guard<std::mutex> lock(mu_);
  if (connections_.size() < pool_size_) {
    connections_.push_back(std::move(conn));
  }
}

size_t MySQLConnectionPool::GetAvailableCount() const {
  return connections_.size();
}

// MySQLMessageStore implementation
MySQLMessageStore::MySQLMessageStore(std::shared_ptr<MySQLConnectionPool> pool)
    : pool_(pool) {}

bool MySQLMessageStore::Initialize() {
  auto conn = pool_->GetConnection();
  if (!conn) {
    return false;
  }

  // Create messages table
  const char* create_messages_table = R"(
    CREATE TABLE IF NOT EXISTS messages (
      id BIGINT AUTO_INCREMENT PRIMARY KEY,
      message_id VARCHAR(255) NOT NULL UNIQUE,
      sender_id VARCHAR(255) NOT NULL,
      receiver_id VARCHAR(255),
      channel_id VARCHAR(255) NOT NULL,
      channel_type INT NOT NULL,
      msg_type INT NOT NULL,
      content TEXT,
      timestamp BIGINT NOT NULL,
      created_at BIGINT NOT NULL,
      INDEX idx_channel (channel_id, channel_type, timestamp),
      INDEX idx_receiver (receiver_id, timestamp),
      INDEX idx_timestamp (timestamp)
    ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
  )";

  if (!conn->Execute(create_messages_table)) {
    pool_->ReturnConnection(std::move(conn));
    return false;
  }

  // Create read_receipts table
  const char* create_read_receipts_table = R"(
    CREATE TABLE IF NOT EXISTS read_receipts (
      id BIGINT AUTO_INCREMENT PRIMARY KEY,
      message_id VARCHAR(255) NOT NULL,
      user_id VARCHAR(255) NOT NULL,
      read_at BIGINT NOT NULL,
      INDEX idx_message (message_id),
      INDEX idx_user (user_id),
      UNIQUE KEY unique_message_user (message_id, user_id)
    ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
  )";

  if (!conn->Execute(create_read_receipts_table)) {
    pool_->ReturnConnection(std::move(conn));
    return false;
  }

  // Create read_cursors table for tracking user's last read position
  const char* create_read_cursors_table = R"(
    CREATE TABLE IF NOT EXISTS read_cursors (
      id BIGINT AUTO_INCREMENT PRIMARY KEY,
      user_id VARCHAR(255) NOT NULL,
      channel_id VARCHAR(255) NOT NULL,
      channel_type INT NOT NULL,
      last_read_message_id VARCHAR(255),
      last_read_timestamp BIGINT NOT NULL,
      unread_count INT DEFAULT 0,
      UNIQUE KEY unique_user_channel (user_id, channel_id, channel_type),
      INDEX idx_user (user_id)
    ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
  )";

  bool result = conn->Execute(create_read_cursors_table);
  pool_->ReturnConnection(std::move(conn));
  return result;
}

bool MySQLMessageStore::StoreMessage(const MessageData& message) {
  auto conn = pool_->GetConnection();
  if (!conn) {
    return false;
  }

  std::string query = "INSERT INTO messages (message_id, sender_id, receiver_id, channel_id, "
                     "channel_type, msg_type, content, timestamp, created_at) VALUES ('" +
                     conn->Escape(message.message_id) + "', '" +
                     conn->Escape(message.sender_id) + "', '" +
                     conn->Escape(message.receiver_id) + "', '" +
                     conn->Escape(message.channel_id) + "', " +
                     std::to_string(message.channel_type) + ", " +
                     std::to_string(message.msg_type) + ", '" +
                     conn->Escape(message.content) + "', " +
                     std::to_string(message.timestamp) + ", " +
                     std::to_string(message.created_at) + ")";

  bool result = conn->Execute(query);
  pool_->ReturnConnection(std::move(conn));
  return result;
}

std::vector<MessageData> MySQLMessageStore::GetHistory(const std::string& channel_id,
                                                      int channel_type,
                                                      int64_t before_timestamp,
                                                      int32_t limit) {
  auto conn = pool_->GetConnection();
  if (!conn) {
    return {};
  }

  std::string query = "SELECT message_id, sender_id, receiver_id, channel_id, "
                     "channel_type, msg_type, content, timestamp FROM messages WHERE "
                     "channel_id = '" + conn->Escape(channel_id) + "' AND "
                     "channel_type = " + std::to_string(channel_type);

  if (before_timestamp > 0) {
    query += " AND timestamp < " + std::to_string(before_timestamp);
  }

  query += " ORDER BY timestamp DESC LIMIT " + std::to_string(limit);

  if (!conn->Query(query)) {
    pool_->ReturnConnection(std::move(conn));
    return {};
  }

  auto rows = conn->FetchResults();
  pool_->ReturnConnection(std::move(conn));

  std::vector<MessageData> messages;
  for (auto& row : rows) {
    MessageData msg;
    msg.message_id = row[0];
    msg.sender_id = row[1];
    msg.receiver_id = row[2];
    msg.channel_id = row[3];
    msg.channel_type = std::stoi(row[4]);
    msg.msg_type = std::stoi(row[5]);
    msg.content = row[6];
    msg.timestamp = std::stoll(row[7]);
    messages.push_back(std::move(msg));
  }

  // Reverse to get chronological order
  std::reverse(messages.begin(), messages.end());
  return messages;
}

std::vector<MessageData> MySQLMessageStore::GetOfflineMessages(const std::string& user_id) {
  auto conn = pool_->GetConnection();
  if (!conn) {
    return {};
  }

  std::string query = "SELECT message_id, sender_id, receiver_id, channel_id, "
                     "channel_type, msg_type, content, timestamp FROM messages WHERE "
                     "receiver_id = '" + conn->Escape(user_id) + "' "
                     "ORDER BY timestamp ASC";

  if (!conn->Query(query)) {
    pool_->ReturnConnection(std::move(conn));
    return {};
  }

  auto rows = conn->FetchResults();
  pool_->ReturnConnection(std::move(conn));

  std::vector<MessageData> messages;
  for (auto& row : rows) {
    MessageData msg;
    msg.message_id = row[0];
    msg.sender_id = row[1];
    msg.receiver_id = row[2];
    msg.channel_id = row[3];
    msg.channel_type = std::stoi(row[4]);
    msg.msg_type = std::stoi(row[5]);
    msg.content = row[6];
    msg.timestamp = std::stoll(row[7]);
    messages.push_back(std::move(msg));
  }

  return messages;
}

bool MySQLMessageStore::ClearOfflineMessages(const std::string& user_id) {
  auto conn = pool_->GetConnection();
  if (!conn) {
    return false;
  }

  std::string query = "DELETE FROM messages WHERE receiver_id = '" + conn->Escape(user_id) + "'";
  bool result = conn->Execute(query);
  pool_->ReturnConnection(std::move(conn));
  return result;
}

bool MySQLMessageStore::StoreReadReceipt(const std::string& message_id,
                                        const std::string& user_id, int64_t read_at) {
  auto conn = pool_->GetConnection();
  if (!conn) {
    return false;
  }

  std::string query = "INSERT INTO read_receipts (message_id, user_id, read_at) VALUES ('" +
                     conn->Escape(message_id) + "', '" +
                     conn->Escape(user_id) + "', " +
                     std::to_string(read_at) + ") "
                     "ON DUPLICATE KEY UPDATE read_at = " + std::to_string(read_at);

  bool result = conn->Execute(query);
  pool_->ReturnConnection(std::move(conn));
  return result;
}

std::vector<ReadReceiptData> MySQLMessageStore::GetReadReceipts(const std::string& message_id) {
  auto conn = pool_->GetConnection();
  if (!conn) {
    return {};
  }

  std::string query = "SELECT message_id, user_id, read_at FROM read_receipts WHERE "
                     "message_id = '" + conn->Escape(message_id) + "'";

  if (!conn->Query(query)) {
    pool_->ReturnConnection(std::move(conn));
    return {};
  }

  auto rows = conn->FetchResults();
  pool_->ReturnConnection(std::move(conn));

  std::vector<ReadReceiptData> receipts;
  for (auto& row : rows) {
    ReadReceiptData receipt;
    receipt.message_id = row[0];
    receipt.user_id = row[1];
    receipt.read_at = std::stoll(row[2]);
    receipts.push_back(std::move(receipt));
  }

  return receipts;
}

bool MySQLMessageStore::MarkAsRead(const std::string& user_id, const std::string& channel_id,
                                  int channel_type, const std::string& message_id, int64_t read_at) {
  auto conn = pool_->GetConnection();
  if (!conn) {
    return false;
  }

  std::string query = "INSERT INTO read_cursors (user_id, channel_id, channel_type, "
                     "last_read_message_id, last_read_timestamp) VALUES ('" +
                     conn->Escape(user_id) + "', '" +
                     conn->Escape(channel_id) + "', " +
                     std::to_string(channel_type) + ", '" +
                     conn->Escape(message_id) + "', " +
                     std::to_string(read_at) + ") "
                     "ON DUPLICATE KEY UPDATE "
                     "last_read_message_id = '" + conn->Escape(message_id) + "', "
                     "last_read_timestamp = " + std::to_string(read_at);

  bool result = conn->Execute(query);
  pool_->ReturnConnection(std::move(conn));
  return result;
}

int32_t MySQLMessageStore::GetUnreadCount(const std::string& user_id) {
  auto conn = pool_->GetConnection();
  if (!conn) {
    return 0;
  }

  std::string query = "SELECT SUM(unread_count) FROM read_cursors WHERE user_id = '" +
                     conn->Escape(user_id) + "'";

  if (!conn->Query(query)) {
    pool_->ReturnConnection(std::move(conn));
    return 0;
  }

  auto rows = conn->FetchResults();
  pool_->ReturnConnection(std::move(conn));

  if (rows.empty() || rows[0].empty() || rows[0][0] == "NULL") {
    return 0;
  }

  return static_cast<int32_t>(std::stoll(rows[0][0]));
}

std::vector<std::pair<std::string, int32_t>> MySQLMessageStore::GetAllUnread(const std::string& user_id) {
  auto conn = pool_->GetConnection();
  if (!conn) {
    return {};
  }

  std::string query = "SELECT channel_id, unread_count FROM read_cursors WHERE user_id = '" +
                     conn->Escape(user_id) + "'";

  if (!conn->Query(query)) {
    pool_->ReturnConnection(std::move(conn));
    return {};
  }

  auto rows = conn->FetchResults();
  pool_->ReturnConnection(std::move(conn));

  std::vector<std::pair<std::string, int32_t>> result;
  for (auto& row : rows) {
    result.emplace_back(row[0], static_cast<int32_t>(std::stoll(row[1])));
  }

  return result;
}

} // namespace chat
} // namespace chirp
