#include "index_sync.h"

#include <cstdlib>
#include <memory>
#include <utility>

#include "logger.h"
#include "mysql_message_store.h"

namespace chirp {
namespace search {

using chirp::chat::MySQLConnection;

MessageIndexSync::MessageIndexSync(MessageSearchIndex& index,
                                   chat::MySQLConnectionPool& pool)
    : index_(index), pool_(pool) {}

bool MessageIndexSync::IngestBatch(const std::string& sql, int64_t* ingested,
                                   std::string* err) {
  *ingested = 0;
  std::unique_ptr<MySQLConnection> conn = pool_.GetConnection();
  if (conn == nullptr || !conn->IsConnected()) {
    if (err != nullptr) {
      *err = "mysql connection unavailable";
    }
    return false;
  }
  if (!conn->Query(sql)) {
    if (err != nullptr) {
      *err = "mysql query failed: " + sql.substr(0, 120);
    }
    pool_.ReturnConnection(std::move(conn));
    return false;
  }
  const auto rows = conn->FetchResults();
  pool_.ReturnConnection(std::move(conn));
  for (const auto& row : rows) {
    // 列序见 SELECT：id, message_id, channel_id, channel_type, msg_type,
    // timestamp, content。
    if (row.size() < 7) {
      continue;
    }
    MessageSearchIndex::Input input;
    input.message_id = row[1];
    input.channel_id = row[2];
    input.channel_type = std::atoi(row[3].c_str());
    input.msg_type = std::atoi(row[4].c_str());
    input.timestamp = std::atoll(row[5].c_str());
    input.content = row[6];
    const uint64_t id = static_cast<uint64_t>(std::atoll(row[0].c_str()));
    if (input.message_id.empty() || id == 0) {
      continue;
    }
    std::string index_err;
    if (!index_.IndexMessage(input, &index_err)) {
      if (err != nullptr) {
        *err = "index insert failed for " + input.message_id + ": " + index_err;
      }
      return false;
    }
    last_id_ = id;
    ++*ingested;
  }
  return true;
}

bool MessageIndexSync::Backfill(int64_t* indexed, std::string* err, int64_t batch) {
  *indexed = 0;
  // 起点游标：重启续传没有意义（索引文件可与库一起删掉重建），固定从 0
  // 全量扫；已入索引的行幂等覆写。
  last_id_ = 0;
  while (true) {
    std::string sql =
        "SELECT id, message_id, channel_id, channel_type, msg_type, timestamp,"
        " content FROM messages WHERE id > " + std::to_string(last_id_) +
        " AND is_recalled = 0 ORDER BY id ASC LIMIT " + std::to_string(batch);
    int64_t ingested = 0;
    if (!IngestBatch(sql, &ingested, err)) {
      return false;
    }
    *indexed += ingested;
    if (ingested < batch) {
      break;  // 吃完了
    }
  }
  indexed_total_ += *indexed;
  return true;
}

bool MessageIndexSync::PumpTail(std::string* err, int64_t batch) {
  std::string sql =
      "SELECT id, message_id, channel_id, channel_type, msg_type, timestamp,"
      " content FROM messages WHERE id > " + std::to_string(last_id_) +
      " AND is_recalled = 0 ORDER BY id ASC LIMIT " + std::to_string(batch);
  int64_t ingested = 0;
  if (!IngestBatch(sql, &ingested, err)) {
    return false;
  }
  indexed_total_ += ingested;
  return true;
}

bool MessageIndexSync::FetchMessageFacts(const std::vector<std::string>& message_ids,
                                         std::map<std::string, MessageFact>* facts,
                                         std::string* err) {
  facts->clear();
  if (message_ids.empty()) {
    return true;
  }
  std::unique_ptr<MySQLConnection> conn = pool_.GetConnection();
  if (conn == nullptr || !conn->IsConnected()) {
    if (err != nullptr) {
      *err = "mysql connection unavailable";
    }
    return false;
  }
  std::string id_list;
  for (size_t i = 0; i < message_ids.size(); ++i) {
    if (i > 0) {
      id_list += ",";
    }
    id_list += "'" + conn->Escape(message_ids[i]) + "'";
  }
  const std::string sql =
      "SELECT message_id, sender_id, sender_kind, receiver_id, is_recalled,"
      " content FROM messages WHERE message_id IN (" + id_list + ")";
  if (!conn->Query(sql)) {
    if (err != nullptr) {
      *err = "mysql facts query failed";
    }
    pool_.ReturnConnection(std::move(conn));
    return false;
  }
  const auto rows = conn->FetchResults();
  pool_.ReturnConnection(std::move(conn));
  for (const auto& row : rows) {
    if (row.size() < 6) {
      continue;
    }
    MessageFact fact;
    fact.sender_id = row[1];
    fact.sender_kind = std::atoi(row[2].c_str());
    fact.receiver_id = row[3];
    fact.recalled = row[4] == "1" || row[4] == "true";
    fact.content = row[5];
    (*facts)[row[0]] = std::move(fact);
  }
  return true;
}

}  // namespace search
}  // namespace chirp
