#include "fetchers.hh"
#include "cache.hh"
#include "globals.hh"
#include "tarfile.hh"
#include "store-api.hh"
#include "url-parts.hh"
#include "pathlocks.hh"
#include "util.hh"
#include "git.hh"
#include "fs-input-accessor.hh"
#include "git-utils.hh"

#include "fetch-settings.hh"

#include <regex>
#include <string.h>
#include <sys/time.h>
#include <sys/wait.h>

using namespace std::string_literals;

namespace nix::fetchers {

namespace {

// Explicit initial branch of our bare repo to suppress warnings from new version of git.
// The value itself does not matter, since we always fetch a specific revision or branch.
// It is set with `-c init.defaultBranch=` instead of `--initial-branch=` to stay compatible with
// old version of git, which will ignore unrecognized `-c` options.
const std::string gitInitialBranch = "__nix_dummy_branch";

bool isCacheFileWithinTtl(time_t now, const struct stat & st)
{
    return st.st_mtime + settings.tarballTtl > now;
}

bool touchCacheFile(const Path & path, time_t touch_time)
{
    struct timeval times[2];
    times[0].tv_sec = touch_time;
    times[0].tv_usec = 0;
    times[1].tv_sec = touch_time;
    times[1].tv_usec = 0;

    return lutimes(path.c_str(), times) == 0;
}

Path getCachePath(std::string_view key)
{
    return getCacheDir() + "/nix/gitv3/" +
        hashString(htSHA256, key).to_string(Base32, false);
}

// Returns the name of the HEAD branch.
//
// Returns the head branch name as reported by git ls-remote --symref, e.g., if
// ls-remote returns the output below, "main" is returned based on the ref line.
//
//   ref: refs/heads/main       HEAD
//   ...
std::optional<std::string> readHead(const Path & path)
{
    auto [status, output] = runProgram(RunOptions {
        .program = "git",
        // FIXME: use 'HEAD' to avoid returning all refs
        .args = {"ls-remote", "--symref", path},
        .isInteractive = true,
    });
    if (status != 0) return std::nullopt;

    std::string_view line = output;
    line = line.substr(0, line.find("\n"));
    if (const auto parseResult = git::parseLsRemoteLine(line)) {
        switch (parseResult->kind) {
            case git::LsRemoteRefLine::Kind::Symbolic:
                debug("resolved HEAD ref '%s' for repo '%s'", parseResult->target, path);
                break;
            case git::LsRemoteRefLine::Kind::Object:
                debug("resolved HEAD rev '%s' for repo '%s'", parseResult->target, path);
                break;
        }
        return parseResult->target;
    }
    return std::nullopt;
}

// Persist the HEAD ref from the remote repo in the local cached repo.
bool storeCachedHead(const std::string & actualUrl, const std::string & headRef)
{
    Path cacheDir = getCachePath(actualUrl);
    try {
        runProgram("git", true, { "-C", cacheDir, "--git-dir", ".", "symbolic-ref", "--", "HEAD", headRef });
    } catch (ExecError &e) {
        if (!WIFEXITED(e.status)) throw;
        return false;
    }
    /* No need to touch refs/HEAD, because `git symbolic-ref` updates the mtime. */
    return true;
}

std::optional<std::string> readHeadCached(const std::string & actualUrl)
{
    // Create a cache path to store the branch of the HEAD ref. Append something
    // in front of the URL to prevent collision with the repository itself.
    Path cacheDir = getCachePath(actualUrl);
    Path headRefFile = cacheDir + "/HEAD";

    time_t now = time(0);
    struct stat st;
    std::optional<std::string> cachedRef;
    if (stat(headRefFile.c_str(), &st) == 0) {
        cachedRef = readHead(cacheDir);
        if (cachedRef != std::nullopt &&
            *cachedRef != gitInitialBranch &&
            isCacheFileWithinTtl(now, st))
        {
            debug("using cached HEAD ref '%s' for repo '%s'", *cachedRef, actualUrl);
            return cachedRef;
        }
    }

    auto ref = readHead(actualUrl);
    if (ref) return ref;

    if (cachedRef) {
        // If the cached git ref is expired in fetch() below, and the 'git fetch'
        // fails, it falls back to continuing with the most recent version.
        // This function must behave the same way, so we return the expired
        // cached ref here.
        warn("could not get HEAD ref for repository '%s'; using expired cached ref '%s'", actualUrl, *cachedRef);
        return *cachedRef;
    }

    return std::nullopt;
}

bool isNotDotGitDirectory(const Path & path)
{
    return baseNameOf(path) != ".git";
}

}  // end namespace

struct GitInputScheme : InputScheme
{
    std::optional<Input> inputFromURL(const ParsedURL & url) const override
    {
        if (url.scheme != "git" &&
            url.scheme != "git+http" &&
            url.scheme != "git+https" &&
            url.scheme != "git+ssh" &&
            url.scheme != "git+file") return {};

        auto url2(url);
        if (hasPrefix(url2.scheme, "git+")) url2.scheme = std::string(url2.scheme, 4);
        url2.query.clear();

        Attrs attrs;
        attrs.emplace("type", "git");

        for (auto & [name, value] : url.query) {
            if (name == "rev" || name == "ref")
                attrs.emplace(name, value);
            else if (name == "shallow" || name == "submodules" || name == "allRefs")
                attrs.emplace(name, Explicit<bool> { value == "1" });
            else
                url2.query.emplace(name, value);
        }

        attrs.emplace("url", url2.to_string());

        return inputFromAttrs(attrs);
    }

