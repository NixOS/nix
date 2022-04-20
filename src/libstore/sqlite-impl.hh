#include "sqlite.hh"
#include "globals.hh"
#include "util.hh"

#include <sqlite3.h>

#include <atomic>

namespace nix {

template<typename... Args>
SQLiteError::SQLiteError(const char *path, int errNo, int extendedErrNo, const Args & ... args)
  : Error(""), path(path), errNo(errNo), extendedErrNo(extendedErrNo)
{
    auto hf = hintfmt(args...);
    err.msg = hintfmt("%s: %s (in '%s')",
        normaltxt(hf.str()),
        sqlite3_errstr(extendedErrNo),
        path ? path : "(in-memory)");
}

template<typename... Args>
[[noreturn]] void SQLiteError::throw_(sqlite3 * db, const std::string & fs, const Args & ... args)
{
    int err = sqlite3_errcode(db);
    int exterr = sqlite3_extended_errcode(db);

    auto path = sqlite3_db_filename(db, nullptr);

    if (err == SQLITE_BUSY || err == SQLITE_PROTOCOL) {
        auto exp = SQLiteBusy(path, err, exterr, fs, args...);
        exp.err.msg = hintfmt(
            err == SQLITE_PROTOCOL
                ? "SQLite database '%s' is busy (SQLITE_PROTOCOL)"
                : "SQLite database '%s' is busy",
            path ? path : "(in-memory)");
        throw exp;
    } else
        throw SQLiteError(path, err, exterr, fs, args...);
}

}
