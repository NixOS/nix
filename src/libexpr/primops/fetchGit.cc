#include "primops.hh"
#include "eval-inline.hh"
#include "download.hh"
#include "store-api.hh"
#include "pathlocks.hh"
#include "hash.hh"
#include "tarfile.hh"

#include <sys/time.h>

#include <regex>

#include <nlohmann/json.hpp>

using namespace std::string_literals;

namespace nix {

struct GitInfo
{
    Path storePath;
    std::string rev;
    std::string shortRev;
    uint64_t revCount = 0;
};

std::regex revRegex("^[0-9a-fA-F]{40}$");

void fetchRev(const Path cacheDir, const std::string & uri, std::string rev) {
    Activity act(*logger, lvlTalkative, actUnknown, fmt("fetching '%s' of Git repository '%s'", rev, uri));

    // FIXME: git stderr messes up our progress indicator, so
    // we're using --quiet for now. Should process its stderr.
    runProgram("git", true, { "-C", cacheDir, "fetch", "--quiet", "--", uri, rev });
}

void fetchRef(const Path cacheDir, const std::string & uri, std::string ref, time_t now, Path localRefFile) {
    Activity act(*logger, lvlTalkative, actUnknown, fmt("fetching '%s' of Git repository '%s'", ref, uri));

    // FIXME: git stderr messes up our progress indicator, so
    // we're using --quiet for now. Should process its stderr.
    runProgram("git", true, { "-C", cacheDir, "fetch", "--quiet", "--force", "--", uri, fmt("%s:%s", ref, ref) });

    struct timeval times[2];
    times[0].tv_sec = now;
    times[0].tv_usec = 0;
    times[1].tv_sec = now;
    times[1].tv_usec = 0;

    utimes(localRefFile.c_str(), times);
}

bool isAncestor(const Path cacheDir, std::string rev1, std::string rev2) {
    try {
        runProgram("git", true, { "-C", cacheDir, "merge-base", "--is-ancestor", rev1, rev2});
        return true;
    } catch (ExecError & e) {
        if (WIFEXITED(e.status)) {
            return false;
        }
        else {
            throw;
        }
    }
}

void checkAncestor(const Path cacheDir, const std::string & uri, std::string rev, std::string ref) {
    time_t now = time(0);

    Path localRefFile;
    if (ref.compare(0, 5, "refs/") == 0)
        localRefFile = cacheDir + "/" + ref;
    else
        localRefFile = cacheDir + "/refs/heads/" + ref;

    struct stat st;
    if (stat(localRefFile.c_str(), &st) == 0 &&
        isAncestor(cacheDir, rev, chomp(readFile(localRefFile)))) return;

    fetchRef(cacheDir, uri, ref, now, localRefFile);
    std::string refRev = chomp(readFile(localRefFile));
    if (!isAncestor(cacheDir, rev, refRev)) {
        throw Error("The specified rev '%s' is not an ancestor of the specified ref '%s' with revision '%s'", rev, ref, refRev);
    }
}

bool revInCache(const Path cacheDir, std::string rev) {
    try {
        runProgram("git", true, { "-C", cacheDir, "cat-file", "-e", rev });
        return true;
    } catch (ExecError & e) {
        if (WIFEXITED(e.status)) {
            return false;
        } else {
            throw;
        }
    }
}

GitInfo exportGit(ref<Store> store, const std::string & uri,
    std::optional<std::string> ref, std::string rev,
    const std::string & name, bool fetchRevInsteadOfRef)
{
    if (evalSettings.pureEval && rev == "")
        throw Error("in pure evaluation mode, 'fetchGit' requires a Git revision");

    if (!ref && rev == "" && hasPrefix(uri, "/") && pathExists(uri + "/.git")) {

        bool clean = true;

        try {
            runProgram("git", true, { "-C", uri, "diff-index", "--quiet", "HEAD", "--" });
        } catch (ExecError & e) {
            if (!WIFEXITED(e.status) || WEXITSTATUS(e.status) != 1) throw;
            clean = false;
        }

        if (!clean) {

            /* This is an unclean working tree. So copy all tracked files. */
            GitInfo gitInfo;
            gitInfo.rev = "0000000000000000000000000000000000000000";
            gitInfo.shortRev = std::string(gitInfo.rev, 0, 7);

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

            gitInfo.storePath = store->printStorePath(store->addToStore("source", uri, true, htSHA256, filter));

            return gitInfo;
        }

        // clean working tree, but no ref or rev specified.  Use 'HEAD'.
        rev = chomp(runProgram("git", true, { "-C", uri, "rev-parse", "HEAD" }));
        ref = "HEAD"s;
    }

    if (rev != "" && !std::regex_match(rev, revRegex))
        throw Error("invalid Git revision '%s'", rev);

    deletePath(getCacheDir() + "/nix/git");

    Path cacheDir = getCacheDir() + "/nix/gitv2/" + hashString(htSHA256, uri).to_string(Base32, false);

    if (!pathExists(cacheDir)) {
        createDirs(dirOf(cacheDir));
        runProgram("git", true, { "init", "--bare", cacheDir });
    }

    if (rev.empty()) {
        std::string fetchedRef = ref ? *ref : "HEAD";

        time_t now = time(0);

        Path localRefFile;
        if (fetchedRef.compare(0, 5, "refs/") == 0)
            localRefFile = cacheDir + "/" + fetchedRef;
        else
            localRefFile = cacheDir + "/refs/heads/" + fetchedRef;

        struct stat st;
        if (stat(localRefFile.c_str(), &st) != 0 ||
            (uint64_t) st.st_mtime + settings.tarballTtl <= (uint64_t) now) {
            fetchRef(cacheDir, uri, fetchedRef, now, localRefFile);
        }
        rev = chomp(readFile(localRefFile));
    } else {
        if (revInCache(cacheDir, rev)) {
            if (ref) { checkAncestor(cacheDir, uri, rev, *ref); }
        } else {
            if (fetchRevInsteadOfRef) {
                fetchRev(cacheDir, uri, rev);
                if (ref) { checkAncestor(cacheDir, uri, rev, *ref); }
            } else {
                std::string fetchedRef = ref ? *ref : "HEAD";

                time_t now = time(0);

                Path localRefFile;
                if (fetchedRef.compare(0, 5, "refs/") == 0)
                    localRefFile = cacheDir + "/" + fetchedRef;
                else
                    localRefFile = cacheDir + "/refs/heads/" + fetchedRef;

                fetchRef(cacheDir, uri, fetchedRef, now, localRefFile);
                std::string refRev = chomp(readFile(localRefFile));

                if (!isAncestor(cacheDir, rev, refRev)) {
                    throw Error(
                        "The specified rev '%s' "
                        "is not an ancestor of ref '%s' "
                        "with revision '%s'. %s",
                        rev, fetchedRef, refRev,
                        ref ? "" :
                          "Note that 'ref' defaults to 'HEAD' when not specified. "
                          "So either set a correct 'ref' "
                          "or set 'fetchRevInsteadOfRef = true;' "
                          "to directly fetch the specified 'rev' "
                          "(which might be denied by git servers "
                          "which have the config option "
                          "'uploadpack.allowAnySHA1InWant' set to 'false')."
                        );
                }
            }
        }
    }

    GitInfo gitInfo;
    gitInfo.rev = rev;
    gitInfo.shortRev = std::string(gitInfo.rev, 0, 7);

    printTalkative("using revision %s of repo '%s'", gitInfo.rev, uri);

    std::string storeLinkName = hashString(htSHA512, name + std::string("\0"s) + gitInfo.rev).to_string(Base32, false);
    Path storeLink = cacheDir + "/" + storeLinkName + ".link";
    PathLocks storeLinkLock({storeLink}, fmt("waiting for lock on '%1%'...", storeLink)); // FIXME: broken

    try {
        auto json = nlohmann::json::parse(readFile(storeLink));

        assert(json["name"] == name && json["rev"] == gitInfo.rev);

        gitInfo.storePath = json["storePath"];

        if (store->isValidPath(store->parseStorePath(gitInfo.storePath))) {
            gitInfo.revCount = json["revCount"];
            return gitInfo;
        }

    } catch (SysError & e) {
        if (e.errNo != ENOENT) throw;
    }

    auto source = sinkToSource([&](Sink & sink) {
        RunOptions gitOptions("git", { "-C", cacheDir, "archive", gitInfo.rev });
        gitOptions.standardOut = &sink;
        runProgram2(gitOptions);
    });

    Path tmpDir = createTempDir();
    AutoDelete delTmpDir(tmpDir, true);

    unpackTarfile(*source, tmpDir);

    gitInfo.storePath = store->printStorePath(store->addToStore(name, tmpDir));

    gitInfo.revCount = std::stoull(runProgram("git", true, { "-C", cacheDir, "rev-list", "--count", gitInfo.rev }));

    nlohmann::json json;
    json["storePath"] = gitInfo.storePath;
    json["uri"] = uri;
    json["name"] = name;
    json["rev"] = gitInfo.rev;
    json["revCount"] = gitInfo.revCount;

    writeFile(storeLink, json.dump());

    return gitInfo;
}

static void prim_fetchGit(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    std::string url;
    std::optional<std::string> ref;
    std::string rev;
    std::string name = "source";
    bool fetchRevInsteadOfRef = false;
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
                rev = state.forceStringNoCtx(*attr.value, *attr.pos);
            else if (n == "name")
                name = state.forceStringNoCtx(*attr.value, *attr.pos);
            else if (n == "fetchRevInsteadOfRef")
                fetchRevInsteadOfRef = state.forceBool(*attr.value, *attr.pos);
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

    auto gitInfo = exportGit(state.store, url, ref, rev, name, fetchRevInsteadOfRef);

    state.mkAttrs(v, 8);
    mkString(*state.allocAttr(v, state.sOutPath), gitInfo.storePath, PathSet({gitInfo.storePath}));
    mkString(*state.allocAttr(v, state.symbols.create("rev")), gitInfo.rev);
    mkString(*state.allocAttr(v, state.symbols.create("shortRev")), gitInfo.shortRev);
    mkInt(*state.allocAttr(v, state.symbols.create("revCount")), gitInfo.revCount);
    v.attrs->sort();

    if (state.allowedPaths)
        state.allowedPaths->insert(state.store->toRealPath(gitInfo.storePath));
}

static RegisterPrimOp r("fetchGit", 1, prim_fetchGit);

}
