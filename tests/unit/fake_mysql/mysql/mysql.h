// Minimal MySQL C API subset for unit tests.
//
// The production sources (user_store.cc / session_store.cc /
// mysql_message_store.cc) include <mysql/mysql.h> and call a handful of
// libmysqlclient functions. Shipping libmysqlclient into the unit-test
// environment is heavyweight, so tests compile against this drop-in header
// instead: every function is implemented by the scripted fake in
// fake_mysql.cc, whose responses (result rows, errors, affected-row counts)
// are programmed per test case.

#ifndef CHIRP_TEST_FAKE_MYSQL_MYSQL_H_
#define CHIRP_TEST_FAKE_MYSQL_MYSQL_H_

#include <cstddef>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct st_mysql MYSQL;
typedef struct st_mysql_res MYSQL_RES;
typedef char** MYSQL_ROW;
typedef unsigned long long my_ulonglong;

enum fake_mysql_option { MYSQL_OPT_RECONNECT = 0 };

#define CLIENT_MULTI_STATEMENTS 65536

MYSQL* mysql_init(MYSQL* mysql);
int mysql_options(MYSQL* mysql, enum fake_mysql_option option, const void* value);
MYSQL* mysql_real_connect(MYSQL* mysql,
                          const char* host,
                          const char* user,
                          const char* password,
                          const char* db,
                          unsigned int port,
                          const char* unix_socket,
                          unsigned long client_flag);
const char* mysql_error(MYSQL* mysql);
void mysql_close(MYSQL* mysql);
int mysql_ping(MYSQL* mysql);
int mysql_query(MYSQL* mysql, const char* query);
MYSQL_RES* mysql_store_result(MYSQL* mysql);
my_ulonglong mysql_num_rows(MYSQL_RES* res);
unsigned int mysql_num_fields(MYSQL_RES* res);
MYSQL_ROW mysql_fetch_row(MYSQL_RES* res);
void mysql_free_result(MYSQL_RES* res);
unsigned long mysql_real_escape_string(MYSQL* mysql,
                                       char* to,
                                       const char* from,
                                       unsigned long length);
my_ulonglong mysql_affected_rows(MYSQL* mysql);
my_ulonglong mysql_insert_id(MYSQL* mysql);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CHIRP_TEST_FAKE_MYSQL_MYSQL_H_
