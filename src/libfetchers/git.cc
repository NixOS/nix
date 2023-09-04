#include "fetchers.hh"
#include "cache.hh"
#include "globals.hh"
#include "tarfile.hh"
#include "store-api.hh"
#include "git.hh"
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

struct WorkdirInfo
{
    bool clean = false;
    bool hasHead = false;
};

// Returns whether a git workdir is clean and has commits.
WorkdirInfo getWorkdirInfo(const Input & input, const Path & workdir)
{
    const bool submodules = maybeGetBoolAttr(input.attrs, "submodules").value_or(false);
    std::string gitDir(".git");

    auto env = getEnv();
    // Set LC_ALL to C: because we rely on the error messages from git rev-parse to determine what went wrong
    // that way unknown errors can lead to a failure instead of continuing through the wrong code path
    env["LC_ALL"] = "C";

    /* Check whether HEAD points to something that looks like a commit,
       since that is the refrence we want to use later on. */
    auto result = runProgram(RunOptions {
        .program = "git",
        .args = { "-C", workdir, "--git-dir", gitDir, "rev-parse", "--verify", "--no-revs", "HEAD^{commit}" },
        .environment = env,
        .mergeStderrToStdout = true
    });
    auto exitCode = WEXITSTATUS(result.first);
    auto errorMessage = result.second;

    if (errorMessage.find("fatal: not a git repository") != std::string::npos) {
        throw Error("'%s' is not a Git repository", workdir);
    } else if (errorMessage.find("fatal: Needed a single revision") != std::string::npos) {
        // indicates that the repo does not have any commits
        // we want to proceed and will consider it dirty later
    } else if (exitCode != 0) {
        // any other errors should lead to a failure
        throw Error("getting the HEAD of the Git tree '%s' failed with exit code %d:\n%s", workdir, exitCode, errorMessage);
    }

    bool clean = false;
    bool hasHead = exitCode == 0;

    try {
        if (hasHead) {
            // Using git diff is preferrable over lower-level operations here,
            // because its conceptually simpler and we only need the exit code anyways.
            auto gitDiffOpts = Strings({ "-C", workdir, "--git-dir", gitDir, "diff", "HEAD", "--quiet"});
            if (!submodules) {
                // Changes in submodules should only make the tree dirty
                // when those submodules will be copied as well.
                gitDiffOpts.emplace_back("--ignore-submodules");
            }
            gitDiffOpts.emplace_back("--");
            runProgram("git", true, gitDiffOpts);

            clean = true;
        }
    } catch (ExecError & e) {
        if (!WIFEXITED(e.status) || WEXITSTATUS(e.status) != 1) throw;
    }

    return WorkdirInfo { .clean = clean, .hasHead = hasHead };
}

