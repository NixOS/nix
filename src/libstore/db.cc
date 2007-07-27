#include "db.hh"
#include "util.hh"
#include "pathlocks.hh"
#include "derivations.hh"
#include "local-store.hh"
#include "misc.hh"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include <memory>

#include <db_cxx.h>


namespace nix {


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
    ~DestroyDbEnv() {
        if (dbenv) {
            if (dbenv->get_DB_ENV()) dbenv->close(0);
            delete dbenv;
        }
    }
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
    std::map<TableId, Db *>::iterator i = tables.find(table);
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
    env->set_lk_max_locks(100000);
    env->set_lk_max_lockers(100000);
    env->set_lk_max_objects(100000);
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
               lock.  If things go wrong there, we bail out.
               !!! argh, we abolished the global lock :-( */
            open2(path, true);

            /* Try again. */
            open2(path, false);

            /* Force a checkpoint, as per the BDB docs. */
            env->txn_checkpoint(DB_FORCE, 0, 0);

            printMsg(lvlError, "database succesfully upgraded to new version");
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

        for (std::map<TableId, Db *>::iterator i = tables.begin();
             i != tables.end(); )
        {
            std::map<TableId, Db *>::iterator j = i;
            ++j;
            closeTable(i->first);
            i = j;
        }

        /* Do a checkpoint every 128 kilobytes, or every 5 minutes. */
        env->txn_checkpoint(128, 5, 0);
        
        env->close(0);

    } catch (DbException e) { rethrow(e); }

    delete env;

    env = 0;
}


TableId Database::openTable(const string & tableName, bool sorted)
{
    requireEnv();
    TableId table = nextId++;

    try {

        Db * db = new Db(env, 0);

        try {
            db->open(0, tableName.c_str(), 0, 
                sorted ? DB_BTREE : DB_HASH,
                DB_CREATE | DB_AUTO_COMMIT, 0666);
        } catch (...) {
            delete db;
            throw;
        }

        tables[table] = db;

    } catch (DbException e) { rethrow(e); }

    return table;
}


void Database::closeTable(TableId table)
{
    try {
        Db * db = getDb(table);
        db->close(DB_NOSYNC);
        delete db;
        tables.erase(table);
    } catch (DbException e) { rethrow(e); }
}


void Database::deleteTable(const string & table)
{
    try {
        env->dbremove(0, table.c_str(), 0, DB_AUTO_COMMIT);
    } catch (DbException e) { rethrow(e); }
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
    Strings & keys, const string & keyPrefix)
{
    try {
        Db * db = getDb(table);

        Dbc * dbc;
        db->cursor(txn.txn, &dbc, 0);
        DestroyDbc destroyDbc(dbc);

        Dbt kt, dt;
        u_int32_t flags = DB_NEXT;

        if (!keyPrefix.empty()) {
            flags = DB_SET_RANGE;
            kt = Dbt((void *) keyPrefix.c_str(), keyPrefix.size());
        }

        while (dbc->get(&kt, &dt, flags) != DB_NOTFOUND) {
            checkInterrupt();
            string data((char *) kt.get_data(), kt.get_size());
            if (!keyPrefix.empty() &&
                string(data, 0, keyPrefix.size()) != keyPrefix)
                break;
            keys.push_back(data);
            flags = DB_NEXT;
        }

    } catch (DbException e) { rethrow(e); }
}

/* State specific db functions */

Path Database::makeStatePathRevision(const Path & statePath, const int revision)
{
	string prefix = "-KEY-";
	return statePath + prefix + int2String(revision);
}

void Database::splitStatePathRevision(const Path & revisionedStatePath, Path & statePath, int & revision)
{
	string prefix = "-KEY-";
	
	int pos = revisionedStatePath.find_last_of(prefix);
	statePath = revisionedStatePath.substr(0, pos - prefix.length() + 1);
	//printMsg(lvlError, format("'%2%' - '%1%'") % revisionedStatePath.substr(pos+1, revisionedStatePath.length()) % int2String(pos));
	bool succeed = string2Int(revisionedStatePath.substr(pos+1, revisionedStatePath.length()), revision);
	if(!succeed)
		throw Error(format("Malformed revision value of path '%1%'") % revisionedStatePath);
}

int Database::getNewRevisionNumber(const Transaction & txn, TableId table,
   	const Path & statePath)
{
	//query
	string data;
	bool notEmpty = queryString(txn, table, statePath, data);
	
	if(!notEmpty){
		setString(txn, table, statePath, int2String(1));
		return 1;	
	}
	
	int revision;
	bool succeed = string2Int(data, revision);
	if(!succeed)
		throw Error(format("Malformed revision counter value of path '%1%'") % statePath);
	
	revision++;
	setString(txn, table, statePath, int2String(revision));
	
	return revision;
}

