// Control API for the scripted fake MySQL C API. The implementation (and the
// fake C symbols themselves) live in fake_mysql.cc; tests include this header
// and link fake_mysql.cc exactly once per test target.

#ifndef CHIRP_TEST_FAKE_MYSQL_H_
#define CHIRP_TEST_FAKE_MYSQL_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace chirp_test {
namespace fake_mysql {

// Resets all scripted state (responses, errors, flags, recorded queries).
void Reset();

void SetConnectShouldFail(bool fail);
void SetInitShouldFail(bool fail);
void SetPingShouldFail(bool fail);
void SetStoreResultShouldFail(bool fail);

// Scripts the rows returned by the next mysql_store_result call. Cells set
// to std::nullopt come back as SQL NULLs.
void PushRows(std::vector<std::vector<std::optional<std::string>>> rows);

// Makes the next mysql_query call fail with the given error text.
void PushQueryError(const std::string& message);

// Makes every mysql_query whose SQL starts with the given prefix fail.
// Use this when the failing statement is not the very next query (e.g. an
// INSERT that follows existence-check SELECTs).
void FailQueriesMatching(const std::string& prefix);

void SetAffectedRows(uint64_t rows);
void SetInsertId(uint64_t id);

// Every query the fake has seen so far, in order (consumes the log).
std::vector<std::string> TakeQueries();

// Number of open fake connection handles (leak detector).
int LiveHandles();

}  // namespace fake_mysql
}  // namespace chirp_test

#endif  // CHIRP_TEST_FAKE_MYSQL_H_
