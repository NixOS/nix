#include "db.hh"
#include "util.hh"

#include <memory>


/* Wrapper class to ensure proper destruction. */
class DestroyDb
{
    Db * db;
public:
    DestroyDb(Db * _db) : db(_db) { }
    ~DestroyDb() { db->close(0); delete db; }
};


class DestroyDbc 
{
    Dbc * dbc;
public:
    DestroyDbc(Dbc * _dbc) : dbc(_dbc) { }
    ~DestroyDbc() { dbc->close(); /* close() frees dbc */ }
};


static void rethrow(DbException & e)
{
    throw Error(e.what());
}


Transaction::Transaction()
    : txn(0)
{
}


Transaction::Transaction(Database & db)
{
    db.requireEnv();
    db.env->txn_begin(0, &txn, 0);
}


Transaction::~Transaction()
{
    if (txn) {
        txn->abort();
        txn = 0;
    }
}


void Transaction::commit()
{
    if (!txn) throw Error("commit called on null transaction");
    txn->commit(0);
    txn = 0;
}


void Database::requireEnv()
{
    if (!env) throw Error("database environment not open");
}


Db * Database::openDB(const Transaction & txn,
    const string & table, bool create)
{
    requireEnv();

    Db * db = new Db(env, 0);

    try {
        // !!! fixme when switching to BDB 4.1: use txn.
        db->open(table.c_str(), 0, 
            DB_HASH, create ? DB_CREATE : 0, 0666);
    } catch (...) {
        delete db;
        throw;
    }

    return db;
}


Database::Database()
    : env(0)
{
}


Database::~Database()
{
    if (env) {
        env->close(0);
        delete env;
    }
}


void Database::open(const string & path)
{
    try {
        
        if (env) throw Error(format("environment already open"));

        env = new DbEnv(0);

        debug("foo" + path);
        env->open(path.c_str(), 
            DB_INIT_LOCK | DB_INIT_LOG | DB_INIT_MPOOL | DB_INIT_TXN |
            DB_CREATE,
            0666);
        
    } catch (DbException e) { rethrow(e); }
}


void Database::createTable(const string & table)
{
    try {
        Db * db = openDB(noTxn, table, true);
        DestroyDb destroyDb(db);
    } catch (DbException e) { rethrow(e); }
}


bool Database::queryString(const Transaction & txn, const string & table, 
    const string & key, string & data)
{
    try {

        Db * db = openDB(txn, table, false);
        DestroyDb destroyDb(db);

        Dbt kt((void *) key.c_str(), key.length());
        Dbt dt;

        int err = db->get(txn.txn, &kt, &dt, 0);
        if (err) return false;

        if (!dt.get_data())
            data = "";
        else
            data = string((char *) dt.get_data(), dt.get_size());
    
    } catch (DbException e) { rethrow(e); }

    return true;
}


bool Database::queryStrings(const Transaction & txn, const string & table, 
    const string & key, Strings & data)
{
    string d;

    if (!queryString(txn, table, key, d))
        return false;

    string::iterator it = d.begin();
    
    while (it != d.end()) {

        if (it + 4 > d.end())
            throw Error(format("short db entry: `%1%'") % d);
        
        unsigned int len;
        len = (unsigned char) *it++;
        len |= ((unsigned char) *it++) << 8;
        len |= ((unsigned char) *it++) << 16;
        len |= ((unsigned char) *it++) << 24;
        
        if (it + len > d.end())
            throw Error(format("short db entry: `%1%'") % d);

        string s;
        while (len--) s += *it++;

        data.push_back(s);
    }

    return true;
}


void Database::setString(const Transaction & txn, const string & table,
    const string & key, const string & data)
{
    try {
        Db * db = openDB(txn, table, false);
        DestroyDb destroyDb(db);

        Dbt kt((void *) key.c_str(), key.length());
        Dbt dt((void *) data.c_str(), data.length());
        db->put(txn.txn, &kt, &dt, 0);
    } catch (DbException e) { rethrow(e); }
}


void Database::setStrings(const Transaction & txn, const string & table,
    const string & key, const Strings & data)
{
    string d;
    
    for (Strings::const_iterator it = data.begin();
         it != data.end(); it++)
    {
        string s = *it;
        unsigned int len = s.size();

        d += len & 0xff;
        d += (len >> 8) & 0xff;
        d += (len >> 16) & 0xff;
        d += (len >> 24) & 0xff;
        
        d += s;
    }

    setString(txn, table, key, d);
}


void Database::delPair(const Transaction & txn, const string & table,
    const string & key)
{
    try {
        Db * db = openDB(txn, table, false);
        DestroyDb destroyDb(db);
        Dbt kt((void *) key.c_str(), key.length());
        db->del(txn.txn, &kt, 0);
    } catch (DbException e) { rethrow(e); }
}


void Database::enumTable(const Transaction & txn, const string & table,
    Strings & keys)
{
    try {

        Db * db = openDB(txn, table, false);
        DestroyDb destroyDb(db);

        Dbc * dbc;
        db->cursor(0, &dbc, 0);
        DestroyDbc destroyDbc(dbc);

        Dbt kt, dt;
        while (dbc->get(&kt, &dt, DB_NEXT) != DB_NOTFOUND)
            keys.push_back(
                string((char *) kt.get_data(), kt.get_size()));

    } catch (DbException e) { rethrow(e); }
}
