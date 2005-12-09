#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include <memory>

#include "db.hh"
#include "util.hh"
#include "pathlocks.hh"


/* Wrapper class to ensure proper destruction. */
class DestroyDbc 
{
    Dbc * dbc;
public:
    DestroyDbc(Dbc * _dbc) : dbc(_dbc) { }
    ~DestroyDbc() { dbc->close(); /* close() frees dbc */ }
};


class DestroyDbEnv
{
    DbEnv * dbenv;
public:
    DestroyDbEnv(DbEnv * _dbenv) : dbenv(_dbenv) { }
    ~DestroyDbEnv() { if (dbenv) { dbenv->close(0); delete dbenv; } }
    void release() { dbenv = 0; };
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
    : txn(0)
{
    begin(db);
}


Transaction::~Transaction()
{
    if (txn) abort();
}


void Transaction::begin(Database & db)
{
    assert(txn == 0);
    db.requireEnv();
    try {
        db.env->txn_begin(0, &txn, 0);
    } catch (DbException e) { rethrow(e); }
}


void Transaction::commit()
{
    if (!txn) throw Error("commit called on null transaction");
    debug(format("committing transaction %1%") % (void *) txn);
    DbTxn * txn2 = txn;
    txn = 0;
    try {
        txn2->commit(0);
    } catch (DbException e) { rethrow(e); }
}


void Transaction::abort()
{
    if (!txn) throw Error("abort called on null transaction");
    debug(format("aborting transaction %1%") % (void *) txn);
    DbTxn * txn2 = txn;
    txn = 0;
    try {
        txn2->abort();
    } catch (DbException e) { rethrow(e); }
}


void Transaction::moveTo(Transaction & t)
{
    if (t.txn) throw Error("target txn already exists");
    t.txn = txn;
    txn = 0;
}


void Database::requireEnv()
{
    checkInterrupt();
    if (!env) throw Error("database environment is not open "
        "(maybe you don't have sufficient permission?)");
}


Db * Database::getDb(TableId table)
{
    if (table == 0)
        throw Error("database table is not open "
            "(maybe you don't have sufficient permission?)");
    map<TableId, Db *>::iterator i = tables.find(table);
    if (i == tables.end())
        throw Error("unknown table id");
    return i->second;
}


Database::Database()
    : env(0)
    , nextId(1)
{
}


Database::~Database()
{
    close();
}


int getAccessorCount(int fd)
{
    if (lseek(fd, 0, SEEK_SET) == -1)
        throw SysError("seeking accessor count");
    char buf[128];
    int len;
    if ((len = read(fd, buf, sizeof(buf) - 1)) == -1)
        throw SysError("reading accessor count");
    buf[len] = 0;
    int count;
    if (sscanf(buf, "%d", &count) != 1) {
        debug(format("accessor count is invalid: `%1%'") % buf);
        return -1;
    }
    return count;
}


void setAccessorCount(int fd, int n)
{
    if (lseek(fd, 0, SEEK_SET) == -1)
        throw SysError("seeking accessor count");
    string s = (format("%1%") % n).str();
    const char * s2 = s.c_str();
    if (write(fd, s2, strlen(s2)) != (ssize_t) strlen(s2) ||
        ftruncate(fd, strlen(s2)) != 0)
        throw SysError("writing accessor count");
}


void openEnv(DbEnv * & env, const string & path, u_int32_t flags)
{
    try {
        env->open(path.c_str(),
            DB_INIT_LOCK | DB_INIT_LOG | DB_INIT_MPOOL | DB_INIT_TXN |
            DB_CREATE | flags,
            0666);
    } catch (DbException & e) {
        printMsg(lvlError, format("environment open failed: %1%") % e.what());
        throw;
    }
}


static int my_fsync(int fd)
{
    return 0;
}


static void errorPrinter(const DbEnv * env, const char * errpfx, const char * msg)
{
    printMsg(lvlError, format("Berkeley DB error: %1%") % msg);
}


static void messagePrinter(const DbEnv * env, const char * msg)
{
    printMsg(lvlError, format("Berkeley DB message: %1%") % msg);
}


void Database::open2(const string & path, bool removeOldEnv)
{
    if (env) throw Error(format("environment already open"));

    debug(format("opening database environment"));


    /* Create the database environment object. */
    DbEnv * env = new DbEnv(0);
    DestroyDbEnv deleteEnv(env);

    env->set_errcall(errorPrinter);
    env->set_msgcall(messagePrinter);
    //env->set_verbose(DB_VERB_REGISTER, 1);
    env->set_verbose(DB_VERB_RECOVERY, 1);
    
    /* Smaller log files. */
    env->set_lg_bsize(32 * 1024); /* default */
    env->set_lg_max(256 * 1024); /* must be > 4 * lg_bsize */
    
    /* Write the log, but don't sync.  This protects transactions
       against application crashes, but if the system crashes, some
       transactions may be undone.  An acceptable risk, I think. */
    env->set_flags(DB_TXN_WRITE_NOSYNC | DB_LOG_AUTOREMOVE, 1);
    
    /* Increase the locking limits.  If you ever get `Dbc::get: Cannot
       allocate memory' or similar, especially while running
       `nix-store --verify', just increase the following number, then
       run db_recover on the database to remove the existing DB
       environment (since changes only take effect on new
       environments). */
    env->set_lk_max_locks(10000);
    env->set_lk_max_lockers(10000);
    env->set_lk_max_objects(10000);
    env->set_lk_detect(DB_LOCK_DEFAULT);
    
    /* Dangerous, probably, but from the docs it *seems* that BDB
       shouldn't sync when DB_TXN_WRITE_NOSYNC is used, but it still
       fsync()s sometimes. */
    db_env_set_func_fsync(my_fsync);

    
    if (removeOldEnv) {
        printMsg(lvlError, "removing old Berkeley DB database environment...");
        env->remove(path.c_str(), DB_FORCE);
        return;
    }

    openEnv(env, path, DB_REGISTER | DB_RECOVER);

    deleteEnv.release();
    this->env = env;
}


void Database::open(const string & path)
{
    try {

        open2(path, false);
        
    } catch (DbException e) {
        
        if (e.get_errno() == DB_VERSION_MISMATCH) {
            /* Remove the environment while we are holding the global
               lock.  If things go wrong there, we bail out.  !!!
               there is some leakage here op DbEnv and lock
               handles. */
            open2(path, true);

            /* Try again. */
            open2(path, false);

            /* Force a checkpoint, as per the BDB docs. */
            env->txn_checkpoint(DB_FORCE, 0, 0);
        }

#if 0        
        else if (e.get_errno() == DB_RUNRECOVERY) {
            /* If recovery is needed, do it. */
            printMsg(lvlError, "running recovery...");
            open2(path, false, true);
        }
#endif        
        
        else
            rethrow(e);
    }
}


void Database::close()
{
    if (!env) return;

    /* Close the database environment. */
    debug(format("closing database environment"));

    try {

        for (map<TableId, Db *>::iterator i = tables.begin();
             i != tables.end(); i++)
        {
            Db * db = i->second;
            db->close(DB_NOSYNC);
            delete db;
        }

        /* Do a checkpoint every 128 kilobytes, or every 5 minutes. */
        env->txn_checkpoint(128, 5, 0);
        
        env->close(0);

    } catch (DbException e) { rethrow(e); }

    delete env;
}


TableId Database::openTable(const string & tableName)
{
    requireEnv();
    TableId table = nextId++;

    try {

        Db * db = new Db(env, 0);

        try {
            db->open(0, tableName.c_str(), 0, 
                DB_HASH, DB_CREATE | DB_AUTO_COMMIT, 0666);
        } catch (...) {
            delete db;
            throw;
        }

        tables[table] = db;

    } catch (DbException e) { rethrow(e); }

    return table;
}


bool Database::queryString(const Transaction & txn, TableId table, 
    const string & key, string & data)
{
    checkInterrupt();

    try {
        Db * db = getDb(table);

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


bool Database::queryStrings(const Transaction & txn, TableId table, 
    const string & key, Strings & data)
{
    string d;
    if (!queryString(txn, table, key, d))
        return false;
    data = unpackStrings(d);
    return true;
}


void Database::setString(const Transaction & txn, TableId table,
    const string & key, const string & data)
{
    checkInterrupt();
    try {
        Db * db = getDb(table);
        Dbt kt((void *) key.c_str(), key.length());
        Dbt dt((void *) data.c_str(), data.length());
        db->put(txn.txn, &kt, &dt, 0);
    } catch (DbException e) { rethrow(e); }
}


void Database::setStrings(const Transaction & txn, TableId table,
    const string & key, const Strings & data, bool deleteEmpty)
{
    if (deleteEmpty && data.size() == 0)
        delPair(txn, table, key);
    else
        setString(txn, table, key, packStrings(data));
}


void Database::delPair(const Transaction & txn, TableId table,
    const string & key)
{
    checkInterrupt();
    try {
        Db * db = getDb(table);
        Dbt kt((void *) key.c_str(), key.length());
        db->del(txn.txn, &kt, 0);
        /* Non-existence of a pair with the given key is not an
           error. */
    } catch (DbException e) { rethrow(e); }
}


void Database::enumTable(const Transaction & txn, TableId table,
    Strings & keys)
{
    try {
        Db * db = getDb(table);

        Dbc * dbc;
        db->cursor(txn.txn, &dbc, 0);
        DestroyDbc destroyDbc(dbc);

        Dbt kt, dt;
        while (dbc->get(&kt, &dt, DB_NEXT) != DB_NOTFOUND) {
            checkInterrupt();
            keys.push_back(
                string((char *) kt.get_data(), kt.get_size()));
        }

    } catch (DbException e) { rethrow(e); }
}