    std::optional<Input> inputFromAttrs(const Attrs & attrs) const override
    {
        if (maybeGetStrAttr(attrs, "type") != "git") return {};

        for (auto & [name, value] : attrs)
            if (name != "type" && name != "url" && name != "ref" && name != "rev" && name != "shallow" && name != "submodules" && name != "lastModified" && name != "revCount" && name != "narHash" && name != "allRefs" && name != "name")
                throw Error("unsupported Git input attribute '%s'", name);

        parseURL(getStrAttr(attrs, "url"));
        maybeGetBoolAttr(attrs, "shallow");
        maybeGetBoolAttr(attrs, "submodules");
        maybeGetBoolAttr(attrs, "allRefs");

        if (auto ref = maybeGetStrAttr(attrs, "ref")) {
            if (std::regex_search(*ref, badGitRefRegex))
                throw BadURL("invalid Git branch/tag name '%s'", *ref);
        }

        Input input;
        input.attrs = attrs;
        return input;
    }

    ParsedURL toURL(const Input & input) const override
    {
        auto url = parseURL(getStrAttr(input.attrs, "url"));
        if (url.scheme != "git") url.scheme = "git+" + url.scheme;
        if (auto rev = input.getRev()) url.query.insert_or_assign("rev", rev->gitRev());
        if (auto ref = input.getRef()) url.query.insert_or_assign("ref", *ref);
        if (maybeGetBoolAttr(input.attrs, "shallow").value_or(false))
            url.query.insert_or_assign("shallow", "1");
        return url;
    }

    Input applyOverrides(
        const Input & input,
        std::optional<std::string> ref,
        std::optional<Hash> rev) const override
    {
        auto res(input);
        if (rev) res.attrs.insert_or_assign("rev", rev->gitRev());
        if (ref) res.attrs.insert_or_assign("ref", *ref);
        if (!res.getRef() && res.getRev())
            throw Error("Git input '%s' has a commit hash but no branch/tag name", res.to_string());
        return res;
    }

    void clone(const Input & input, const Path & destDir) const override
    {
        auto repoInfo = getRepoInfo(input);

        Strings args = {"clone"};

        args.push_back(repoInfo.url);

        if (auto ref = input.getRef()) {
            args.push_back("--branch");
            args.push_back(*ref);
        }

        if (input.getRev()) throw UnimplementedError("cloning a specific revision is not implemented");

        args.push_back(destDir);

        runProgram("git", true, args, {}, true);
    }

