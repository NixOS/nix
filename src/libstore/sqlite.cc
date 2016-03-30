#include "sqlite.hh"
#include "util.hh"

#include <sqlite3.h>

namespace nix {

[[noreturn]] void throwSQLiteError(sqlite3 * db, const format & f)
{
    int err = sqlite3_errcode(db);
    if (err == SQLITE_BUSY || err == SQLITE_PROTOCOL) {
        if (err == SQLITE_PROTOCOL)
            printMsg(lvlError, "warning: SQLite database is busy (SQLITE_PROTOCOL)");
        else {
            static bool warned = false;
            if (!warned) {
                printMsg(lvlError, "warning: SQLite database is busy");
                warned = true;
            }
        }
        /* Sleep for a while since retrying the transaction right away
           is likely to fail again. */
#if HAVE_NANOSLEEP
        struct timespec t;
        t.tv_sec = 0;
        t.tv_nsec = (random() % 100) * 1000 * 1000; /* <= 0.1s */
        nanosleep(&t, 0);
#else
        sleep(1);
#endif
        throw SQLiteBusy(format("%1%: %2%") % f.str() % sqlite3_errmsg(db));
    }
    else
        throw SQLiteError(format("%1%: %2%") % f.str() % sqlite3_errmsg(db));
}

SQLite::~SQLite()
{
    try {
        if (db && sqlite3_close(db) != SQLITE_OK)
            throwSQLiteError(db, "closing database");
    } catch (...) {
        ignoreException();
    }
}

void SQLiteStmt::create(sqlite3 * db, const string & s)
{
    checkInterrupt();
    assert(!stmt);
    if (sqlite3_prepare_v2(db, s.c_str(), -1, &stmt, 0) != SQLITE_OK)
        throwSQLiteError(db, "creating statement");
    this->db = db;
}

void SQLiteStmt::reset()
{
    assert(stmt);
    /* Note: sqlite3_reset() returns the error code for the most
       recent call to sqlite3_step().  So ignore it. */
    sqlite3_reset(stmt);
    curArg = 1;
}

SQLiteStmt::~SQLiteStmt()
{
    try {
        if (stmt && sqlite3_finalize(stmt) != SQLITE_OK)
            throwSQLiteError(db, "finalizing statement");
    } catch (...) {
        ignoreException();
    }
}

void SQLiteStmt::bind(const string & value)
{
    if (sqlite3_bind_text(stmt, curArg++, value.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK)
        throwSQLiteError(db, "binding argument");
}

void SQLiteStmt::bind(int value)
{
    if (sqlite3_bind_int(stmt, curArg++, value) != SQLITE_OK)
        throwSQLiteError(db, "binding argument");
}

void SQLiteStmt::bind64(long long value)
{
    if (sqlite3_bind_int64(stmt, curArg++, value) != SQLITE_OK)
        throwSQLiteError(db, "binding argument");
}

void SQLiteStmt::bind()
{
    if (sqlite3_bind_null(stmt, curArg++) != SQLITE_OK)
        throwSQLiteError(db, "binding argument");
}

SQLiteStmtUse::SQLiteStmtUse(SQLiteStmt & stmt)
    : stmt(stmt)
{
    stmt.reset();
}

SQLiteStmtUse::~SQLiteStmtUse()
{
    try {
        stmt.reset();
    } catch (...) {
        ignoreException();
    }
}

SQLiteTxn::SQLiteTxn(sqlite3 * db)
{
    this->db = db;
    if (sqlite3_exec(db, "begin;", 0, 0, 0) != SQLITE_OK)
        throwSQLiteError(db, "starting transaction");
    active = true;
}

void SQLiteTxn::commit()
{
    if (sqlite3_exec(db, "commit;", 0, 0, 0) != SQLITE_OK)
        throwSQLiteError(db, "committing transaction");
    active = false;
}

SQLiteTxn::~SQLiteTxn()
{
    try {
        if (active && sqlite3_exec(db, "rollback;", 0, 0, 0) != SQLITE_OK)
            throwSQLiteError(db, "aborting transaction");
    } catch (...) {
        ignoreException();
    }
}

}
