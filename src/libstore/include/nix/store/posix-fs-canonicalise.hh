#pragma once
///@file

#include <sys/stat.h>
#include <sys/time.h>

#include <filesystem>

#include "nix/util/types.hh"
#include "nix/util/error.hh"
#include "nix/store/config.hh"

namespace nix {

typedef std::pair<dev_t, ino_t> Inode;
typedef std::set<Inode> InodesSeen;

struct CanonicalizePathMetadataOptions
{
#ifndef _WIN32
    /**
     * If uidRange is not empty, this function will throw an error if it
     * encounters files owned by a user outside of the closed interval
     * [uidRange->first, uidRange->second].
     */
    std::optional<std::pair<uid_t, uid_t>> uidRange;
#endif

#if NIX_SUPPORT_ACL
    /**
     * A list of ACLs that should be ignored when canonicalising.
     * Normally Nix attempts to remove all ACLs from files and directories
     * in the Nix store, but some ACLs like `security.selinux` or
     * `system.nfs4_acl` can't be removed even by root.
     */
    const StringSet & ignoredAcls;
#endif
};

/**
 * Makes it easier to cope with conditionally-available fields.
 *
 * @todo Switch to a better way, as having a macro is not the nicest.
 * This will be easier to do after further settings refactors.
 */
#if NIX_SUPPORT_ACL
#  define NIX_WHEN_SUPPORT_ACLS(ARG) .ignoredAcls = ARG,
#else
#  define NIX_WHEN_SUPPORT_ACLS(ARG)
#endif

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
 *   running as root. (Unix only.)
 */
void canonicalisePathMetaData(
    const std::filesystem::path & path, CanonicalizePathMetadataOptions options, InodesSeen & inodesSeen);

void canonicalisePathMetaData(const std::filesystem::path & path, CanonicalizePathMetadataOptions options);

void canonicaliseTimestampAndPermissions(const std::filesystem::path & path);

MakeError(PathInUse, Error);

} // namespace nix
