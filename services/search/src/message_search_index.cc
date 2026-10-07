#include "message_search_index.h"

#include "logger.h"
#include "text_segmenter.h"

namespace chirp {
namespace search {

namespace {

// 4 MiB 与其余内部面单帧上限一致；单条消息正文远小于该值，这里只是防止
// 病态数据撑爆语句内存。
constexpr int kMaxContentBytes = 4 * 1024 * 1024;

std::string SqlTextError(sqlite3* db, const char* what) {
  return std::string(what) + ": " + (sqlite3_errmsg(db) ? sqlite3_errmsg(db) : "unknown");
}

// 便捷 RAII 语句：析构 finalize，构造失败由调用方查 rc。
class Stmt {
 public:
  Stmt(sqlite3* db, const char* sql) {
    rc_ = sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr);
  }
  ~Stmt() {
    if (stmt_ != nullptr) {
      sqlite3_finalize(stmt_);
    }
  }
  Stmt(const Stmt&) = delete;
  Stmt& operator=(const Stmt&) = delete;

  bool ok() const { return rc_ == SQLITE_OK && stmt_ != nullptr; }
  int prepare_rc() const { return rc_; }
  sqlite3_stmt* get() const { return stmt_; }

  // step 到完成；SQLITE_DONE/ROW 都算执行成功（SELECT 由调用方自行取行）。
  bool Run(std::string* err) {
    const int rc = sqlite3_step(stmt_);
    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
      if (err != nullptr) {
        *err = SqlTextError(sqlite3_db_handle(stmt_), "sqlite step") +
               " (rc=" + std::to_string(rc) + ")";
      }
      return false;
    }
    return true;
  }

 private:
  int rc_ = SQLITE_OK;
  sqlite3_stmt* stmt_ = nullptr;
};

void BindText(sqlite3_stmt* stmt, int idx, const std::string& value) {
  sqlite3_bind_text(stmt, idx, value.data(), static_cast<int>(value.size()),
                    SQLITE_TRANSIENT);
}

}  // namespace

bool MessageSearchIndex::Open(const std::string& db_path, std::string* err) {
  if (db_ != nullptr) {
    if (err != nullptr) {
      *err = "index already open";
    }
    return false;
  }
  const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
  if (sqlite3_open_v2(db_path.c_str(), &db_, flags, nullptr) != SQLITE_OK) {
    if (err != nullptr) {
      *err = SqlTextError(db_, "sqlite open");
    }
    if (db_ != nullptr) {
      sqlite3_close(db_);
      db_ = nullptr;
    }
    return false;
  }
  // WAL + NORMAL 同步：单写者场景下足够，崩溃只丢最近一次检查点之后的
  // 索引写入——索引可由 MySQL 全量重建，不值得为它加 fsync 开销。
  Stmt wal(db_, "PRAGMA journal_mode=WAL");
  if (wal.ok()) {
    (void)wal.Run(nullptr);
  }
  Stmt sync(db_, "PRAGMA synchronous=NORMAL");
  if (sync.ok()) {
    (void)sync.Run(nullptr);
  }
  // 两条建表语句分开 prepare：sqlite3_prepare_v2 只编译首条语句，拼在一起
  // 的尾段（FTS5 虚表）永远不会执行。
  const char* kSchemaMap =
      "CREATE TABLE IF NOT EXISTS message_map("
      "  message_id TEXT PRIMARY KEY,"
      "  fts_row INTEGER NOT NULL);";
  const char* kSchemaFts =
      "CREATE VIRTUAL TABLE IF NOT EXISTS message_fts USING fts5("
      "  content,"
      "  message_id UNINDEXED, channel_id UNINDEXED, channel_type UNINDEXED,"
      "  msg_type UNINDEXED, timestamp UNINDEXED,"
      "  tokenize='unicode61');";
  for (const char* sql : {kSchemaMap, kSchemaFts}) {
    Stmt schema(db_, sql);
    if (!schema.ok()) {
      if (err != nullptr) {
        *err = SqlTextError(db_, "sqlite prepare schema") +
               " (rc=" + std::to_string(schema.prepare_rc()) + ")";
      }
      sqlite3_close(db_);
      db_ = nullptr;
      return false;
    }
    if (!schema.Run(err)) {
      sqlite3_close(db_);
      db_ = nullptr;
      return false;
    }
  }
  return true;
}

MessageSearchIndex::~MessageSearchIndex() {
  if (db_ != nullptr) {
    sqlite3_close(db_);
  }
}

