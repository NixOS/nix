#include "nix/util/error.hh"
#include "nix/fetchers/fetchers.hh"
#include "nix/util/users.hh"
#include "nix/fetchers/cache.hh"
#include "nix/store/globals.hh"
#include "nix/util/tarfile.hh"
#include "nix/store/store-api.hh"
#include "nix/util/url-parts.hh"
#include "nix/store/pathlocks.hh"
#include "nix/util/processes.hh"
#include "nix/util/git.hh"
#include "nix/fetchers/git-utils.hh"
#include "nix/util/logging.hh"
#include "nix/util/finally.hh"
#include "nix/fetchers/fetch-settings.hh"
#include "nix/util/json-utils.hh"
#include "nix/util/archive.hh"
#include "nix/util/mounted-source-accessor.hh"

#include <regex>
#include <string.h>
#include <sys/time.h>

#ifndef _WIN32
#  include <sys/wait.h>
#endif

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
    return st.st_mtime + static_cast<time_t>(settings.tarballTtl) > now;
}

Path getCachePath(std::string_view key, bool shallow)
{
    return getCacheDir() + "/gitv3/" + hashString(HashAlgorithm::SHA256, key).to_string(HashFormat::Nix32, false)
           + (shallow ? "-shallow" : "");
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
    auto [status, output] = runProgram(
        RunOptions{
            .program = "git",
            // FIXME: use 'HEAD' to avoid returning all refs
            .args = {"ls-remote", "--symref", path},
            .isInteractive = true,
        });
    if (status != 0)
        return std::nullopt;

    std::string_view line = output;
    line = line.substr(0, line.find("\n"));
    if (const auto parseResult = git::parseLsRemoteLine(line); parseResult && parseResult->reference == "HEAD") {
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
bool storeCachedHead(const std::string & actualUrl, bool shallow, const std::string & headRef)
{
    Path cacheDir = getCachePath(actualUrl, shallow);
    try {
        runProgram("git", true, {"-C", cacheDir, "--git-dir", ".", "symbolic-ref", "--", "HEAD", headRef});
    } catch (ExecError & e) {
        if (
#ifndef WIN32 // TODO abstract over exit status handling on Windows
            !WIFEXITED(e.status)
#else
            e.status != 0
#endif
        )
            throw;

        return false;
    }
    /* No need to touch refs/HEAD, because `git symbolic-ref` updates the mtime. */
    return true;
}

std::optional<std::string> readHeadCached(const std::string & actualUrl, bool shallow)
{
    // Create a cache path to store the branch of the HEAD ref. Append something
    // in front of the URL to prevent collision with the repository itself.
    Path cacheDir = getCachePath(actualUrl, shallow);
    Path headRefFile = cacheDir + "/HEAD";

    time_t now = time(0);
    struct stat st;
    std::optional<std::string> cachedRef;
    if (stat(headRefFile.c_str(), &st) == 0) {
        cachedRef = readHead(cacheDir);
        if (cachedRef != std::nullopt && *cachedRef != gitInitialBranch && isCacheFileWithinTtl(now, st)) {
            debug("using cached HEAD ref '%s' for repo '%s'", *cachedRef, actualUrl);
            return cachedRef;
        }
    }

    auto ref = readHead(actualUrl);
    if (ref)
        return ref;

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

std::vector<PublicKey> getPublicKeys(const Attrs & attrs)
{
    std::vector<PublicKey> publicKeys;
    if (attrs.contains("publicKeys")) {
        auto pubKeysJson = nlohmann::json::parse(getStrAttr(attrs, "publicKeys"));
        auto & pubKeys = getArray(pubKeysJson);

        for (auto & key : pubKeys) {
            publicKeys.push_back(key);
        }
    }
    if (attrs.contains("publicKey"))
        publicKeys.push_back(
            PublicKey{maybeGetStrAttr(attrs, "keytype").value_or("ssh-ed25519"), getStrAttr(attrs, "publicKey")});
    return publicKeys;
}

} // end namespace

static const Hash nullRev{HashAlgorithm::SHA1};

struct GitInputScheme : InputScheme
{
    std::optional<Input> inputFromURL(const Settings & settings, const ParsedURL & url, bool requireTree) const override
    {
        if (url.scheme != "git" && parseUrlScheme(url.scheme).application != "git")
            return {};

        auto url2(url);
        url2.query.clear();

        Attrs attrs;
        attrs.emplace("type", "git");

        for (auto & [name, value] : url.query) {
            if (name == "rev" || name == "ref" || name == "keytype" || name == "publicKey" || name == "publicKeys")
                attrs.emplace(name, value);
            else if (
                name == "shallow" || name == "submodules" || name == "lfs" || name == "exportIgnore"
                || name == "allRefs" || name == "verifyCommit")
                attrs.emplace(name, Explicit<bool>{value == "1"});
            else
                url2.query.emplace(name, value);
        }

        attrs.emplace("url", url2.to_string());

        return inputFromAttrs(settings, attrs);
    }

    std::string_view schemeName() const override
    {
        return "git";
    }

    StringSet allowedAttrs() const override
    {
        return {
            "url",
            "ref",
            "rev",
            "shallow",
            "submodules",
            "lfs",
            "exportIgnore",
            "lastModified",
            "revCount",
            "narHash",
            "allRefs",
            "name",
            "dirtyRev",
            "dirtyShortRev",
            "verifyCommit",
            "keytype",
            "publicKey",
            "publicKeys",
        };
    }

    std::optional<Input> inputFromAttrs(const Settings & settings, const Attrs & attrs) const override
    {
        for (auto & [name, _] : attrs)
            if (name == "verifyCommit" || name == "keytype" || name == "publicKey" || name == "publicKeys")
                experimentalFeatureSettings.require(Xp::VerifiedFetches);

        maybeGetBoolAttr(attrs, "verifyCommit");

        if (auto ref = maybeGetStrAttr(attrs, "ref"); ref && !isLegalRefName(*ref))
            throw BadURL("invalid Git branch/tag name '%s'", *ref);

        Input input{settings};
        input.attrs = attrs;
        input.attrs["url"] = fixGitURL(getStrAttr(attrs, "url")).to_string();
        getShallowAttr(input);
        getSubmodulesAttr(input);
        getAllRefsAttr(input);
        return input;
    }

    ParsedURL toURL(const Input & input) const override
    {
        auto url = parseURL(getStrAttr(input.attrs, "url"));
        if (url.scheme != "git")
            url.scheme = "git+" + url.scheme;
        if (auto rev = input.getRev())
            url.query.insert_or_assign("rev", rev->gitRev());
        if (auto ref = input.getRef())
            url.query.insert_or_assign("ref", *ref);
        if (getShallowAttr(input))
            url.query.insert_or_assign("shallow", "1");
        if (getLfsAttr(input))
            url.query.insert_or_assign("lfs", "1");
        if (getSubmodulesAttr(input))
            url.query.insert_or_assign("submodules", "1");
        if (maybeGetBoolAttr(input.attrs, "exportIgnore").value_or(false))
            url.query.insert_or_assign("exportIgnore", "1");
        if (maybeGetBoolAttr(input.attrs, "verifyCommit").value_or(false))
            url.query.insert_or_assign("verifyCommit", "1");
        auto publicKeys = getPublicKeys(input.attrs);
        if (publicKeys.size() == 1) {
            url.query.insert_or_assign("keytype", publicKeys.at(0).type);
            url.query.insert_or_assign("publicKey", publicKeys.at(0).key);
        } else if (publicKeys.size() > 1)
            url.query.insert_or_assign("publicKeys", publicKeys_to_string(publicKeys));
        return url;
    }

    Input applyOverrides(const Input & input, std::optional<std::string> ref, std::optional<Hash> rev) const override
    {
        auto res(input);
        if (rev)
            res.attrs.insert_or_assign("rev", rev->gitRev());
        if (ref)
            res.attrs.insert_or_assign("ref", *ref);
        if (!res.getRef() && res.getRev())
            throw Error("Git input '%s' has a commit hash but no branch/tag name", res.to_string());
        return res;
    }

    void clone(const Input & input, const Path & destDir) const override
    {
        auto repoInfo = getRepoInfo(input);

        Strings args = {"clone"};

        args.push_back(repoInfo.locationToArg());

        if (auto ref = input.getRef()) {
            args.push_back("--branch");
            args.push_back(*ref);
        }

        if (input.getRev())
            throw UnimplementedError("cloning a specific revision is not implemented");

        args.push_back(destDir);

        runProgram("git", true, args, {}, true);
    }

    std::optional<std::filesystem::path> getSourcePath(const Input & input) const override
    {
        return getRepoInfo(input).getPath();
    }

    void putFile(
        const Input & input,
        const CanonPath & path,
        std::string_view contents,
        std::optional<std::string> commitMsg) const override
    {
        auto repoInfo = getRepoInfo(input);
        auto repoPath = repoInfo.getPath();
        if (!repoPath)
            throw Error(
                "cannot commit '%s' to Git repository '%s' because it's not a working tree", path, input.to_string());

        writeFile(*repoPath / path.rel(), contents);

        auto result = runProgram(
            RunOptions{
                .program = "git",
                .args =
                    {"-C",
                     repoPath->string(),
                     "--git-dir",
                     repoInfo.gitDir,
                     "check-ignore",
                     "--quiet",
                     std::string(path.rel())},
            });
        auto exitCode =
#ifndef WIN32 // TODO abstract over exit status handling on Windows
            WEXITSTATUS(result.first)
#else
            result.first
#endif
            ;

        if (exitCode != 0) {
            // The path is not `.gitignore`d, we can add the file.
            runProgram(
                "git",
                true,
                {"-C",
                 repoPath->string(),
                 "--git-dir",
                 repoInfo.gitDir,
                 "add",
                 "--intent-to-add",
                 "--",
                 std::string(path.rel())});

            if (commitMsg) {
                // Pause the logger to allow for user input (such as a gpg passphrase) in `git commit`
                auto suspension = logger->suspend();
                runProgram(
                    "git",
                    true,
                    {"-C",
                     repoPath->string(),
                     "--git-dir",
                     repoInfo.gitDir,
                     "commit",
                     std::string(path.rel()),
                     "-F",
                     "-"},
                    *commitMsg);
            }
        }
    }

    struct RepoInfo
    {
        /* Either the path of the repo (for local, non-bare repos), or
           the URL (which is never a `file` URL). */
        std::variant<std::filesystem::path, ParsedURL> location;

        /* Working directory info: the complete list of files, and
           whether the working directory is dirty compared to HEAD. */
        GitRepo::WorkdirInfo workdirInfo;

        std::string locationToArg() const
        {
            return std::visit(
                overloaded{
                    [&](const std::filesystem::path & path) { return path.string(); },
                    [&](const ParsedURL & url) { return url.to_string(); }},
                location);
        }

        std::optional<std::filesystem::path> getPath() const
        {
            if (auto path = std::get_if<std::filesystem::path>(&location))
                return *path;
            else
                return std::nullopt;
        }

        void warnDirty(const Settings & settings) const
        {
            if (workdirInfo.isDirty) {
                if (!settings.allowDirty)
                    throw Error("Git tree '%s' is dirty", locationToArg());

                if (settings.warnDirty)
                    warn("Git tree '%s' is dirty", locationToArg());
            }
        }

        std::string gitDir = ".git";
    };

    bool getShallowAttr(const Input & input) const
    {
        return maybeGetBoolAttr(input.attrs, "shallow").value_or(false);
    }

    bool getSubmodulesAttr(const Input & input) const
    {
        return maybeGetBoolAttr(input.attrs, "submodules").value_or(false);
    }

    bool getLfsAttr(const Input & input) const
    {
        return maybeGetBoolAttr(input.attrs, "lfs").value_or(false);
    }

    bool getExportIgnoreAttr(const Input & input) const
    {
        return maybeGetBoolAttr(input.attrs, "exportIgnore").value_or(false);
    }

    bool getAllRefsAttr(const Input & input) const
    {
        return maybeGetBoolAttr(input.attrs, "allRefs").value_or(false);
    }

    RepoInfo getRepoInfo(const Input & input) const
    {
        auto checkHashAlgorithm = [&](const std::optional<Hash> & hash) {
            if (hash.has_value() && !(hash->algo == HashAlgorithm::SHA1 || hash->algo == HashAlgorithm::SHA256))
                throw Error(
                    "Hash '%s' is not supported by Git. Supported types are sha1 and sha256.",
                    hash->to_string(HashFormat::Base16, true));
        };

        if (auto rev = input.getRev())
            checkHashAlgorithm(rev);

        RepoInfo repoInfo;

        // file:// URIs are normally not cloned (but otherwise treated the
        // same as remote URIs, i.e. we don't use the working tree or
        // HEAD). Exception: If _NIX_FORCE_HTTP is set, or the repo is a bare git
        // repo, treat as a remote URI to force a clone.
        static bool forceHttp = getEnv("_NIX_FORCE_HTTP") == "1"; // for testing
        auto url = parseURL(getStrAttr(input.attrs, "url"));

        // Why are we checking for bare repository?
        // well if it's a bare repository we want to force a git fetch rather than copying the folder
        auto isBareRepository = [](PathView path) { return pathExists(path) && !pathExists(path + "/.git"); };

        // FIXME: here we turn a possibly relative path into an absolute path.
        // This allows relative git flake inputs to be resolved against the
        // **current working directory** (as in POSIX), which tends to work out
        // ok in the context of flakes, but is the wrong behavior,
        // as it should resolve against the flake.nix base directory instead.
        //
        // See: https://discourse.nixos.org/t/57783 and #9708
        //
        if (url.scheme == "file" && !forceHttp && !isBareRepository(renderUrlPathEnsureLegal(url.path))) {
            auto path = renderUrlPathEnsureLegal(url.path);

            if (!isAbsolute(path)) {
                warn(
                    "Fetching Git repository '%s', which uses a path relative to the current directory. "
                    "This is not supported and will stop working in a future release. "
                    "See https://github.com/NixOS/nix/issues/12281 for details.",
                    url);
            }

            // If we don't check here for the path existence, then we can give libgit2 any directory
            // and it will initialize them as git directories.
            if (!pathExists(path)) {
                throw Error("The path '%s' does not exist.", path);
            }
            repoInfo.location = std::filesystem::absolute(path);
        } else {
            if (url.scheme == "file")
                /* Query parameters are meaningless for file://, but
                   Git interprets them as part of the file name. So get
                   rid of them. */
                url.query.clear();
            /* Backward compatibility hack: In old versions of Nix, if you had
               a flake input like

                 inputs.foo.url = "git+https://foo/bar?dir=subdir";

               it would result in a lock file entry like

                 "original": {
                   "dir": "subdir",
                   "type": "git",
                   "url": "https://foo/bar?dir=subdir"
                 }

               New versions of Nix remove `?dir=subdir` from the `url` field,
               since the subdirectory is intended for `FlakeRef`, not the
               fetcher (and specifically the remote server), that is, the
               flakeref is parsed into

                 "original": {
                   "dir": "subdir",
                   "type": "git",
                   "url": "https://foo/bar"
                 }

               However, new versions of nix parsing old flake.lock files would pass the dir=
               query parameter in the "url" attribute to git, which will then complain.

               For this reason, we are filtering the `dir` query parameter from the URL
               before passing it to git. */
            url.query.erase("dir");
            repoInfo.location = url;
        }

        // If this is a local directory and no ref or revision is
        // given, then allow the use of an unclean working tree.
        if (auto repoPath = repoInfo.getPath(); !input.getRef() && !input.getRev() && repoPath)
            repoInfo.workdirInfo = GitRepo::getCachedWorkdirInfo(*repoPath);

        return repoInfo;
    }

    uint64_t getLastModified(
        const Settings & settings,
        const RepoInfo & repoInfo,
        const std::filesystem::path & repoDir,
        const Hash & rev) const
    {
        Cache::Key key{"gitLastModified", {{"rev", rev.gitRev()}}};

        auto cache = settings.getCache();

        if (auto res = cache->lookup(key))
            return getIntAttr(*res, "lastModified");

        auto lastModified = GitRepo::openRepo(repoDir)->getLastModified(rev);

        cache->upsert(key, {{"lastModified", lastModified}});

        return lastModified;
    }

    uint64_t getRevCount(
        const Settings & settings,
        const RepoInfo & repoInfo,
        const std::filesystem::path & repoDir,
        const Hash & rev) const
    {
        Cache::Key key{"gitRevCount", {{"rev", rev.gitRev()}}};

        auto cache = settings.getCache();

        if (auto revCountAttrs = cache->lookup(key))
            return getIntAttr(*revCountAttrs, "revCount");

        Activity act(
            *logger, lvlChatty, actUnknown, fmt("getting Git revision count of '%s'", repoInfo.locationToArg()));

        auto revCount = GitRepo::openRepo(repoDir)->getRevCount(rev);

        cache->upsert(key, Attrs{{"revCount", revCount}});

        return revCount;
    }

    std::string getDefaultRef(const RepoInfo & repoInfo, bool shallow) const
    {
        auto head = std::visit(
            overloaded{
                [&](const std::filesystem::path & path) { return GitRepo::openRepo(path)->getWorkdirRef(); },
                [&](const ParsedURL & url) { return readHeadCached(url.to_string(), shallow); }},
            repoInfo.location);
        if (!head) {
            warn("could not read HEAD ref from repo at '%s', using 'master'", repoInfo.locationToArg());
            return "master";
        }
        return *head;
    }

    static MakeNotAllowedError makeNotAllowedError(std::filesystem::path repoPath)
    {
        return [repoPath{std::move(repoPath)}](const CanonPath & path) -> RestrictedPathError {
            if (pathExists(repoPath / path.rel()))
                return RestrictedPathError(
                    "Path '%1%' in the repository %2% is not tracked by Git.\n"
                    "\n"
                    "To make it visible to Nix, run:\n"
                    "\n"
                    "git -C %2% add \"%1%\"",
                    path.rel(),
                    repoPath);
            else
                return RestrictedPathError("Path '%s' does not exist in Git repository %s.", path.rel(), repoPath);
        };
    }

    void verifyCommit(const Input & input, std::shared_ptr<GitRepo> repo) const
    {
        auto publicKeys = getPublicKeys(input.attrs);
        auto verifyCommit = maybeGetBoolAttr(input.attrs, "verifyCommit").value_or(!publicKeys.empty());

        if (verifyCommit) {
            if (input.getRev() && repo)
                repo->verifyCommit(*input.getRev(), publicKeys);
            else
                throw Error(
                    "commit verification is required for Git repository '%s', but it's dirty", input.to_string());
        }
    }

    std::pair<ref<SourceAccessor>, Input>
    getAccessorFromCommit(ref<Store> store, RepoInfo & repoInfo, Input && input) const
    {
        assert(!repoInfo.workdirInfo.isDirty);

        auto origRev = input.getRev();

        auto originalRef = input.getRef();
        bool shallow = getShallowAttr(input);
        auto ref = originalRef ? *originalRef : getDefaultRef(repoInfo, shallow);
        input.attrs.insert_or_assign("ref", ref);

        std::filesystem::path repoDir;

        if (auto repoPath = repoInfo.getPath()) {
            repoDir = *repoPath;
            if (!input.getRev())
                input.attrs.insert_or_assign("rev", GitRepo::openRepo(repoDir)->resolveRef(ref).gitRev());
        } else {
            auto repoUrl = std::get<ParsedURL>(repoInfo.location);
            std::filesystem::path cacheDir = getCachePath(repoUrl.to_string(), shallow);
            repoDir = cacheDir;
            repoInfo.gitDir = ".";

            std::filesystem::create_directories(cacheDir.parent_path());
            PathLocks cacheDirLock({cacheDir.string()});

            auto repo = GitRepo::openRepo(cacheDir, true, true);

            // We need to set the origin so resolving submodule URLs works
            repo->setRemote("origin", repoUrl.to_string());

            auto localRefFile = ref.compare(0, 5, "refs/") == 0 ? cacheDir / ref : cacheDir / "refs/heads" / ref;

            bool doFetch = false;
            time_t now = time(0);

            /* If a rev was specified, we need to fetch if it's not in the
               repo. */
            if (auto rev = input.getRev()) {
                doFetch = !repo->hasObject(*rev);
            } else {
                if (getAllRefsAttr(input)) {
                    doFetch = true;
                } else {
                    /* If the local ref is older than ‘tarball-ttl’ seconds, do a
                       git fetch to update the local ref to the remote ref. */
                    struct stat st;
                    doFetch = stat(localRefFile.string().c_str(), &st) != 0 || !isCacheFileWithinTtl(now, st);
                }
            }

            if (doFetch) {
                bool shallow = getShallowAttr(input);
                try {
                    auto fetchRef = getAllRefsAttr(input)             ? "refs/*:refs/*"
                                    : input.getRev()                  ? input.getRev()->gitRev()
                                    : ref.compare(0, 5, "refs/") == 0 ? fmt("%1%:%1%", ref)
                                    : ref == "HEAD"                   ? ref
                                                                      : fmt("%1%:%1%", "refs/heads/" + ref);

                    repo->fetch(repoUrl.to_string(), fetchRef, shallow);
                } catch (Error & e) {
                    if (!std::filesystem::exists(localRefFile))
                        throw;
                    logError(e.info());
                    warn(
                        "could not update local clone of Git repository '%s'; continuing with the most recent version",
                        repoInfo.locationToArg());
                }

                try {
                    if (!input.getRev())
                        setWriteTime(localRefFile, now, now);
                } catch (Error & e) {
                    warn("could not update mtime for file %s: %s", localRefFile, e.info().msg);
                }
                if (!originalRef && !storeCachedHead(repoUrl.to_string(), shallow, ref))
                    warn("could not update cached head '%s' for '%s'", ref, repoInfo.locationToArg());
            }

            if (auto rev = input.getRev()) {
                if (!repo->hasObject(*rev))
                    throw Error(
                        "Cannot find Git revision '%s' in ref '%s' of repository '%s'! "
                        "Please make sure that the " ANSI_BOLD "rev" ANSI_NORMAL " exists on the " ANSI_BOLD
                        "ref" ANSI_NORMAL " you've specified or add " ANSI_BOLD "allRefs = true;" ANSI_NORMAL
                        " to " ANSI_BOLD "fetchGit" ANSI_NORMAL ".",
                        rev->gitRev(),
                        ref,
                        repoInfo.locationToArg());
            } else
                input.attrs.insert_or_assign("rev", repo->resolveRef(ref).gitRev());

            // cache dir lock is removed at scope end; we will only use read-only operations on specific revisions in
            // the remainder
        }

        auto repo = GitRepo::openRepo(repoDir);

        auto isShallow = repo->isShallow();

        if (isShallow && !getShallowAttr(input))
            throw Error(
                "'%s' is a shallow Git repository, but shallow repositories are only allowed when `shallow = true;` is specified",
                repoInfo.locationToArg());

        // FIXME: check whether rev is an ancestor of ref?

        auto rev = *input.getRev();

        input.attrs.insert_or_assign("lastModified", getLastModified(*input.settings, repoInfo, repoDir, rev));

        if (!getShallowAttr(input))
            input.attrs.insert_or_assign("revCount", getRevCount(*input.settings, repoInfo, repoDir, rev));

        printTalkative("using revision %s of repo '%s'", rev.gitRev(), repoInfo.locationToArg());

        verifyCommit(input, repo);

        bool exportIgnore = getExportIgnoreAttr(input);
        bool smudgeLfs = getLfsAttr(input);
        auto accessor = repo->getAccessor(rev, exportIgnore, "«" + input.to_string() + "»", smudgeLfs);

        /* If the repo has submodules, fetch them and return a mounted
           input accessor consisting of the accessor for the top-level
           repo and the accessors for the submodules. */
        if (getSubmodulesAttr(input)) {
            std::map<CanonPath, nix::ref<SourceAccessor>> mounts;

            for (auto & [submodule, submoduleRev] : repo->getSubmodules(rev, exportIgnore)) {
                auto resolved = repo->resolveSubmoduleUrl(submodule.url);
                debug(
                    "Git submodule %s: %s %s %s -> %s",
                    submodule.path,
                    submodule.url,
                    submodule.branch,
                    submoduleRev.gitRev(),
                    resolved);
                fetchers::Attrs attrs;
                attrs.insert_or_assign("type", "git");
                attrs.insert_or_assign("url", resolved);
                if (submodule.branch != "") {
                    // A special value of . is used to indicate that the name of the branch in the submodule
                    // should be the same name as the current branch in the current repository.
                    // https://git-scm.com/docs/gitmodules
                    if (submodule.branch == ".") {
                        attrs.insert_or_assign("ref", ref);
                    } else {
                        attrs.insert_or_assign("ref", submodule.branch);
                    }
                }
                attrs.insert_or_assign("rev", submoduleRev.gitRev());
                attrs.insert_or_assign("exportIgnore", Explicit<bool>{exportIgnore});
                attrs.insert_or_assign("submodules", Explicit<bool>{true});
                attrs.insert_or_assign("lfs", Explicit<bool>{smudgeLfs});
                attrs.insert_or_assign("allRefs", Explicit<bool>{true});
                auto submoduleInput = fetchers::Input::fromAttrs(*input.settings, std::move(attrs));
                auto [submoduleAccessor, submoduleInput2] = submoduleInput.getAccessor(store);
                submoduleAccessor->setPathDisplay("«" + submoduleInput.to_string() + "»");
                mounts.insert_or_assign(submodule.path, submoduleAccessor);
            }

            if (!mounts.empty()) {
                mounts.insert_or_assign(CanonPath::root, accessor);
                accessor = makeMountedSourceAccessor(std::move(mounts));
            }
        }

        assert(!origRev || origRev == rev);

        return {accessor, std::move(input)};
    }

    std::pair<ref<SourceAccessor>, Input>
    getAccessorFromWorkdir(ref<Store> store, RepoInfo & repoInfo, Input && input) const
    {
        auto repoPath = repoInfo.getPath().value();

        if (getSubmodulesAttr(input))
            /* Create mountpoints for the submodules. */
            for (auto & submodule : repoInfo.workdirInfo.submodules)
                repoInfo.workdirInfo.files.insert(submodule.path);

        auto repo = GitRepo::openRepo(repoPath, false, false);

        auto exportIgnore = getExportIgnoreAttr(input);

        ref<SourceAccessor> accessor =
            repo->getAccessor(repoInfo.workdirInfo, exportIgnore, makeNotAllowedError(repoPath));

        /* If the repo has submodules, return a mounted input accessor
           consisting of the accessor for the top-level repo and the
           accessors for the submodule workdirs. */
        if (getSubmodulesAttr(input) && !repoInfo.workdirInfo.submodules.empty()) {
            std::map<CanonPath, nix::ref<SourceAccessor>> mounts;

            for (auto & submodule : repoInfo.workdirInfo.submodules) {
                auto submodulePath = repoPath / submodule.path.rel();
                fetchers::Attrs attrs;
                attrs.insert_or_assign("type", "git");
                attrs.insert_or_assign("url", submodulePath.string());
                attrs.insert_or_assign("exportIgnore", Explicit<bool>{exportIgnore});
                attrs.insert_or_assign("submodules", Explicit<bool>{true});
                // TODO: fall back to getAccessorFromCommit-like fetch when submodules aren't checked out
                // attrs.insert_or_assign("allRefs", Explicit<bool>{ true });

                auto submoduleInput = fetchers::Input::fromAttrs(*input.settings, std::move(attrs));
                auto [submoduleAccessor, submoduleInput2] = submoduleInput.getAccessor(store);
                submoduleAccessor->setPathDisplay("«" + submoduleInput.to_string() + "»");

                /* If the submodule is dirty, mark this repo dirty as
                   well. */
                if (!submoduleInput2.getRev())
                    repoInfo.workdirInfo.isDirty = true;

                mounts.insert_or_assign(submodule.path, submoduleAccessor);
            }

            mounts.insert_or_assign(CanonPath::root, accessor);
            accessor = makeMountedSourceAccessor(std::move(mounts));
        }

        if (!repoInfo.workdirInfo.isDirty) {
            auto repo = GitRepo::openRepo(repoPath);

            if (auto ref = repo->getWorkdirRef())
                input.attrs.insert_or_assign("ref", *ref);

            /* Return a rev of 000... if there are no commits yet. */
            auto rev = repoInfo.workdirInfo.headRev.value_or(nullRev);

            input.attrs.insert_or_assign("rev", rev.gitRev());
            if (!getShallowAttr(input)) {
                input.attrs.insert_or_assign(
                    "revCount", rev == nullRev ? 0 : getRevCount(*input.settings, repoInfo, repoPath, rev));
            }

            verifyCommit(input, repo);
        } else {
            repoInfo.warnDirty(*input.settings);

            if (repoInfo.workdirInfo.headRev) {
                input.attrs.insert_or_assign("dirtyRev", repoInfo.workdirInfo.headRev->gitRev() + "-dirty");
                input.attrs.insert_or_assign("dirtyShortRev", repoInfo.workdirInfo.headRev->gitShortRev() + "-dirty");
            }

            verifyCommit(input, nullptr);
        }

        input.attrs.insert_or_assign(
            "lastModified",
            repoInfo.workdirInfo.headRev
                ? getLastModified(*input.settings, repoInfo, repoPath, *repoInfo.workdirInfo.headRev)
                : 0);

        return {accessor, std::move(input)};
    }

    std::pair<ref<SourceAccessor>, Input> getAccessor(ref<Store> store, const Input & _input) const override
    {
        Input input(_input);

        auto repoInfo = getRepoInfo(input);

        if (getExportIgnoreAttr(input) && getSubmodulesAttr(input)) {
            /* In this situation, we don't have a git CLI behavior that we can copy.
               `git archive` does not support submodules, so it is unclear whether
               rules from the parent should affect the submodule or not.
               When git may eventually implement this, we need Nix to match its
               behavior. */
            throw UnimplementedError("exportIgnore and submodules are not supported together yet");
        }

        auto [accessor, final] = input.getRef() || input.getRev() || !repoInfo.getPath()
                                     ? getAccessorFromCommit(store, repoInfo, std::move(input))
                                     : getAccessorFromWorkdir(store, repoInfo, std::move(input));

        return {accessor, std::move(final)};
    }

    std::optional<std::string> getFingerprint(ref<Store> store, const Input & input) const override
    {
        auto makeFingerprint = [&](const Hash & rev) {
            return rev.gitRev() + (getSubmodulesAttr(input) ? ";s" : "") + (getExportIgnoreAttr(input) ? ";e" : "")
                   + (getLfsAttr(input) ? ";l" : "");
        };

        if (auto rev = input.getRev())
            return makeFingerprint(*rev);
        else {
            auto repoInfo = getRepoInfo(input);
            if (auto repoPath = repoInfo.getPath(); repoPath && repoInfo.workdirInfo.submodules.empty()) {
                /* Calculate a fingerprint that takes into account the
                   deleted and modified/added files. */
                HashSink hashSink{HashAlgorithm::SHA512};
                for (auto & file : repoInfo.workdirInfo.dirtyFiles) {
                    writeString("modified:", hashSink);
                    writeString(file.abs(), hashSink);
                    dumpPath((*repoPath / file.rel()).string(), hashSink);
                }
                for (auto & file : repoInfo.workdirInfo.deletedFiles) {
                    writeString("deleted:", hashSink);
                    writeString(file.abs(), hashSink);
                }
                return makeFingerprint(repoInfo.workdirInfo.headRev.value_or(nullRev))
                       + ";d=" + hashSink.finish().hash.to_string(HashFormat::Base16, false);
            }
            return std::nullopt;
        }
    }

    bool isLocked(const Input & input) const override
    {
        auto rev = input.getRev();
        return rev && rev != nullRev;
    }
};

static auto rGitInputScheme = OnStartup([] { registerInputScheme(std::make_unique<GitInputScheme>()); });

} // namespace nix::fetchers
