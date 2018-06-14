#include "util.hh"
#include "local-store.hh"
#include "globals.hh"

#include <cstdlib>
#include <cstring>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <regex>


namespace nix {


static void makeWritable(const Path & path)
{
    struct stat st;
    if (lstat(path.c_str(), &st))
        throw SysError(format("getting attributes of path '%1%'") % path);
    if (chmod(path.c_str(), st.st_mode | S_IWUSR) == -1)
        throw SysError(format("changing writability of '%1%'") % path);
}


struct MakeReadOnly
{
    Path path;
    MakeReadOnly(const Path & path) : path(path) { }
    ~MakeReadOnly()
    {
        try {
            /* This will make the path read-only. */
            if (path != "") canonicaliseTimestampAndPermissions(path);
        } catch (...) {
            ignoreException();
        }
    }
};


LocalStore::InodeHash LocalStore::loadInodeHash()
{
    debug("loading hash inodes in memory");
    InodeHash inodeHash;

    AutoCloseDir dir(opendir(linksDir.c_str()));
    if (!dir) throw SysError(format("opening directory '%1%'") % linksDir);

    struct dirent * dirent;
    while (errno = 0, dirent = readdir(dir.get())) { /* sic */
        checkInterrupt();
        // We don't care if we hit non-hash files, anything goes
        inodeHash.insert(dirent->d_ino);
    }
    if (errno) throw SysError(format("reading directory '%1%'") % linksDir);

    printMsg(lvlTalkative, format("loaded %1% hash inodes") % inodeHash.size());

    return inodeHash;
}


Strings LocalStore::readDirectoryIgnoringInodes(const Path & path, const InodeHash & inodeHash)
{
    Strings names;

    AutoCloseDir dir(opendir(path.c_str()));
    if (!dir) throw SysError(format("opening directory '%1%'") % path);

    struct dirent * dirent;
    while (errno = 0, dirent = readdir(dir.get())) { /* sic */
        checkInterrupt();

        if (inodeHash.count(dirent->d_ino)) {
            debug(format("'%1%' is already linked") % dirent->d_name);
            continue;
        }

        string name = dirent->d_name;
        if (name == "." || name == "..") continue;
        names.push_back(name);
    }
    if (errno) throw SysError(format("reading directory '%1%'") % path);

    return names;
}


void LocalStore::optimisePath_(Activity * act, OptimiseStats & stats,
    const Path & path, InodeHash & inodeHash)
{
    checkInterrupt();

    struct stat st;
    if (lstat(path.c_str(), &st))
        throw SysError(format("getting attributes of path '%1%'") % path);

#if __APPLE__
    /* HFS/macOS has some undocumented security feature disabling hardlinking for
       special files within .app dirs. *.app/Contents/PkgInfo and
       *.app/Contents/Resources/\*.lproj seem to be the only paths affected. See
       https://github.com/NixOS/nix/issues/1443 for more discussion. */

    if (std::regex_search(path, std::regex("\\.app/Contents/.+$")))
    {
        debug(format("'%1%' is not allowed to be linked in macOS") % path);
        return;
    }
#endif

    if (S_ISDIR(st.st_mode)) {
        Strings names = readDirectoryIgnoringInodes(path, inodeHash);
        for (auto & i : names)
            optimisePath_(act, stats, path + "/" + i, inodeHash);
        return;
    }

    /* We can hard link regular files and maybe symlinks. */
    if (!S_ISREG(st.st_mode)
#if CAN_LINK_SYMLINK
        && !S_ISLNK(st.st_mode)
#endif
        ) return;

    /* Sometimes SNAFUs can cause files in the Nix store to be
       modified, in particular when running programs as root under
       NixOS (example: $fontconfig/var/cache being modified).  Skip
       those files.  FIXME: check the modification time. */
    if (S_ISREG(st.st_mode) && (st.st_mode & S_IWUSR)) {
        printError(format("skipping suspicious writable file '%1%'") % path);
        return;
    }

    /* This can still happen on top-level files. */
    if (st.st_nlink > 1 && inodeHash.count(st.st_ino)) {
        debug(format("'%1%' is already linked, with %2% other file(s)") % path % (st.st_nlink - 2));
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
    Hash hash = hashPath(htSHA256, path).first;
    debug(format("'%1%' has hash '%2%'") % path % hash.to_string());

    /* Check if this is a known hash. */
    Path linkPath = linksDir + "/" + hash.to_string(Base32, false);

 retry:
    if (!pathExists(linkPath)) {
        /* Nope, create a hard link in the links directory. */
        if (link(path.c_str(), linkPath.c_str()) == 0) {
            inodeHash.insert(st.st_ino);
            return;
        }

        switch (errno) {
        case EEXIST:
            /* Fall through if another process created ‘linkPath’ before
               we did. */
            break;

        case ENOSPC:
            /* On ext4, that probably means the directory index is
               full.  When that happens, it's fine to ignore it: we
               just effectively disable deduplication of this
               file.  */
            printInfo("cannot link '%s' to '%s': %s", linkPath, path, strerror(errno));
            return;

        default:
            throw SysError("cannot link '%1%' to '%2%'", linkPath, path);
        }
    }

    /* Yes!  We've seen a file with the same contents.  Replace the
       current file with a hard link to that file. */
    struct stat stLink;
    if (lstat(linkPath.c_str(), &stLink))
        throw SysError(format("getting attributes of path '%1%'") % linkPath);

    if (st.st_ino == stLink.st_ino) {
        debug(format("'%1%' is already linked to '%2%'") % path % linkPath);
        return;
    }

    if (st.st_size != stLink.st_size) {
        printError(format("removing corrupted link '%1%'") % linkPath);
        unlink(linkPath.c_str());
        goto retry;
    }

    printMsg(lvlTalkative, format("linking '%1%' to '%2%'") % path % linkPath);

    /* Make the containing directory writable, but only if it's not
       the store itself (we don't want or need to mess with its
       permissions). */
    bool mustToggle = dirOf(path) != realStoreDir;
    if (mustToggle) makeWritable(dirOf(path));

    /* When we're done, make the directory read-only again and reset
       its timestamp back to 0. */
    MakeReadOnly makeReadOnly(mustToggle ? dirOf(path) : "");

    Path tempLink = (format("%1%/.tmp-link-%2%-%3%")
        % realStoreDir % getpid() % random()).str();

    if (link(linkPath.c_str(), tempLink.c_str()) == -1) {
        if (errno == EMLINK) {
            /* Too many links to the same file (>= 32000 on most file
               systems).  This is likely to happen with empty files.
               Just shrug and ignore. */
            if (st.st_size)
                printInfo(format("'%1%' has maximum number of links") % linkPath);
            return;
        }
        throw SysError("cannot link '%1%' to '%2%'", tempLink, linkPath);
    }

    /* Atomically replace the old file with the new hard link. */
    if (rename(tempLink.c_str(), path.c_str()) == -1) {
        if (unlink(tempLink.c_str()) == -1)
            printError(format("unable to unlink '%1%'") % tempLink);
        if (errno == EMLINK) {
            /* Some filesystems generate too many links on the rename,
               rather than on the original link.  (Probably it
               temporarily increases the st_nlink field before
               decreasing it again.) */
            debug("'%s' has reached maximum number of links", linkPath);
            return;
        }
        throw SysError(format("cannot rename '%1%' to '%2%'") % tempLink % path);
    }

    stats.filesLinked++;
    stats.bytesFreed += st.st_size;
    stats.blocksFreed += st.st_blocks;

    if (act)
        act->result(resFileLinked, st.st_size, st.st_blocks);
}


void LocalStore::optimiseStore(OptimiseStats & stats)
{
    Activity act(*logger, actOptimiseStore);

    PathSet paths = queryAllValidPaths();
    InodeHash inodeHash = loadInodeHash();

    act.progress(0, paths.size());

    uint64_t done = 0;

    for (auto & i : paths) {
        addTempRoot(i);
        if (!isValidPath(i)) continue; /* path was GC'ed, probably */
        {
            Activity act(*logger, lvlTalkative, actUnknown, fmt("optimising path '%s'", i));
            optimisePath_(&act, stats, realStoreDir + "/" + baseNameOf(i), inodeHash);
        }
        done++;
        act.progress(done, paths.size());
    }
}

static string showBytes(unsigned long long bytes)
{
    return (format("%.2f MiB") % (bytes / (1024.0 * 1024.0))).str();
}

void LocalStore::optimiseStore()
{
    OptimiseStats stats;

    optimiseStore(stats);

    printInfo(
        format("%1% freed by hard-linking %2% files")
        % showBytes(stats.bytesFreed)
        % stats.filesLinked);
}

void LocalStore::optimisePath(const Path & path)
{
    OptimiseStats stats;
    InodeHash inodeHash;

    if (settings.autoOptimiseStore) optimisePath_(nullptr, stats, path, inodeHash);
}


}
