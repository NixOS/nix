#include "db.hh"
#include "util.hh"

#include <memory>

#include <db_cxx.h>


/* Wrapper classes that ensures that the database is closed upon
   object destruction. */
class Db2 : public Db 
{
public:
    Db2(DbEnv *env, u_int32_t flags) : Db(env, flags) { }
    ~Db2() { close(0); }
};


class DbcClose 
{
    Dbc * cursor;
public:
    DbcClose(Dbc * c) : cursor(c) { }
    ~DbcClose() { cursor->close(); }
};


static auto_ptr<Db2> openDB(const string & filename, const string & dbname,
    bool readonly)
{
    auto_ptr<Db2> db(new Db2(0, 0));

    db->open(filename.c_str(), dbname.c_str(),
        DB_HASH, readonly ? DB_RDONLY : DB_CREATE, 0666);

    return db;
}


static void rethrow(DbException & e)
{
    throw Error(e.what());
}


void createDB(const string & filename, const string & dbname)
{
    try {
        openDB(filename, dbname, false);
    } catch (DbException e) { rethrow(e); }
}


bool queryDB(const string & filename, const string & dbname,
    const string & key, string & data)
{
    try {

        int err;
        auto_ptr<Db2> db = openDB(filename, dbname, true);

        Dbt kt((void *) key.c_str(), key.length());
        Dbt dt;

        err = db->get(0, &kt, &dt, 0);
        if (err) return false;

        data = string((char *) dt.get_data(), dt.get_size());
    
    } catch (DbException e) { rethrow(e); }

    return true;
}


void setDB(const string & filename, const string & dbname,
    const string & key, const string & data)
{
    try {
        auto_ptr<Db2> db = openDB(filename, dbname, false);
        Dbt kt((void *) key.c_str(), key.length());
        Dbt dt((void *) data.c_str(), data.length());
        db->put(0, &kt, &dt, 0);
    } catch (DbException e) { rethrow(e); }
}


void delDB(const string & filename, const string & dbname,
    const string & key)
{
    try {
        auto_ptr<Db2> db = openDB(filename, dbname, false);
        Dbt kt((void *) key.c_str(), key.length());
        db->del(0, &kt, 0);
    } catch (DbException e) { rethrow(e); }
}


void enumDB(const string & filename, const string & dbname,
    DBPairs & contents)
{
    try {

        auto_ptr<Db2> db = openDB(filename, dbname, true);

        Dbc * cursor;
        db->cursor(0, &cursor, 0);
        DbcClose cursorCloser(cursor);

        Dbt kt, dt;
        while (cursor->get(&kt, &dt, DB_NEXT) != DB_NOTFOUND) {
            string key((char *) kt.get_data(), kt.get_size());
            string data((char *) dt.get_data(), dt.get_size());
            contents.push_back(DBPair(key, data));
        }

    } catch (DbException e) { rethrow(e); }
}
