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

        if (!dt.get_data())
            data = "";
        else
            data = string((char *) dt.get_data(), dt.get_size());
    
    } catch (DbException e) { rethrow(e); }

    return true;
}


bool queryListDB(const string & filename, const string & dbname,
    const string & key, Strings & data)
{
    string d;

    if (!queryDB(filename, dbname, key, d))
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


void setListDB(const string & filename, const string & dbname,
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

    setDB(filename, dbname, key, d);
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