bool Database::lookupHighestRevivison(const Strings & keys, const Path & statePath, string & key, int lowerthan)
{
	int highestRev = -1;

	//Lookup which key we need	
	for (Strings::const_iterator i = keys.begin(); i != keys.end(); ++i) {
		
		if((*i).substr(0, statePath.length()) != statePath || (*i).length() == statePath.length()) //dont check the new-revision key or other keys
			continue;

		//printMsg(lvlError, format("'%1%' - '%2%'") % *i % statePath);
		Path getStatePath;
		int getRevision;
		splitStatePathRevision(*i, getStatePath, getRevision);
		if(getRevision > highestRev){
		
			if(lowerthan != -1){
				if(getRevision <= lowerthan)			//if we have an uppper limit, see to it that we downt go over it
					highestRev = getRevision;
			}
			else
				highestRev = getRevision;
		}
	}

	if(highestRev == -1)	//no records found
		return false;
	
	key = makeStatePathRevision(statePath, highestRev);		//final key that matches revision + statePath
	return true;	
}

bool Database::revisionToTimeStamp(const Transaction & txn, TableId revisions_table, const Path & statePath, const int revision, int & timestamp)
{
	string key = makeStatePathRevision(statePath, revision);
	Strings references;
	bool notempty = queryStrings(txn, revisions_table, key, references);
	
	if(notempty){	
		Path empty; 
		splitStatePathRevision(*(references.begin()), empty, timestamp);	//extract the timestamp
		//printMsg(lvlError, format("PRINT '%1%'") % timestamp);
		return true;
	}
	else
		return false;		
}

void Database::setStateReferences(const Transaction & txn, TableId references_table, TableId revisions_table,
   	const Path & statePath, const Strings & references, int revision)
{
	//printMsg(lvlError, format("setStateReferences/Referrers %1%") % table);
	
	int timestamp;
	if(revision == -1)
		timestamp = getTimeStamp();
	else{
		bool found = revisionToTimeStamp(txn, revisions_table, statePath, revision, timestamp);
		if(!found)
			throw Error(format("Revision '%1%' cannot be matched to a timestamp...") % revision);
	}		

	//for (Strings::const_iterator i = references.begin(); i != references.end(); ++i)
	//	printMsg(lvlError, format("setStateReferences::: '%1%'") % *i);
	
	//Warning if it already exists
	Strings empty;
	if( queryStateReferences(txn, references_table, revisions_table, statePath, empty, -1, timestamp) )
		printMsg(lvlError, format("Warning: The timestamp '%1%' already exists for set-references/referrers of path '%2%' with db '%3%'") % timestamp % statePath % references_table);
	
	//Create the key
	string key = makeStatePathRevision(statePath, timestamp);
	
	//Insert	
	setStrings(txn, references_table, key, references);
}

bool Database::queryStateReferences(const Transaction & txn, TableId references_table, TableId revisions_table,
   	const Path & statePath, Strings & references, int revision, int timestamp)
{
	//printMsg(lvlError, format("queryStateReferences/Referrers '%1%' with revision '%2%'") % references_table % revision);

	//Convert revision to timestamp number useing the revisions_table
	if(timestamp == -1 && revision != -1){	
		bool found = revisionToTimeStamp(txn, revisions_table, statePath, revision, timestamp);
		if(!found)			//we are asked for references of some revision, but there are no references registered yet, so we return false;
			return false;
	}		
	
	Strings keys;
	enumTable(txn, references_table, keys);

	//Mabye we need the latest timestamp?
	string key = "";
	if(timestamp == -1){
		bool foundsomething = lookupHighestRevivison(keys, statePath, key);
		if(!foundsomething)
			return false;
		else
			return queryStrings(txn, references_table, key, references);
	}
	
	//If a specific key is given: check if this timestamp exists key in the table
	key = makeStatePathRevision(statePath, timestamp);
	bool found = false;
	for (Strings::const_iterator i = keys.begin(); i != keys.end(); ++i) {
		if(key == *i){
			found = true;
			key = makeStatePathRevision(statePath, timestamp);
		}
	}
	
	//If it doesn't exist in the table then find the highest key lower than it
	if(!found){
		bool foundsomething = lookupHighestRevivison(keys, statePath, key, -1);
		if(!foundsomething)
			return false;
		//printMsg(lvlError, format("Warning: References for timestamp '%1%' not was not found, so taking the highest rev-key possible for statePath '%2%'") % timestamp % statePath);
	}
		
	return queryStrings(txn, references_table, key, references);		//now that we have the key, we can query the references
}

void Database::setStateReferrers(const Transaction & txn, TableId referrers_table, TableId revisions_table,
    	const Path & statePath, const Strings & referrers, int revision)
{
	//Exactly the same as the setStateReferences
	setStateReferences(txn, referrers_table, revisions_table, statePath, referrers, revision);
}

bool Database::queryStateReferrers(const Transaction & txn, TableId referrers_table, TableId revisions_table,
 	const Path & statePath, Strings & referrers, int revision, int timestamp)
{
	//Exactly the same as queryStateReferences
	return queryStateReferences(txn, referrers_table, revisions_table, statePath, referrers, revision, timestamp);
}