    void putFile(
        const Input & input,
        const CanonPath & path,
        std::string_view contents,
        std::optional<std::string> commitMsg) const override
    {
        auto repoInfo = getRepoInfo(input);
        if (!repoInfo.isLocal)
            throw Error("cannot commit '%s' to Git repository '%s' because it's not a working tree", path, input.to_string());

        auto absPath = CanonPath(repoInfo.url) + path;

        // FIXME: make sure that absPath is not a symlink that escapes
        // the repo.
        writeFile(absPath.abs(), contents);

        runProgram("git", true,
            { "-C", repoInfo.url, "--git-dir", repoInfo.gitDir, "add", "--intent-to-add", "--", std::string(path.rel()) });

        if (commitMsg)
            runProgram("git", true,
                { "-C", repoInfo.url, "--git-dir", repoInfo.gitDir, "commit", std::string(path.rel()), "-m", *commitMsg });
    }

    struct RepoInfo
    {
        bool shallow = false;
        bool submodules = false;
        bool allRefs = false;

        std::string cacheType;

        /* Whether this is a local, non-bare repository. */
        bool isLocal = false;

        /* Working directory info: the complete list of files, and
           whether the working directory is dirty compared to HEAD. */
        GitRepo::WorkdirInfo workdirInfo;

        /* URL of the repo, or its path if isLocal. */
        std::string url;

        void warnDirty() const
        {
            if (workdirInfo.isDirty) {
                if (!fetchSettings.allowDirty)
                    throw Error("Git tree '%s' is dirty", url);

                if (fetchSettings.warnDirty)
                    warn("Git tree '%s' is dirty", url);
            }
        }

        std::string gitDir = ".git";
    };

    bool getSubmodulesAttr(const Input & input) const
    {
        return maybeGetBoolAttr(input.attrs, "submodules").value_or(false);
    }

    RepoInfo getRepoInfo(const Input & input) const
    {
        auto checkHashType = [&](const std::optional<Hash> & hash)
        {
            if (hash.has_value() && !(hash->type == htSHA1 || hash->type == htSHA256))
                throw Error("Hash '%s' is not supported by Git. Supported types are sha1 and sha256.", hash->to_string(Base16, true));
        };

        if (auto rev = input.getRev())
            checkHashType(rev);

        RepoInfo repoInfo {
            .shallow = maybeGetBoolAttr(input.attrs, "shallow").value_or(false),
            .submodules = getSubmodulesAttr(input),
            .allRefs = maybeGetBoolAttr(input.attrs, "allRefs").value_or(false)
        };

        repoInfo.cacheType = "git";
        if (repoInfo.shallow) repoInfo.cacheType += "-shallow";
        if (repoInfo.submodules) repoInfo.cacheType += "-submodules";
        if (repoInfo.allRefs) repoInfo.cacheType += "-all-refs";

        // file:// URIs are normally not cloned (but otherwise treated the
        // same as remote URIs, i.e. we don't use the working tree or
        // HEAD). Exception: If _NIX_FORCE_HTTP is set, or the repo is a bare git
        // repo, treat as a remote URI to force a clone.
        static bool forceHttp = getEnv("_NIX_FORCE_HTTP") == "1"; // for testing
        auto url = parseURL(getStrAttr(input.attrs, "url"));
        bool isBareRepository = url.scheme == "file" && !pathExists(url.path + "/.git");
        repoInfo.isLocal = url.scheme == "file" && !forceHttp && !isBareRepository;
        repoInfo.url = repoInfo.isLocal ? url.path : url.base;

        // If this is a local directory and no ref or revision is
        // given, then allow the use of an unclean working tree.
        if (!input.getRef() && !input.getRev() && repoInfo.isLocal)
            repoInfo.workdirInfo = GitRepo::openRepo(CanonPath(repoInfo.url))->getWorkdirInfo();

        return repoInfo;
    }

    uint64_t getLastModified(const RepoInfo & repoInfo, const std::string & repoDir, const Hash & rev) const
    {
        auto key = fmt("git-%s-last-modified", rev.gitRev());

        auto cache = getCache();

        if (auto lastModifiedS = cache->queryFact(key)) {
            if (auto lastModified = string2Int<uint64_t>(*lastModifiedS))
                return *lastModified;
        }

        auto lastModified = GitRepo::openRepo(CanonPath(repoDir))->getLastModified(rev);

        cache->upsertFact(key, std::to_string(lastModified));

        return lastModified;
    }

