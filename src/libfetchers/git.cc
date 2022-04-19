#include "fetchers.hh"
#include "cache.hh"
#include "globals.hh"
#include "tarfile.hh"
#include "store-api.hh"
#include "url-parts.hh"
#include "pathlocks.hh"

#include "fetch-settings.hh"

#include <sys/time.h>
#include <sys/wait.h>

using namespace std::string_literals;

namespace nix::fetchers {

// Explicit initial branch of our bare repo to suppress warnings from new version of git.
// The value itself does not matter, since we always fetch a specific revision or branch.
// It is set with `-c init.defaultBranch=` instead of `--initial-branch=` to stay compatible with
// old version of git, which will ignore unrecognized `-c` options.
const std::string gitInitialBranch = "__nix_dummy_branch";

static std::string readHead(const Path & path)
{
    return chomp(runProgram("git", true, { "-C", path, "rev-parse", "--abbrev-ref", "HEAD" }));
}

static bool isNotDotGitDirectory(const Path & path)
{
    return baseNameOf(path) != ".git";
}

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

        for (auto &[name, value] : url.query) {
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
        if (url.scheme == "file" && !input.getRef() && !input.getRev())
            return url.path;
        return {};
    }

    void markChangedFile(const Input & input, std::string_view file, std::optional<std::string> commitMsg) override
    {
        auto sourcePath = getSourcePath(input);
        assert(sourcePath);

        runProgram("git", true,
            { "-C", *sourcePath, "add", "--force", "--intent-to-add", "--", std::string(file) });

        if (commitMsg)
            runProgram("git", true,
                { "-C", *sourcePath, "commit", std::string(file), "-m", *commitMsg });
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

    std::pair<StorePath, Input> fetch(ref<Store> store, const Input & _input) override
    {
        Input input(_input);

        std::string name = input.getName();

        bool shallow = maybeGetBoolAttr(input.attrs, "shallow").value_or(false);
        bool submodules = maybeGetBoolAttr(input.attrs, "submodules").value_or(false);
        bool allRefs = maybeGetBoolAttr(input.attrs, "allRefs").value_or(false);

        std::string cacheType = "git";
        if (shallow) cacheType += "-shallow";
        if (submodules) cacheType += "-submodules";
        if (allRefs) cacheType += "-all-refs";

        auto checkHashType = [&](const std::optional<Hash> & hash)
        {
            if (hash.has_value() && !(hash->type == htSHA1 || hash->type == htSHA256))
                throw Error("Hash '%s' is not supported by Git. Supported types are sha1 and sha256.", hash->to_string(Base16, true));
        };

        auto getLockedAttrs = [&]()
        {
            checkHashType(input.getRev());

            return Attrs({
                {"type", cacheType},
                {"name", name},
                {"rev", input.getRev()->gitRev()},
            });
        };

        auto makeResult = [&](const Attrs & infoAttrs, StorePath && storePath)
            -> std::pair<StorePath, Input>
        {
            assert(input.getRev());
            assert(!_input.getRev() || _input.getRev() == input.getRev());
            if (!shallow)
                input.attrs.insert_or_assign("revCount", getIntAttr(infoAttrs, "revCount"));
            input.attrs.insert_or_assign("lastModified", getIntAttr(infoAttrs, "lastModified"));
            return {std::move(storePath), input};
        };

        if (input.getRev()) {
            if (auto res = getCache()->lookup(store, getLockedAttrs()))
                return makeResult(res->first, std::move(res->second));
        }

        auto [isLocal, actualUrl_] = getActualUrl(input);
        auto actualUrl = actualUrl_; // work around clang bug

        // If this is a local directory and no ref or revision is
        // given, then allow the use of an unclean working tree.
        if (!input.getRef() && !input.getRev() && isLocal) {
            bool clean = false;

            auto env = getEnv();
            // Set LC_ALL to C: because we rely on the error messages from git rev-parse to determine what went wrong
            // that way unknown errors can lead to a failure instead of continuing through the wrong code path
            env["LC_ALL"] = "C";

            /* Check whether HEAD points to something that looks like a commit,
               since that is the refrence we want to use later on. */
            auto result = runProgram(RunOptions {
                .program = "git",
                .args = { "-C", actualUrl, "--git-dir=.git", "rev-parse", "--verify", "--no-revs", "HEAD^{commit}" },
                .environment = env,
                .mergeStderrToStdout = true
            });
            auto exitCode = WEXITSTATUS(result.first);
            auto errorMessage = result.second;

            if (errorMessage.find("fatal: not a git repository") != std::string::npos) {
                throw Error("'%s' is not a Git repository", actualUrl);
            } else if (errorMessage.find("fatal: Needed a single revision") != std::string::npos) {
                // indicates that the repo does not have any commits
                // we want to proceed and will consider it dirty later
            } else if (exitCode != 0) {
                // any other errors should lead to a failure
                throw Error("getting the HEAD of the Git tree '%s' failed with exit code %d:\n%s", actualUrl, exitCode, errorMessage);
            }

            bool hasHead = exitCode == 0;
            try {
                if (hasHead) {
                    // Using git diff is preferrable over lower-level operations here,
                    // because its conceptually simpler and we only need the exit code anyways.
                    auto gitDiffOpts = Strings({ "-C", actualUrl, "diff", "HEAD", "--quiet"});
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

            if (!clean) {

                /* This is an unclean working tree. So copy all tracked files. */

                if (!fetchSettings.allowDirty)
                    throw Error("Git tree '%s' is dirty", actualUrl);

                if (fetchSettings.warnDirty)
                    warn("Git tree '%s' is dirty", actualUrl);

                auto gitOpts = Strings({ "-C", actualUrl, "ls-files", "-z" });
                if (submodules)
                    gitOpts.emplace_back("--recurse-submodules");

                auto files = tokenizeString<std::set<std::string>>(
                    runProgram("git", true, gitOpts), "\0"s);

                Path actualPath(absPath(actualUrl));

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

                auto storePath = store->addToStore(input.getName(), actualPath, FileIngestionMethod::Recursive, htSHA256, filter);

                // FIXME: maybe we should use the timestamp of the last
                // modified dirty file?
                input.attrs.insert_or_assign(
                    "lastModified",
                    hasHead ? std::stoull(runProgram("git", true, { "-C", actualPath, "log", "-1", "--format=%ct", "--no-show-signature", "HEAD" })) : 0);

                return {std::move(storePath), input};
            }
        }

        if (!input.getRef()) input.attrs.insert_or_assign("ref", isLocal ? readHead(actualUrl) : "master");

        Attrs unlockedAttrs({
            {"type", cacheType},
            {"name", name},
            {"url", actualUrl},
            {"ref", *input.getRef()},
        });

        Path repoDir;

        if (isLocal) {

            if (!input.getRev())
                input.attrs.insert_or_assign("rev",
                    Hash::parseAny(chomp(runProgram("git", true, { "-C", actualUrl, "rev-parse", *input.getRef() })), htSHA1).gitRev());

            repoDir = actualUrl;

        } else {

            if (auto res = getCache()->lookup(store, unlockedAttrs)) {
                auto rev2 = Hash::parseAny(getStrAttr(res->first, "rev"), htSHA1);
                if (!input.getRev() || input.getRev() == rev2) {
                    input.attrs.insert_or_assign("rev", rev2.gitRev());
                    return makeResult(res->first, std::move(res->second));
                }
            }

            Path cacheDir = getCacheDir() + "/nix/gitv3/" + hashString(htSHA256, actualUrl).to_string(Base32, false);
            repoDir = cacheDir;

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

            /* If a rev was specified, we need to fetch if it's not in the
               repo. */
            if (input.getRev()) {
                try {
                    runProgram("git", true, { "-C", repoDir, "cat-file", "-e", input.getRev()->gitRev() });
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
                        (uint64_t) st.st_mtime + settings.tarballTtl <= (uint64_t) now;
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
                    runProgram("git", true, { "-C", repoDir, "fetch", "--quiet", "--force", "--", actualUrl, fmt("%s:%s", fetchRef, fetchRef) });
                } catch (Error & e) {
                    if (!pathExists(localRefFile)) throw;
                    warn("could not update local clone of Git repository '%s'; continuing with the most recent version", actualUrl);
                }

                struct timeval times[2];
                times[0].tv_sec = now;
                times[0].tv_usec = 0;
                times[1].tv_sec = now;
                times[1].tv_usec = 0;

                utimes(localRefFile.c_str(), times);
            }

            if (!input.getRev())
                input.attrs.insert_or_assign("rev", Hash::parseAny(chomp(readFile(localRefFile)), htSHA1).gitRev());

            // cache dir lock is removed at scope end; we will only use read-only operations on specific revisions in the remainder
        }

        bool isShallow = chomp(runProgram("git", true, { "-C", repoDir, "rev-parse", "--is-shallow-repository" })) == "true";

        if (isShallow && !shallow)
            throw Error("'%s' is a shallow Git repository, but a non-shallow repository is needed", actualUrl);

        // FIXME: check whether rev is an ancestor of ref.

        printTalkative("using revision %s of repo '%s'", input.getRev()->gitRev(), actualUrl);

        /* Now that we know the ref, check again whether we have it in
           the store. */
        if (auto res = getCache()->lookup(store, getLockedAttrs()))
            return makeResult(res->first, std::move(res->second));

        Path tmpDir = createTempDir();
        AutoDelete delTmpDir(tmpDir, true);
        PathFilter filter = defaultPathFilter;

        auto result = runProgram(RunOptions {
            .program = "git",
            .args = { "-C", repoDir, "cat-file", "commit", input.getRev()->gitRev() },
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
                *input.getRef(),
                actualUrl
            );
        }

        if (submodules) {
            Path tmpGitDir = createTempDir();
            AutoDelete delTmpGitDir(tmpGitDir, true);

            runProgram("git", true, { "-c", "init.defaultBranch=" + gitInitialBranch, "init", tmpDir, "--separate-git-dir", tmpGitDir });
            // TODO: repoDir might lack the ref (it only checks if rev
            // exists, see FIXME above) so use a big hammer and fetch
            // everything to ensure we get the rev.
            runProgram("git", true, { "-C", tmpDir, "fetch", "--quiet", "--force",
                                      "--update-head-ok", "--", repoDir, "refs/*:refs/*" });

            runProgram("git", true, { "-C", tmpDir, "checkout", "--quiet", input.getRev()->gitRev() });
            runProgram("git", true, { "-C", tmpDir, "remote", "add", "origin", actualUrl });
            runProgram("git", true, { "-C", tmpDir, "submodule", "--quiet", "update", "--init", "--recursive" });

            filter = isNotDotGitDirectory;
        } else {
            // FIXME: should pipe this, or find some better way to extract a
            // revision.
            auto source = sinkToSource([&](Sink & sink) {
                runProgram2({
                    .program = "git",
                    .args = { "-C", repoDir, "archive", input.getRev()->gitRev() },
                    .standardOut = &sink
                });
            });

            unpackTarfile(*source, tmpDir);
        }

        auto storePath = store->addToStore(name, tmpDir, FileIngestionMethod::Recursive, htSHA256, filter);

        auto lastModified = std::stoull(runProgram("git", true, { "-C", repoDir, "log", "-1", "--format=%ct", "--no-show-signature", input.getRev()->gitRev() }));

        Attrs infoAttrs({
            {"rev", input.getRev()->gitRev()},
            {"lastModified", lastModified},
        });

        if (!shallow)
            infoAttrs.insert_or_assign("revCount",
                std::stoull(runProgram("git", true, { "-C", repoDir, "rev-list", "--count", input.getRev()->gitRev() })));

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
};

static auto rGitInputScheme = OnStartup([] { registerInputScheme(std::make_unique<GitInputScheme>()); });

}
