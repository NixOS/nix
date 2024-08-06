#include "sqlite.hh"
#include "globals.hh"
#include "util.hh"
#include "url.hh"
#include "signals.hh"

#include <sqlite3.h>

#include <atomic>
#include <thread>

namespace nix {

SQLiteError::SQLiteError(const char *path, const char *errMsg, int errNo, int extendedErrNo, int offset, HintFmt && hf)
  : Error(""), path(path), errMsg(errMsg), errNo(errNo), extendedErrNo(extendedErrNo), offset(offset)
{
    auto offsetStr = (offset == -1) ? "" : "at offset " + std::to_string(offset) + ": ";
    err.msg = HintFmt("%s: %s%s, %s (in '%s')",
        Uncolored(hf.str()),
        offsetStr,
        sqlite3_errstr(extendedErrNo),
        errMsg,
        path ? path : "(in-memory)");
}

[[noreturn]] void SQLiteError::throw_(sqlite3 * db, HintFmt && hf)
{
    int err = sqlite3_errcode(db);
    int exterr = sqlite3_extended_errcode(db);
    int offset = sqlite3_error_offset(db);

    auto path = sqlite3_db_filename(db, nullptr);
    auto errMsg = sqlite3_errmsg(db);

    if (err == SQLITE_BUSY || err == SQLITE_PROTOCOL) {
        auto exp = SQLiteBusy(path, errMsg, err, exterr, offset, std::move(hf));
        exp.err.msg = HintFmt(
            err == SQLITE_PROTOCOL
                ? "SQLite database '%s' is busy (SQLITE_PROTOCOL)"
                : "SQLite database '%s' is busy",
            path ? path : "(in-memory)");
        throw exp;
    } else
        throw SQLiteError(path, errMsg, err, exterr, offset, std::move(hf));
}

static void traceSQL(void * x, const char * sql)
{
    // wacky delimiters:
    //   so that we're quite unambiguous without escaping anything
    // notice instead of trace:
    //   so that this can be enabled without getting the firehose in our face.
    notice("SQL<[%1%]>", sql);
};

SQLite::SQLite(const Path & path, SQLiteOpenMode mode)
{
    // useSQLiteWAL also indicates what virtual file system we need.  Using
    // `unix-dotfile` is needed on NFS file systems and on Windows' Subsystem
    // for Linux (WSL) where useSQLiteWAL should be false by default.
    const char *vfs = settings.useSQLiteWAL ? 0 : "unix-dotfile";
    bool immutable = mode == SQLiteOpenMode::Immutable;
    int flags = immutable ? SQLITE_OPEN_READONLY : SQLITE_OPEN_READWRITE;
    if (mode == SQLiteOpenMode::Normal) flags |= SQLITE_OPEN_CREATE;
    auto uri = "file:" + percentEncode(path) + "?immutable=" + (immutable ? "1" : "0");
    int ret = sqlite3_open_v2(uri.c_str(), &db, SQLITE_OPEN_URI | flags, vfs);
    if (ret != SQLITE_OK) {
        const char * err = sqlite3_errstr(ret);
        throw Error("cannot open SQLite database '%s': %s", path, err);
    }

    if (sqlite3_busy_timeout(db, 60 * 60 * 1000) != SQLITE_OK)
        SQLiteError::throw_(db, "setting timeout");

    if (getEnv("NIX_DEBUG_SQLITE_TRACES") == "1") {
        // To debug sqlite statements; trace all of them
        sqlite3_trace(db, &traceSQL, nullptr);
    }

    exec("pragma foreign_keys = 1");
}

SQLite::~SQLite()
{
    try {
        if (db && sqlite3_close(db) != SQLITE_OK)
            SQLiteError::throw_(db, "closing database");
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
            SQLiteError::throw_(db, "executing SQLite statement '%s'", stmt);
    });
}

uint64_t SQLite::getLastInsertedRowId()
{
    return sqlite3_last_insert_rowid(db);
}

void SQLiteStmt::create(sqlite3 * db, const std::string & sql)
{
    checkInterrupt();
    assert(!stmt);
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK)
        SQLiteError::throw_(db, "creating statement '%s'", sql);
    this->db = db;
    this->sql = sql;
}

SQLiteStmt::~SQLiteStmt()
{
    try {
        if (stmt && sqlite3_finalize(stmt) != SQLITE_OK)
            SQLiteError::throw_(db, "finalizing statement '%s'", sql);
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
            SQLiteError::throw_(stmt.db, "binding argument");
    } else
        bind();
    return *this;
}

SQLiteStmt::Use & SQLiteStmt::Use::operator () (const unsigned char * data, size_t len, bool notNull)
{
    if (notNull) {
        if (sqlite3_bind_blob(stmt, curArg++, data, len, SQLITE_TRANSIENT) != SQLITE_OK)
            SQLiteError::throw_(stmt.db, "binding argument");
    } else
        bind();
    return *this;
}

SQLiteStmt::Use & SQLiteStmt::Use::operator () (int64_t value, bool notNull)
{
    if (notNull) {
        if (sqlite3_bind_int64(stmt, curArg++, value) != SQLITE_OK)
            SQLiteError::throw_(stmt.db, "binding argument");
    } else
        bind();
    return *this;
}

SQLiteStmt::Use & SQLiteStmt::Use::bind()
{
    if (sqlite3_bind_null(stmt, curArg++) != SQLITE_OK)
        SQLiteError::throw_(stmt.db, "binding argument");
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
        SQLiteError::throw_(stmt.db, fmt("executing SQLite statement '%s'", sqlite3_expanded_sql(stmt.stmt)));
}

bool SQLiteStmt::Use::next()
{
    int r = step();
    if (r != SQLITE_DONE && r != SQLITE_ROW)
        SQLiteError::throw_(stmt.db, fmt("executing SQLite query '%s'", sqlite3_expanded_sql(stmt.stmt)));
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
        SQLiteError::throw_(db, "starting transaction");
    active = true;
}

void SQLiteTxn::commit()
{
    if (sqlite3_exec(db, "commit;", 0, 0, 0) != SQLITE_OK)
        SQLiteError::throw_(db, "committing transaction");
    active = false;
}

SQLiteTxn::~SQLiteTxn()
{
    try {
        if (active && sqlite3_exec(db, "rollback;", 0, 0, 0) != SQLITE_OK)
            SQLiteError::throw_(db, "aborting transaction");
    } catch (...) {
        ignoreException();
    }
}

void handleSQLiteBusy(const SQLiteBusy & e, time_t & nextWarning)
{
    time_t now = time(0);
    if (now > nextWarning) {
        nextWarning = now + 10;
        logWarning({
            .msg = HintFmt(e.what())
        });
    }

    /* Sleep for a while since retrying the transaction right away
       is likely to fail again. */
    checkInterrupt();
    /* <= 0.1s */
    std::this_thread::sleep_for(std::chrono::milliseconds { rand() % 100 });
}

}