    uint64_t getRevCount(const RepoInfo & repoInfo, const std::string & repoDir, const Hash & rev) const
    {
        auto key = fmt("git-%s-revcount", rev.gitRev());

        auto cache = getCache();

        if (auto revCountS = cache->queryFact(key)) {
            if (auto revCount = string2Int<uint64_t>(*revCountS))
                return *revCount;
        }

        Activity act(*logger, lvlChatty, actUnknown, fmt("getting Git revision count of '%s'", repoInfo.url));

        auto revCount = GitRepo::openRepo(CanonPath(repoDir))->getRevCount(rev);

        cache->upsertFact(key, std::to_string(revCount));

        return revCount;
    }

    std::string getDefaultRef(const RepoInfo & repoInfo) const
    {
        auto head = repoInfo.isLocal
            ? GitRepo::openRepo(CanonPath(repoInfo.url))->getWorkdirRef()
            : readHeadCached(repoInfo.url);
        if (!head) {
            warn("could not read HEAD ref from repo at '%s', using 'master'", repoInfo.url);
            return "master";
        }
        return *head;
    }

    static MakeNotAllowedError makeNotAllowedError(std::string url)
    {
        return [url{std::move(url)}](const CanonPath & path) -> RestrictedPathError
        {
            if (nix::pathExists(path.abs()))
                return RestrictedPathError("access to path '%s' is forbidden because it is not under Git control; maybe you should 'git add' it to the repository '%s'?", path, url);
            else
                return RestrictedPathError("path '%s' does not exist in Git repository '%s'", path, url);
        };
    }

