#include "nix/store/local-store.hh"
#include "nix/store/local-settings.hh"
#include "nix/util/signals.hh"
#include "nix/store/posix-fs-canonicalise.hh"
#include "nix/util/source-accessor.hh"
#include "nix/util/file-system.hh"
#include "nix/util/file-system-at.hh"
#include "nix/util/system-error.hh"

#include <cstdlib>
#include <cstring>
#ifdef __APPLE__
#  include <regex>
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#include "store-config-private.hh"

namespace nix {

static void makeWritable(const LocalStore::DirectoryInParent & dir)
{
#ifndef _WIN32
    auto st = fstatat(dir.parentFd, dir.name);
    unix::fchmodatTryNoFollow(dir.parentFd, dir.name, (st.st_mode & ~S_IFMT) | S_IWUSR);
#else
    // TODO: implement fchmodatTryNoFollow for Windows
#endif
}

struct MakeReadOnly
{
    const std::optional<LocalStore::DirectoryInParent> & dir;

    ~MakeReadOnly()
    {
        try {
            /* This will make the directory read-only. */
            if (dir)
                canonicaliseTimestampAndPermissions(dir->parentFd, dir->name);
        } catch (...) {
            ignoreExceptionInDestructor();
        }
    }
};

LocalStore::InodeHash LocalStore::loadInodeHash()
{
    debug("loading hash inodes in memory");
    InodeHash inodeHash;

    /* On Windows, directory reads report no inode numbers (and `fstat`
       sets no `st_ino` either), so there is nothing to load. */
#ifndef _WIN32
    for (const auto & entry : DirectoryIterator{reopenDirectory(linksDirFd.get())}) {
        checkInterrupt();
        // We don't care if we hit non-hash files, anything goes
        inodeHash.insert(entry.ino);
    }
#endif

    printMsg(lvlTalkative, "loaded %1% hash inodes", inodeHash.size());

    return inodeHash;
}

std::list<OsFilename> LocalStore::readDirectoryIgnoringInodes(Descriptor dirFd, const InodeHash & inodeHash)
{
    std::list<OsFilename> names;

    for (const auto & entry : DirectoryIterator{reopenDirectory(dirFd)}) {
        checkInterrupt();

#ifndef _WIN32
        if (inodeHash.count(entry.ino)) {
            debug("%1% is already linked", PathFmt(entry.name.path()));
            continue;
        }
#endif

        /* `DirectoryIterator` has already filtered out `.` and `..`. */
        names.push_back(entry.name);
    }

    return names;
}

void LocalStore::optimisePath_(
    Activity * act,
    OptimiseStats & stats,
    Descriptor dirFd,
    const OsFilename & name,
    const std::filesystem::path & path,
    const std::optional<DirectoryInParent> & dir,
    InodeHash & inodeHash,
    RepairFlag repair)
{
    checkInterrupt();

    auto st = fstatat(dirFd, name);

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
        auto childDirFd = valueOr(
            openDirectoryAt(dirFd, name), NIX_THROW_NATIVE_SYS_ERROR_HANDLER("opening directory %s", PathFmt(path)));
        auto names = readDirectoryIgnoringInodes(childDirFd.get(), inodeHash);
        std::optional<DirectoryInParent> childDir = DirectoryInParent{dirFd, name};
        for (const auto & i : names)
            optimisePath_(act, stats, childDirFd.get(), i, path / i.path(), childDir, inodeHash, repair);
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
    Hash hash =
        hashPath(makeFSSourceAccessor(dirFd, name, path), FileSerialisationMethod::NixArchive, HashAlgorithm::SHA256)
            .hash;
    debug("%s has hash '%s'", PathFmt(path), hash.to_string(HashFormat::Nix32, true));

    /* Check if this is a known hash. */
    auto linkName = OsFilename::fromPathAsserting(hash.to_string(HashFormat::Nix32, false));
    std::filesystem::path linkPath = linksDir / linkName.path();

    /* Maybe delete the link, if it has been corrupted. */
    auto stLink = maybeFstatat(linksDirFd.get(), linkName);
    if (stLink) {
        if (st.st_size != stLink->st_size
            || (repair && hash != ({
                              hashPath(
                                  makeFSSourceAccessor(linksDirFd.get(), linkName, linkPath),
                                  FileSerialisationMethod::NixArchive,
                                  HashAlgorithm::SHA256)
                                  .hash;
                          }))) {
            // XXX: Consider overwriting linkPath with our valid version.
            warn("removing corrupted link %s", PathFmt(linkPath));
            warn(
                "There may be more corrupted paths."
                "\nYou should run `nix-store --verify --check-contents --repair` to fix them all");
            unlinkAt(linksDirFd.get(), linkName);
            stLink.reset();
        }
    }

    if (!stLink) {
        /* Nope, create a hard link in the links directory. */
        try {
            createHardLinkAt(dirFd, name, linksDirFd.get(), linkName);
            inodeHash.insert(st.st_ino);
        } catch (SystemError & e) {
            if (e.is(std::errc::file_exists)) {
                /* Fall through if another process created ‘linkPath’ before
                   we did. */
            }

            else if (e.is(std::errc::no_space_on_device)) {
                /* On ext4, that probably means the directory index is
                   full.  When that happens, it's fine to ignore it: we
                   just effectively disable deduplication of this
                   file.
                   */
                printInfo("cannot link %s to %s: %s", PathFmt(linkPath), PathFmt(path), e.ec().message());
                return;
            }

            else
                throw;
        }
    }

    /* Yes!  We've seen a file with the same contents.  Replace the
       current file with a hard link to that file. */
    stLink = maybeFstatat(linksDirFd.get(), linkName);

    /* A concurrent garbage collection may have removed the link in the
       links directory between the existence check above and now. Skip
       optimising this path; a later pass will dedup it. */
    if (!stLink)
        return;

    if (st.st_ino == stLink->st_ino) {
        debug("%1% is already linked to %2%", PathFmt(path), PathFmt(linkPath));
        return;
    }

    printMsg(lvlTalkative, "linking %1% to %2%", PathFmt(path), PathFmt(linkPath));

    /* Make the containing directory writable, but only if it's not
       the store itself (we don't want or need to mess with its
       permissions), which is when `dir` is absent. */
    if (dir)
        makeWritable(*dir);

    /* When we're done, make the directory read-only again and reset
       its timestamp back to 0. */
    MakeReadOnly makeReadOnly{dir};

    auto tempName = makeTempName(".tmp-link");

    try {
        createHardLinkAt(linksDirFd.get(), linkName, getRealStoreDirFd(), tempName);
        inodeHash.insert(st.st_ino);
    } catch (SystemError & e) {
        if (e.is(std::errc::too_many_links)) {
            /* Too many links to the same file (>= 32000 on most file
               systems).  This is likely to happen with empty files.
               Just shrug and ignore. */
            if (st.st_size)
                printInfo("%1% has maximum number of links", PathFmt(linkPath));
            return;
        }
        if (e.is(std::errc::no_such_file_or_directory)) {
            /* A concurrent garbage collection removed the link in the
               links directory. Skip optimising this path; a later pass
               will dedup it. */
            return;
        }
        throw;
    }

    /* Atomically replace the old file with the new hard link. */
    try {
        movePathAt(getRealStoreDirFd(), tempName, dirFd, name);
    } catch (SystemError & e) {
        try {
            unlinkAt(getRealStoreDirFd(), tempName); /* Clean up after ourselves. */
        } catch (Error & e2) {
            printError(
                "unable to unlink %1%: %2%", PathFmt(config->realStoreDir.get() / tempName.path()), e2.info().msg);
        }
        if (e.is(std::errc::too_many_links)) {
            /* Some filesystems generate too many links on the rename,
               rather than on the original link.  (Probably it
               temporarily increases the st_nlink field before
               decreasing it again.) */
            debug("%s has reached maximum number of links", PathFmt(linkPath));
            return;
        }
        throw;
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
    InodeHash inodeHash = loadInodeHash();

    act.progress(0, paths.size());

    uint64_t done = 0;

    for (auto & i : paths) {
        addTempRoot(i);
        if (!isValidPath(i))
            continue; /* path was GC'ed, probably */
        {
            Activity act(*logger, lvlTalkative, actUnknown, fmt("optimising path '%s'", printStorePath(i)));
            optimisePath_(
                &act,
                stats,
                getRealStoreDirFd(),
                OsFilename{i.filename()},
                config->realStoreDir.get() / i.to_string(),
                std::nullopt,
                inodeHash,
                NoRepair);
        }
        done++;
        act.progress(done, paths.size());
    }
}

void LocalStore::optimiseStore()
{
    OptimiseStats stats;

    optimiseStore(stats);

    printInfo("%s freed by hard-linking %d files", renderSize(stats.bytesFreed), stats.filesLinked);
}

void LocalStore::optimisePath(const StorePath & path, RepairFlag repair)
{
    OptimiseStats stats;
    InodeHash inodeHash;

    if (config->getLocalSettings().autoOptimiseStore)
        optimisePath_(
            nullptr,
            stats,
            getRealStoreDirFd(),
            OsFilename{path.filename()},
            toRealPath(path),
            std::nullopt,
            inodeHash,
            repair);
}

} // namespace nix
