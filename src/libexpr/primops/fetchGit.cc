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

static Path getCacheInfoPathFor(const std::string & name, const Hash & rev)
{
    Path cacheDir = getCacheDir() + "/nix/git-revs";
    std::string linkName =
        name == "source"
        ? rev.gitRev()
        : hashString(htSHA512, name + std::string("\0"s) + rev.gitRev()).to_string(Base32, false);
    return cacheDir + "/" + linkName + ".link";
}

static void cacheGitInfo(const std::string & name, const GitInfo & gitInfo)
{
    nlohmann::json json;
    json["storePath"] = gitInfo.storePath;
    json["name"] = name;
    json["rev"] = gitInfo.rev.gitRev();
    if (gitInfo.revCount)
        json["revCount"] = *gitInfo.revCount;
    json["lastModified"] = gitInfo.lastModified;

    auto cacheInfoPath = getCacheInfoPathFor(name, gitInfo.rev);
    createDirs(dirOf(cacheInfoPath));
    writeFile(cacheInfoPath, json.dump());
}

static std::optional<GitInfo> lookupGitInfo(
    ref<Store> store,
    const std::string & name,
    const Hash & rev)
{
    try {
        auto json = nlohmann::json::parse(readFile(getCacheInfoPathFor(name, rev)));

        assert(json["name"] == name && Hash((std::string) json["rev"], htSHA1) == rev);

        Path storePath = json["storePath"];

        if (store->isValidPath(storePath)) {
            GitInfo gitInfo;
            gitInfo.storePath = storePath;
            gitInfo.rev = rev;
            if (json.find("revCount") != json.end())
                gitInfo.revCount = json["revCount"];
            gitInfo.lastModified = json["lastModified"];
            return gitInfo;
        }

    } catch (SysError & e) {
        if (e.errNo != ENOENT) throw;
    }

    return {};
}

