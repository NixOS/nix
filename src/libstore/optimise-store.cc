#include "config.h"

#include "util.hh"
#include "local-store.hh"
#include "globals.hh"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>


namespace nix {


static void makeWritable(const Path & path)
{
    struct stat st;
    if (lstat(path.c_str(), &st))
        throw SysError(format("getting attributes of path `%1%'") % path);
    if (chmod(path.c_str(), st.st_mode | S_IWUSR) == -1)
        throw SysError(format("changing writability of `%1%'") % path);
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


void LocalStore::optimisePath_(OptimiseStats & stats, const Path & path)
{
    checkInterrupt();
    
    struct stat st;
    if (lstat(path.c_str(), &st))
        throw SysError(format("getting attributes of path `%1%'") % path);

    if (S_ISDIR(st.st_mode)) {
        Strings names = readDirectory(path);
        foreach (Strings::iterator, i, names)
            optimisePath_(stats, path + "/" + *i);
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
        printMsg(lvlError, format("skipping suspicious writable file `%1%'") % path);
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
    stats.totalFiles++;
    printMsg(lvlDebug, format("`%1%' has hash `%2%'") % path % printHash(hash));

    /* Check if this is a known hash. */
    Path linkPath = linksDir + "/" + printHash32(hash);

    if (!pathExists(linkPath)) {
        /* Nope, create a hard link in the links directory. */
        if (link(path.c_str(), linkPath.c_str()) == 0) return;
        if (errno != EEXIST)
            throw SysError(format("cannot link `%1%' to `%2%'") % linkPath % path);
        /* Fall through if another process created ‘linkPath’ before
           we did. */
    }

    /* Yes!  We've seen a file with the same contents.  Replace the
       current file with a hard link to that file. */
    struct stat stLink;
    if (lstat(linkPath.c_str(), &stLink))
        throw SysError(format("getting attributes of path `%1%'") % linkPath);

    stats.sameContents++;
    if (st.st_ino == stLink.st_ino) {
        printMsg(lvlDebug, format("`%1%' is already linked to `%2%'") % path % linkPath);
        return;
    }

    printMsg(lvlTalkative, format("linking `%1%' to `%2%'") % path % linkPath);

    /* Make the containing directory writable, but only if it's not
       the store itself (we don't want or need to mess with its
       permissions). */
    bool mustToggle = !isStorePath(path);
    if (mustToggle) makeWritable(dirOf(path));

    /* When we're done, make the directory read-only again and reset
       its timestamp back to 0. */
    MakeReadOnly makeReadOnly(mustToggle ? dirOf(path) : "");

    Path tempLink = (format("%1%/.tmp-link-%2%-%3%")
        % settings.nixStore % getpid() % rand()).str();

    if (link(linkPath.c_str(), tempLink.c_str()) == -1) {
        if (errno == EMLINK) {
            /* Too many links to the same file (>= 32000 on most file
               systems).  This is likely to happen with empty files.
               Just shrug and ignore. */
            if (st.st_size)
                printMsg(lvlInfo, format("`%1%' has maximum number of links") % linkPath);
            return;
        }
        throw SysError(format("cannot link `%1%' to `%2%'") % tempLink % linkPath);
    }

    /* Atomically replace the old file with the new hard link. */
    if (rename(tempLink.c_str(), path.c_str()) == -1) {
        if (unlink(tempLink.c_str()) == -1)
            printMsg(lvlError, format("unable to unlink `%1%'") % tempLink);
        if (errno == EMLINK) {
            /* Some filesystems generate too many links on the rename,
               rather than on the original link.  (Probably it
               temporarily increases the st_nlink field before
               decreasing it again.) */
            if (st.st_size)
                printMsg(lvlInfo, format("`%1%' has maximum number of links") % linkPath);
            return;
        }
        throw SysError(format("cannot rename `%1%' to `%2%'") % tempLink % path);
    }

    stats.filesLinked++;
    stats.bytesFreed += st.st_size;
    stats.blocksFreed += st.st_blocks;
}


void LocalStore::optimiseStore(OptimiseStats & stats)
{
    PathSet paths = queryAllValidPaths();

    foreach (PathSet::iterator, i, paths) {
        addTempRoot(*i);
        if (!isValidPath(*i)) continue; /* path was GC'ed, probably */
        startNest(nest, lvlChatty, format("hashing files in `%1%'") % *i);
        optimisePath_(stats, *i);
    }
}


void LocalStore::optimisePath(const Path & path)
{
    OptimiseStats stats;
    if (settings.autoOptimiseStore) optimisePath_(stats, path);
}


}
