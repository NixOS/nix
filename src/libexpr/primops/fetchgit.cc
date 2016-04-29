#include "primops.hh"
#include "eval-inline.hh"
#include "download.hh"
#include "store-api.hh"

namespace nix {

Path exportGit(ref<Store> store, const std::string & uri, const std::string & rev)
{
    if (!isUri(uri))
        throw EvalError(format("‘%s’ is not a valid URI") % uri);

    Path cacheDir = getCacheDir() + "/nix/git";

    if (!pathExists(cacheDir)) {
        createDirs(cacheDir);
        runProgram("git", true, { "init", "--bare", cacheDir });
    }

    Activity act(*logger, lvlInfo, format("fetching Git repository ‘%s’") % uri);

    std::string localRef = "pid-" + std::to_string(getpid());
    Path localRefFile = cacheDir + "/refs/heads/" + localRef;

    runProgram("git", true, { "-C", cacheDir, "fetch", uri, rev + ":" + localRef });

    std::string commitHash = chomp(readFile(localRefFile));

    unlink(localRefFile.c_str());

    debug(format("got revision ‘%s’") % commitHash);

    // FIXME: should pipe this, or find some better way to extract a
    // revision.
    auto tar = runProgram("git", true, { "-C", cacheDir, "archive", commitHash });

    Path tmpDir = createTempDir();
    AutoDelete delTmpDir(tmpDir, true);

    runProgram("tar", true, { "x", "-C", tmpDir }, tar);

    return store->addToStore("git-export", tmpDir);
}

static void prim_fetchgit(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    // FIXME: cut&paste from fetch().
    if (state.restricted) throw Error("‘fetchgit’ is not allowed in restricted mode");

    std::string url;
    std::string rev = "master";

    state.forceValue(*args[0]);

    if (args[0]->type == tAttrs) {

        state.forceAttrs(*args[0], pos);

        for (auto & attr : *args[0]->attrs) {
            string name(attr.name);
            if (name == "url")
                url = state.forceStringNoCtx(*attr.value, *attr.pos);
            else if (name == "rev")
                rev = state.forceStringNoCtx(*attr.value, *attr.pos);
            else
                throw EvalError(format("unsupported argument ‘%1%’ to ‘fetchgit’, at %3%") % attr.name % attr.pos);
        }

        if (url.empty())
            throw EvalError(format("‘url’ argument required, at %1%") % pos);

    } else
        url = state.forceStringNoCtx(*args[0], pos);

    Path storePath = exportGit(state.store, url, rev);

    mkString(v, storePath, PathSet({storePath}));
}

static RegisterPrimOp r("__fetchgit", 1, prim_fetchgit);

}
