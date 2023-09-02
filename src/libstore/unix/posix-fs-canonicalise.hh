#pragma once
///@file

#include <sys/stat.h>
#include <sys/time.h>

#include "types.hh"
#include "error.hh"

namespace nix {

typedef std::pair<dev_t, ino_t> Inode;
typedef std::set<Inode> InodesSeen;


/**
 * "Fix", or canonicalise, the meta-data of the files in a store path
 * after it has been built.  In particular:
 *
 * - the last modification date on each file is set to 1 (i.e.,
 *   00:00:01 1/1/1970 UTC)
 *
 * - the permissions are set of 444 or 555 (i.e., read-only with or
 *   without execute permission; setuid bits etc. are cleared)
 *
 * - the owner and group are set to the Nix user and group, if we're
 *   running as root.
 *
 * If uidRange is not empty, this function will throw an error if it
 * encounters files owned by a user outside of the closed interval
 * [uidRange->first, uidRange->second].
 */
void canonicalisePathMetaData(
    const Path & path,
    std::optional<std::pair<uid_t, uid_t>> uidRange,
    InodesSeen & inodesSeen);
void canonicalisePathMetaData(
    const Path & path,
    std::optional<std::pair<uid_t, uid_t>> uidRange);

void canonicaliseTimestampAndPermissions(const Path & path);

MakeError(PathInUse, Error);

}