GitInfo exportGit(ref<Store> store, std::string uri,
    std::optional<std::string> ref,
    std::optional<Hash> rev,
    const std::string & name)
{
    assert(!rev || rev->type == htSHA1);

    if (rev) {
        if (auto gitInfo = lookupGitInfo(store, name, *rev)) {
            // If this gitInfo was produced by exportGitHub, then it won't
            // have a revCount. So we have to do a full clone.
            if (gitInfo->revCount) {
                gitInfo->ref = ref;
                return *gitInfo;
            }
        }
    }

    if (hasPrefix(uri, "git+")) uri = std::string(uri, 4);

    bool isLocal = hasPrefix(uri, "/") && pathExists(uri + "/.git");

    // If this is a local directory (but not a file:// URI) and no ref
    // or revision is given, then allow the use of an unclean working
    // tree.
    if (!ref && !rev && isLocal) {
        bool clean = false;

        /* Check whether this repo has any commits. There are
           probably better ways to do this. */
        bool haveCommits = !readDirectory(uri + "/.git/refs/heads").empty();

        try {
            if (haveCommits) {
                runProgram("git", true, { "-C", uri, "diff-index", "--quiet", "HEAD", "--" });
                clean = true;
            }
        } catch (ExecError & e) {
            if (!WIFEXITED(e.status) || WEXITSTATUS(e.status) != 1) throw;
        }

        if (!clean) {

            /* This is an unclean working tree. So copy all tracked
               files. */

            if (!evalSettings.allowDirty)
                throw Error("Git tree '%s' is dirty", uri);

            if (evalSettings.warnDirty)
                warn("Git tree '%s' is dirty", uri);

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
            gitInfo.revCount = haveCommits ? std::stoull(runProgram("git", true, { "-C", uri, "rev-list", "--count", "HEAD" })) : 0;
            // FIXME: maybe we should use the timestamp of the last
            // modified dirty file?
            gitInfo.lastModified = haveCommits ? std::stoull(runProgram("git", true, { "-C", uri, "show", "-s", "--format=%ct", "HEAD" })) : 0;

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

    Path cacheDir = getCacheDir() + "/nix/gitv3/" + hashString(htSHA256, uri).to_string(Base32, false);
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

        Path localRefFile =
            ref->compare(0, 5, "refs/") == 0
            ? cacheDir + "/" + *ref
            : cacheDir + "/refs/heads/" + *ref;

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
                (uint64_t) st.st_mtime + settings.tarballTtl <= (uint64_t) now;
        }

        if (doFetch) {
            Activity act(*logger, lvlTalkative, actUnknown, fmt("fetching Git repository '%s'", uri));

            // FIXME: git stderr messes up our progress indicator, so
            // we're using --quiet for now. Should process its stderr.
            try {
                runProgram("git", true, { "-C", repoDir, "fetch", "--quiet", "--force", "--", uri, fmt("%s:%s", *ref, *ref) });
            } catch (Error & e) {
                if (!pathExists(localRefFile)) throw;
                warn("could not update local clone of Git repository '%s'; continuing with the most recent version", uri);
            }

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

    if (auto gitInfo = lookupGitInfo(store, name, *rev)) {
        if (gitInfo->revCount) {
            gitInfo->ref = ref;
            return *gitInfo;
        }
    }

    // FIXME: check whether rev is an ancestor of ref.
    GitInfo gitInfo;
    gitInfo.ref = *ref;
    gitInfo.rev = *rev;

    printTalkative("using revision %s of repo '%s'", gitInfo.rev, uri);

    // FIXME: should pipe this, or find some better way to extract a
    // revision.
    auto tar = runProgram("git", true, { "-C", repoDir, "archive", gitInfo.rev.gitRev() });

    Path tmpDir = createTempDir();
    AutoDelete delTmpDir(tmpDir, true);

    runProgram("tar", true, { "x", "-C", tmpDir }, tar);

    gitInfo.storePath = store->addToStore(name, tmpDir);

    gitInfo.revCount = std::stoull(runProgram("git", true, { "-C", repoDir, "rev-list", "--count", gitInfo.rev.gitRev() }));
    gitInfo.lastModified = std::stoull(runProgram("git", true, { "-C", repoDir, "show", "-s", "--format=%ct", gitInfo.rev.gitRev() }));

    cacheGitInfo(name, gitInfo);

    return gitInfo;
}

GitInfo exportGitHub(
    ref<Store> store,
    const std::string & owner,
    const std::string & repo,
    std::optional<std::string> ref,
    std::optional<Hash> rev)
{
    if (rev) {
        if (auto gitInfo = lookupGitInfo(store, "source", *rev))
            return *gitInfo;
    }

    // FIXME: use regular /archive URLs instead? api.github.com
    // might have stricter rate limits.

    auto url = fmt("https://api.github.com/repos/%s/%s/tarball/%s",
        owner, repo, rev ? rev->to_string(Base16, false) : ref ? *ref : "master");

    std::string accessToken = settings.githubAccessToken.get();
    if (accessToken != "")
        url += "?access_token=" + accessToken;

    CachedDownloadRequest request(url);
    request.unpack = true;
    request.name = "source";
    request.ttl = rev ? 1000000000 : settings.tarballTtl;
    request.getLastModified = true;
    auto result = getDownloader()->downloadCached(store, request);

    if (!result.etag)
        throw Error("did not receive an ETag header from '%s'", url);

    if (result.etag->size() != 42 || (*result.etag)[0] != '"' || (*result.etag)[41] != '"')
        throw Error("ETag header '%s' from '%s' is not a Git revision", *result.etag, url);

    assert(result.lastModified);

    GitInfo gitInfo;
    gitInfo.storePath = result.storePath;
    gitInfo.rev = Hash(std::string(*result.etag, 1, result.etag->size() - 2), htSHA1);
    gitInfo.lastModified = *result.lastModified;

    // FIXME: this can overwrite a cache file that contains a revCount.
    cacheGitInfo("source", gitInfo);

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
    assert(gitInfo.revCount);
    mkInt(*state.allocAttr(v, state.symbols.create("revCount")), *gitInfo.revCount);
    v.attrs->sort();

    if (state.allowedPaths)
        state.allowedPaths->insert(state.store->toRealPath(gitInfo.storePath));
}

static RegisterPrimOp r("fetchGit", 1, prim_fetchGit);

}
