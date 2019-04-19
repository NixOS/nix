#include "fetchGit.hh"
#include "primops.hh"
#include "eval-inline.hh"
#include "download.hh"
#include "store-api.hh"
#include "pathlocks.hh"
#include "hash.hh"

#include <sys/time.h>

#include <regex>

#include <nlohmann/json.hpp>

using namespace std::string_literals;

namespace nix {

extern std::regex revRegex;

GitInfo exportGit(ref<Store> store, std::string uri,
    std::optional<std::string> ref,
    std::optional<Hash> rev,
    const std::string & name)
{
    assert(!rev || rev->type == htSHA1);

    bool isLocal = hasPrefix(uri, "/") && pathExists(uri + "/.git");

    // If this is a local directory (but not a file:// URI) and no ref
    // or revision is given, then allow the use of an unclean working
    // tree.
    if (!ref && !rev && isLocal) {

        bool clean = true;

        try {
            runProgram("git", true, { "-C", uri, "diff-index", "--quiet", "HEAD", "--" });
        } catch (ExecError e) {
            if (!WIFEXITED(e.status) || WEXITSTATUS(e.status) != 1) throw;
            clean = false;
        }

        if (!clean) {

            /* This is an unclean working tree. So copy all tracked
               files. */

            GitInfo gitInfo;
            gitInfo.ref = "HEAD";

            auto files = tokenizeString<std::set<std::string>>(
                runProgram("git", true, { "-C", uri, "ls-files", "-z" }), "\0"s);

            PathFilter filter = [&](const Path & p) -> bool {
                assert(hasPrefix(p, uri));
                std::string file(p, uri.size() + 1);

                auto st = lstat(p);

                if (S_ISDIR(st.st_mode)) {
                    auto prefix = file + "/";
                    auto i = files.lower_bound(prefix);
                    return i != files.end() && hasPrefix(*i, prefix);
                }

                return files.count(file);
            };

            gitInfo.storePath = store->addToStore("source", uri, true, htSHA256, filter);

            return gitInfo;
        }
    }

    if (!ref) ref = isLocal ? "HEAD" : "master";

    // Don't clone file:// URIs (but otherwise treat them the same as
    // remote URIs, i.e. don't use the working tree or HEAD).
    static bool forceHttp = getEnv("_NIX_FORCE_HTTP") == "1"; // for testing
    if (!forceHttp && hasPrefix(uri, "file://")) {
        uri = std::string(uri, 7);
        isLocal = true;
    }

    deletePath(getCacheDir() + "/nix/git");

    Path cacheDir = getCacheDir() + "/nix/gitv2/" + hashString(htSHA256, uri).to_string(Base32, false);
    Path repoDir;

    if (isLocal) {

        if (!rev)
            rev = Hash(chomp(runProgram("git", true, { "-C", uri, "rev-parse", *ref })), htSHA1);

        if (!pathExists(cacheDir))
            createDirs(cacheDir);

        repoDir = uri;

    } else {

        repoDir = cacheDir;

        if (!pathExists(cacheDir)) {
            createDirs(dirOf(cacheDir));
            runProgram("git", true, { "init", "--bare", repoDir });
        }

        Path localRefFile = repoDir + "/refs/heads/" + *ref;

        bool doFetch;
        time_t now = time(0);

        /* If a rev was specified, we need to fetch if it's not in the
           repo. */
        if (rev) {
            try {
                runProgram("git", true, { "-C", repoDir, "cat-file", "-e", rev->gitRev() });
                doFetch = false;
            } catch (ExecError & e) {
                if (WIFEXITED(e.status)) {
                    doFetch = true;
                } else {
                    throw;
                }
            }
        } else {
            /* If the local ref is older than ‘tarball-ttl’ seconds, do a
               git fetch to update the local ref to the remote ref. */
            struct stat st;
            doFetch = stat(localRefFile.c_str(), &st) != 0 ||
                st.st_mtime + settings.tarballTtl <= now;
        }

        if (doFetch) {
            Activity act(*logger, lvlTalkative, actUnknown, fmt("fetching Git repository '%s'", uri));

            // FIXME: git stderr messes up our progress indicator, so
            // we're using --quiet for now. Should process its stderr.
            runProgram("git", true, { "-C", repoDir, "fetch", "--quiet", "--force", "--", uri, fmt("%s:%s", *ref, *ref) });

            struct timeval times[2];
            times[0].tv_sec = now;
            times[0].tv_usec = 0;
            times[1].tv_sec = now;
            times[1].tv_usec = 0;

            utimes(localRefFile.c_str(), times);
        }

        if (!rev)
            rev = Hash(chomp(readFile(localRefFile)), htSHA1);
    }

    // FIXME: check whether rev is an ancestor of ref.
    GitInfo gitInfo;
    gitInfo.ref = *ref;
    gitInfo.rev = *rev;

    printTalkative("using revision %s of repo '%s'", gitInfo.rev, uri);

    std::string storeLinkName = hashString(htSHA512,
        name + std::string("\0"s) + gitInfo.rev.gitRev()).to_string(Base32, false);
    Path storeLink = cacheDir + "/" + storeLinkName + ".link";
    PathLocks storeLinkLock({storeLink}, fmt("waiting for lock on '%1%'...", storeLink)); // FIXME: broken

    try {
        auto json = nlohmann::json::parse(readFile(storeLink));

        assert(json["name"] == name && Hash((std::string) json["rev"], htSHA1) == gitInfo.rev);

        Path storePath = json["storePath"];

        if (store->isValidPath(storePath)) {
            gitInfo.storePath = storePath;
            gitInfo.revCount = json["revCount"];
            return gitInfo;
        }

    } catch (SysError & e) {
        if (e.errNo != ENOENT) throw;
    }

    // FIXME: should pipe this, or find some better way to extract a
    // revision.
    auto tar = runProgram("git", true, { "-C", repoDir, "archive", gitInfo.rev.gitRev() });

    Path tmpDir = createTempDir();
    AutoDelete delTmpDir(tmpDir, true);

    runProgram("tar", true, { "x", "-C", tmpDir }, tar);

    gitInfo.storePath = store->addToStore(name, tmpDir);

    gitInfo.revCount = std::stoull(runProgram("git", true, { "-C", repoDir, "rev-list", "--count", gitInfo.rev.gitRev() }));

    nlohmann::json json;
    json["storePath"] = gitInfo.storePath;
    json["uri"] = uri;
    json["name"] = name;
    json["rev"] = gitInfo.rev.gitRev();
    json["revCount"] = *gitInfo.revCount;

    writeFile(storeLink, json.dump());

    return gitInfo;
}

static void prim_fetchGit(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    std::string url;
    std::optional<std::string> ref;
    std::optional<Hash> rev;
    std::string name = "source";
    PathSet context;

    state.forceValue(*args[0]);

    if (args[0]->type == tAttrs) {

        state.forceAttrs(*args[0], pos);

        for (auto & attr : *args[0]->attrs) {
            string n(attr.name);
            if (n == "url")
                url = state.coerceToString(*attr.pos, *attr.value, context, false, false);
            else if (n == "ref")
                ref = state.forceStringNoCtx(*attr.value, *attr.pos);
            else if (n == "rev")
                rev = Hash(state.forceStringNoCtx(*attr.value, *attr.pos), htSHA1);
            else if (n == "name")
                name = state.forceStringNoCtx(*attr.value, *attr.pos);
            else
                throw EvalError("unsupported argument '%s' to 'fetchGit', at %s", attr.name, *attr.pos);
        }

        if (url.empty())
            throw EvalError(format("'url' argument required, at %1%") % pos);

    } else
        url = state.coerceToString(pos, *args[0], context, false, false);

    // FIXME: git externals probably can be used to bypass the URI
    // whitelist. Ah well.
    state.checkURI(url);

    if (evalSettings.pureEval && !rev)
        throw Error("in pure evaluation mode, 'fetchGit' requires a Git revision");

    auto gitInfo = exportGit(state.store, url, ref, rev, name);

    state.mkAttrs(v, 8);
    mkString(*state.allocAttr(v, state.sOutPath), gitInfo.storePath, PathSet({gitInfo.storePath}));
    mkString(*state.allocAttr(v, state.symbols.create("rev")), gitInfo.rev.gitRev());
    mkString(*state.allocAttr(v, state.symbols.create("shortRev")), gitInfo.rev.gitShortRev());
    mkInt(*state.allocAttr(v, state.symbols.create("revCount")), gitInfo.revCount.value_or(0));
    v.attrs->sort();

    if (state.allowedPaths)
        state.allowedPaths->insert(gitInfo.storePath);
}

static RegisterPrimOp r("fetchGit", 1, prim_fetchGit);

}
