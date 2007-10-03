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

	
	// **************************************** *******************************************
	
	
	/* TODO */
    bool lookupHighestRevivison(const Strings & keys, const Path & statePath, string & key, unsigned int lowerthan = 0);
    
    /* TODO */
    unsigned int getNewRevisionNumber(Database & nixDB, const Transaction & txn, TableId table, const Path & statePath);

    /* TODO */
    Path mergeToDBKey(const Path & statePath, const unsigned int intvalue);
    
    /* TODO */
    void splitDBKey(const Path & revisionedStatePath, Path & statePath, unsigned int & intvalue);

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
    
    /* Copy all files and folders recursively (also the hidden ones) from the dir from/... to the dir to/... and delete the rest in to/ (Rsync) */
	void copyContents(const Path & from, const Path & to);

}

#endif /* !__STORESTATE_H */
