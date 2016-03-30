#pragma once

#include <functional>
#include <string>

#include "types.hh"

class sqlite3;
class sqlite3_stmt;

namespace nix {

/* RAII wrapper to close a SQLite database automatically. */
struct SQLite
{
    sqlite3 * db;
    SQLite() { db = 0; }
    ~SQLite();
    operator sqlite3 * () { return db; }
};

/* RAII wrapper to create and destroy SQLite prepared statements. */
struct SQLiteStmt
{
    sqlite3 * db;
    sqlite3_stmt * stmt;
    unsigned int curArg;
    SQLiteStmt() { stmt = 0; }
    void create(sqlite3 * db, const std::string & s);
    void reset();
    ~SQLiteStmt();
    operator sqlite3_stmt * () { return stmt; }
    void bind(const std::string & value);
    void bind(int value);
    void bind64(long long value);
    void bind();
};

/* Helper class to ensure that prepared statements are reset when
   leaving the scope that uses them.  Unfinished prepared statements
   prevent transactions from being aborted, and can cause locks to be
   kept when they should be released. */
struct SQLiteStmtUse
{
    SQLiteStmt & stmt;
    SQLiteStmtUse(SQLiteStmt & stmt);
    ~SQLiteStmtUse();
};

/* RAII helper that ensures transactions are aborted unless explicitly
   committed. */
struct SQLiteTxn
{
    bool active = false;
    sqlite3 * db;

    SQLiteTxn(sqlite3 * db);

    void commit();

    ~SQLiteTxn();
};


MakeError(SQLiteError, Error);
MakeError(SQLiteBusy, SQLiteError);

[[noreturn]] void throwSQLiteError(sqlite3 * db, const format & f);

/* Convenience function for retrying a SQLite transaction when the
   database is busy. */
template<typename T>
T retrySQLite(std::function<T()> fun)
{
    while (true) {
        try {
            return fun();
        } catch (SQLiteBusy & e) {
        }
    }
}

}
