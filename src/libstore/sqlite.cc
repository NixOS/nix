#include "sqlite.hh"
#include "globals.hh"
#include "util.hh"

#include <sqlite3.h>

#include <atomic>

namespace nix {

[[noreturn]] void throwSQLiteError(sqlite3 * db, const FormatOrString & fs)
{
    int err = sqlite3_errcode(db);
    int exterr = sqlite3_extended_errcode(db);

    auto path = sqlite3_db_filename(db, nullptr);
    if (!path) path = "(in-memory)";

    if (err == SQLITE_BUSY || err == SQLITE_PROTOCOL) {
        throw SQLiteBusy(
            err == SQLITE_PROTOCOL
            ? fmt("SQLite database '%s' is busy (SQLITE_PROTOCOL)", path)
            : fmt("SQLite database '%s' is busy", path));
    }
    else
        throw SQLiteError("%s: %s (in '%s')", fs.s, sqlite3_errstr(exterr), path);
}

SQLite::SQLite(const Path & path, bool create)
{
    // useSQLiteWAL also indicates what virtual file system we need.  Using
    // `unix-dotfile` is needed on NFS file systems and on Windows' Subsystem
    // for Linux (WSL) where useSQLiteWAL should be false by default.
    const char *vfs = settings.useSQLiteWAL ? 0 : "unix-dotfile";
    if (sqlite3_open_v2(path.c_str(), &db,
            SQLITE_OPEN_READWRITE | (create ? SQLITE_OPEN_CREATE : 0), vfs) != SQLITE_OK)
        throw Error("cannot open SQLite database '%s'", path);

    if (sqlite3_busy_timeout(db, 60 * 60 * 1000) != SQLITE_OK)
        throwSQLiteError(db, "setting timeout");

    exec("pragma foreign_keys = 1");
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

void SQLite::isCache()
{
    exec("pragma synchronous = off");
    exec("pragma main.journal_mode = truncate");
}

void SQLite::exec(const std::string & stmt)
{
    retrySQLite<void>([&]() {
        if (sqlite3_exec(db, stmt.c_str(), 0, 0, 0) != SQLITE_OK)
            throwSQLiteError(db, format("executing SQLite statement '%s'") % stmt);
    });
}

uint64_t SQLite::getLastInsertedRowId()
{
    return sqlite3_last_insert_rowid(db);
}

void SQLiteStmt::create(sqlite3 * db, const string & sql)
{
    checkInterrupt();
    assert(!stmt);
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK)
        throwSQLiteError(db, fmt("creating statement '%s'", sql));
    this->db = db;
    this->sql = sql;
}

SQLiteStmt::~SQLiteStmt()
{
    try {
        if (stmt && sqlite3_finalize(stmt) != SQLITE_OK)
            throwSQLiteError(db, fmt("finalizing statement '%s'", sql));
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

SQLiteStmt::Use & SQLiteStmt::Use::operator () (std::string_view value, bool notNull)
{
    if (notNull) {
        if (sqlite3_bind_text(stmt, curArg++, value.data(), -1, SQLITE_TRANSIENT) != SQLITE_OK)
            throwSQLiteError(stmt.db, "binding argument");
    } else
        bind();
    return *this;
}

SQLiteStmt::Use & SQLiteStmt::Use::operator () (const unsigned char * data, size_t len, bool notNull)
{
    if (notNull) {
        if (sqlite3_bind_blob(stmt, curArg++, data, len, SQLITE_TRANSIENT) != SQLITE_OK)
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
        throwSQLiteError(stmt.db, fmt("executing SQLite statement '%s'", sqlite3_expanded_sql(stmt.stmt)));
}

bool SQLiteStmt::Use::next()
{
    int r = step();
    if (r != SQLITE_DONE && r != SQLITE_ROW)
        throwSQLiteError(stmt.db, fmt("executing SQLite query '%s'", sqlite3_expanded_sql(stmt.stmt)));
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

void handleSQLiteBusy(const SQLiteBusy & e)
{
    static std::atomic<time_t> lastWarned{0};

    time_t now = time(0);

    if (now > lastWarned + 10) {
        lastWarned = now;
        logWarning({
            .name = "Sqlite busy",
            .msg = hintfmt(e.what())
        });
    }

    /* Sleep for a while since retrying the transaction right away
       is likely to fail again. */
    checkInterrupt();
    struct timespec t;
    t.tv_sec = 0;
    t.tv_nsec = (random() % 100) * 1000 * 1000; /* <= 0.1s */
    nanosleep(&t, 0);
}

}