void Database::setStateRevisions(const Transaction & txn, TableId revisions_table, TableId snapshots_table,
   	const RevisionClosure & revisions)
{
	int ts = getTimeStamp();
	
	//Insert all ss_epochs into snapshots_table with the current ts.
	for (RevisionClosure::const_iterator i = revisions.begin(); i != revisions.end(); ++i){
		string key = makeStatePathRevision((*i).first, ts);
		Strings data;
		//the map<> takes care of the sorting on the Path
		for (Snapshots::const_iterator j = (*i).second.begin(); j != (*i).second.end(); ++j)
			data.push_back(int2String((*j).second));
		setStrings(txn, snapshots_table, key, data);
	}
	
	//Insert for each statePath a new revision record linked to the ss_epochs
	for (RevisionClosure::const_iterator i = revisions.begin(); i != revisions.end(); ++i){
		Path statePath = (*i).first;
		
		int revision = getNewRevisionNumber(txn, revisions_table, statePath);	//get a new revision number
		string key = makeStatePathRevision(statePath, revision);
		
		//get all its requisites
		PathSet statePath_references;
		storePathRequisitesTxn(txn, statePath, false, statePath_references, false, true, -1);
		statePath_references.insert(statePath);
		
		//save in db
		Strings data;
		for (PathSet::const_iterator j = statePath_references.begin(); j != statePath_references.end(); ++j)
			data.push_back(makeStatePathRevision(*j, ts));		
		setStrings(txn, revisions_table, key, data);
	}
}   

bool Database::queryStateRevisions(const Transaction & txn, TableId revisions_table, TableId snapshots_table,
   	const Path & statePath, RevisionClosure & revisions, RevisionClosureTS & timestamps, int root_revision)
{
	string key;
	
	if(root_revision == -1){
		Strings keys;
		enumTable(txn, revisions_table, keys);		//get all revisions
		bool foundsomething = lookupHighestRevivison(keys, statePath, key);
		if(!foundsomething)
			return false;
	}
	else
		key = makeStatePathRevision(statePath, root_revision);
	
	//Get references pointing to snapshots_table from revisions_table with root_revision
	Strings statePaths;
	bool notempty = queryStrings(txn, revisions_table, key, statePaths);
	
	if(!notempty)
		throw Error(format("Root revision '%1%' not found of statePath '%2%'") % int2String(root_revision) % statePath);
		
	//For each statePath add the revisions
	for (Strings::iterator i = statePaths.begin(); i != statePaths.end(); ++i){
		
		Path getStatePath;
		int getTimestamp;
		splitStatePathRevision(*i, getStatePath, getTimestamp);
		
		//query state versioined directorys/files
		vector<Path> sortedPaths;
		Derivation drv = derivationFromPath(queryStatePathDrvTxn(txn, getStatePath));
  		DerivationStateOutputs stateOutputs = drv.stateOutputs; 
    	DerivationStateOutputDirs stateOutputDirs = drv.stateOutputDirs;
    	for (DerivationStateOutputDirs::const_iterator j = stateOutputDirs.begin(); j != stateOutputDirs.end(); ++j){
			string thisdir = (j->second).path;
			string fullstatedir = getStatePath + "/" + thisdir;
			if(thisdir == "/")									//exception for the root dir
				fullstatedir = statePath + "/";
			sortedPaths.push_back(fullstatedir);
    	}			
		sort(sortedPaths.begin(), sortedPaths.end());	//sort
		
		Strings snapshots_s;
		Snapshots snapshots;		
		queryStrings(txn, snapshots_table, *i, snapshots_s);
		int counter=0;
		for (Strings::iterator j = snapshots_s.begin(); j != snapshots_s.end(); ++j){
			
			unsigned int revision;
			bool succeed = string2UnsignedInt(*j, revision);
			if(!succeed)
				throw Error(format("Malformed epoch (snapshot timestamp) value of path '%1%'") % statePath);
			
			snapshots[sortedPaths.at(counter)] = revision;
			counter++;
		}
		
		revisions[getStatePath] = snapshots;
		timestamps[getStatePath] = getTimestamp;
	}
	
	
	return notempty;
}  

//TODO include comments into revisions?
bool Database::queryAvailableStateRevisions(const Transaction & txn, TableId revisions_table,
    const Path & statePath, RevisionNumbers & revisions)
{
	Strings keys;
	enumTable(txn, revisions_table, keys);		//get all revisions
	
	for (Strings::const_iterator i = keys.begin(); i != keys.end(); ++i) {
	
		if((*i).substr(0, statePath.length()) != statePath || (*i).length() == statePath.length()) 		//dont check the new-revision key or other keys
			continue;

		Path getStatePath;
		int getRevision;
		splitStatePathRevision(*i, getStatePath, getRevision);
		revisions.push_back(getRevision);
	}
    
    if(revisions.empty())
    	return false;
    else
    	return true;
}
 
}
