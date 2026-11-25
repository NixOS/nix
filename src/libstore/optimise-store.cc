#include "nix/store/local-store.hh"
#include "nix/store/local-settings.hh"
#include "nix/util/signals.hh"
#include "nix/store/posix-fs-canonicalise.hh"
#include "nix/util/source-accessor.hh"
#include "nix/util/file-system.hh"
#include "nix/util/system-error.hh"
#include "nix/util/thread-pool.hh"

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

std::list<OsFilename>
LocalStore::readDirectoryIgnoringInodes(const std::filesystem::path & path, const Sync<InodeHash> & inodeHash)
{
    std::list<OsFilename> names;

    AutoCloseDir dir(opendir(path.string().c_str()));
    if (!dir)
        throw SysError("opening directory %s", PathFmt(path));

    struct dirent * dirent;
    while (errno = 0, dirent = readdir(dir.get())) { /* sic */
        checkInterrupt();

        if (inodeHash.readLock()->count(dirent->d_ino)) {
            debug("'%1%' is already linked", dirent->d_name);
            continue;
        }

        std::string name = dirent->d_name;
        if (name == "." || name == "..")
            continue;
        /* Having just filtered out `.` and `..`, the `OsFilename` invariants
           hold, so the return type can carry them to callers. */
        names.push_back(OsFilename::fromPathAsserting(std::move(name)));
    }
    if (errno)
        throw SysError("reading directory %s", PathFmt(path));

    return names;
}

void LocalStore::optimisePath_(
    Activity * act,
    OptimiseStats & stats,
    const std::filesystem::path & path,
    Sync<InodeHash> & inodeHash,
    RepairFlag repair,
    const std::filesystem::path & tempDir)
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
        auto names = readDirectoryIgnoringInodes(path, inodeHash);
        for (const auto & i : names)
            optimisePath_(act, stats, path / i.path(), inodeHash, repair, tempDir);
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
    if (st.st_nlink > 1 && inodeHash.readLock()->count(st.st_ino)) {
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

    /* Maybe delete the link, if it has been corrupted. */
    if (auto stLink = maybeLstat(linkPath)) {
        if (st.st_size != stLink->st_size || (repair && hash != ({
                                                            hashPath(
                                                                makeFSSourceAccessor(linkPath),
                                                                FileSerialisationMethod::NixArchive,
                                                                HashAlgorithm::SHA256)
                                                                .hash;
                                                        }))) {
            warn("removing corrupted link %s", PathFmt(linkPath));
            warn(
                "There may be more corrupted paths."
                "\nYou should run `nix-store --verify --check-contents --repair` to fix them all");
            auto [replacementDir, replacementDirFd] = createTempDirInStore();
            AutoDelete delReplacementDir(replacementDir);
            auto replacement = makeTempPath(replacementDir, ".tmp-link");
            AutoDelete delReplacement(replacement, false);
            std::filesystem::create_hard_link(path, replacement);
            std::filesystem::rename(replacement, linkPath);
        }
    }

    if (!pathExists(linkPath)) {
        /* Nope, create a hard link in the links directory. */
        try {
            std::filesystem::create_hard_link(path, linkPath);
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
    auto stLink = maybeLstat(linkPath);

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
       permissions). */
    const auto dirOfPath = path.parent_path();
    bool mustToggle = dirOfPath != config->realStoreDir.get();
    if (mustToggle)
        makeWritable(dirOfPath);

    /* When we're done, make the directory read-only again and reset
       its timestamp back to 0. */
    MakeReadOnly makeReadOnly(mustToggle ? dirOfPath : std::filesystem::path{});

    std::filesystem::path tempLink =
        makeTempPath(tempDir.empty() ? std::filesystem::path{config->realStoreDir.get()} : tempDir, ".tmp-link");
    AutoDelete delTempLink(tempLink, false);
    ino_t linkedInode;

    try {
        std::filesystem::create_hard_link(linkPath, tempLink);
        linkedInode = lstat(tempLink).st_ino;
    } catch (std::filesystem::filesystem_error & e) {
        if (e.code() == std::errc::too_many_links) {
            /* Too many links to the same file (>= 32000 on most file
               systems).  This is likely to happen with empty files.
               Just shrug and ignore. */
            if (st.st_size)
                printInfo("%1% has maximum number of links", PathFmt(linkPath));
            return;
        }
        if (e.code() == std::errc::no_such_file_or_directory) {
            /* A concurrent garbage collection removed the link in the
               links directory. Skip optimising this path; a later pass
               will dedup it. */
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

    inodeHash.lock()->insert(linkedInode);
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
    if (paths.empty())
        return;
    addTempRoots(paths);
    Sync<InodeHash> inodeHash(loadInodeHash());
    auto [tempDir, tempDirFd] = createTempDirInStore();
    AutoDelete delTempDir(tempDir);

    act.progress(0, paths.size());

    Sync<uint64_t> done{0};
    Sync<StorePathSet::const_iterator> next(paths.cbegin());
    std::atomic_bool stopped{false};
    auto jobs = std::min(paths.size(), std::max<size_t>(1, config->getLocalSettings().optimiseStoreJobs.get()));
    ThreadPool pool(jobs);

    for (size_t worker = 0; worker < 2 * jobs; ++worker) {
        try {
            pool.enqueue([&] {
                try {
                    while (!stopped) {
                        checkInterrupt();
                        auto path = [&]() -> std::optional<StorePath> {
                            auto current = next.lock();
                            if (*current == paths.cend())
                                return std::nullopt;
                            return *(*current)++;
                        }();
                        if (!path)
                            return;
                        if (!isValidPath(*path))
                            continue; /* path was GC'ed, probably */
                        Activity actPath(
                            *logger,
                            lvlTalkative,
                            actUnknown,
                            fmt("optimising path '%s'", printStorePath(*path)),
                            {},
                            act.id);
                        OptimiseStats pathStats;
                        optimisePath_(
                            &actPath,
                            pathStats,
                            config->realStoreDir.get() / path->to_string(),
                            inodeHash,
                            NoRepair,
                            tempDir);
                        auto completed = done.lock();
                        stats.filesLinked += pathStats.filesLinked;
                        stats.bytesFreed += pathStats.bytesFreed;
                        act.progress(++*completed, paths.size());
                    }
                } catch (...) {
                    stopped = true;
                    throw;
                }
            });
        } catch (ThreadPoolShutDown &) {
            break;
        }
    }

    pool.process();
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
    Sync<InodeHash> inodeHash;

    if (config->getLocalSettings().autoOptimiseStore)
        optimisePath_(nullptr, stats, path, inodeHash, repair);
}

} // namespace nix