    std::pair<ref<InputAccessor>, Input> getAccessorFromCommit(
        ref<Store> store,
        RepoInfo & repoInfo,
        Input && input) const
    {
        assert(!repoInfo.workdirInfo.isDirty);

        auto origRev = input.getRev();

        std::string name = input.getName();

        auto makeResult2 = [&](const Attrs & infoAttrs, ref<InputAccessor> accessor) -> std::pair<ref<InputAccessor>, Input>
        {
            assert(input.getRev());
            assert(!origRev || origRev == input.getRev());
            if (!repoInfo.shallow)
                input.attrs.insert_or_assign("revCount", getIntAttr(infoAttrs, "revCount"));
            input.attrs.insert_or_assign("lastModified", getIntAttr(infoAttrs, "lastModified"));

            accessor->setPathDisplay("«" + input.to_string() + "»");
            return {accessor, std::move(input)};
        };

        auto makeResult = [&](const Attrs & infoAttrs, const StorePath & storePath) -> std::pair<ref<InputAccessor>, Input>
        {
            // FIXME: remove?
            //input.attrs.erase("narHash");
            auto narHash = store->queryPathInfo(storePath)->narHash;
            input.attrs.insert_or_assign("narHash", narHash.to_string(SRI, true));

            auto accessor = makeStorePathAccessor(store, storePath, makeNotAllowedError(repoInfo.url));

            return makeResult2(infoAttrs, accessor);
        };

        auto originalRef = input.getRef();
        auto ref = originalRef ? *originalRef : getDefaultRef(repoInfo);
        input.attrs.insert_or_assign("ref", ref);

        Path repoDir;

        if (repoInfo.isLocal) {
            repoDir = repoInfo.url;
            if (!input.getRev())
                input.attrs.insert_or_assign("rev", GitRepo::openRepo(CanonPath(repoDir))->resolveRef(ref).gitRev());
        } else {
            Path cacheDir = getCachePath(repoInfo.url);
            repoDir = cacheDir;
            repoInfo.gitDir = ".";

            createDirs(dirOf(cacheDir));
            PathLocks cacheDirLock({cacheDir});

            auto repo = GitRepo::openRepo(CanonPath(cacheDir), true, true);

            Path localRefFile =
                ref.compare(0, 5, "refs/") == 0
                ? cacheDir + "/" + ref
                : cacheDir + "/refs/heads/" + ref;

            bool doFetch;
            time_t now = time(0);

            /* If a rev was specified, we need to fetch if it's not in the
               repo. */
            if (auto rev = input.getRev()) {
                doFetch = !repo->hasObject(*rev);
            } else {
                if (repoInfo.allRefs) {
                    doFetch = true;
                } else {
                    /* If the local ref is older than ‘tarball-ttl’ seconds, do a
                       git fetch to update the local ref to the remote ref. */
                    struct stat st;
                    doFetch = stat(localRefFile.c_str(), &st) != 0 ||
                        !isCacheFileWithinTtl(now, st);
                }
            }

            if (doFetch) {
                Activity act(*logger, lvlTalkative, actUnknown, fmt("fetching Git repository '%s'", repoInfo.url));

                try {
                    auto fetchRef = repoInfo.allRefs
                        ? "refs/*"
                        : ref.compare(0, 5, "refs/") == 0
                        ? ref
                        : ref == "HEAD"
                        ? ref
                        : "refs/heads/" + ref;

                    repo->fetch(repoInfo.url, fmt("%s:%s", fetchRef, fetchRef));
                } catch (Error & e) {
                    if (!pathExists(localRefFile)) throw;
                    logError(e.info());
                    warn("could not update local clone of Git repository '%s'; continuing with the most recent version", repoInfo.url);
                }

                if (!touchCacheFile(localRefFile, now))
                    warn("could not update mtime for file '%s': %s", localRefFile, strerror(errno));
                if (!originalRef && !storeCachedHead(repoInfo.url, ref))
                    warn("could not update cached head '%s' for '%s'", ref, repoInfo.url);
            }

            if (auto rev = input.getRev()) {
                if (!repo->hasObject(*rev))
                    throw Error(
                        "Cannot find Git revision '%s' in ref '%s' of repository '%s'! "
                        "Please make sure that the " ANSI_BOLD "rev" ANSI_NORMAL " exists on the "
                        ANSI_BOLD "ref" ANSI_NORMAL " you've specified or add " ANSI_BOLD
                        "allRefs = true;" ANSI_NORMAL " to " ANSI_BOLD "fetchGit" ANSI_NORMAL ".",
                        rev->gitRev(),
                        ref,
                        repoInfo.url
                        );
            } else
                input.attrs.insert_or_assign("rev", Hash::parseAny(chomp(readFile(localRefFile)), htSHA1).gitRev());

            // cache dir lock is removed at scope end; we will only use read-only operations on specific revisions in the remainder
        }

        auto isShallow = GitRepo::openRepo(CanonPath(repoDir))->isShallow();

        if (isShallow && !repoInfo.shallow)
            throw Error("'%s' is a shallow Git repository, but shallow repositories are only allowed when `shallow = true;` is specified", repoInfo.url);

        // FIXME: check whether rev is an ancestor of ref?

        auto rev = *input.getRev();

        Attrs infoAttrs({
            {"rev", rev.gitRev()},
            {"lastModified", getLastModified(repoInfo, repoDir, rev)},
        });

        if (!repoInfo.shallow)
            infoAttrs.insert_or_assign("revCount",
                getRevCount(repoInfo, repoDir, rev));

        printTalkative("using revision %s of repo '%s'", rev.gitRev(), repoInfo.url);

        if (!repoInfo.submodules) {
            auto accessor = GitRepo::openRepo(CanonPath(repoDir))->getAccessor(rev);
            return makeResult2(infoAttrs, accessor);
        }

        else {
            Path tmpDir = createTempDir();
            AutoDelete delTmpDir(tmpDir, true);
            PathFilter filter = defaultPathFilter;

            Activity act(*logger, lvlChatty, actUnknown, fmt("copying Git tree '%s' to the store", input.to_string()));

            Path tmpGitDir = createTempDir();
            AutoDelete delTmpGitDir(tmpGitDir, true);

            runProgram("git", true, { "-c", "init.defaultBranch=" + gitInitialBranch, "init", tmpDir, "--separate-git-dir", tmpGitDir });

            {
                // TODO: repoDir might lack the ref (it only checks if rev
                // exists, see FIXME above) so use a big hammer and fetch
                // everything to ensure we get the rev.
                Activity act(*logger, lvlTalkative, actUnknown, fmt("making temporary clone of '%s'", repoDir));
                runProgram("git", true, { "-C", tmpDir, "fetch", "--quiet", "--force",
                        "--update-head-ok", "--", repoDir, "refs/*:refs/*" }, {}, true);
            }

            runProgram("git", true, { "-C", tmpDir, "checkout", "--quiet", rev.gitRev() });

            /* Ensure that we use the correct origin for fetching
               submodules. This matters for submodules with relative
               URLs. */
            if (repoInfo.isLocal) {
                writeFile(tmpGitDir + "/config", readFile(repoDir + "/" + repoInfo.gitDir + "/config"));

                /* Restore the config.bare setting we may have just
                   copied erroneously from the user's repo. */
                runProgram("git", true, { "-C", tmpDir, "config", "core.bare", "false" });
            } else
                runProgram("git", true, { "-C", tmpDir, "config", "remote.origin.url", repoInfo.url });

            /* As an optimisation, copy the modules directory of the
               source repo if it exists. */
            auto modulesPath = repoDir + "/" + repoInfo.gitDir + "/modules";
            if (pathExists(modulesPath)) {
                Activity act(*logger, lvlTalkative, actUnknown, fmt("copying submodules of '%s'", repoInfo.url));
                runProgram("cp", true, { "-R", "--", modulesPath, tmpGitDir + "/modules" });
            }

            {
                Activity act(*logger, lvlTalkative, actUnknown, fmt("fetching submodules of '%s'", repoInfo.url));
                runProgram("git", true, { "-C", tmpDir, "submodule", "--quiet", "update", "--init", "--recursive" }, {}, true);
            }

            filter = isNotDotGitDirectory;

            auto storePath = store->addToStore(name, tmpDir, FileIngestionMethod::Recursive, htSHA256, filter);

            return makeResult(infoAttrs, std::move(storePath));
        }
    }