bool MessageSearchIndex::IndexMessage(const Input& msg, std::string* err) {
  if (db_ == nullptr) {
    if (err != nullptr) {
      *err = "index not open";
    }
    return false;
  }
  if (msg.content.size() > static_cast<size_t>(kMaxContentBytes)) {
    // 超限正文不入索引（历史里也发不出来，chat_validation 有更小的长度帽）；
    // 视为成功，避免一条病态行卡死 tail 泵。
    return true;
  }
  // 事务内：删旧行（幂等覆写）-> 插 FTS 行 -> 记 rowid 映射。
  Stmt begin(db_, "BEGIN");
  if (!begin.Run(err)) {
    return false;
  }
  {
    Stmt del_fts(db_,
                 "DELETE FROM message_fts WHERE rowid="
                 "(SELECT fts_row FROM message_map WHERE message_id=?1)");
    Stmt del_map(db_, "DELETE FROM message_map WHERE message_id=?1");
    if (!del_fts.ok() || !del_map.ok()) {
      if (err != nullptr) {
        *err = SqlTextError(db_, "sqlite prepare delete") +
               " (rc=" + std::to_string(del_fts.prepare_rc()) + "/" +
               std::to_string(del_map.prepare_rc()) + ")";
      }
      (void)Stmt(db_, "ROLLBACK").Run(nullptr);
      return false;
    }
    BindText(del_fts.get(), 1, msg.message_id);
    BindText(del_map.get(), 1, msg.message_id);
    if (!del_fts.Run(err) || !del_map.Run(err)) {
      (void)Stmt(db_, "ROLLBACK").Run(nullptr);
      return false;
    }
  }
  {
    // content 列存分段后的文本（unicode61 之下「每字一 token」），其余列
    // 是纯存储的过滤镜像。
    const std::string segmented = SegmentForIndex(msg.content);
    Stmt ins_fts(db_,
                 "INSERT INTO message_fts(content, message_id, channel_id,"
                 " channel_type, msg_type, timestamp) VALUES(?1,?2,?3,?4,?5,?6)");
    if (!ins_fts.ok()) {
      if (err != nullptr) {
        *err = SqlTextError(db_, "sqlite prepare insert fts");
      }
      (void)Stmt(db_, "ROLLBACK").Run(nullptr);
      return false;
    }
    BindText(ins_fts.get(), 1, segmented);
    BindText(ins_fts.get(), 2, msg.message_id);
    BindText(ins_fts.get(), 3, msg.channel_id);
    sqlite3_bind_int(ins_fts.get(), 4, msg.channel_type);
    sqlite3_bind_int(ins_fts.get(), 5, msg.msg_type);
    sqlite3_bind_int64(ins_fts.get(), 6, msg.timestamp);
    if (!ins_fts.Run(err)) {
      (void)Stmt(db_, "ROLLBACK").Run(nullptr);
      return false;
    }
    Stmt ins_map(db_,
                 "INSERT INTO message_map(message_id, fts_row)"
                 " VALUES(?1, last_insert_rowid())");
    if (!ins_map.ok()) {
      if (err != nullptr) {
        *err = SqlTextError(db_, "sqlite prepare insert map");
      }
      (void)Stmt(db_, "ROLLBACK").Run(nullptr);
      return false;
    }
    BindText(ins_map.get(), 1, msg.message_id);
    if (!ins_map.Run(err)) {
      (void)Stmt(db_, "ROLLBACK").Run(nullptr);
      return false;
    }
  }
  Stmt commit(db_, "COMMIT");
  if (!commit.Run(err)) {
    (void)Stmt(db_, "ROLLBACK").Run(nullptr);
    return false;
  }
  return true;
}

bool MessageSearchIndex::DeleteMessage(const std::string& message_id, std::string* err) {
  if (db_ == nullptr) {
    if (err != nullptr) {
      *err = "index not open";
    }
    return false;
  }
  Stmt del_fts(db_,
               "DELETE FROM message_fts WHERE rowid="
               "(SELECT fts_row FROM message_map WHERE message_id=?1)");
  Stmt del_map(db_, "DELETE FROM message_map WHERE message_id=?1");
  if (!del_fts.ok() || !del_map.ok()) {
    if (err != nullptr) {
      *err = SqlTextError(db_, "sqlite prepare delete");
    }
    return false;
  }
  BindText(del_fts.get(), 1, message_id);
  BindText(del_map.get(), 1, message_id);
  // 子查询不命中时 DELETE 影响 0 行，按成功处理（自愈路径的重复删除）。
  return del_fts.Run(err) && del_map.Run(err);
}

