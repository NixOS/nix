#ifndef BUILD_HH_
#define BUILD_HH_

#include "db.hh"

namespace nix {

	void ensurePathTxn(const Transaction & txn, const Path & path);

}

#endif /*BUILD_HH_*/
