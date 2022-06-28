#include "fetchers.hh"
#include "cache.hh"
#include "globals.hh"
#include "tarfile.hh"
#include "store-api.hh"
#include "url-parts.hh"
#include "pathlocks.hh"
#include "util.hh"
#include "git.hh"

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
    std::optional<Input> inputFromURL(const ParsedURL & url) override
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
            else if (name == "shallow" || name == "submodules")
                attrs.emplace(name, Explicit<bool> { value == "1" });
            else
                url2.query.emplace(name, value);
        }

        attrs.emplace("url", url2.to_string());

        return inputFromAttrs(attrs);
    }

    std::optional<Input> inputFromAttrs(const Attrs & attrs) override
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

    ParsedURL toURL(const Input & input) override
    {
        auto url = parseURL(getStrAttr(input.attrs, "url"));
        if (url.scheme != "git") url.scheme = "git+" + url.scheme;
        if (auto rev = input.getRev()) url.query.insert_or_assign("rev", rev->gitRev());
        if (auto ref = input.getRef()) url.query.insert_or_assign("ref", *ref);
        if (maybeGetBoolAttr(input.attrs, "shallow").value_or(false))
            url.query.insert_or_assign("shallow", "1");
        return url;
    }

    bool hasAllInfo(const Input & input) override
    {
        bool maybeDirty = !input.getRef();
        bool shallow = maybeGetBoolAttr(input.attrs, "shallow").value_or(false);
        return
            maybeGetIntAttr(input.attrs, "lastModified")
            && (shallow || maybeDirty || maybeGetIntAttr(input.attrs, "revCount"));
    }

    Input applyOverrides(
        const Input & input,
        std::optional<std::string> ref,
        std::optional<Hash> rev) override
    {
        auto res(input);
        if (rev) res.attrs.insert_or_assign("rev", rev->gitRev());
        if (ref) res.attrs.insert_or_assign("ref", *ref);
        if (!res.getRef() && res.getRev())
            throw Error("Git input '%s' has a commit hash but no branch/tag name", res.to_string());
        return res;
    }

    void clone(const Input & input, const Path & destDir) override
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

        runProgram("git", true, args);
    }

    std::optional<Path> getSourcePath(const Input & input) override
    {
        auto url = parseURL(getStrAttr(input.attrs, "url"));
        if (url.scheme == "file" && !input.getRef() && !input.getRev())
            return url.path;
        return {};
    }

    void markChangedFile(const Input & input, std::string_view file, std::optional<std::string> commitMsg) override
    {
        auto sourcePath = getSourcePath(input);
        assert(sourcePath);
        auto gitDir = ".git";

        runProgram("git", true,
            { "-C", *sourcePath, "--git-dir", gitDir, "add", "--force", "--intent-to-add", "--", std::string(file) });

        if (commitMsg)
            runProgram("git", true,
                { "-C", *sourcePath, "--git-dir", gitDir, "commit", std::string(file), "-m", *commitMsg });
    }

    struct RepoInfo
    {
        bool shallow = false;
        bool submodules = false;
        bool allRefs = false;

        std::string cacheType;

        /* Whether this is a local, non-bare repository. */
        bool isLocal = false;

        /* Whether this is a local, non-bare, dirty repository. */
        bool isDirty = false;

        /* Whether this repository has any commits. */
        bool hasHead = true;

        /* URL of the repo, or its path if isLocal. */
        std::string url;

        void checkDirty() const
        {
            if (isDirty) {
                if (!fetchSettings.allowDirty)
                    throw Error("Git tree '%s' is dirty", url);

                if (fetchSettings.warnDirty)
                    warn("Git tree '%s' is dirty", url);
            }
        }

        std::string gitDir = ".git";
    };

    RepoInfo getRepoInfo(const Input & input)
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
            .submodules = maybeGetBoolAttr(input.attrs, "submodules").value_or(false),
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
        if (!input.getRef() && !input.getRev() && repoInfo.isLocal) {
            repoInfo.isDirty = true;

            auto env = getEnv();
            /* Set LC_ALL to C: because we rely on the error messages
               from git rev-parse to determine what went wrong that
               way unknown errors can lead to a failure instead of
               continuing through the wrong code path. */
            env["LC_ALL"] = "C";

            /* Check whether HEAD points to something that looks like
               a commit, since that is the ref we want to use later
               on. */
            auto result = runProgram(RunOptions {
                .program = "git",
                .args = { "-C", repoInfo.url, "--git-dir", repoInfo.gitDir, "rev-parse", "--verify", "--no-revs", "HEAD^{commit}" },
                .environment = env,
                .mergeStderrToStdout = true
            });
            auto exitCode = WEXITSTATUS(result.first);
            auto errorMessage = result.second;

            if (errorMessage.find("fatal: not a git repository") != std::string::npos) {
                throw Error("'%s' is not a Git repository", repoInfo.url);
            } else if (errorMessage.find("fatal: Needed a single revision") != std::string::npos) {
                // indicates that the repo does not have any commits
                // we want to proceed and will consider it dirty later
            } else if (exitCode != 0) {
                // any other errors should lead to a failure
                throw Error("getting the HEAD of the Git tree '%s' failed with exit code %d:\n%s", repoInfo.url, exitCode, errorMessage);
            }

            repoInfo.hasHead = exitCode == 0;

            try {
                if (repoInfo.hasHead) {
                    // Using git diff is preferrable over lower-level operations here,
                    // because it's conceptually simpler and we only need the exit code anyways.
                    auto gitDiffOpts = Strings({ "-C", repoInfo.url, "--git-dir", repoInfo.gitDir, "diff", "HEAD", "--quiet"});
                    if (!repoInfo.submodules) {
                        // Changes in submodules should only make the tree dirty
                        // when those submodules will be copied as well.
                        gitDiffOpts.emplace_back("--ignore-submodules");
                    }
                    gitDiffOpts.emplace_back("--");
                    runProgram("git", true, gitDiffOpts);

                    repoInfo.isDirty = false;
                }
            } catch (ExecError & e) {
                if (!WIFEXITED(e.status) || WEXITSTATUS(e.status) != 1) throw;
            }
        }

        return repoInfo;
    }

    std::set<CanonPath> listFiles(const RepoInfo & repoInfo)
    {
        auto gitOpts = Strings({ "-C", repoInfo.url, "--git-dir", repoInfo.gitDir, "ls-files", "-z" });
        if (repoInfo.submodules)
            gitOpts.emplace_back("--recurse-submodules");

        std::set<CanonPath> res;

        for (auto & p : tokenizeString<std::set<std::string>>(
                runProgram("git", true, gitOpts), "\0"s))
            res.insert(CanonPath(p));

        return res;
    }

    void updateRev(Input & input, const RepoInfo & repoInfo, const std::string & ref)
    {
        if (!input.getRev())
            input.attrs.insert_or_assign("rev",
                Hash::parseAny(chomp(runProgram("git", true, { "-C", repoInfo.url, "--git-dir", repoInfo.gitDir, "rev-parse", ref })), htSHA1).gitRev());
    }

    uint64_t getLastModified(const RepoInfo & repoInfo, const std::string & repoDir, const std::string & ref)
    {
        return
            repoInfo.hasHead
            ? std::stoull(
                runProgram("git", true,
                    { "-C", repoDir, "--git-dir", repoInfo.gitDir, "log", "-1", "--format=%ct", "--no-show-signature", ref }))
            : 0;
    }

    uint64_t getRevCount(const RepoInfo & repoInfo, const std::string & repoDir, const Hash & rev)
    {
        // FIXME: cache this.
        return
            repoInfo.hasHead
            ? std::stoull(
                runProgram("git", true,
                    { "-C", repoDir, "--git-dir", repoInfo.gitDir, "rev-list", "--count", rev.gitRev() }))
            : 0;
    }

    std::string getDefaultRef(const RepoInfo & repoInfo)
    {
        auto head = repoInfo.isLocal
            ? readHead(repoInfo.url)
            : readHeadCached(repoInfo.url);
        if (!head) {
            warn("could not read HEAD ref from repo at '%s', using 'master'", repoInfo.url);
            return "master";
        }
        return *head;
    }

    std::pair<StorePath, Input> fetch(ref<Store> store, const Input & _input) override
    {
        Input input(_input);

        auto repoInfo = getRepoInfo(input);

        std::string name = input.getName();

        auto getLockedAttrs = [&]()
        {
            return Attrs({
                {"type", repoInfo.cacheType},
                {"name", name},
                {"rev", input.getRev()->gitRev()},
            });
        };

        auto makeResult = [&](const Attrs & infoAttrs, StorePath && storePath)
            -> std::pair<StorePath, Input>
        {
            assert(input.getRev());
            assert(!_input.getRev() || _input.getRev() == input.getRev());
            if (!repoInfo.shallow)
                input.attrs.insert_or_assign("revCount", getIntAttr(infoAttrs, "revCount"));
            input.attrs.insert_or_assign("lastModified", getIntAttr(infoAttrs, "lastModified"));
            return {std::move(storePath), input};
        };

        if (input.getRev()) {
            if (auto res = getCache()->lookup(store, getLockedAttrs()))
                return makeResult(res->first, std::move(res->second));
        }

        if (repoInfo.isDirty)
            return fetchFromWorkdir(store, repoInfo, std::move(input));

        auto originalRef = input.getRef();
        auto ref = originalRef ? *originalRef : getDefaultRef(repoInfo);
        input.attrs.insert_or_assign("ref", ref);

        Attrs unlockedAttrs({
            {"type", repoInfo.cacheType},
            {"name", name},
            {"url", repoInfo.url},
            {"ref", ref},
        });

        Path repoDir;

        if (repoInfo.isLocal) {
            updateRev(input, repoInfo, ref);
            repoDir = repoInfo.url;
        } else {
            if (auto res = getCache()->lookup(store, unlockedAttrs)) {
                auto rev2 = Hash::parseAny(getStrAttr(res->first, "rev"), htSHA1);
                if (!input.getRev() || input.getRev() == rev2) {
                    input.attrs.insert_or_assign("rev", rev2.gitRev());
                    return makeResult(res->first, std::move(res->second));
                }
            }

            Path cacheDir = getCachePath(repoInfo.url);
            repoDir = cacheDir;
            repoInfo.gitDir = ".";

            createDirs(dirOf(cacheDir));
            PathLocks cacheDirLock({cacheDir + ".lock"});

            if (!pathExists(cacheDir)) {
                runProgram("git", true, { "-c", "init.defaultBranch=" + gitInitialBranch, "init", "--bare", repoDir });
            }

            Path localRefFile =
                ref.compare(0, 5, "refs/") == 0
                ? cacheDir + "/" + ref
                : cacheDir + "/refs/heads/" + ref;

            bool doFetch;
            time_t now = time(0);

            /* If a rev was specified, we need to fetch if it's not in the
               repo. */
            if (input.getRev()) {
                try {
                    runProgram("git", true, { "-C", repoDir, "--git-dir", repoInfo.gitDir, "cat-file", "-e", input.getRev()->gitRev() });
                    doFetch = false;
                } catch (ExecError & e) {
                    if (WIFEXITED(e.status)) {
                        doFetch = true;
                    } else {
                        throw;
                    }
                }
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

                // FIXME: git stderr messes up our progress indicator, so
                // we're using --quiet for now. Should process its stderr.
                try {
                    auto fetchRef = repoInfo.allRefs
                        ? "refs/*"
                        : ref.compare(0, 5, "refs/") == 0
                            ? ref
                            : ref == "HEAD"
                                ? ref
                                : "refs/heads/" + ref;
                    runProgram("git", true,
                        { "-C", repoDir,
                          "--git-dir", repoInfo.gitDir,
                          "fetch",
                          "--quiet",
                          "--force",
                          "--",
                          repoInfo.url,
                          fmt("%s:%s", fetchRef, fetchRef)
                        });
                } catch (Error & e) {
                    if (!pathExists(localRefFile)) throw;
                    warn("could not update local clone of Git repository '%s'; continuing with the most recent version", repoInfo.url);
                }

                if (!touchCacheFile(localRefFile, now))
                    warn("could not update mtime for file '%s': %s", localRefFile, strerror(errno));
                if (!originalRef && !storeCachedHead(repoInfo.url, ref))
                    warn("could not update cached head '%s' for '%s'", ref, repoInfo.url);
            }

            if (!input.getRev())
                input.attrs.insert_or_assign("rev", Hash::parseAny(chomp(readFile(localRefFile)), htSHA1).gitRev());

            // cache dir lock is removed at scope end; we will only use read-only operations on specific revisions in the remainder
        }

        bool isShallow = chomp(runProgram("git", true, { "-C", repoDir, "--git-dir", repoInfo.gitDir, "rev-parse", "--is-shallow-repository" })) == "true";

        if (isShallow && !repoInfo.shallow)
            throw Error("'%s' is a shallow Git repository, but shallow repositories are only allowed when `shallow = true;` is specified", repoInfo.url);

        // FIXME: check whether rev is an ancestor of ref.

        printTalkative("using revision %s of repo '%s'", input.getRev()->gitRev(), repoInfo.url);

        /* Now that we know the ref, check again whether we have it in
           the store. */
        if (auto res = getCache()->lookup(store, getLockedAttrs()))
            return makeResult(res->first, std::move(res->second));

        Path tmpDir = createTempDir();
        AutoDelete delTmpDir(tmpDir, true);
        PathFilter filter = defaultPathFilter;

        auto result = runProgram(RunOptions {
            .program = "git",
            .args = { "-C", repoDir, "--git-dir", repoInfo.gitDir, "cat-file", "commit", input.getRev()->gitRev() },
            .mergeStderrToStdout = true
        });
        if (WEXITSTATUS(result.first) == 128
            && result.second.find("bad file") != std::string::npos)
        {
            throw Error(
                "Cannot find Git revision '%s' in ref '%s' of repository '%s'! "
                    "Please make sure that the " ANSI_BOLD "rev" ANSI_NORMAL " exists on the "
                    ANSI_BOLD "ref" ANSI_NORMAL " you've specified or add " ANSI_BOLD
                    "allRefs = true;" ANSI_NORMAL " to " ANSI_BOLD "fetchGit" ANSI_NORMAL ".",
                input.getRev()->gitRev(),
                ref,
                repoInfo.url
            );
        }

        if (repoInfo.submodules) {
            Path tmpGitDir = createTempDir();
            AutoDelete delTmpGitDir(tmpGitDir, true);

            runProgram("git", true, { "-c", "init.defaultBranch=" + gitInitialBranch, "init", tmpDir, "--separate-git-dir", tmpGitDir });
            // TODO: repoDir might lack the ref (it only checks if rev
            // exists, see FIXME above) so use a big hammer and fetch
            // everything to ensure we get the rev.
            runProgram("git", true, { "-C", tmpDir, "fetch", "--quiet", "--force",
                                      "--update-head-ok", "--", repoDir, "refs/*:refs/*" });

            runProgram("git", true, { "-C", tmpDir, "checkout", "--quiet", input.getRev()->gitRev() });
            runProgram("git", true, { "-C", tmpDir, "remote", "add", "origin", repoInfo.url });
            runProgram("git", true, { "-C", tmpDir, "submodule", "--quiet", "update", "--init", "--recursive" });

            filter = isNotDotGitDirectory;
        } else {
            // FIXME: should pipe this, or find some better way to extract a
            // revision.
            auto source = sinkToSource([&](Sink & sink) {
                runProgram2({
                    .program = "git",
                    .args = { "-C", repoDir, "--git-dir", repoInfo.gitDir, "archive", input.getRev()->gitRev() },
                    .standardOut = &sink
                });
            });

            unpackTarfile(*source, tmpDir);
        }

        auto storePath = store->addToStore(name, tmpDir, FileIngestionMethod::Recursive, htSHA256, filter);

        auto rev = *input.getRev();

        Attrs infoAttrs({
            {"rev", rev.gitRev()},
            {"lastModified", getLastModified(repoInfo, repoDir, rev.gitRev())},
        });

        if (!repoInfo.shallow)
            infoAttrs.insert_or_assign("revCount",
                getRevCount(repoInfo, repoDir, rev));

        if (!_input.getRev())
            getCache()->add(
                store,
                unlockedAttrs,
                infoAttrs,
                storePath,
                false);

        getCache()->add(
            store,
            getLockedAttrs(),
            infoAttrs,
            storePath,
            true);

        return makeResult(infoAttrs, std::move(storePath));
    }

    std::pair<StorePath, Input> fetchFromWorkdir(
        ref<Store> store,
        const RepoInfo & repoInfo,
        Input input)
    {
        /* This is an unclean working tree. So copy all tracked
           files. */
        repoInfo.checkDirty();

        auto files = listFiles(repoInfo);

        CanonPath repoDir(repoInfo.url);

        PathFilter filter = [&](const Path & p) -> bool {
            return CanonPath(p).removePrefix(repoDir).isAllowed(files);
        };

        auto storePath = store->addToStore(input.getName(), repoInfo.url, FileIngestionMethod::Recursive, htSHA256, filter);

        // FIXME: maybe we should use the timestamp of the last
        // modified dirty file?
        input.attrs.insert_or_assign(
            "lastModified",
            getLastModified(repoInfo, repoInfo.url, "HEAD"));

        return {std::move(storePath), input};
    }

    std::pair<ref<InputAccessor>, Input> lazyFetch(ref<Store> store, const Input & _input) override
    {
        Input input(_input);

        auto repoInfo = getRepoInfo(input);

        /* Unless we're using the working tree, copy the tree into the
           Nix store. TODO: We could have an accessor for fetching
           files from the Git repository directly. */
        if (input.getRef() || input.getRev() || !repoInfo.isLocal)
            return InputScheme::lazyFetch(store, input);

        repoInfo.checkDirty();

        auto ref = getDefaultRef(repoInfo);
        input.attrs.insert_or_assign("ref", ref);

        if (!repoInfo.isDirty) {
            updateRev(input, repoInfo, ref);

            input.attrs.insert_or_assign(
                "revCount",
                getRevCount(repoInfo, repoInfo.url, *input.getRev()));

            input.locked = true;
        }

        // FIXME: maybe we should use the timestamp of the last
        // modified dirty file?
        input.attrs.insert_or_assign(
            "lastModified",
            getLastModified(repoInfo, repoInfo.url, ref));

        auto makeNotAllowedError = [url{repoInfo.url}](const CanonPath & path) -> RestrictedPathError
        {
            if (nix::pathExists(path.abs()))
                return RestrictedPathError("access to path '%s' is forbidden because it is not under Git control; maybe you should 'git add' it to the repository '%s'?", path, url);
            else
                return RestrictedPathError("path '%s' does not exist in Git repository '%s'", path, url);
        };

        return {makeFSInputAccessor(CanonPath(repoInfo.url), listFiles(repoInfo), std::move(makeNotAllowedError)), input};
    }
};

static auto rGitInputScheme = OnStartup([] { registerInputScheme(std::make_unique<GitInputScheme>()); });

}
