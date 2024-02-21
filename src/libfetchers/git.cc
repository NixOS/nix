#include "error.hh"
#include "fetchers.hh"
#include "users.hh"
#include "cache.hh"
#include "globals.hh"
#include "tarfile.hh"
#include "store-api.hh"
#include "url-parts.hh"
#include "pathlocks.hh"
#include "processes.hh"
#include "git.hh"
#include "fs-input-accessor.hh"
#include "mounted-input-accessor.hh"
#include "git-utils.hh"
#include "logging.hh"
#include "finally.hh"

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

Path getCachePath(std::string_view key, bool shallow)
{
    return getCacheDir()
    + "/nix/gitv3/"
    + hashString(HashAlgorithm::SHA256, key).to_string(HashFormat::Nix32, false)
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
    // set shallow=false as HEAD will never be queried for a shallow repo
    Path cacheDir = getCachePath(actualUrl, false);
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
    // set shallow=false as HEAD will never be queried for a shallow repo
    Path cacheDir = getCachePath(actualUrl, false);
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

std::vector<PublicKey> getPublicKeys(const Attrs & attrs)
{
    std::vector<PublicKey> publicKeys;
    if (attrs.contains("publicKeys")) {
        nlohmann::json publicKeysJson = nlohmann::json::parse(getStrAttr(attrs, "publicKeys"));
        ensureType(publicKeysJson, nlohmann::json::value_t::array);
        publicKeys = publicKeysJson.get<std::vector<PublicKey>>();
    }
    if (attrs.contains("publicKey"))
        publicKeys.push_back(PublicKey{maybeGetStrAttr(attrs, "keytype").value_or("ssh-ed25519"),getStrAttr(attrs, "publicKey")});
    return publicKeys;
}

}  // end namespace

static const Hash nullRev{HashAlgorithm::SHA1};

