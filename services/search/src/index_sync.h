#ifndef CHIRP_SERVICES_SEARCH_INDEX_SYNC_H_
#define CHIRP_SERVICES_SEARCH_INDEX_SYNC_H_

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "message_search_index.h"

namespace chirp {
namespace chat {
class MySQLConnectionPool;
class MySQLConnection;
}  // namespace chat

namespace search {

// MySQL messages 表 -> FTS5 索引的摄取与核对（message_search 批）。
//
// - 启动全量回填（Backfill）：按 id 升序分批，is_recalled=0 才入索引；
// - tail 增量（PumpTail）：id 游标之上的新行，100ms 计时器驱动；
// - 查询核对（FetchMessageFacts）：按候选 message_id 批量回权威事实——
//   撤回位（命中即从结果剔除并顺手删索引，自愈）与发送者/正文等展示字段。
//
// 所有语句都走 MySQLConnection 的拼转义接口（与 mysql_message_store 同一
// 操纵面）；线程模型：单一 io 线程串行调用。
class MessageIndexSync {
 public:
  // 查询核对用的权威事实（messages 表行切片）。
  struct MessageFact {
    std::string sender_id;
    int sender_kind = 0;
    std::string receiver_id;
    bool recalled = false;
    std::string content;
  };

  MessageIndexSync(MessageSearchIndex& index, chat::MySQLConnectionPool& pool);

  // 全量回填；返回成功与否，*indexed 记实际入索引条数。
  bool Backfill(int64_t* indexed, std::string* err, int64_t batch = 500);

  // tail 泵一步：last_id_ 之上的一批新行入索引；返回成功与否。
  bool PumpTail(std::string* err, int64_t batch = 200);

  uint64_t last_id() const { return last_id_; }
  int64_t indexed_total() const { return indexed_total_; }

  // 按候选 id 批量取权威事实；不在表里的 id 不出现在 *facts。
  bool FetchMessageFacts(const std::vector<std::string>& message_ids,
                         std::map<std::string, MessageFact>* facts, std::string* err);

 private:
  // 共用的取行+入索引逻辑（回填与 tail 同一 SELECT 形态）。
  bool IngestBatch(const std::string& sql, int64_t* ingested, std::string* err);

  MessageSearchIndex& index_;
  chat::MySQLConnectionPool& pool_;
  uint64_t last_id_ = 0;
  int64_t indexed_total_ = 0;
};

}  // namespace search
}  // namespace chirp

#endif  // CHIRP_SERVICES_SEARCH_INDEX_SYNC_H_
