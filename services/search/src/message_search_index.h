#ifndef CHIRP_SERVICES_SEARCH_MESSAGE_SEARCH_INDEX_H_
#define CHIRP_SERVICES_SEARCH_MESSAGE_SEARCH_INDEX_H_

#include <cstdint>
#include <string>
#include <vector>

#include <sqlite3.h>

namespace chirp {
namespace search {

// SQLite FTS5 持久消息索引（message_search 批）：chirp_search 的检索结构。
// 只存检索与过滤所需的最小列集；应答里的发送者/正文等展示字段一律回
// MySQL 取（权威事实），索引列只是可过滤的镜像。
//
// 索引形态：FTS5 虚表（unicode61 tokenizer，content 列存 SegmentForIndex
// 变换后的文本）+ 一张 message_id -> fts rowid 的映射表。映射表负责幂等
// 覆写（重复 IndexMessage 同一消息先删旧行）与按 id 删除（FTS5 的
// UNINDEXED 列没有索引，直接按 id 删会全表扫）。
//
// 生命周期：由调用方（tail 泵/核对路径）保证「撤回消息不入索引」；本类
// 只做存储。线程模型：所有调用都发生在服务的单一 io 线程（tail 计时器与
// 查询路径同线程），连接按 SQLITE_OPEN_FULLMUTEX 打开兜底。
class MessageSearchIndex {
 public:
  // 一条待索引消息（与 messages 表行对应；content 为原文，入索引自分段）。
  struct Input {
    std::string message_id;
    std::string channel_id;
    int channel_type = 0;
    int msg_type = 0;
    int64_t timestamp = 0;
    std::string content;
  };

  // 一条检索命中（无展示字段，见类注）。
  struct Hit {
    std::string message_id;
    std::string channel_id;
    int channel_type = 0;
    int msg_type = 0;
    int64_t timestamp = 0;
  };

  struct Query {
    std::string keyword;             // 原文关键词；服务端 BuildMatchPhrase
    std::string channel_id;          // 空 = 不限频道
    std::vector<int> content_types;  // 空 = 不限类型
    int64_t before_timestamp = 0;    // 复合游标（0 = 首页）
    std::string before_message_id;
    int32_t limit = 20;              // 调用方已钳到 [1,50]
  };

  struct QueryResult {
    // 最多 limit+1 条（timestamp DESC, message_id DESC）。has_more 置位时
    // 第 limit+1 条是翻页边界样本：游标翻页的调用方以它推进游标，单次取页
    // 的调用方只取前 limit 条。
    std::vector<Hit> hits;
    bool has_more = false;
  };

  // 打开（不存在则建库建表）。失败返回 false 并置 *err。
  bool Open(const std::string& db_path, std::string* err);
  ~MessageSearchIndex();

  // 幂等覆写：同 message_id 先删旧行再插入。
  bool IndexMessage(const Input& msg, std::string* err);
  // 不存在时静默成功（核对自愈路径允许重复删除）。
  bool DeleteMessage(const std::string& message_id, std::string* err);

  QueryResult Search(const Query& q, std::string* err);

  // 已索引消息数（映射表行数；stats 日志与测试用）。
  int64_t DocumentCount();

 private:
  sqlite3* db_ = nullptr;
};

}  // namespace search
}  // namespace chirp

#endif  // CHIRP_SERVICES_SEARCH_MESSAGE_SEARCH_INDEX_H_
