#pragma once

#include "sync.hh"
#include "types.hh"
#include "util.hh"

namespace nix {

class UserLock
{
private:
    /* POSIX locks suck.  If we have a lock on a file, and we open and
       close that file again (without closing the original file
       descriptor), we lose the lock.  So we have to be *very* careful
       not to open a lock file on which we are holding a lock. */
    static Sync<PathSet> lockedPaths_;

    Path fnUserLock;
    AutoCloseFD fdUserLock;

    string user;
    uid_t uid;
    gid_t gid;
    std::vector<gid_t> supplementaryGIDs;

public:
    UserLock();
    ~UserLock();

    void kill();

    string getUser() { return user; }
    uid_t getUID() { assert(uid); return uid; }
    uid_t getGID() { assert(gid); return gid; }
    std::vector<gid_t> getSupplementaryGIDs() { return supplementaryGIDs; }

    bool enabled() { return uid != 0; }

};

}
