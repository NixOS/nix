#include "nix/store/local-store.hh"
#include "nix/store/local-settings.hh"
#include "nix/util/signals.hh"
#include "nix/store/posix-fs-canonicalise.hh"
#include "nix/util/posix-source-accessor.hh"
#include "nix/util/file-system.hh"

#include <cassert>
#include <cstdlib>
#include <cstring>
#ifdef __APPLE__
#  include <regex>
#endif

#include <dirent.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#include <boost/unordered/unordered_flat_set.hpp>

#include "store-config-private.hh"

namespace nix {

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

/* True for "." and ".." without allocating a std::string. */
static bool isDotEntry(const char * name)
{
    return name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'));
}

/* Credit a just-opened directory's inode toward `fullWalk` exactly once.
   Used by callers that opened the directory via opendir() rather than
   reaching it through the recursive lstat() in walkSubtree. */
static void creditOpenedDirInode(
    boost::unordered_flat_set<ino_t> & seenInodes,
    Store::ContentStats::FullWalk & walk,
    int fd,
    const std::filesystem::path & path)
{
    struct stat st;
    if (fstat(fd, &st))
        throw SysError("fstat'ing '%1%'", PathFmt(path));
    if (!seenInodes.insert(st.st_ino).second)
        return;
    walk.dirInodes++;
    walk.totalDiskBytes += uint64_t(st.st_blocks) * 512;
}

void LocalStore::scanLinks(OptimiseStats & stats, WalkState & state, bool computeHistograms)
{
    debug("scanning .links/");

    auto & dedup = stats.dedup;
    auto & walk = stats.fullWalk;
    auto & inodeHash = state.inodeHash;
    auto & seenInodes = state.seenInodes;

    /* Tolerate the directory not existing (fresh store). */
    AutoCloseDir linksHandle(opendir(linksDir.string().c_str()));
    if (!linksHandle) {
        if (errno == ENOENT)
            return;
        throw SysError("opening directory %1%", PathFmt(linksDir));
    }

    int fd = dirfd(linksHandle.get());
    creditOpenedDirInode(seenInodes, walk, fd, linksDir);

    struct dirent * e;
    while (errno = 0, e = readdir(linksHandle.get())) {
        checkInterrupt();
        if (isDotEntry(e->d_name))
            continue;
        struct stat est;
        if (fstatat(fd, e->d_name, &est, AT_SYMLINK_NOFOLLOW)) {
            /* ENOENT here means the entry was unlinked between
               readdir and fstatat — likely a concurrent GC of an
               unreferenced `.links/` entry. Skip and move on. */
            if (errno == ENOENT)
                continue;
            throw SysError("statting '%1%/%2%'", PathFmt(linksDir), e->d_name);
        }
        if (!S_ISREG(est.st_mode))
            continue;

        inodeHash.insert(est.st_ino);
        if (!seenInodes.insert(est.st_ino).second)
            continue;

        walk.fileInodes++;
        uint64_t diskBytes = uint64_t(est.st_blocks) * 512;
        walk.totalDiskBytes += diskBytes;

        dedup.linksFileCount++;
        dedup.uniqueBytes += est.st_size;
        dedup.uniqueDiskBytes += diskBytes;
        /* .links entry: nlink = 1 (itself) + one per store file
           sharing its content; real dedup starts at nlink > 2. */
        if (est.st_nlink > 2) {
            uint64_t extraCopies = est.st_nlink - 2;
            dedup.dedupedFileCount++;
            dedup.inodesSaved += extraCopies;
            dedup.dedupBytes += extraCopies * uint64_t(est.st_size);
            dedup.dedupDiskBytes += extraCopies * diskBytes;
        }
        if (computeHistograms)
            dedup.sizeHistogram[Store::ContentStats::bucket(est.st_size)]++;
    }
    if (errno)
        throw SysError("reading directory %1%", PathFmt(linksDir));

    printMsg(lvlTalkative, "scanned %1% .links/ entries", dedup.linksFileCount);
}

void LocalStore::walkSubtree(
    Activity * act,
    OptimiseStats & stats,
    const std::filesystem::path & path,
    WalkState & state,
    OptimiseState * opt,
    RepairFlag repair)
{
    checkInterrupt();

    auto & inodeHash = state.inodeHash;
    auto & seenInodes = state.seenInodes;

    /* Raw `::lstat` rather than the throwing `nix::lstat` wrapper:
       a valid path can disappear between `queryAllValidPaths` and
       this call (concurrent GC), and we want to skip rather than
       fail the whole walk. */
    struct stat st;
    if (::lstat(path.c_str(), &st)) {
        if (errno == ENOENT)
            return;
        throw SysError("statting '%1%'", PathFmt(path));
    }

    /* The Nix store only ever contains regular files, directories,
       and symlinks. Any other type (device node, fifo, socket, etc.)
       represents corruption or a bug in whatever wrote the path. */
    assert(S_ISREG(st.st_mode) || S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode));

