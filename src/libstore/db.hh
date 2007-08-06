#ifndef __DB_H
#define __DB_H

#include "types.hh"

#include <map>


/* Defined externally. */
class DbTxn;
class DbEnv;
class Db;


namespace nix {


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
    std::map<TableId, Db *> tables;

    void requireEnv();

    Db * getDb(TableId table);

    void open2(const string & path, bool removeOldEnv);
    
    /* TODO */
    bool lookupHighestRevivison(const Strings & keys, const Path & statePath, string & key, int lowerthan = -1);
    
    /* TODO */
    int getNewRevisionNumber(const Transaction & txn, TableId table, const Path & statePath);
    
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
    
    /* TODO */
    Path mergeToDBKey(const Path & statePath, const int revision);
    
    /* TODO */
    void splitDBKey(const Path & revisionedStatePath, Path & statePath, int & revision);

	/* TODO */
    bool revisionToTimeStamp(const Transaction & txn, TableId revisions_table, const Path & statePath, const int revision, int & timestamp);
	    
    /* Set the stateReferences for a specific revision (and older until the next higher revision number in the table) */    
    void setStateReferences(const Transaction & txn, TableId references_table, TableId revisions_table,
    	const Path & statePath, const Strings & references, int revision = -1, int timestamp = -1);
    
    /* Returns the references for a specific revision (and older until the next higher revision number in the table) */
    bool queryStateReferences(const Transaction & txn, TableId references_table, TableId revisions_table,
    	const Path & statePath, Strings & references, int revision = -1, int timestamp = -1);

    /* Set the revision number of the statePath and the revision numbers of all state paths in the references closure */
    void setStateRevisions(const Transaction & txn, TableId revisions_table, TableId revisions_comments,
    	TableId snapshots_table, const RevisionClosure & revisions, const Path & rootStatePath, const string & comment);
    
    /* Returns all the revision numbers of the state references closure of the given state path */
    bool queryStateRevisions(const Transaction & txn, TableId revisions_table, TableId snapshots_table,
    const Path & statePath, RevisionClosure & revisions, RevisionClosureTS & timestamps, int root_revision = -1);
    
    /* Returns all available revision numbers of the given state path */
    bool queryAvailableStateRevisions(const Transaction & txn, TableId revisions_table, TableId revisions_comments,
    	const Path & statePath, RevisionInfos & revisions);	
   
    
    
};


class DbNoPermission : public Error
{
public:
    DbNoPermission(const format & f) : Error(f) { };
};

 
}


#endif /* !__DB_H */
