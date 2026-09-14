// Scripted fake implementation of the MySQL C API subset declared in
// fake_mysql/mysql/mysql.h. Tests program responses through FakeMysql::*
// before driving the production code under test.

#include "mysql/mysql.h"

#include <algorithm>
#include <cstring>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "fake_mysql.h"

namespace chirp_test {
namespace fake_mysql {

// A scripted result: rows of columns; a column may be NULL (renders as a null
// MYSQL_ROW entry, exercising the production null guards).
struct ScriptedResult {
  std::vector<std::vector<std::optional<std::string>>> rows;
};

struct State {
  std::mutex mu;
  bool connect_should_fail = false;
  bool init_should_fail = false;
  int init_fail_after = -1;  // fail mysql_init once init_calls exceeds this
  int init_calls = 0;
  bool ping_should_fail = false;
  bool store_result_should_fail = false;
  std::deque<ScriptedResult> results;      // popped by mysql_store_result
  std::deque<std::string> query_errors;    // popped when non-empty on query
  std::vector<std::string> fail_prefixes;  // queries matching a prefix fail
  my_ulonglong affected_rows = 0;
  my_ulonglong insert_id = 0;
  std::string last_error;
  std::vector<std::string> queries;        // every query seen, in order
  int live_handles = 0;                    // open fake connections
};

inline State& state() {
  static State s;
  return s;
}

}  // namespace fake_mysql
}  // namespace chirp_test

// ---------------------------------------------------------------------------
// Control API (declared in fake_mysql.h)
// ---------------------------------------------------------------------------

namespace chirp_test {
namespace fake_mysql {

void Reset() {
  auto& s = state();
  std::lock_guard<std::mutex> lock(s.mu);
  s.connect_should_fail = false;
  s.init_should_fail = false;
  s.init_fail_after = -1;
  s.init_calls = 0;
  s.ping_should_fail = false;
  s.store_result_should_fail = false;
  s.results.clear();
  s.query_errors.clear();
  s.fail_prefixes.clear();
  s.affected_rows = 0;
  s.insert_id = 0;
  s.last_error.clear();
  s.queries.clear();
  s.live_handles = 0;
}

void SetConnectShouldFail(bool fail) { state().connect_should_fail = fail; }
void SetInitShouldFail(bool fail) { state().init_should_fail = fail; }
void SetInitShouldFailAfter(int n) { state().init_fail_after = n; }
void SetPingShouldFail(bool fail) { state().ping_should_fail = fail; }
void SetStoreResultShouldFail(bool fail) { state().store_result_should_fail = fail; }

void PushRows(std::vector<std::vector<std::optional<std::string>>> rows) {
  state().results.push_back(ScriptedResult{std::move(rows)});
}

void PushQueryError(const std::string& message) {
  state().query_errors.push_back(message);
}

void FailQueriesMatching(const std::string& prefix) {
  state().fail_prefixes.push_back(prefix);
}

void SetAffectedRows(uint64_t rows) { state().affected_rows = rows; }
void SetInsertId(uint64_t id) { state().insert_id = id; }

std::vector<std::string> TakeQueries() {
  auto& s = state();
  std::lock_guard<std::mutex> lock(s.mu);
  return std::move(s.queries);
}

int LiveHandles() {
  auto& s = state();
  std::lock_guard<std::mutex> lock(s.mu);
  return s.live_handles;
}

}  // namespace fake_mysql
}  // namespace chirp_test

// ---------------------------------------------------------------------------
// C API implementation
// ---------------------------------------------------------------------------

namespace {

using chirp_test::fake_mysql::state;

struct FakeMysql {
  int cookie = 0x5eed;
  char error[256] = {0};
};

struct FakeResult {
  // Storage for cell values plus the row pointer arrays MYSQL_ROW points at.
  std::vector<std::vector<std::optional<std::string>>> rows;
  std::vector<std::vector<char*>> row_ptrs;
  std::vector<MYSQL_ROW> row_views;
  size_t next_row = 0;
  unsigned int num_fields = 0;
};

}  // namespace

