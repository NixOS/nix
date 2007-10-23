#ifndef __STORESTATE_H
#define __STORESTATE_H

#include "derivations.hh"
#include "types.hh"
#include "db.hh"

namespace nix {

	/* Create a state directory. */
	void createSubStateDirsTxn(const Transaction & txn, const DerivationStateOutputDirs & stateOutputDirs, const DerivationStateOutputs & stateOutputs);
	
	/* TODO */
	Snapshots commitStatePathTxn(const Transaction & txn, const Path & statePath);
	
	/* TODO */
	//void updateRevisionsRecursivelyTxn(const Transaction & txn, const Path & statePath);
	
	/* TODO */
	//int readRevisionNumber(Path statePath);
	
	
	void scanAndUpdateAllReferencesTxn(const Transaction & txn, const Path & statePath
									, PathSet & newFoundComponentReferences, PathSet & newFoundStateReferences);
									
	void scanAndUpdateAllReferencesRecusivelyTxn(const Transaction & txn, const Path & statePath);
	
	/* revision 0 == latest ????? */
	void revertToRevisionTxn(const Transaction & txn, const Path & statePath, const int revision_arg, const bool recursive);

    /* Copy all files and folders recursively (also the hidden ones) from the dir from/... to the dir to/... and delete the rest in to/ (Rsync) */
	void rsyncPaths(const Path & from, const Path & to, const bool addSlashes);

	
	// **************************************** *******************************************
	
	
	/* TODO */
    bool lookupHighestRevivison(const Strings & keys, const Path & statePath, string & key, unsigned int lowerthan = 0);
    
    /* TODO */
    unsigned int getNewRevisionNumber(Database & nixDB, const Transaction & txn, TableId table, const Path & statePath);

    /* TODO */
    Path mergeToDBKey(const string & s1, const string & s2);
    Path mergeToDBRevKey(const Path & statePath, const unsigned int intvalue);
    
    /* TODO */
    void splitDBKey(const string & s, string & s1, string & s2);
    void splitDBRevKey(const Path & revisionedStatePath, Path & statePath, unsigned int & intvalue);

	

	/* TODO */
    bool revisionToTimeStamp(Database & nixDB, const Transaction & txn, TableId revisions_table, const Path & statePath, const int revision, unsigned int & timestamp);
	    
    /* Set the stateReferences for a specific revision (and older until the next higher revision number in the table) */    
    void setStateReferences(Database & nixDB, const Transaction & txn, TableId references_table, TableId revisions_table,
    	const Path & statePath, const Strings & references, const unsigned int revision = 0, const unsigned int timestamp = 0);
    
    /* Returns the references for a specific revision (and older until the next higher revision number in the table) */
    bool queryStateReferences(Database & nixDB, const Transaction & txn, TableId references_table, TableId revisions_table,
    	const Path & statePath, Strings & references, const unsigned int revision = 0, const unsigned int timestamp = 0);		

    /* Get the revision number of the statePath and the revision numbers of all state paths in the references closure */
    void setStateRevisions(Database & nixDB, const Transaction & txn, TableId revisions_table, TableId revisions_comments,
    	TableId snapshots_table, const RevisionClosure & revisions, const Path & rootStatePath, const string & comment);
    
    /* Returns all the revision numbers of the state references closure of the given state path */
    bool queryStateRevisions(Database & nixDB, const Transaction & txn, TableId revisions_table, TableId snapshots_table,
    const Path & statePath, RevisionClosure & revisions, RevisionClosureTS & timestamps, const unsigned int root_revision = 0);
    
    /* Returns all available revision numbers of the given state path */
    bool queryAvailableStateRevisions(Database & nixDB, const Transaction & txn, TableId revisions_table, TableId revisions_comments,
    	const Path & statePath, RevisionInfos & revisions);	
    
    /**/
    void invalidateAllStateReferences(Database & nixDB, const Transaction & txn, TableId references_table, const Path & statePath);
   
   	/**/
   	void removeAllStatePathRevisions(Database & nixDB, const Transaction & txn, TableId revisions_table, 
		TableId revisions_comments, TableId snapshots_table, TableId statecounters, const Path & statePath);
   
    /**/
    void setVersionedStateEntries(Database & nixDB, const Transaction & txn, TableId versionItems, TableId revisions_table, 
    	const Path & statePath, const StateInfos & infos, const unsigned int revision = 0, const unsigned int timestamp = 0);
    
    /**/	
	bool getVersionedStateEntries(Database & nixDB, const Transaction & txn, TableId versionItems, TableId revisions_table, 
		const Path & statePath, StateInfos & infos, const unsigned int revision = 0, const unsigned int timestamp = 0);
    
    /**/
    void setStateOptions(Database & nixDB, const Transaction & txn, TableId stateOptions, const Path & statePath, const string & user, const string & group, int chmod, const string & runtimeArgs);

	/**/
	void getStateOptions(Database & nixDB, const Transaction & txn, TableId stateOptions, const Path & statePath, string & user, string & group, int & chmod, string & runtimeArgs);
	
    
    
}

#endif /* !__STORESTATE_H */
