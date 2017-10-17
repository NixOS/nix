#include "primops.hh"
#include "eval-inline.hh"
#include "download.hh"
#include "store-api.hh"
#include "pathlocks.hh"

#include <sys/time.h>

#include <regex>

namespace nix {

Path exportGit(EvalState & state, const std::string & uri,
    const std::string & ref, const std::string & rev)
{
    auto store = state.store;
    if (rev != "") {
        std::regex revRegex("^[0-9a-fA-F]{40}$");
        if (!std::regex_match(rev, revRegex))
            throw Error("invalid Git revision '%s'", rev);
    }

    auto refLookup = state.validGitRefCache.find(ref);
    if (refLookup == state.validGitRefCache.end()) {
        try {
            runProgram("git", true, { "check-ref-format", "--allow-onelevel", ref });
            refLookup = state.validGitRefCache.emplace(ref, true).first;
        } catch (ExecError & e) {
            if (WIFSIGNALED(e.status)) {
                throw;
            }
            refLookup = state.validGitRefCache.emplace(ref, false).first;
        }
    }
    if (!refLookup->second) {
        throw Error("invalid Git ref '%s'", ref);
    }

    Path cacheDir = getCacheDir() + "/nix/git";

    if (!pathExists(cacheDir)) {
        createDirs(cacheDir);
        runProgram("git", true, { "init", "--bare", cacheDir });
    }

    //Activity act(*logger, lvlInfo, format("fetching Git repository '%s'") % uri);

    std::string localRef = hashString(htSHA256, fmt("%s-%s", uri, ref)).to_string(Base32, false);

    Path localRefFile = cacheDir + "/refs/heads/" + localRef;

    /* If the local ref is older than ‘tarball-ttl’ seconds, do a git
       fetch to update the local ref to the remote ref. */
    time_t now = time(0);
    struct stat st;
    if (stat(localRefFile.c_str(), &st) != 0 ||
        st.st_mtime < now - settings.tarballTtl)
    {
        runProgram("git", true, { "-C", cacheDir, "fetch", "--force", "--", uri, ref + ":" + localRef });

        struct timeval times[2];
        times[0].tv_sec = now;
        times[0].tv_usec = 0;
        times[1].tv_sec = now;
        times[1].tv_usec = 0;

        utimes(localRefFile.c_str(), times);
    }

    // FIXME: check whether rev is an ancestor of ref.
    std::string commitHash =
        rev != "" ? rev : chomp(readFile(localRefFile));

    printTalkative("using revision %s of repo '%s'", uri, commitHash);

    Path storeLink = cacheDir + "/" + commitHash + ".link";
    PathLocks storeLinkLock({storeLink}, fmt("waiting for lock on '%1%'...", storeLink));

    if (pathExists(storeLink)) {
        auto storePath = readLink(storeLink);
        store->addTempRoot(storePath);
        if (store->isValidPath(storePath)) {
            return storePath;
        }
    }

    // FIXME: should pipe this, or find some better way to extract a
    // revision.
    auto tar = runProgram("git", true, { "-C", cacheDir, "archive", commitHash });

    Path tmpDir = createTempDir();
    AutoDelete delTmpDir(tmpDir, true);

    runProgram("tar", true, { "x", "-C", tmpDir }, tar);

    auto storePath = store->addToStore("git-export", tmpDir);

    replaceSymlink(storePath, storeLink);

    return storePath;
}

static void prim_fetchgit(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    // FIXME: cut&paste from fetch().
    if (state.restricted) throw Error("'fetchgit' is not allowed in restricted mode");

    std::string url;
    std::string ref = "master";
    std::string rev;

    state.forceValue(*args[0]);

    if (args[0]->type == tAttrs) {

        state.forceAttrs(*args[0], pos);

        for (auto & attr : *args[0]->attrs) {
            string name(attr.name);
            if (name == "url") {
                PathSet context;
                url = state.coerceToString(*attr.pos, *attr.value, context, false, false);
                if (hasPrefix(url, "/")) url = "file://" + url;
            }
            else if (name == "ref")
                ref = state.forceStringNoCtx(*attr.value, *attr.pos);
            else if (name == "rev")
                rev = state.forceStringNoCtx(*attr.value, *attr.pos);
            else
                throw EvalError("unsupported argument '%s' to 'fetchgit', at %s", attr.name, *attr.pos);
        }

        if (url.empty())
            throw EvalError(format("'url' argument required, at %1%") % pos);

    } else
        url = state.forceStringNoCtx(*args[0], pos);

    Path storePath = exportGit(state, url, ref, rev);

    mkString(v, storePath, PathSet({storePath}));
}

static RegisterPrimOp r("__fetchgit", 1, prim_fetchgit);

}