    /* Credit this inode toward `fullWalk` exactly once. `nlink <= 1`
       (the typical case for files just written, plus the edge case
       of an inode being unlinked while still open) needs no
       deduplication — it has no other referrers — so it always
       counts as first sighting. Otherwise the set guards against
       re-crediting hard-linked siblings. The else-branch asserts
       S_ISLNK so a non-reg/dir/link slipping past the top-level
       assert in a release build is caught here rather than
       silently mis-bucketed. */
    bool firstSighting = st.st_nlink <= 1 || seenInodes.insert(st.st_ino).second;
    if (firstSighting) {
        stats.fullWalk.totalDiskBytes += uint64_t(st.st_blocks) * 512;
        if (S_ISREG(st.st_mode))
            stats.fullWalk.fileInodes++;
        else if (S_ISDIR(st.st_mode))
            stats.fullWalk.dirInodes++;
        else {
            assert(S_ISLNK(st.st_mode));
            stats.fullWalk.symlinkInodes++;
        }
    }

    if (S_ISDIR(st.st_mode)) {
        AutoCloseDir dir(opendir(path.string().c_str()));
        if (!dir) {
            if (errno == ENOENT)
                return;
            throw SysError("opening directory %s", PathFmt(path));
        }
        struct dirent * dirent;
        while (errno = 0, dirent = readdir(dir.get())) {
            /* Check per dirent so the user can interrupt a directory
               with many `.links/`-skipped entries; the recursive
               `walkSubtree` only fires `checkInterrupt` for entries
               we recurse into. */
            checkInterrupt();
            if (isDotEntry(dirent->d_name))
                continue;
            /* Fast-path: file already hard-linked into `.links/` was
               credited by `scanLinks`; skip the lstat-and-bail dance. */
            if (inodeHash.count(dirent->d_ino))
                continue;
            walkSubtree(act, stats, path / dirent->d_name, state, opt, repair);
        }
        if (errno)
            throw SysError("reading directory %s", PathFmt(path));
        return;
    }

    /* From here down is optimise-only: hashing and linking. Garbage
       subtrees just want their inodes credited, which the section
       above already did. */
    if (!opt)
        return;

    /* A second occurrence of the same inode in this walk doesn't need
       to be hashed: the first sighting already decided the outcome
       for that inode — in wet mode it created `.links/<hash>` (or
       hard-linked the file to an existing one) and inserted the new
       inode into `inodeHash`; in dry-run mode it recorded the hash
       in `opt->seenHashes` and either credited `predictedDedup` or
       not. Either way, repeating the work for a sibling sharing the
       same inode is redundant — and in dry-run mode would
       double-count the inode in `predictedDedup`. */
    if (!firstSighting)
        return;

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

    if (stats.predicted) {
        /* This branch sees one occurrence per inode (the firstSighting
           short-circuit above filters subsequent same-inode entries).
           So a hash already in `seenHashes` always means a different
           inode with identical content — optimise would replace it
           with a hard link. A pre-existing `.links/<hash>` is the
           other linkable case; the file under examination must hold
           a different inode from the `.links/` entry, because the
           dirent-level skip on `inodeHash` (for recursed entries) and
           the `nlink > 1 && inodeHash.count` guard above (for the
           top-level dispatch) already filtered files pointing at the
           `.links/<hash>` inode. */
        bool wouldLink = !opt->seenHashes.insert(hash).second || pathExists(linkPath);
        if (wouldLink) {
            stats.predicted->filesLinkable++;
            stats.predicted->bytesLinkable += st.st_size;
        }
        return;
    }

