// Test corpus for nix-sqlite-non-literal-sql check.
// RUN: clang-tidy --checks='-*,nix-sqlite-non-literal-sql' %s --

// Minimal sqlite3 forward declarations for testing.
struct sqlite3;
typedef int (*sqlite3_callback)(void*, int, char**, char**);
extern int sqlite3_exec(sqlite3 *db, const char *sql,
                        sqlite3_callback callback, void *arg, char **errmsg);

sqlite3 *db;

// --- Positive cases: non-literal SQL (should warn) ---

void bad_variable_sql() {
  const char *sql = "SELECT 1"; // not a compile-time guarantee
  sqlite3_exec(db, sql, nullptr, nullptr, nullptr); // warn
}

void bad_concatenated(const char *table) {
  // Non-literal: runtime pointer arithmetic
  const char *query = table + 1;
  sqlite3_exec(db, query, nullptr, nullptr, nullptr); // warn
}

void bad_parameter(const char *user_sql) {
  sqlite3_exec(db, user_sql, nullptr, nullptr, nullptr); // warn
}

// --- Negative cases: literal/constexpr SQL (should NOT warn) ---

void good_string_literal() {
  sqlite3_exec(db, "CREATE TABLE t(id INTEGER)", nullptr, nullptr, nullptr); // ok
}

static const char *const SAFE_SQL = "VACUUM";
void good_static_const() {
  sqlite3_exec(db, SAFE_SQL, nullptr, nullptr, nullptr); // ok
}

constexpr const char *CX_SQL = "PRAGMA journal_mode=WAL";
void good_constexpr() {
  sqlite3_exec(db, CX_SQL, nullptr, nullptr, nullptr); // ok
}

namespace {
const char *const NS_SQL = "ANALYZE";
}
void good_namespace_const() {
  sqlite3_exec(db, NS_SQL, nullptr, nullptr, nullptr); // ok
}