MessageSearchIndex::QueryResult MessageSearchIndex::Search(const Query& q,
                                                           std::string* err) {
  QueryResult result;
  if (db_ == nullptr) {
    if (err != nullptr) {
      *err = "index not open";
    }
    return result;
  }
  const std::string phrase = BuildMatchPhrase(q.keyword);
  if (phrase.empty()) {
    if (err != nullptr) {
      *err = "empty keyword";
    }
    return result;
  }

  // 可选子句按出现顺序占参数槽（MATCH=1 恒在），绑定段保持同一顺序。
  // 基础句拆成单行语句拼接（多字面量同行拼接会被 gcov 归并出伪 0 计数行）。
  std::string sql = "SELECT message_id, channel_id, channel_type,";
  sql += " msg_type, timestamp";
  sql += " FROM message_fts WHERE message_fts MATCH ?";
  const bool scoped = !q.channel_id.empty();
  if (scoped) {
    sql += " AND channel_id = ?";
  }
  std::vector<int> content_types(q.content_types);
  bool typed = false;
  if (!content_types.empty()) {
    typed = true;
    sql += " AND msg_type IN (";
    for (size_t i = 0; i < content_types.size(); ++i) {
      sql += i == 0 ? "?" : ",?";
    }
    sql += ")";
  }
  const bool cursor = !q.before_message_id.empty() || q.before_timestamp != 0;
  if (cursor) {
    sql += " AND (timestamp < ? OR (timestamp = ? AND message_id < ?))";
  }
  sql += " ORDER BY timestamp DESC, message_id DESC LIMIT ";
  // 多捞一条判定 has_more；多出的那条不进结果。字面量拼接（非参数），
  // 避免 "?" 撞上后续数字变成显式参数号。
  sql += std::to_string(q.limit + 1);

  Stmt stmt(db_, sql.c_str());
  if (!stmt.ok()) {
    if (err != nullptr) {
      *err = SqlTextError(db_, "sqlite prepare search");
    }
    return result;
  }
  int idx = 1;
  sqlite3_bind_text(stmt.get(), idx++, phrase.data(), static_cast<int>(phrase.size()),
                    SQLITE_TRANSIENT);
  if (scoped) {
    BindText(stmt.get(), idx++, q.channel_id);
  }
  if (typed) {
    for (int type : content_types) {
      sqlite3_bind_int(stmt.get(), idx++, type);
    }
  }
  if (cursor) {
    sqlite3_bind_int64(stmt.get(), idx++, q.before_timestamp);
    sqlite3_bind_int64(stmt.get(), idx++, q.before_timestamp);
    BindText(stmt.get(), idx++, q.before_message_id);
  }

  while (true) {
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_DONE) {
      break;
    }
    if (rc != SQLITE_ROW) {
      if (err != nullptr) {
        *err = SqlTextError(db_, "sqlite search step");
      }
      result.hits.clear();
      result.has_more = false;
      return result;
    }
    Hit hit;
    hit.message_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 0));
    hit.channel_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 1));
    hit.channel_type = sqlite3_column_int(stmt.get(), 2);
    hit.msg_type = sqlite3_column_int(stmt.get(), 3);
    hit.timestamp = sqlite3_column_int64(stmt.get(), 4);
    result.hits.push_back(std::move(hit));
  }
  // has_more 置位时不裁剪：hits 可含第 limit+1 条（边界样本）。游标翻页的
  // 调用方用它推进 (timestamp, message_id)——若在这里截掉，服务端的内部翻页
  // 会以第 limit 条为游标，下一页与本页重叠、重复候选还会触发错误的陈旧自
  // 愈删除。单次取页的调用方自行只取前 limit 条。
  if (static_cast<int32_t>(result.hits.size()) > q.limit) {
    result.has_more = true;
  }
  return result;
}

int64_t MessageSearchIndex::DocumentCount() {
  if (db_ == nullptr) {
    return 0;
  }
  Stmt stmt(db_, "SELECT count(*) FROM message_map");
  if (!stmt.ok() || !stmt.Run(nullptr)) {
    return 0;
  }
  return sqlite3_column_int64(stmt.get(), 0);
}

}  // namespace search
}  // namespace chirp