    /* Maybe delete the link, if it has been corrupted. */
    if (pathExists(linkPath)) {
        auto stLink = lstat(linkPath);
        if (st.st_size != stLink.st_size || (repair && hash != ({
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
        }
    }

    if (!pathExists(linkPath)) {
        /* Nope, create a hard link in the links directory. */
        try {
            std::filesystem::create_hard_link(path, linkPath);
            inodeHash.insert(st.st_ino);
        } catch (std::filesystem::filesystem_error & e) {
            if (e.code() == std::errc::file_exists) {
                /* Fall through if another process created ‘linkPath’ before
                   we did. */
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
    auto stLink = lstat(linkPath);

    if (st.st_ino == stLink.st_ino) {
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

    try {
        std::filesystem::create_hard_link(linkPath, tempLink);
        inodeHash.insert(st.st_ino);
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
    } catch (std::filesystem::filesystem_error & e) {
        {
            std::error_code ec;
            remove(tempLink, ec); /* Clean up after ourselves. */
            if (ec)
                printError("unable to unlink %1%: %2%", PathFmt(tempLink), ec.message());
        }
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

    /* The rename above replaced this dirent's inode (the one we
       lstat'd at the top) with `linkPath`'s inode. When the old
       inode had no other referrers (`st.st_nlink == 1`), it is now
       orphaned and its blocks are freed; the credit we gave
       `fullWalk` at the top of this call is no longer accurate.
       The new inode (which `linkPath` shares) was either credited
       by `scanLinks` (for a pre-existing `.links/` entry) or
       credited at the top of this very call (when the
       `create_hard_link` branch above turned this file into the
       canonical `.links/<hash>`) — either way no recredit is
       needed. The `firstSighting` assert documents that we only
       reach this code on the first-credit path. */
    if (st.st_nlink == 1) {
        assert(firstSighting);
        stats.fullWalk.totalDiskBytes -= uint64_t(st.st_blocks) * 512;
        if (S_ISREG(st.st_mode))
            stats.fullWalk.fileInodes--;
        else {
            assert(S_ISLNK(st.st_mode));
            stats.fullWalk.symlinkInodes--;
        }
    }

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

void LocalStore::optimiseStore(OptimiseStats & stats, bool dryRun, bool computeHistograms)
{
    assert(!stats.predicted);
    if (dryRun)
        stats.predicted.emplace();

    /* The actOptimiseStore renderer is wet-mode-specific (it shows
       bytes/inodes freed); dry-run uses actUnknown instead. */
    Activity act(*logger, dryRun ? actUnknown : actOptimiseStore);

    /* One SQL query for the set of valid-path basenames. The walk
       below dispatches on basename-match: valid paths get hashed,
       everything else under the store dir is treated as garbage. */
    auto paths = queryAllValidPaths();
    boost::unordered_flat_set<std::string> validNames;
    validNames.reserve(paths.size());
    for (auto & p : paths)
        validNames.emplace(p.to_string());

    InodeHash inodeHash;
    boost::unordered_flat_set<ino_t> seenInodes;
    WalkState state{inodeHash, seenInodes};
    OptimiseState opt;

    const auto & realStore = config->realStoreDir.get();

    AutoCloseDir storeHandle(opendir(realStore.c_str()));
    if (!storeHandle) {
        if (errno == ENOENT)
            return;
        throw SysError("opening directory %1%", PathFmt(realStore));
    }
    creditOpenedDirInode(seenInodes, stats.fullWalk, dirfd(storeHandle.get()), realStore);

    /* Two-pass: collect all top-level entries up front, then dispatch
       in two phases (`.links/` first so `inodeHash` is populated
       before any other entry is walked). The cost of materialising
       the entry list is O(top-level entries) basenames; a typical
       store has tens of thousands. */
    std::vector<std::string> entries;
    bool sawLinks = false;
    struct dirent * e;
    while (errno = 0, e = readdir(storeHandle.get())) {
        checkInterrupt();
        if (isDotEntry(e->d_name))
            continue;
        if (std::string_view{e->d_name} == ".links") {
            sawLinks = true;
            continue;
        }
        entries.emplace_back(e->d_name);
    }
    if (errno)
        throw SysError("reading directory %1%", PathFmt(realStore));

    if (sawLinks)
        scanLinks(stats, state, computeHistograms);

    /* Rough headroom: typical stores have more not-yet-linked inodes
       than `.links/` entries. Avoids a few rehashes during dispatch;
       not load-bearing. */
    seenInodes.reserve(seenInodes.size() * 2);

    /* Dispatch every remaining top-level entry. Valid paths get the
       full optimise/predict treatment; anything else is garbage and
       gets only its `fullWalk` credit. */
    act.progress(0, paths.size());
    uint64_t done = 0;
    for (auto & name : entries) {
        bool isValid = validNames.contains(name);
        auto subtree = realStore / name;
        if (isValid) {
            auto storePath = StorePath{name};
            /* Wet optimise mutates files (replacing inodes with
               hard links); pin a temp root so the GC can't yank the
               path mid-link. Dry-run only reads, and `walkSubtree`
               returns on ENOENT if a path disappears under us, so
               no temp root is needed there — which keeps stats
               available without write access to
               `/nix/var/nix/temproots/`. */
            if (!dryRun)
                addTempRoot(storePath);
            Activity act2(
                *logger,
                lvlTalkative,
                actUnknown,
                fmt("%s path '%s'", dryRun ? "scanning" : "optimising", printStorePath(storePath)));
            walkSubtree(&act2, stats, subtree, state, &opt, NoRepair);
            done++;
            act.progress(done, paths.size());
        } else {
            walkSubtree(nullptr, stats, subtree, state, /*opt=*/nullptr, NoRepair);
        }
    }
}

void LocalStore::optimiseStore()
{
    OptimiseStats stats;

    optimiseStore(stats);

    notice(
        "previously saved by hard-linking: %s across %d duplicate copies",
        renderSize(stats.dedup.dedupBytes),
        stats.dedup.inodesSaved);
    notice("%s freed by hard-linking %d files this run", renderSize(stats.bytesFreed), stats.filesLinked);
}

void LocalStore::optimisePath(const std::filesystem::path & path, RepairFlag repair)
{
    if (!config->getLocalSettings().autoOptimiseStore)
        return;

    /* Single-path autoOptimise during AddToStore: we don't share
       state with any other walk, so `inodeHash`/`seenInodes`/`opt`
       are constructed empty just to satisfy walkSubtree's signature.
       The resulting `stats` is discarded — autoOptimise only cares
       about the linking side effect. */
    OptimiseStats stats;
    InodeHash inodeHash;
    boost::unordered_flat_set<ino_t> seenInodes;
    WalkState state{inodeHash, seenInodes};
    OptimiseState opt;
    walkSubtree(nullptr, stats, path, state, &opt, repair);
}

} // namespace nix
