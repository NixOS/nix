#ifndef __DB_H
#define __DB_H

#include <string>
#include <list>
#include <map>

#include "util.hh"

using namespace std;


/* Defined externally. */
class DbTxn;
class DbEnv;
class Db;


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

    void begin(Database & db);
    void abort();
    void commit();

    void moveTo(Transaction & t);
};


#define noTxn Transaction()


typedef unsigned int TableId; /* table handles */


class Database
{
    friend class Transaction;

private:
    DbEnv * env;

    TableId nextId;
    map<TableId, Db *> tables;

    void requireEnv();

    Db * getDb(TableId table);

    void open2(const string & path, bool removeOldEnv);
    
public:
    Database();
    ~Database();
    
    void open(const string & path);
    void close();

    TableId openTable(const string & table, bool sorted = false);
    void closeTable(TableId table);
    void deleteTable(const string & table);

    bool queryString(const Transaction & txn, TableId table, 
        const string & key, string & data);

    bool queryStrings(const Transaction & txn, TableId table, 
        const string & key, Strings & data);

    void setString(const Transaction & txn, TableId table,
        const string & key, const string & data);

    void setStrings(const Transaction & txn, TableId table,
        const string & key, const Strings & data,
        bool deleteEmpty = true);

    void delPair(const Transaction & txn, TableId table,
        const string & key);

    void enumTable(const Transaction & txn, TableId table,
        Strings & keys, const string & keyPrefix = "");
};


class DbNoPermission : public Error
{
public:
    DbNoPermission(const format & f) : Error(f) { };
};


#endif /* !__DB_H */
