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
        checkInterrupt();
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

SQLite::SQLite(const Path & path)
{
    if (sqlite3_open_v2(path.c_str(), &db,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, 0) != SQLITE_OK)
        throw Error(format("cannot open SQLite database ‘%s’") % path);
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

void SQLite::exec(const std::string & stmt)
{
    if (sqlite3_exec(db, stmt.c_str(), 0, 0, 0) != SQLITE_OK)
        throwSQLiteError(db, format("executing SQLite statement ‘%s’") % stmt);
}

void SQLiteStmt::create(sqlite3 * db, const string & s)
{
    checkInterrupt();
    assert(!stmt);
    if (sqlite3_prepare_v2(db, s.c_str(), -1, &stmt, 0) != SQLITE_OK)
        throwSQLiteError(db, "creating statement");
    this->db = db;
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

SQLiteStmt::Use::Use(SQLiteStmt & stmt)
    : stmt(stmt)
{
    assert(stmt.stmt);
    /* Note: sqlite3_reset() returns the error code for the most
       recent call to sqlite3_step().  So ignore it. */
    sqlite3_reset(stmt);
}

SQLiteStmt::Use::~Use()
{
    sqlite3_reset(stmt);
}

SQLiteStmt::Use & SQLiteStmt::Use::operator () (const std::string & value, bool notNull)
{
    if (notNull) {
        if (sqlite3_bind_text(stmt, curArg++, value.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK)
            throwSQLiteError(stmt.db, "binding argument");
    } else
        bind();
    return *this;
}

SQLiteStmt::Use & SQLiteStmt::Use::operator () (int64_t value, bool notNull)
{
    if (notNull) {
        if (sqlite3_bind_int64(stmt, curArg++, value) != SQLITE_OK)
            throwSQLiteError(stmt.db, "binding argument");
    } else
        bind();
    return *this;
}

SQLiteStmt::Use & SQLiteStmt::Use::bind()
{
    if (sqlite3_bind_null(stmt, curArg++) != SQLITE_OK)
        throwSQLiteError(stmt.db, "binding argument");
    return *this;
}

int SQLiteStmt::Use::step()
{
    return sqlite3_step(stmt);
}

void SQLiteStmt::Use::exec()
{
    int r = step();
    assert(r != SQLITE_ROW);
    if (r != SQLITE_DONE)
        throwSQLiteError(stmt.db, "executing SQLite statement");
}

bool SQLiteStmt::Use::next()
{
    int r = step();
    if (r != SQLITE_DONE && r != SQLITE_ROW)
        throwSQLiteError(stmt.db, "executing SQLite query");
    return r == SQLITE_ROW;
}

std::string SQLiteStmt::Use::getStr(int col)
{
    auto s = (const char *) sqlite3_column_text(stmt, col);
    assert(s);
    return s;
}

int64_t SQLiteStmt::Use::getInt(int col)
{
    // FIXME: detect nulls?
    return sqlite3_column_int64(stmt, col);
}

bool SQLiteStmt::Use::isNull(int col)
{
    return sqlite3_column_type(stmt, col) == SQLITE_NULL;
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
