#include "util.hh"
#include "local-store.hh"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>


namespace nix {


typedef std::map<Hash, std::pair<Path, ino_t> > HashToPath;


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
            if (path != "") canonicalisePathMetaData(path, false);
        } catch (...) {
            ignoreException();
        }
    }
};


static void hashAndLink(bool dryRun, HashToPath & hashToPath,
    OptimiseStats & stats, const Path & path)
{
    struct stat st;
    if (lstat(path.c_str(), &st))
	throw SysError(format("getting attributes of path `%1%'") % path);

    /* Sometimes SNAFUs can cause files in the Nix store to be
       modified, in particular when running programs as root under
       NixOS (example: $fontconfig/var/cache being modified).  Skip
       those files. */
    if (S_ISREG(st.st_mode) && (st.st_mode & S_IWUSR)) {
        printMsg(lvlError, format("skipping suspicious writable file `%1%'") % path);
        return;
    }

    /* We can hard link regular files and symlinks. */
    if (S_ISREG(st.st_mode) || S_ISLNK(st.st_mode)) {

        /* Hash the file.  Note that hashPath() returns the hash over
           the NAR serialisation, which includes the execute bit on
           the file.  Thus, executable and non-executable files with
           the same contents *won't* be linked (which is good because
           otherwise the permissions would be screwed up).

           Also note that if `path' is a symlink, then we're hashing
           the contents of the symlink (i.e. the result of
           readlink()), not the contents of the target (which may not
           even exist). */
        Hash hash = hashPath(htSHA256, path);
        stats.totalFiles++;
        printMsg(lvlDebug, format("`%1%' has hash `%2%'") % path % printHash(hash));

        std::pair<Path, ino_t> prevPath = hashToPath[hash];
        
        if (prevPath.first == "") {
            hashToPath[hash] = std::pair<Path, ino_t>(path, st.st_ino);
            return;
        }
            
        /* Yes!  We've seen a file with the same contents.  Replace
           the current file with a hard link to that file. */
        stats.sameContents++;
        if (prevPath.second == st.st_ino) {
            printMsg(lvlDebug, format("`%1%' is already linked to `%2%'") % path % prevPath.first);
            return;
        }
        
        if (!dryRun) {
            
            printMsg(lvlTalkative, format("linking `%1%' to `%2%'") % path % prevPath.first);

            Path tempLink = (format("%1%.tmp-%2%-%3%")
                % path % getpid() % rand()).str();

            /* Make the containing directory writable, but only if
               it's not the store itself (we don't want or need to
               mess with  its permissions). */
            bool mustToggle = !isStorePath(path);
            if (mustToggle) makeWritable(dirOf(path));
            
            /* When we're done, make the directory read-only again and
               reset its timestamp back to 0. */
            MakeReadOnly makeReadOnly(mustToggle ? dirOf(path) : "");
        
            if (link(prevPath.first.c_str(), tempLink.c_str()) == -1) {
                if (errno == EMLINK) {
                    /* Too many links to the same file (>= 32000 on
                       most file systems).  This is likely to happen
                       with empty files.  Just start over, creating
                       links to the current file. */
                    printMsg(lvlInfo, format("`%1%' has maximum number of links") % prevPath.first);
                    hashToPath[hash] = std::pair<Path, ino_t>(path, st.st_ino);
                    return;
                }
                throw SysError(format("cannot link `%1%' to `%2%'")
                    % tempLink % prevPath.first);
            }

            /* Atomically replace the old file with the new hard link. */
            if (rename(tempLink.c_str(), path.c_str()) == -1) {
                if (errno == EMLINK) {
                    /* Some filesystems generate too many links on the
                       rename, rather than on the original link.
                       (Probably it temporarily increases the st_nlink
                       field before decreasing it again.) */
                    printMsg(lvlInfo, format("`%1%' has maximum number of links") % prevPath.first);
                    hashToPath[hash] = std::pair<Path, ino_t>(path, st.st_ino);

                    /* Unlink the temp link. */
                    if (unlink(tempLink.c_str()) == -1)
                        printMsg(lvlError, format("unable to unlink `%1%'") % tempLink);
                    return;
                }
                throw SysError(format("cannot rename `%1%' to `%2%'")
                    % tempLink % path);
            }
        } else
            printMsg(lvlTalkative, format("would link `%1%' to `%2%'") % path % prevPath.first);
        
        stats.filesLinked++;
        stats.bytesFreed += st.st_size;
        stats.blocksFreed += st.st_blocks;
    }

    if (S_ISDIR(st.st_mode)) {
        Strings names = readDirectory(path);
	foreach (Strings::iterator, i, names)
	    hashAndLink(dryRun, hashToPath, stats, path + "/" + *i);
    }
}


void LocalStore::optimiseStore(bool dryRun, OptimiseStats & stats)
{
    HashToPath hashToPath;

    PathSet paths = queryValidPaths();

    foreach (PathSet::iterator, i, paths) {
        addTempRoot(*i);
        if (!isValidPath(*i)) continue; /* path was GC'ed, probably */
        startNest(nest, lvlChatty, format("hashing files in `%1%'") % *i);
        hashAndLink(dryRun, hashToPath, stats, *i);
    }
}


}