struct GitInputScheme : InputScheme
{
    std::optional<Input> inputFromURL(const ParsedURL & url, bool requireTree) const override
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
            if (name == "rev" || name == "ref" || name == "keytype" || name == "publicKey" || name == "publicKeys")
                attrs.emplace(name, value);
            else if (name == "shallow" || name == "submodules" || name == "exportIgnore" || name == "allRefs" || name == "verifyCommit")
                attrs.emplace(name, Explicit<bool> { value == "1" });
            else
                url2.query.emplace(name, value);
        }

        attrs.emplace("url", url2.to_string());

        return inputFromAttrs(attrs);
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

    std::optional<Input> inputFromAttrs(const Attrs & attrs) const override
    {
        for (auto & [name, _] : attrs)
            if (name == "verifyCommit"
                || name == "keytype"
                || name == "publicKey"
                || name == "publicKeys")
                experimentalFeatureSettings.require(Xp::VerifiedFetches);

        maybeGetBoolAttr(attrs, "verifyCommit");

        if (auto ref = maybeGetStrAttr(attrs, "ref")) {
            if (std::regex_search(*ref, badGitRefRegex))
                throw BadURL("invalid Git branch/tag name '%s'", *ref);
        }

        Input input;
        input.attrs = attrs;
        auto url = fixGitURL(getStrAttr(attrs, "url"));
        parseURL(url);
        input.attrs["url"] = url;
        getShallowAttr(input);
        getSubmodulesAttr(input);
        getAllRefsAttr(input);
        return input;
    }

    ParsedURL toURL(const Input & input) const override
    {
        auto url = parseURL(getStrAttr(input.attrs, "url"));
        if (url.scheme != "git") url.scheme = "git+" + url.scheme;
        if (auto rev = input.getRev()) url.query.insert_or_assign("rev", rev->gitRev());
        if (auto ref = input.getRef()) url.query.insert_or_assign("ref", *ref);
        if (getShallowAttr(input))
            url.query.insert_or_assign("shallow", "1");
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
        }
        else if (publicKeys.size() > 1)
            url.query.insert_or_assign("publicKeys", publicKeys_to_string(publicKeys));
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

        writeFile((CanonPath(repoInfo.url) / path).abs(), contents);

        auto result = runProgram(RunOptions {
            .program = "git",
            .args = {"-C", repoInfo.url, "--git-dir", repoInfo.gitDir, "check-ignore", "--quiet", std::string(path.rel())},
        });
        auto exitCode = WEXITSTATUS(result.first);

        if (exitCode != 0) {
            // The path is not `.gitignore`d, we can add the file.
            runProgram("git", true,
                { "-C", repoInfo.url, "--git-dir", repoInfo.gitDir, "add", "--intent-to-add", "--", std::string(path.rel()) });


            if (commitMsg) {
                // Pause the logger to allow for user input (such as a gpg passphrase) in `git commit`
                logger->pause();
                Finally restoreLogger([]() { logger->resume(); });
                runProgram("git", true,
                    { "-C", repoInfo.url, "--git-dir", repoInfo.gitDir, "commit", std::string(path.rel()), "-m", *commitMsg });
            }
        }
    }

    struct RepoInfo
    {
        /* Whether this is a local, non-bare repository. */
        bool isLocal = false;

        /* Working directory info: the complete list of files, and
           whether the working directory is dirty compared to HEAD. */
        GitRepo::WorkdirInfo workdirInfo;

        /* URL of the repo, or its path if isLocal. Never a `file` URL. */
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

    bool getShallowAttr(const Input & input) const
    {
        return maybeGetBoolAttr(input.attrs, "shallow").value_or(false);
    }

    bool getSubmodulesAttr(const Input & input) const
    {
        return maybeGetBoolAttr(input.attrs, "submodules").value_or(false);
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
        auto checkHashAlgorithm = [&](const std::optional<Hash> & hash)
        {
            if (hash.has_value() && !(hash->algo == HashAlgorithm::SHA1 || hash->algo == HashAlgorithm::SHA256))
                throw Error("Hash '%s' is not supported by Git. Supported types are sha1 and sha256.", hash->to_string(HashFormat::Base16, true));
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
        bool isBareRepository = url.scheme == "file" && !pathExists(url.path + "/.git");
        repoInfo.isLocal = url.scheme == "file" && !forceHttp && !isBareRepository;
        repoInfo.url = repoInfo.isLocal ? url.path : url.base;

        // If this is a local directory and no ref or revision is
        // given, then allow the use of an unclean working tree.
        if (!input.getRef() && !input.getRev() && repoInfo.isLocal)
            repoInfo.workdirInfo = GitRepo::openRepo(repoInfo.url)->getWorkdirInfo();

        return repoInfo;
    }

    uint64_t getLastModified(const RepoInfo & repoInfo, const std::string & repoDir, const Hash & rev) const
    {
        Attrs key{{"_what", "gitLastModified"}, {"rev", rev.gitRev()}};

        auto cache = getCache();

        if (auto res = cache->lookup(key))
            return getIntAttr(*res, "lastModified");

        auto lastModified = GitRepo::openRepo(repoDir)->getLastModified(rev);

        cache->upsert(key, Attrs{{"lastModified", lastModified}});

        return lastModified;
    }

    uint64_t getRevCount(const RepoInfo & repoInfo, const std::string & repoDir, const Hash & rev) const
    {
        Attrs key{{"_what", "gitRevCount"}, {"rev", rev.gitRev()}};

        auto cache = getCache();

        if (auto revCountAttrs = cache->lookup(key))
            return getIntAttr(*revCountAttrs, "revCount");

        Activity act(*logger, lvlChatty, actUnknown, fmt("getting Git revision count of '%s'", repoInfo.url));

        auto revCount = GitRepo::openRepo(repoDir)->getRevCount(rev);

        cache->upsert(key, Attrs{{"revCount", revCount}});

        return revCount;
    }

    std::string getDefaultRef(const RepoInfo & repoInfo) const
    {
        auto head = repoInfo.isLocal
            ? GitRepo::openRepo(repoInfo.url)->getWorkdirRef()
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

    void verifyCommit(const Input & input, std::shared_ptr<GitRepo> repo) const
    {
        auto publicKeys = getPublicKeys(input.attrs);
        auto verifyCommit = maybeGetBoolAttr(input.attrs, "verifyCommit").value_or(!publicKeys.empty());

        if (verifyCommit) {
            if (input.getRev() && repo)
                repo->verifyCommit(*input.getRev(), publicKeys);
            else
                throw Error("commit verification is required for Git repository '%s', but it's dirty", input.to_string());
        }
    }

    std::pair<ref<InputAccessor>, Input> getAccessorFromCommit(
        ref<Store> store,
        RepoInfo & repoInfo,
        Input && input) const
    {
        assert(!repoInfo.workdirInfo.isDirty);

        auto origRev = input.getRev();

        std::string name = input.getName();

        auto originalRef = input.getRef();
        auto ref = originalRef ? *originalRef : getDefaultRef(repoInfo);
        input.attrs.insert_or_assign("ref", ref);

        Path repoDir;

        if (repoInfo.isLocal) {
            repoDir = repoInfo.url;
            if (!input.getRev())
                input.attrs.insert_or_assign("rev", GitRepo::openRepo(repoDir)->resolveRef(ref).gitRev());
        } else {
            Path cacheDir = getCachePath(repoInfo.url, getShallowAttr(input));
            repoDir = cacheDir;
            repoInfo.gitDir = ".";

            createDirs(dirOf(cacheDir));
            PathLocks cacheDirLock({cacheDir});

            auto repo = GitRepo::openRepo(cacheDir, true, true);

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
                if (getAllRefsAttr(input)) {
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
                try {
                    auto fetchRef =
                        getAllRefsAttr(input)
                        ? "refs/*"
                        : input.getRev()
                        ? input.getRev()->gitRev()
                        : ref.compare(0, 5, "refs/") == 0
                        ? ref
                        : ref == "HEAD"
                        ? ref
                        : "refs/heads/" + ref;

                    repo->fetch(repoInfo.url, fmt("%s:%s", fetchRef, fetchRef), getShallowAttr(input));
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
                input.attrs.insert_or_assign("rev", Hash::parseAny(chomp(readFile(localRefFile)), HashAlgorithm::SHA1).gitRev());

            // cache dir lock is removed at scope end; we will only use read-only operations on specific revisions in the remainder
        }

        auto repo = GitRepo::openRepo(repoDir);

        auto isShallow = repo->isShallow();

        if (isShallow && !getShallowAttr(input))
            throw Error("'%s' is a shallow Git repository, but shallow repositories are only allowed when `shallow = true;` is specified", repoInfo.url);

        // FIXME: check whether rev is an ancestor of ref?

        auto rev = *input.getRev();

        Attrs infoAttrs({
            {"rev", rev.gitRev()},
            {"lastModified", getLastModified(repoInfo, repoDir, rev)},
        });

        if (!getShallowAttr(input))
            infoAttrs.insert_or_assign("revCount",
                getRevCount(repoInfo, repoDir, rev));

        printTalkative("using revision %s of repo '%s'", rev.gitRev(), repoInfo.url);

        verifyCommit(input, repo);

        bool exportIgnore = getExportIgnoreAttr(input);
        auto accessor = repo->getAccessor(rev, exportIgnore);

        accessor->setPathDisplay("«" + input.to_string() + "»");

        /* If the repo has submodules, fetch them and return a mounted
           input accessor consisting of the accessor for the top-level
           repo and the accessors for the submodules. */
        if (getSubmodulesAttr(input)) {
            std::map<CanonPath, nix::ref<InputAccessor>> mounts;

            for (auto & [submodule, submoduleRev] : repo->getSubmodules(rev, exportIgnore)) {
                auto resolved = repo->resolveSubmoduleUrl(submodule.url, repoInfo.url);
                debug("Git submodule %s: %s %s %s -> %s",
                    submodule.path, submodule.url, submodule.branch, submoduleRev.gitRev(), resolved);
                fetchers::Attrs attrs;
                attrs.insert_or_assign("type", "git");
                attrs.insert_or_assign("url", resolved);
                if (submodule.branch != "")
                    attrs.insert_or_assign("ref", submodule.branch);
                attrs.insert_or_assign("rev", submoduleRev.gitRev());
                attrs.insert_or_assign("exportIgnore", Explicit<bool>{ exportIgnore });
                auto submoduleInput = fetchers::Input::fromAttrs(std::move(attrs));
                auto [submoduleAccessor, submoduleInput2] =
                    submoduleInput.getAccessor(store);
                mounts.insert_or_assign(submodule.path, submoduleAccessor);
            }

            if (!mounts.empty()) {
                mounts.insert_or_assign(CanonPath::root, accessor);
                accessor = makeMountedInputAccessor(std::move(mounts));
            }
        }

        assert(!origRev || origRev == rev);
        if (!getShallowAttr(input))
            input.attrs.insert_or_assign("revCount", getIntAttr(infoAttrs, "revCount"));
        input.attrs.insert_or_assign("lastModified", getIntAttr(infoAttrs, "lastModified"));

        return {accessor, std::move(input)};
    }

    std::pair<ref<InputAccessor>, Input> getAccessorFromWorkdir(
        ref<Store> store,
        RepoInfo & repoInfo,
        Input && input) const
    {
        if (getSubmodulesAttr(input))
            /* Create mountpoints for the submodules. */
            for (auto & submodule : repoInfo.workdirInfo.submodules)
                repoInfo.workdirInfo.files.insert(submodule.path);

        auto repo = GitRepo::openRepo(repoInfo.url, false, false);

        auto exportIgnore = getExportIgnoreAttr(input);

        ref<InputAccessor> accessor =
            repo->getAccessor(repoInfo.workdirInfo,
                exportIgnore,
                makeNotAllowedError(repoInfo.url));

        /* If the repo has submodules, return a mounted input accessor
           consisting of the accessor for the top-level repo and the
           accessors for the submodule workdirs. */
        if (getSubmodulesAttr(input) && !repoInfo.workdirInfo.submodules.empty()) {
            std::map<CanonPath, nix::ref<InputAccessor>> mounts;

            for (auto & submodule : repoInfo.workdirInfo.submodules) {
                auto submodulePath = CanonPath(repoInfo.url) / submodule.path;
                fetchers::Attrs attrs;
                attrs.insert_or_assign("type", "git");
                attrs.insert_or_assign("url", submodulePath.abs());
                attrs.insert_or_assign("exportIgnore", Explicit<bool>{ exportIgnore });

                auto submoduleInput = fetchers::Input::fromAttrs(std::move(attrs));
                auto [submoduleAccessor, submoduleInput2] =
                    submoduleInput.getAccessor(store);

                /* If the submodule is dirty, mark this repo dirty as
                   well. */
                if (!submoduleInput2.getRev())
                    repoInfo.workdirInfo.isDirty = true;

                mounts.insert_or_assign(submodule.path, submoduleAccessor);
            }

            mounts.insert_or_assign(CanonPath::root, accessor);
            accessor = makeMountedInputAccessor(std::move(mounts));
        }

        if (!repoInfo.workdirInfo.isDirty) {
            auto repo = GitRepo::openRepo(repoInfo.url);

            if (auto ref = repo->getWorkdirRef())
                input.attrs.insert_or_assign("ref", *ref);

            /* Return a rev of 000... if there are no commits yet. */
            auto rev = repoInfo.workdirInfo.headRev.value_or(nullRev);

            input.attrs.insert_or_assign("rev", rev.gitRev());
            input.attrs.insert_or_assign("revCount",
                rev == nullRev ? 0 : getRevCount(repoInfo, repoInfo.url, rev));

            verifyCommit(input, repo);
        } else {
            repoInfo.warnDirty();

            if (repoInfo.workdirInfo.headRev) {
                input.attrs.insert_or_assign("dirtyRev",
                    repoInfo.workdirInfo.headRev->gitRev() + "-dirty");
                input.attrs.insert_or_assign("dirtyShortRev",
                    repoInfo.workdirInfo.headRev->gitShortRev() + "-dirty");
            }

            verifyCommit(input, nullptr);
        }

        input.attrs.insert_or_assign(
            "lastModified",
            repoInfo.workdirInfo.headRev
            ? getLastModified(repoInfo, repoInfo.url, *repoInfo.workdirInfo.headRev)
            : 0);

        return {accessor, std::move(input)};
    }

    std::pair<ref<InputAccessor>, Input> getAccessor(ref<Store> store, const Input & _input) const override
    {
        Input input(_input);

        auto repoInfo = getRepoInfo(input);

        if (getExportIgnoreAttr(input)
            && getSubmodulesAttr(input)) {
            /* In this situation, we don't have a git CLI behavior that we can copy.
               `git archive` does not support submodules, so it is unclear whether
               rules from the parent should affect the submodule or not.
               When git may eventually implement this, we need Nix to match its
               behavior. */
            throw UnimplementedError("exportIgnore and submodules are not supported together yet");
        }

        auto [accessor, final] =
            input.getRef() || input.getRev() || !repoInfo.isLocal
            ? getAccessorFromCommit(store, repoInfo, std::move(input))
            : getAccessorFromWorkdir(store, repoInfo, std::move(input));

        accessor->fingerprint = final.getFingerprint(store);

        return {accessor, std::move(final)};
    }

    std::optional<std::string> getFingerprint(ref<Store> store, const Input & input) const override
    {
        if (auto rev = input.getRev())
            return rev->gitRev() + (getSubmodulesAttr(input) ? ";s" : "") + (getExportIgnoreAttr(input) ? ";e" : "");
        else
            return std::nullopt;
    }

    bool isLocked(const Input & input) const override
    {
        return (bool) input.getRev();
    }
};

static auto rGitInputScheme = OnStartup([] { registerInputScheme(std::make_unique<GitInputScheme>()); });

}
