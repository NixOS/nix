#include "nix/store/local-store.hh"
#include "nix/store/local-settings.hh"
#include "nix/util/signals.hh"
#include "nix/store/posix-fs-canonicalise.hh"
#include "nix/util/posix-source-accessor.hh"
#include "nix/util/file-system.hh"

#include <cstdlib>
#include <cstring>
#ifdef __APPLE__
#  include <regex>
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#if NIX_SUPPORT_ACL
#  include <sys/xattr.h>
#endif

#include "store-config-private.hh"

namespace nix {

#if NIX_SUPPORT_ACL
// Use trusted.* namespace:
// - Works on all file types (including symlinks)
// - Requires CAP_SYS_ADMIN (nix-daemon has this)
// - Non-root users can't read it, so they get no optimization (acceptable)
static constexpr const char* XATTR_OPTIMISED = "trusted.nix.optimised";
#endif

static void makeWritable(const std::filesystem::path & path)
{
    auto st = lstat(path);
    chmod(path, st.st_mode | S_IWUSR);
}

struct MakeReadOnly
{
    std::filesystem::path path;

    MakeReadOnly(std::filesystem::path path)
        : path(std::move(path))
    {
    }

    ~MakeReadOnly()
    {
        try {
            /* This will make the path read-only. */
            if (!path.empty())
                canonicaliseTimestampAndPermissions(path.string());
        } catch (...) {
            ignoreExceptionInDestructor();
        }
    }
};

bool LocalStore::isPathOptimised(const std::filesystem::path & path) const
{
#if NIX_SUPPORT_ACL
    if (xattrsUnsupported.load(std::memory_order_relaxed))
        return false;

    char buf[32];
    ssize_t size = lgetxattr(path.c_str(), XATTR_OPTIMISED, buf, sizeof(buf));

    if (size < 0) {
        if (errno == ENOTSUP || errno == EOPNOTSUPP) {
            // Filesystem doesn't support xattrs - disable further attempts
            // for the lifetime of this store, so we don't retry a syscall
            // that's known to fail on every remaining path.
            if (!xattrsUnsupported.exchange(true, std::memory_order_relaxed)) {
                debug("filesystem at %s doesn't support extended attributes, optimization tracking disabled", path.string());
            }
            return false;
        }
        // EPERM/EACCES means trusted.* but no CAP_SYS_ADMIN (non-root user).
        // Our credentials won't change for the lifetime of this process,
        // so this is just as permanent as ENOTSUP - cache it the same way.
        if (errno == EPERM || errno == EACCES) {
            if (!xattrsUnsupported.exchange(true, std::memory_order_relaxed)) {
                debug("no permission to read extended attributes at %s, optimization tracking disabled", path.string());
            }
            return false;
        }
        // No xattr (ENODATA/ENOATTR) = not optimised
        return false;
    }

    return true;  // xattr exists = already optimised
#else
    return false;  // Platform doesn't support xattrs: always re-optimize
#endif
}

void LocalStore::markPathOptimised(const std::filesystem::path & path)
{
#if NIX_SUPPORT_ACL
    if (xattrsUnsupported.load(std::memory_order_relaxed))
        return;

    std::string timestamp = std::to_string(time(nullptr));

    if (lsetxattr(path.c_str(), XATTR_OPTIMISED, timestamp.c_str(),
                  timestamp.size(), 0) < 0) {
        if (errno == ENOTSUP || errno == EOPNOTSUPP || errno == EPERM || errno == EACCES) {
            // Same permanent conditions as isPathOptimised() - stop retrying.
            xattrsUnsupported.store(true, std::memory_order_relaxed);
        } else if (errno != EROFS) {
            // Log unexpected errors, but ignore read-only filesystem
            // (also permanent, but not worth a dedicated flag - EROFS
            // only matters for markPathOptimised, and failing silently
            // there is already the correct behaviour).
            debug("failed to mark path optimised: %s", strerror(errno));
        }
        // Don't fail optimization if xattr fails
    }
#endif
}

LocalStore::InodeHash LocalStore::loadInodeHash()
{
    debug("loading hash inodes in memory");
    InodeHash inodeHash;

    AutoCloseDir dir(opendir(linksDir.string().c_str()));
    if (!dir)
        throw SysError("opening directory %1%", PathFmt(linksDir));

    struct dirent * dirent;
    while (errno = 0, dirent = readdir(dir.get())) { /* sic */
        checkInterrupt();
        // We don't care if we hit non-hash files, anything goes
        inodeHash.insert(dirent->d_ino);
    }
    if (errno)
        throw SysError("reading directory %1%", PathFmt(linksDir));

    printMsg(lvlTalkative, "loaded %1% hash inodes", inodeHash.size());

    return inodeHash;
}

Strings LocalStore::readDirectoryIgnoringInodes(const std::filesystem::path & path, const InodeHash & inodeHash)
{
    Strings names;

    AutoCloseDir dir(opendir(path.string().c_str()));
    if (!dir)
        throw SysError("opening directory %s", PathFmt(path));

    struct dirent * dirent;
    while (errno = 0, dirent = readdir(dir.get())) { /* sic */
        checkInterrupt();

        if (inodeHash.count(dirent->d_ino)) {
            debug("'%1%' is already linked", dirent->d_name);
            continue;
        }

        std::string name = dirent->d_name;
        if (name == "." || name == "..")
            continue;
        names.push_back(name);
    }
    if (errno)
        throw SysError("reading directory %s", PathFmt(path));

    return names;
}

void LocalStore::optimisePath_(
    Activity * act, OptimiseStats & stats, const std::filesystem::path & path, InodeHash & inodeHash, RepairFlag repair)
{
    checkInterrupt();

    auto st = lstat(path);

#ifdef __APPLE__
    /* HFS/macOS has some undocumented security feature disabling hardlinking for
       special files within .app dirs. Known affected paths include
       *.app/Contents/{PkgInfo,Resources/\*.lproj,_CodeSignature} and .DS_Store.
       See https://github.com/NixOS/nix/issues/1443 and
       https://github.com/NixOS/nix/pull/2230 for more discussion. */

    if (std::regex_search(path.string(), std::regex("\\.app/Contents/.+$"))) {
        debug("%s is not allowed to be linked in macOS", PathFmt(path));
        return;
    }
#endif

    if (S_ISDIR(st.st_mode)) {
        Strings names = readDirectoryIgnoringInodes(path, inodeHash);
        for (auto & i : names)
            optimisePath_(act, stats, path / i, inodeHash, repair);
        return;
    }

    /* We can hard link regular files and maybe symlinks. */
    if (!S_ISREG(st.st_mode)
#if CAN_LINK_SYMLINK
        && !S_ISLNK(st.st_mode)
#endif
    )
        return;

    /* Sometimes SNAFUs can cause files in the Nix store to be
       modified, in particular when running programs as root under
       NixOS (example: $fontconfig/var/cache being modified).  Skip
       those files.  FIXME: check the modification time. */
    if (S_ISREG(st.st_mode) && (st.st_mode & S_IWUSR)) {
        warn("skipping suspicious writable file '%s'", PathFmt(path));
        return;
    }

    /* This can still happen on top-level files. */
    if (st.st_nlink > 1 && inodeHash.count(st.st_ino)) {
        debug("%s is already linked, with %d other file(s)", PathFmt(path), st.st_nlink - 2);
        return;
    }

    /* Hash the file.  Note that hashPath() returns the hash over the
       NAR serialisation, which includes the execute bit on the file.
       Thus, executable and non-executable files with the same
       contents *won't* be linked (which is good because otherwise the
       permissions would be screwed up).

       Also note that if `path' is a symlink, then we're hashing the
       contents of the symlink (i.e. the result of readlink()), not
       the contents of the target (which may not even exist). */
    Hash hash = hashPath(makeFSSourceAccessor(path), FileSerialisationMethod::NixArchive, HashAlgorithm::SHA256).hash;
    debug("%s has hash '%s'", PathFmt(path), hash.to_string(HashFormat::Nix32, true));

    /* Check if this is a known hash. */
    std::filesystem::path linkPath = std::filesystem::path{linksDir} / hash.to_string(HashFormat::Nix32, false);

    /* Stat the link once, if it exists. */
    auto stLink = maybeLstat(linkPath);

    /* Maybe delete the link, if it has been corrupted. */
    if (stLink) {
        if (st.st_size != stLink->st_size || (repair && hash != ({
                                                            hashPath(
                                                                makeFSSourceAccessor(linkPath),
                                                                FileSerialisationMethod::NixArchive,
                                                                HashAlgorithm::SHA256)
                                                                .hash;
                                                        }))) {
            // XXX: Consider overwriting linkPath with our valid version.
            warn("removing corrupted link %s", PathFmt(linkPath));
            warn(
                "There may be more corrupted paths."
                "\nYou should run `nix-store --verify --check-contents --repair` to fix them all");
            unlinkIfExists(linkPath);
            stLink.reset();
        }
    }

    if (!stLink) {
        /* Nope, create a hard link in the links directory. */
        try {
            std::filesystem::create_hard_link(path, linkPath);
            inodeHash.insert(st.st_ino);
            stLink = st;  // After hardlinking, linkPath has same stat as path
        } catch (std::filesystem::filesystem_error & e) {
            if (e.code() == std::errc::file_exists) {
                /* Another process created ‘linkPath’ before
                   we did. */
                stLink = lstat(linkPath);
                inodeHash.insert(stLink->st_ino);
            }

            else if (e.code() == std::errc::no_space_on_device) {
                /* On ext4, that probably means the directory index is
                   full.  When that happens, it's fine to ignore it: we
                   just effectively disable deduplication of this
                   file.
                   */
                printInfo("cannot link %s to '%s': %s", PathFmt(linkPath), PathFmt(path), e.code().message());
                return;
            }

            else
                throw SystemError(e.code(), "creating hard link from %1% to %2%", PathFmt(linkPath), PathFmt(path));
        }
    }

    /* Yes!  We've seen a file with the same contents.  Replace the
       current file with a hard link to that file. */

    if (st.st_ino == stLink->st_ino) {
        debug("%1% is already linked to %2%", PathFmt(path), PathFmt(linkPath));
        return;
    }

    printMsg(lvlTalkative, "linking %1% to %2%", PathFmt(path), PathFmt(linkPath));

    /* Make the containing directory writable, but only if it's not
       the store itself (we don't want or need to mess with its
       permissions). */
    const auto dirOfPath = path.parent_path();
    bool mustToggle = dirOfPath != config->realStoreDir.get();
    if (mustToggle)
        makeWritable(dirOfPath);

    /* When we're done, make the directory read-only again and reset
       its timestamp back to 0. */
    MakeReadOnly makeReadOnly(mustToggle ? dirOfPath : std::filesystem::path{});

    std::filesystem::path tempLink = makeTempPath(config->realStoreDir.get(), ".tmp-link");

    /* RAII cleanup for tempLink */
    bool tempLinkCreated = false;
    Finally cleanupTempLink([&]() {
        if (tempLinkCreated) {
            std::error_code ec;
            std::filesystem::remove(tempLink, ec);
        }
    });

    try {
        std::filesystem::create_hard_link(linkPath, tempLink);
        tempLinkCreated = true;
        inodeHash.insert(stLink->st_ino);
    } catch (std::filesystem::filesystem_error & e) {
        if (e.code() == std::errc::too_many_links) {
            /* Too many links to the same file (>= 32000 on most file
               systems).  This is likely to happen with empty files.
               Just shrug and ignore. */
            if (st.st_size)
                printInfo("%1% has maximum number of links", PathFmt(linkPath));
            return;
        }
        throw SystemError(e.code(), "creating hard link from %1% to %2%", PathFmt(linkPath), PathFmt(tempLink));
    }

    /* Atomically replace the old file with the new hard link. */
    try {
        std::filesystem::rename(tempLink, path);
        tempLinkCreated = false; /* Successfully renamed, no cleanup needed */
    } catch (std::filesystem::filesystem_error & e) {
        if (e.code() == std::errc::too_many_links) {
            /* Some filesystems generate too many links on the rename,
               rather than on the original link.  (Probably it
               temporarily increases the st_nlink field before
               decreasing it again.) */
            debug("%s has reached maximum number of links", PathFmt(linkPath));
            return;
        }
        throw SystemError(e.code(), "renaming %1% to %2%", PathFmt(tempLink), PathFmt(path));
    }

    stats.filesLinked++;
    stats.bytesFreed += st.st_size;

    if (act)
        act->result(
            resFileLinked,
            st.st_size
#ifndef _WIN32
            ,
            st.st_blocks
#endif
        );
}

void LocalStore::optimiseStore(OptimiseStats & stats)
{
    Activity act(*logger, actOptimiseStore);

    auto paths = queryAllValidPaths();

    // Check if xattrs are usable by trying to list xattrs on linksDir.
    // If not usable, we need to pre-load the inode hash as a fallback,
    // and can skip the per-path isPathOptimised()/markPathOptimised()
    // syscalls entirely for the rest of this run.
    InodeHash inodeHash;
#if NIX_SUPPORT_ACL
    // Try to list xattrs on linksDir to detect if they're usable
    ssize_t size = llistxattr(linksDir.string().c_str(), nullptr, 0);
    if (size < 0) {
        // Any failure means we can't rely on xattrs for optimization tracking
        // Load inode hash as fallback to avoid duplicate work
        debug("cannot use xattrs for optimization tracking (%s), loading inode hash", strerror(errno));
        inodeHash = loadInodeHash();
        xattrsUnsupported.store(true, std::memory_order_relaxed);
    }
    // Otherwise: xattrs work (size >= 0)
    // Start with empty hash - xattr checks will skip already-optimised paths
#else
    // Platform doesn't support xattrs at compile time
    inodeHash = loadInodeHash();
#endif

    act.progress(0, paths.size());

    uint64_t done = 0;
    uint64_t skipped = 0;

    for (auto & i : paths) {
        checkInterrupt();

        auto fullPath = config->realStoreDir.get() / i.to_string();

        // Check xattr FIRST - if optimised, skip expensive DB operations
        if (isPathOptimised(fullPath)) {
            debug("skipping already-optimised path '%s'", printStorePath(i));
            skipped++;
            done++;
            act.progress(done, paths.size());
            continue;
        }

        // Only do DB operations for paths that need optimization
        addTempRoot(i);
        if (!isValidPath(i)) {
            /* path was GC'ed, probably */
            done++;
            act.progress(done, paths.size());
            continue;
        }

        {
            Activity act(*logger, lvlTalkative, actUnknown, fmt("optimising path '%s'", printStorePath(i)));
            optimisePath_(&act, stats, fullPath, inodeHash, NoRepair);
        }

        // Mark path as optimised
        markPathOptimised(fullPath);

        done++;
        act.progress(done, paths.size());
    }

    if (skipped > 0) {
        printInfo("skipped %d already-optimised paths", skipped);
    }
}

void LocalStore::optimiseStore()
{
    OptimiseStats stats;

    optimiseStore(stats);

    printInfo("%s freed by hard-linking %d files", renderSize(stats.bytesFreed), stats.filesLinked);
}

void LocalStore::optimisePath(const std::filesystem::path & path, RepairFlag repair)
{
    OptimiseStats stats;
    InodeHash inodeHash;

    if (config->getLocalSettings().autoOptimiseStore)
        optimisePath_(nullptr, stats, path, inodeHash, repair);
}

} // namespace nix
