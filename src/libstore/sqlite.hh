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
    sqlite3 * db = 0;
    SQLite() { }
    SQLite(const Path & path);
    SQLite(const SQLite & from) = delete;
    SQLite& operator = (const SQLite & from) = delete;
    SQLite& operator = (SQLite && from) { db = from.db; from.db = 0; return *this; }
    ~SQLite();
    operator sqlite3 * () { return db; }

    void exec(const std::string & stmt);
};

/* RAII wrapper to create and destroy SQLite prepared statements. */
struct SQLiteStmt
{
    sqlite3 * db = 0;
    sqlite3_stmt * stmt = 0;
    std::string sql;
    SQLiteStmt() { }
    SQLiteStmt(sqlite3 * db, const std::string & sql) { create(db, sql); }
    void create(sqlite3 * db, const std::string & s);
    ~SQLiteStmt();
    operator sqlite3_stmt * () { return stmt; }

    /* Helper for binding / executing statements. */
    class Use
    {
        friend struct SQLiteStmt;
    private:
        SQLiteStmt & stmt;
        unsigned int curArg = 1;
        Use(SQLiteStmt & stmt);

    public:

        ~Use();

        /* Bind the next parameter. */
        Use & operator () (const std::string & value, bool notNull = true);
        Use & operator () (int64_t value, bool notNull = true);
        Use & bind(); // null

        int step();

        /* Execute a statement that does not return rows. */
        void exec();

        /* For statements that return 0 or more rows. Returns true iff
           a row is available. */
        bool next();

        std::string getStr(int col);
        int64_t getInt(int col);
        bool isNull(int col);
    };

    Use use()
    {
        return Use(*this);
    }
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

[[noreturn]] void throwSQLiteError(sqlite3 * db, const FormatOrString & fs);

void handleSQLiteBusy(const SQLiteBusy & e);

/* Convenience function for retrying a SQLite transaction when the
   database is busy. */
template<typename T>
T retrySQLite(std::function<T()> fun)
{
    while (true) {
        try {
            return fun();
        } catch (SQLiteBusy & e) {
            handleSQLiteBusy(e);
        }
    }
}

}