    std::pair<ref<InputAccessor>, Input> getAccessorFromWorkdir(
        RepoInfo & repoInfo,
        Input && input) const
    {
        if (!repoInfo.workdirInfo.isDirty) {
            if (auto ref = GitRepo::openRepo(CanonPath(repoInfo.url))->getWorkdirRef())
                input.attrs.insert_or_assign("ref", *ref);

            auto rev = repoInfo.workdirInfo.headRev.value();

            input.attrs.insert_or_assign("rev", rev.gitRev());

            input.attrs.insert_or_assign("revCount", getRevCount(repoInfo, repoInfo.url, rev));
        } else
            repoInfo.warnDirty();

        input.attrs.insert_or_assign(
            "lastModified",
            repoInfo.workdirInfo.headRev
            ? getLastModified(repoInfo, repoInfo.url, *repoInfo.workdirInfo.headRev)
            : 0);

        return {
            makeFSInputAccessor(CanonPath(repoInfo.url), repoInfo.workdirInfo.files, makeNotAllowedError(repoInfo.url)),
            std::move(input)
        };
    }

    std::pair<ref<InputAccessor>, Input> getAccessor(ref<Store> store, const Input & _input) const override
    {
        Input input(_input);

        auto repoInfo = getRepoInfo(input);

        if (input.getRef() || input.getRev() || !repoInfo.isLocal)
            return getAccessorFromCommit(store, repoInfo, std::move(input));
        else
            return getAccessorFromWorkdir(repoInfo, std::move(input));
    }

    bool isLocked(const Input & input) const override
    {
        return (bool) input.getRev();
    }

    std::optional<std::string> getFingerprint(ref<Store> store, const Input & input) const override
    {
        if (auto rev = input.getRev()) {
            return fmt("%s;%s", rev->gitRev(), getSubmodulesAttr(input) ? "1" : "0");
        } else
            return std::nullopt;
    }

};

static auto rGitInputScheme = OnStartup([] { registerInputScheme(std::make_unique<GitInputScheme>()); });

}