extern "C" {

MYSQL* mysql_init(MYSQL*) {
  auto& s = state();
  std::lock_guard<std::mutex> lock(s.mu);
  ++s.init_calls;
  if (s.init_should_fail ||
      (s.init_fail_after >= 0 && s.init_calls > s.init_fail_after)) {
    return nullptr;
  }
  ++s.live_handles;
  return reinterpret_cast<MYSQL*>(new FakeMysql());
}

int mysql_options(MYSQL*, enum fake_mysql_option, const void*) {
  return 0;
}

MYSQL* mysql_real_connect(MYSQL* mysql,
                          const char*,
                          const char*,
                          const char*,
                          const char*,
                          unsigned int,
                          const char*,
                          unsigned long) {
  auto& s = state();
  std::lock_guard<std::mutex> lock(s.mu);
  if (s.connect_should_fail) {
    std::snprintf(reinterpret_cast<FakeMysql*>(mysql)->error,
                  sizeof(reinterpret_cast<FakeMysql*>(mysql)->error),
                  "can't connect to MySQL server (fake)");
    return nullptr;
  }
  return mysql;
}

const char* mysql_error(MYSQL* mysql) {
  return reinterpret_cast<FakeMysql*>(mysql)->error;
}

void mysql_close(MYSQL* mysql) {
  if (mysql == nullptr) {
    return;
  }
  auto& s = state();
  std::lock_guard<std::mutex> lock(s.mu);
  --s.live_handles;
  delete reinterpret_cast<FakeMysql*>(mysql);
}

int mysql_ping(MYSQL*) {
  auto& s = state();
  std::lock_guard<std::mutex> lock(s.mu);
  return s.ping_should_fail ? 1 : 0;
}

int mysql_query(MYSQL* mysql, const char* query) {
  auto& s = state();
  std::string error;
  {
    std::lock_guard<std::mutex> lock(s.mu);
    s.queries.emplace_back(query);
    if (!s.query_errors.empty()) {
      error = std::move(s.query_errors.front());
      s.query_errors.pop_front();
    } else {
      for (const auto& prefix : s.fail_prefixes) {
        if (s.queries.back().rfind(prefix, 0) == 0) {
          error = "query failed (scripted): " + s.queries.back().substr(0, 32);
          break;
        }
      }
    }
  }
  if (!error.empty()) {
    std::snprintf(reinterpret_cast<FakeMysql*>(mysql)->error,
                  sizeof(reinterpret_cast<FakeMysql*>(mysql)->error), "%s",
                  error.c_str());
    return 1;
  }
  return 0;
}

MYSQL_RES* mysql_store_result(MYSQL*) {
  auto& s = state();
  std::lock_guard<std::mutex> lock(s.mu);
  if (s.store_result_should_fail) {
    return nullptr;
  }
  auto* res = new FakeResult();
  if (!s.results.empty()) {
    res->rows = std::move(s.results.front().rows);
    s.results.pop_front();
  }
  res->num_fields = 0;
  for (const auto& row : res->rows) {
    res->num_fields = std::max(res->num_fields, static_cast<unsigned int>(row.size()));
  }
  for (auto& row : res->rows) {
    std::vector<char*> ptrs;
    ptrs.reserve(res->num_fields);
    for (auto& cell : row) {
      // An absent optional keeps the null pointer so production code sees
      // exactly what a SQL NULL looks like.
      ptrs.push_back(cell.has_value() ? const_cast<char*>(cell->c_str()) : nullptr);
    }
    // Narrow rows are padded with NULLs so column reads stay in bounds even
    // when a test scripts fewer columns than production code reads (production
    // parsers expect up to 10 columns).
    constexpr unsigned int kMinColumnSlots = 16;
    ptrs.resize(std::max(res->num_fields, kMinColumnSlots), nullptr);
    res->row_views.push_back(ptrs.data());
    res->row_ptrs.push_back(std::move(ptrs));
  }
  return reinterpret_cast<MYSQL_RES*>(res);
}

my_ulonglong mysql_num_rows(MYSQL_RES* res) {
  return reinterpret_cast<FakeResult*>(res)->rows.size();
}

unsigned int mysql_num_fields(MYSQL_RES* res) {
  return reinterpret_cast<FakeResult*>(res)->num_fields;
}

MYSQL_ROW mysql_fetch_row(MYSQL_RES* res) {
  auto* r = reinterpret_cast<FakeResult*>(res);
  if (r->next_row >= r->row_views.size()) {
    return nullptr;
  }
  return r->row_views[r->next_row++];
}

void mysql_free_result(MYSQL_RES* res) {
  delete reinterpret_cast<FakeResult*>(res);
}

unsigned long mysql_real_escape_string(MYSQL*, char* to, const char* from, unsigned long length) {
  unsigned long out = 0;
  for (unsigned long i = 0; i < length; ++i) {
    char c = from[i];
    if (c == '\'' || c == '"' || c == '\\') {
      to[out++] = '\\';
    }
    to[out++] = c;
  }
  to[out] = '\0';
  return out;
}

my_ulonglong mysql_affected_rows(MYSQL*) {
  auto& s = state();
  std::lock_guard<std::mutex> lock(s.mu);
  return s.affected_rows;
}

my_ulonglong mysql_insert_id(MYSQL*) {
  auto& s = state();
  std::lock_guard<std::mutex> lock(s.mu);
  return s.insert_id;
}

}  // extern "C"