std::pair<StorePathDescriptor, Input> fetchFromWorkdir(ref<Store> store, Input & input, const Path & workdir, const WorkdirInfo & workdirInfo, FileIngestionMethod ingestionMethod)
{
    const bool submodules = maybeGetBoolAttr(input.attrs, "submodules").value_or(false);
    auto gitDir = ".git";

    if (!fetchSettings.allowDirty)
        throw Error("Git tree '%s' is dirty", workdir);

    if (fetchSettings.warnDirty)
        warn("Git tree '%s' is dirty", workdir);

    auto gitOpts = Strings({ "-C", workdir, "--git-dir", gitDir, "ls-files", "-z" });
    if (submodules)
        gitOpts.emplace_back("--recurse-submodules");

    auto files = tokenizeString<std::set<std::string>>(
        runProgram("git", true, gitOpts), "\0"s);

    Path actualPath(absPath(workdir));

    PathFilter filter = [&](const Path & p) -> bool {
        assert(hasPrefix(p, actualPath));
        std::string file(p, actualPath.size() + 1);

        auto st = lstat(p);

        if (S_ISDIR(st.st_mode)) {
            auto prefix = file + "/";
            auto i = files.lower_bound(prefix);
            return i != files.end() && hasPrefix(*i, prefix);
        }

        return files.count(file);
    };

    auto storePath = store->addToStore(input.getName(), actualPath, ingestionMethod, htSHA256, filter);
    // FIXME: just have Store::addToStore return a StorePathDescriptor, as
    // it has the underlying information.
    auto storePathDesc = store->queryPathInfo(storePath)->fullStorePathDescriptorOpt().value();

    // FIXME: maybe we should use the timestamp of the last
    // modified dirty file?
    input.attrs.insert_or_assign(
        "lastModified",
        workdirInfo.hasHead ? std::stoull(runProgram("git", true, { "-C", actualPath, "--git-dir", gitDir, "log", "-1", "--format=%ct", "--no-show-signature", "HEAD" })) : 0);

    return {std::move(storePathDesc), input};
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
            if (name == "rev" || name == "ref" || name == "treeHash" || name == "gitIngestion")
                attrs.emplace(name, value);
            else if (name == "shallow" || name == "submodules")
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
            if (name != "type" && name != "url" && name != "ref" && name != "rev" && name != "shallow" && name != "submodules" && name != "gitIngestion" && name != "treeHash" && name != "lastModified" && name != "revCount" && name != "narHash" && name != "allRefs" && name != "name")
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
        if (auto treeHash = input.getTreeHash()) url.query.insert_or_assign("treeHash", treeHash->gitRev());
        if (maybeGetBoolAttr(input.attrs, "gitIngestion").value_or((bool) input.getTreeHash()))
            url.query.insert_or_assign("gitIngestion", "1");
        if (auto ref = input.getRef()) url.query.insert_or_assign("ref", *ref);
        if (maybeGetBoolAttr(input.attrs, "shallow").value_or(false))
            url.query.insert_or_assign("shallow", "1");
        return url;
    }

    bool hasAllInfo(const Input & input) const override
    {
        bool maybeDirty = !input.getRef();
        bool shallow = maybeGetBoolAttr(input.attrs, "shallow").value_or(false);
        bool submodules = maybeGetBoolAttr(input.attrs, "submodules").value_or(false);
        /* FIXME just requiring tree hash is necessary for substitutions to
           work for now, but breaks eval purity. Need a better solution before
           upstreaming. */
        return (input.getTreeHash() && !submodules) || (
            maybeGetIntAttr(input.attrs, "lastModified")
            && (shallow || maybeDirty || maybeGetIntAttr(input.attrs, "revCount"))
            && input.getNarHash());
    }

    /* FIXME no overriding the tree hash / flake registry support for tree
       hashes, for now. */
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
        auto [isLocal, actualUrl] = getActualUrl(input);

        Strings args = {"clone"};

        args.push_back(actualUrl);

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
        if (url.scheme == "file" && !input.getRef() && !input.getRev() && !input.getTreeHash())
            return url.path;
        return {};
    }

    void markChangedFile(const Input & input, std::string_view file, std::optional<std::string> commitMsg) override
    {
        auto sourcePath = getSourcePath(input);
        assert(sourcePath);
        auto gitDir = ".git";

        runProgram("git", true,
            { "-C", *sourcePath, "--git-dir", gitDir, "add", "--intent-to-add", "--", std::string(file) });

        if (commitMsg)
            runProgram("git", true,
                { "-C", *sourcePath, "--git-dir", gitDir, "commit", std::string(file), "-m", *commitMsg });
    }

    std::pair<bool, std::string> getActualUrl(const Input & input) const
    {
        // file:// URIs are normally not cloned (but otherwise treated the
        // same as remote URIs, i.e. we don't use the working tree or
        // HEAD). Exception: If _NIX_FORCE_HTTP is set, or the repo is a bare git
        // repo, treat as a remote URI to force a clone.
        static bool forceHttp = getEnv("_NIX_FORCE_HTTP") == "1"; // for testing
        auto url = parseURL(getStrAttr(input.attrs, "url"));
        bool isBareRepository = url.scheme == "file" && !pathExists(url.path + "/.git");
        bool isLocal = url.scheme == "file" && !forceHttp && !isBareRepository;
        return {isLocal, isLocal ? url.path : url.base};
    }

    std::pair<StorePathDescriptor, Input> fetch(ref<Store> store, const Input & _input) override
    {
        Input input(_input);
        auto gitDir = ".git";

        std::string name = input.getName();

        bool shallow = maybeGetBoolAttr(input.attrs, "shallow").value_or(false);
        bool submodules = maybeGetBoolAttr(input.attrs, "submodules").value_or(false);
        bool allRefs = maybeGetBoolAttr(input.attrs, "allRefs").value_or(false);

        std::string cacheType = "git";
        if (shallow) cacheType += "-shallow";
        if (submodules) cacheType += "-submodules";
        if (allRefs) cacheType += "-all-refs";

        auto ingestionMethod =
            maybeGetBoolAttr(input.attrs, "gitIngestion").value_or((bool) input.getTreeHash())
            ? FileIngestionMethod::Git
            : FileIngestionMethod::Recursive;

        auto checkHashType = [&](const std::optional<Hash> & hash)
        {
            if (hash.has_value() && !(hash->type == htSHA1 || hash->type == htSHA256))
                throw Error("Hash '%s' is not supported by Git. Supported types are sha1 and sha256.", hash->to_string(Base16, true));
        };

        auto getLockedAttrs = [&]()
        {
            Attrs attrs({
                {"type", cacheType},
                {"name", name},
            });
            if (auto optH = input.getTreeHash()) {
                auto h = *std::move(optH);
                checkHashType(h);
                attrs.insert_or_assign("treeHash", h.gitRev());
            }
            if (auto optH = input.getRev()) {
                auto h = *std::move(optH);
                checkHashType(h);
                attrs.insert_or_assign("rev", h.gitRev());
            }
            if (maybeGetBoolAttr(input.attrs, "gitIngestion").value_or((bool) input.getTreeHash()))
                attrs.insert_or_assign("gitIngestion", true);
            return attrs;
        };

        auto makeResult = [&](const Attrs & infoAttrs, StorePathDescriptor && storePathDesc)
            -> std::pair<StorePathDescriptor, Input>
        {
            assert(input.getRev() || input.getTreeHash());
            /* If was originally set, that original value must be preserved. */
            assert(!_input.getRev() || _input.getRev() == input.getRev());
            assert(!_input.getTreeHash() || _input.getTreeHash() == input.getTreeHash());
            if (!shallow)
                input.attrs.insert_or_assign("revCount", getIntAttr(infoAttrs, "revCount"));
            input.attrs.insert_or_assign("lastModified", getIntAttr(infoAttrs, "lastModified"));
            return {std::move(storePathDesc), input};
        };

        if (input.getRev()) {
            if (auto res = getCache()->lookup(store, getLockedAttrs()))
                return makeResult(res->first, std::move(res->second));
        }

        auto [isLocal, actualUrl_] = getActualUrl(input);
        auto actualUrl = actualUrl_; // work around clang bug

        /* If this is a local directory and no ref or revision is given,
           allow fetching directly from a dirty workdir. */
        if (!input.getRef() && !input.getRev() && !input.getTreeHash() && isLocal) {
            auto workdirInfo = getWorkdirInfo(input, actualUrl);
            if (!workdirInfo.clean) {
                return fetchFromWorkdir(store, input, actualUrl, workdirInfo, ingestionMethod);
            }
        }

        Attrs unlockedAttrs({
            {"type", cacheType},
            {"name", name},
            {"url", actualUrl},
        });
        if (ingestionMethod == FileIngestionMethod::Git)
            unlockedAttrs.insert_or_assign("gitIngestion", true);

        Path repoDir;

        if (isLocal) {
            if (!input.getRef()) {
                auto head = readHead(actualUrl);
                if (!head) {
                    warn("could not read HEAD ref from repo at '%s', using 'master'", actualUrl);
                    head = "master";
                }
                input.attrs.insert_or_assign("ref", *head);
                unlockedAttrs.insert_or_assign("ref", *head);
            }

            if (!input.getRev() && !input.getTreeHash()) {
                auto getHash = [&](std::string rev) {
                    return Hash::parseAny(chomp(runProgram("git", true, { "-C", actualUrl, "--git-dir", gitDir, "rev-parse", rev })), htSHA1).gitRev();
                };
                input.attrs.insert_or_assign("rev", getHash(*input.getRef()));
                if (settings.isExperimentalFeatureEnabled(Xp::GitHashing))
                    input.attrs.insert_or_assign("treeHash", getHash(*input.getRef() + ":"));
            }

            repoDir = actualUrl;
        } else {
            const bool useHeadRef = !input.getRef();
            if (useHeadRef) {
                auto head = readHeadCached(actualUrl);
                if (!head) {
                    warn("could not read HEAD ref from repo at '%s', using 'master'", actualUrl);
                    head = "master";
                }
                input.attrs.insert_or_assign("ref", *head);
                unlockedAttrs.insert_or_assign("ref", *head);
            } else {
                if (!input.getRev()) {
                    unlockedAttrs.insert_or_assign("ref", input.getRef().value());
                }
            }

            if (auto res = getCache()->lookup(store, unlockedAttrs)) {
                bool found = false;

                if (std::optional revS = maybeGetStrAttr(res->first, "rev")) {
                    auto rev2 = Hash::parseAny(*revS, htSHA1);
                    if (!input.getRev() || input.getRev() == rev2) {
                        input.attrs.insert_or_assign("rev", rev2.gitRev());
                        found = true;
                    }
                }

                if (settings.isExperimentalFeatureEnabled(Xp::GitHashing)) {
                    if (std::optional treeHashS = maybeGetStrAttr(res->first, "treeHash")) {
                        auto treeHash2 = Hash::parseNonSRIUnprefixed(*treeHashS, htSHA1);
                        if (!input.getTreeHash() || input.getTreeHash() == treeHash2) {
                            input.attrs.insert_or_assign("treeHash", treeHash2.gitRev());
                            found = true;
                        }
                    }
                }

                bool correctIngestion =
                    maybeGetBoolAttr(input.attrs, "gitIngestion").value_or(false)
                    ? ingestionMethod == FileIngestionMethod::Git
                    : ingestionMethod == FileIngestionMethod::Recursive;

                if (correctIngestion && found)
                    return makeResult(res->first, std::move(res->second));
            }

            Path cacheDir = getCachePath(actualUrl);
            repoDir = cacheDir;
            gitDir = ".";

            createDirs(dirOf(cacheDir));
            PathLocks cacheDirLock({cacheDir + ".lock"});

            if (!pathExists(cacheDir)) {
                runProgram("git", true, { "-c", "init.defaultBranch=" + gitInitialBranch, "init", "--bare", repoDir });
            }

            Path localRefFile =
                input.getRef()->compare(0, 5, "refs/") == 0
                ? cacheDir + "/" + *input.getRef()
                : cacheDir + "/refs/heads/" + *input.getRef();

            bool doFetch;
            time_t now = time(0);

            /* If a rev or treeHash is specified, we need to fetch if
               it's not in the repo. */
            if (input.getRev() || input.getTreeHash()) {
                try {
                    auto fetchHash = input.getTreeHash() ? input.getTreeHash() : input.getRev();
                    runProgram("git", true, { "-C", repoDir, "--git-dir", gitDir, "cat-file", "-e", fetchHash->gitRev() });
                    doFetch = false;
                } catch (ExecError & e) {
                    if (WIFEXITED(e.status)) {
                        doFetch = true;
                    } else {
                        throw;
                    }
                }
            } else {
                if (allRefs) {
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
                Activity act(*logger, lvlTalkative, actUnknown, fmt("fetching Git repository '%s'", actualUrl));

                // FIXME: git stderr messes up our progress indicator, so
                // we're using --quiet for now. Should process its stderr.
                try {
                    auto ref = input.getRef();
                    auto fetchRef = allRefs
                        ? "refs/*"
                        : ref->compare(0, 5, "refs/") == 0
                            ? *ref
                            : ref == "HEAD"
                                ? *ref
                                : "refs/heads/" + *ref;
                    runProgram("git", true, { "-C", repoDir, "--git-dir", gitDir, "fetch", "--quiet", "--force", "--", actualUrl, fmt("%s:%s", fetchRef, fetchRef) });
                } catch (Error & e) {
                    if (!pathExists(localRefFile)) throw;
                    warn("could not update local clone of Git repository '%s'; continuing with the most recent version", actualUrl);
                }

                if (!touchCacheFile(localRefFile, now))
                    warn("could not update mtime for file '%s': %s", localRefFile, strerror(errno));
                if (useHeadRef && !storeCachedHead(actualUrl, *input.getRef()))
                    warn("could not update cached head '%s' for '%s'", *input.getRef(), actualUrl);
            }

            if (!input.getRev() && !input.getTreeHash()) {
                auto rev = Hash::parseAny(chomp(readFile(localRefFile)), htSHA1).gitRev();
                input.attrs.insert_or_assign("rev", rev);
                if (settings.isExperimentalFeatureEnabled(Xp::GitHashing))
                    input.attrs.insert_or_assign("treeHash",
                        Hash::parseAny(chomp(runProgram("git", true, { "-C", repoDir, "rev-parse", rev + ":" })), htSHA1).gitRev());
            }

            // cache dir lock is removed at scope end; we will only use read-only operations on specific revisions in the remainder
        }

        if (input.getTreeHash()) {
            auto type = chomp(runProgram("git", true, { "-C", repoDir, "--git-dir", gitDir, "cat-file", "-t", input.getTreeHash()->gitRev() }));
            if (type != "tree")
                throw Error("Need a tree object, found '%s' object in %s", type, input.getTreeHash()->gitRev());
        }

        bool isShallow = chomp(runProgram("git", true, { "-C", repoDir, "--git-dir", gitDir, "rev-parse", "--is-shallow-repository" })) == "true";

        if (isShallow && !shallow)
            throw Error("'%s' is a shallow Git repository, but shallow repositories are only allowed when `shallow = true;` is specified.", actualUrl);

        // FIXME: check whether rev is an ancestor of ref.

        if (auto rev = input.getRev())
            printTalkative("using revision %s of repo '%s'", rev->gitRev(), actualUrl);
        if (auto treeHash = input.getTreeHash())
            printTalkative("using tree %s of repo '%s'", treeHash->gitRev(), actualUrl);

        /* Now that we know the ref, check again whether we have it in
           the store. */
        if (auto res = getCache()->lookup(store, getLockedAttrs()))
            return makeResult(res->first, std::move(res->second));

        Path tmpDir = createTempDir();
        AutoDelete delTmpDir(tmpDir, true);
        PathFilter filter = defaultPathFilter;

        auto [fetchHash, fetchHashType] = input.getTreeHash()
            ? (std::pair { input.getTreeHash().value(), true })
            : (std::pair { input.getRev().value(), false });

        auto result = runProgram(RunOptions {
            .program = "git",
            .args = {
                "-C", repoDir,
                "--git-dir", gitDir,
                "cat-file",
                fetchHashType ? "tree" : "commit",
                fetchHash.gitRev(),
            },
            .mergeStderrToStdout = true,
        });
        if (WEXITSTATUS(result.first) == 128
            && result.second.find("bad file") != std::string::npos)
        {
            throw Error(
                "Cannot find Git revision '%s' in ref '%s' of repository '%s'! "
                "Please make sure that the " ANSI_BOLD "rev" ANSI_NORMAL " exists on the "
                ANSI_BOLD "ref" ANSI_NORMAL " you've specified or add " ANSI_BOLD
                "allRefs = true;" ANSI_NORMAL " to " ANSI_BOLD "fetchGit" ANSI_NORMAL ".",
                fetchHash.gitRev(),
                *input.getRef(),
                actualUrl
            );
        }

        if (submodules) {
            if (input.getTreeHash())
                throw Error("Cannot fetch specific tree hashes if there are submodules");
            if (ingestionMethod == FileIngestionMethod::Git)
                warn("Nix's computed git tree hash will be different when submodules are converted to regular directories");

            Path tmpGitDir = createTempDir();
            AutoDelete delTmpGitDir(tmpGitDir, true);

            runProgram("git", true, { "-c", "init.defaultBranch=" + gitInitialBranch, "init", tmpDir, "--separate-git-dir", tmpGitDir });
            // TODO: repoDir might lack the ref (it only checks if rev
            // exists, see FIXME above) so use a big hammer and fetch
            // everything to ensure we get the rev.
            runProgram("git", true, { "-C", tmpDir, "fetch", "--quiet", "--force",
                                      "--update-head-ok", "--", repoDir, "refs/*:refs/*" });

            runProgram("git", true, { "-C", tmpDir, "checkout", "--quiet", input.getTreeHash() ? input.getTreeHash()->gitRev() : input.getRev()->gitRev() });
            runProgram("git", true, { "-C", tmpDir, "remote", "add", "origin", actualUrl });
            runProgram("git", true, { "-C", tmpDir, "submodule", "--quiet", "update", "--init", "--recursive" });

            filter = isNotDotGitDirectory;
        } else {
            // FIXME: should pipe this, or find some better way to extract a
            // revision.
            auto source = sinkToSource([&](Sink & sink) {
                runProgram2({
                    .program = "git",
                    .args = { "-C", repoDir, "--git-dir", gitDir, "archive", fetchHash.gitRev() },
                    .standardOut = &sink
                });
            });

            unpackTarfile(*source, tmpDir);
        }

        auto storePath = store->addToStore(name, tmpDir, ingestionMethod, ingestionMethod == FileIngestionMethod::Git ? htSHA1 : htSHA256, filter);
        // FIXME: just have Store::addToStore return a StorePathDescriptor, as
        // it has the underlying information.
        auto storePathDesc = store->queryPathInfo(storePath)->fullStorePathDescriptorOpt().value();

        // verify treeHash is what we actually obtained in the nix store
        if (auto treeHash = input.getTreeHash()) {
            auto path = store->toRealPath(store->printStorePath(storePath));
            auto gotHash = dumpGitHash(htSHA1, path);
            if (gotHash != input.getTreeHash())
                throw Error("Git hash mismatch in input '%s' (%s), expected '%s', got '%s'",
                    input.to_string(), path, treeHash->gitRev(), gotHash.gitRev());
        }

        Attrs infoAttrs({});

        if (auto rev = input.getRev()) {
            infoAttrs.insert_or_assign("rev", rev->gitRev());
            auto lastModified = std::stoull(runProgram("git", true, { "-C", repoDir, "--git-dir", gitDir, "log", "-1", "--format=%ct", "--no-show-signature", rev->gitRev() }));
            infoAttrs.insert_or_assign("lastModified", lastModified);
        } else
            infoAttrs.insert_or_assign("lastModified", (uint64_t) 0);

        if (settings.isExperimentalFeatureEnabled(Xp::GitHashing))
            if (auto treeHash = input.getTreeHash())
                infoAttrs.insert_or_assign("treeHash", treeHash->gitRev());

        if (!shallow) {
            if (auto rev = input.getRev())
                infoAttrs.insert_or_assign("revCount",
                    std::stoull(runProgram("git", true, { "-C", repoDir, "--git-dir", gitDir, "rev-list", "--count", rev->gitRev() })));
            else
                infoAttrs.insert_or_assign("revCount", (uint64_t) 0);
        }

        if (!_input.getRev() && !_input.getTreeHash())
            getCache()->add(
                store,
                unlockedAttrs,
                infoAttrs,
                storePathDesc,
                false);

        getCache()->add(
            store,
            getLockedAttrs(),
            infoAttrs,
            storePathDesc,
            true);

        return makeResult(infoAttrs, std::move(storePathDesc));
    }
};

static auto rGitInputScheme = OnStartup([] { registerInputScheme(std::make_unique<GitInputScheme>()); });

}
