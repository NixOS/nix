#ifndef __DB_H
#define __DB_H

#include <string>
#include <list>

#include <db_cxx.h>

#include "util.hh"

using namespace std;


class Database;


class Transaction
{
    friend class Database;

private:
    DbTxn * txn;
    
public:
    Transaction();
    Transaction(Database & _db);
    ~Transaction();

    void commit();
};


#define noTxn Transaction()


class Database
{
    friend class Transaction;

private:
    DbEnv * env;

    void requireEnv();

    Db * openDB(const Transaction & txn,
        const string & table, bool create);

public:
    Database();
    ~Database();
    
    void open(const string & path);

    void createTable(const string & table);

    bool queryString(const Transaction & txn, const string & table, 
        const string & key, string & data);

    bool queryStrings(const Transaction & txn, const string & table, 
        const string & key, Strings & data);

    void setString(const Transaction & txn, const string & table,
        const string & key, const string & data);

    void setStrings(const Transaction & txn, const string & table,
        const string & key, const Strings & data);

    void delPair(const Transaction & txn, const string & table,
        const string & key);

    void enumTable(const Transaction & txn, const string & table,
        Strings & keys);
};


#endif /* !__DB_H */
