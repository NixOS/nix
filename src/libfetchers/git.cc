#include "fetchers.hh"
#include "cache.hh"
#include "globals.hh"
#include "tarfile.hh"
#include "store-api.hh"
#include "url-parts.hh"

#include <sys/time.h>
#include <sys/wait.h>

using namespace std::string_literals;

namespace nix::fetchers {

static std::string readHead(const Path & path)
{
    return chomp(runProgram("git", true, { "-C", path, "rev-parse", "--abbrev-ref", "HEAD" }));
}

static bool isNotDotGitDirectory(const Path & path)
{
    static const std::regex gitDirRegex("^(?:.*/)?\\.git$");

    return not std::regex_match(path, gitDirRegex);
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
            else if (name == "shallow")
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
            if (name != "type" && name != "url" && name != "ref" && name != "rev" && name != "shallow" && name != "submodules" && name != "lastModified" && name != "revCount" && name != "narHash" && name != "allRefs")
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

    std::pair<Tree, Input> fetch(ref<Store> store, const Input & _input) override
    {
        auto name = "source";

        Input input(_input);

        bool shallow = maybeGetBoolAttr(input.attrs, "shallow").value_or(false);
        bool submodules = maybeGetBoolAttr(input.attrs, "submodules").value_or(false);
        bool allRefs = maybeGetBoolAttr(input.attrs, "allRefs").value_or(false);

        std::string cacheType = "git";
        if (shallow) cacheType += "-shallow";
        if (submodules) cacheType += "-submodules";
        if (allRefs) cacheType += "-all-refs";

        bool isDirtyTree = false;

        // TODO: Recursively use the `write-tree` logic for the submodules.
        //       For now, we use a diff to get the uncommitted submodule
        //       changes. This only works correctly in cases where the diff
        //       does not depend on smudge/clean behavior, which we can't
        //       assume. The submodule worktree does come from a fresh repo,
        //       so at least it seems that git-crypt security is not at risk.
        std::string dirtySubmoduleDiff;

        auto getImmutableAttrs = [&]()
        {
            return Attrs({
                {"type", cacheType},
                {"name", name},
                {"rev", input.getRev()->gitRev()},
            });
        };

        auto makeResult = [&](const Attrs & infoAttrs, StorePath && storePath)
            -> std::pair<Tree, Input>
        {
            assert(input.getRev());
            assert(!_input.getRev() || _input.getRev() == input.getRev());
            if (!shallow)
                input.attrs.insert_or_assign("revCount", getIntAttr(infoAttrs, "revCount"));
            input.attrs.insert_or_assign("lastModified", getIntAttr(infoAttrs, "lastModified"));

            // If the tree is dirty, we use a tree hash internally, but we don't
            // want to expose it.
            if (isDirtyTree) {
                input.attrs.insert_or_assign("rev", "0000000000000000000000000000000000000000");
            }

            return {
                Tree(store->toRealPath(storePath), std::move(storePath)),
                input
            };
        };

        if (input.getRev()) {
            if (auto res = getCache()->lookup(store, getImmutableAttrs()))
                return makeResult(res->first, std::move(res->second));
        }

        auto [isLocal, actualUrl_] = getActualUrl(input);
        auto actualUrl = actualUrl_; // work around clang bug

        bool haveHEAD = true;

        // If this is a local directory and no ref or revision is
        // given, then allow the use of an unclean working tree.
        if (!input.getRef() && !input.getRev() && isLocal) {
            bool clean = false;

            /* Check whether this repo has any commits. There are
               probably better ways to do this. */
            auto gitDir = actualUrl + "/.git";
            auto commonGitDir = chomp(runProgram(
                "git",
                true,
                { "-C", actualUrl, "rev-parse", "--git-common-dir" }
            ));
            if (commonGitDir != ".git")
                    gitDir = commonGitDir;

            haveHEAD = !readDirectory(gitDir + "/refs/heads").empty();

            try {
                if (haveHEAD) {
                    runProgram("git", true, { "-C", actualUrl, "diff-index", "--quiet", "HEAD", "--" });
                    clean = true;
                }
            } catch (ExecError & e) {
                if (!WIFEXITED(e.status) || WEXITSTATUS(e.status) != 1) throw;
            }

            if (!clean) {

                /* This is an unclean working tree. We can't use the worktree
                   files, because those may be smudged. */

                if (!settings.allowDirty)
                    throw Error("Git tree '%s' is dirty", actualUrl);

                if (settings.warnDirty)
                    warn("Git tree '%s' is dirty", actualUrl);

                isDirtyTree = true;

                // We can't use an existing file for the temporary git index,
                // so we need to use a tmpdir instead of a tmpfile.
                // Non-submodule changes are captured by the tree we build using
                // this temporary index.
                Path tmpIndexDir = createTempDir();
                AutoDelete delTmpIndexDir(tmpIndexDir, true);
                Path tmpIndex = tmpIndexDir + "/tmp-git-index";

                std::set<Path> files = tokenizeString<std::set<std::string>>(
                    runProgram("git", true, { "-C", actualUrl, "ls-files", "-z" }),
                    "\0"s);

                {
                    RunOptions gitOptions("git", { "-C", actualUrl, "add", "--no-warn-embedded-repo", "--" });
                    auto env = getEnv();
                    env["GIT_INDEX_FILE"] = tmpIndex;
                    gitOptions.environment = env;
                    for (auto file : files)
                        gitOptions.args.push_back(file);

                    auto result = runProgram(gitOptions);
                    if (result.first)
                        throw ExecError(result.first, fmt("program git add -u %1%", statusToString(result.first)));
                }
                std::string tree;
                {
                    RunOptions gitOptions("git", { "-C", actualUrl, "write-tree" });
                    auto env = getEnv();
                    env["GIT_INDEX_FILE"] = tmpIndex;
                    gitOptions.environment = env;

                    auto result = runProgram(gitOptions);
                    if (result.first)
                        throw ExecError(result.first, fmt("program git write-tree %1%", statusToString(result.first)));
                    tree = trim(result.second);

                    // Note [tree as rev]
                    // We set `rev` to a tree object, even if it's normally a
                    // commit object. This way, we get some use out of the
                    // cache, to avoid copying files unnecessarily.
                    input.attrs.insert_or_assign("rev", trim(result.second));
                }

                // Use a diff to gather submodule changes as well. See `dirtySubmoduleDiff`
                if (submodules) {
                    RunOptions gitOptions("git", { "-C", actualUrl, "diff", tree, "--submodule=diff" });

                    auto result = runProgram(gitOptions);
                    if (result.first)
                        throw ExecError(result.first, fmt("program git diff %1%", statusToString(result.first)));

                    dirtySubmoduleDiff = result.second;
                }
            }
        }

        if (!input.getRef() && haveHEAD)
            input.attrs.insert_or_assign("ref", isLocal ? readHead(actualUrl) : "master");

        Attrs mutableAttrs({
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

            if (auto res = getCache()->lookup(store, mutableAttrs)) {
                auto rev2 = Hash::parseAny(getStrAttr(res->first, "rev"), htSHA1);
                if (!input.getRev() || input.getRev() == rev2) {
                    input.attrs.insert_or_assign("rev", rev2.gitRev());
                    return makeResult(res->first, std::move(res->second));
                }
            }

            Path cacheDir = getCacheDir() + "/nix/gitv3/" + hashString(htSHA256, actualUrl).to_string(Base32, false);
            repoDir = cacheDir;

            if (!pathExists(cacheDir)) {
                createDirs(dirOf(cacheDir));
                runProgram("git", true, { "init", "--bare", repoDir });
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
        }

        bool isShallow = chomp(runProgram("git", true, { "-C", repoDir, "rev-parse", "--is-shallow-repository" })) == "true";

        if (isShallow && !shallow)
            throw Error("'%s' is a shallow Git repository, but a non-shallow repository is needed", actualUrl);

        // FIXME: check whether rev is an ancestor of ref.

        printTalkative("using revision %s of repo '%s'", input.getRev()->gitRev(), actualUrl);

        /* Now that we know the ref, check again whether we have it in
           the store. */
        if (auto res = getCache()->lookup(store, getImmutableAttrs()))
            return makeResult(res->first, std::move(res->second));

        Path tmpDir = createTempDir();
        AutoDelete delTmpDir(tmpDir, true);
        PathFilter filter = defaultPathFilter;

        // Skip check if rev is set to a tree object. See Note [tree as rev]
        if (!isDirtyTree) {
            RunOptions checkCommitOpts(
                "git",
                { "-C", repoDir, "cat-file", "commit", input.getRev()->gitRev() }
            );
            checkCommitOpts.searchPath = true;
            checkCommitOpts.mergeStderrToStdout = true;

            auto result = runProgram(checkCommitOpts);
            if (WEXITSTATUS(result.first) == 128
                && result.second.find("bad file") != std::string::npos
            ) {
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
        }

        if (submodules) {
            Path tmpGitDir = createTempDir();
            AutoDelete delTmpGitDir(tmpGitDir, true);

            // For this checkout approach, we need a commit, not just a treeish.
            if (isDirtyTree) {
                RunOptions gitOptions("git", { "-C", actualUrl, "commit-tree", "-m", "temporary commit for dirty tree", input.getRev()->gitRev() });
                auto result = runProgram(gitOptions);
                if (result.first)
                    throw ExecError(result.first, fmt("program git commit-tree %1%", statusToString(result.first)));
                input.attrs.insert_or_assign("rev", trim(result.second));
            }

            runProgram("git", true, { "init", tmpDir, "--separate-git-dir", tmpGitDir });
            // TODO: repoDir might lack the ref (it only checks if rev
            // exists, see FIXME above) so use a big hammer and fetch
            // everything to ensure we get the rev.
            runProgram("git", true, { "-C", tmpDir, "fetch", "--quiet", "--force",
                                      "--update-head-ok", "--", repoDir, "refs/*:refs/*",
                                      input.getRev()->gitRev() });

            runProgram("git", true, { "-C", tmpDir, "checkout", "--quiet", input.getRev()->gitRev() });
            runProgram("git", true, { "-C", tmpDir, "remote", "add", "origin", actualUrl });
            runProgram("git", true, { "-C", tmpDir, "submodule", "--quiet", "update", "--init", "--recursive" });

            if (dirtySubmoduleDiff.size()) {
                RunOptions gitOptions("git", { "-C", tmpDir, "apply" });
                StringSource s(dirtySubmoduleDiff);
                gitOptions.standardIn = &s;
                auto result = runProgram(gitOptions);
                if (result.first)
                    throw ExecError(result.first, fmt("program git apply %1%", statusToString(result.first)));
            }

            filter = isNotDotGitDirectory;
        } else {
            Strings noSmudgeOptions;
            {
                RunOptions gitOptions("git", { "-C", repoDir, "config", "-l" });
                auto result = runProgram(gitOptions);
                auto ss = std::stringstream{result.second};
                StringSet filters;

                for (std::string line; std::getline(ss, line, '\n');) {
                    std::string prefix = "filter.";
                    std::string infix = ".smudge=";
                    auto infixPos = line.find(infix);
                    if (hasPrefix(line, prefix) && infixPos != std::string::npos) {
                        filters.emplace(line.substr(prefix.size(), infixPos - prefix.size()));
                    }
                }
                for (auto filter : filters) {
                    noSmudgeOptions.emplace_back("-c");
                    noSmudgeOptions.emplace_back("filter." + filter + ".smudge=cat --");
                }
            }

            // FIXME: should pipe this, or find some better way to extract a
            // revision.
            auto source = sinkToSource([&](Sink & sink) {
                RunOptions gitOptions("git", noSmudgeOptions);
                gitOptions.args.push_back("-C");
                gitOptions.args.push_back(repoDir);
                gitOptions.args.push_back("archive");
                gitOptions.args.push_back(input.getRev()->gitRev());
                gitOptions.standardOut = &sink;
                runProgram2(gitOptions);
            });

            unpackTarfile(*source, tmpDir);
        }

        auto storePath = store->addToStore(name, tmpDir, FileIngestionMethod::Recursive, htSHA256, filter);

        const auto now = std::chrono::system_clock::now();

        // FIXME: when isDirtyTree, maybe we should use the timestamp
        //        of the last modified dirty file?
        auto lastModified = isDirtyTree ?
            (std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count())
            : std::stoull(runProgram("git", true, { "-C", repoDir, "log", "-1", "--format=%ct", "--no-show-signature", input.getRev()->gitRev() }));

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
                mutableAttrs,
                infoAttrs,
                storePath,
                false);

        getCache()->add(
            store,
            getImmutableAttrs(),
            infoAttrs,
            storePath,
            true);

        return makeResult(infoAttrs, std::move(storePath));
    }
};

static auto rGitInputScheme = OnStartup([] { registerInputScheme(std::make_unique<GitInputScheme>()); });

}
