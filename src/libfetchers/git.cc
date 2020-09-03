#include "fetchers.hh"
#include "cache.hh"
#include "globals.hh"
#include "tarfile.hh"
#include "store-api.hh"
#include "git.hh"

#include <sys/time.h>

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
            if (name == "rev" || name == "ref" || name == "treehash" || name == "gitIngestion")
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
            if (name != "type" && name != "url" && name != "ref" && name != "rev" && name != "shallow" && name != "submodules" && name != "gitIngestion" && name != "treeHash" && name != "lastModified" && name != "revCount" && name != "narHash")
                throw Error("unsupported Git input attribute '%s'", name);

        parseURL(getStrAttr(attrs, "url"));
        maybeGetBoolAttr(attrs, "shallow");
        maybeGetBoolAttr(attrs, "submodules");

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
        if (auto treeHash = input.getTreeHash()) url.query.insert_or_assign("treeHash", treeHash->gitRev());
        if (maybeGetBoolAttr(input.attrs, "gitIngestion").value_or((bool) input.getTreeHash()))
            url.query.insert_or_assign("shallow", "1");
        if (auto ref = input.getRef()) url.query.insert_or_assign("ref", *ref);
        if (maybeGetBoolAttr(input.attrs, "shallow").value_or(false))
            url.query.insert_or_assign("shallow", "1");
        return url;
    }

    bool hasAllInfo(const Input & input) override
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
        if (url.scheme == "file" && !input.getRef() && !input.getRev() && !input.getTreeHash())
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
        // Don't clone file:// URIs (but otherwise treat them the
        // same as remote URIs, i.e. don't use the working tree or
        // HEAD).
        static bool forceHttp = getEnv("_NIX_FORCE_HTTP") == "1"; // for testing
        auto url = parseURL(getStrAttr(input.attrs, "url"));
        bool isLocal = url.scheme == "file" && !forceHttp;
        return {isLocal, isLocal ? url.path : url.base};
    }

    std::pair<Tree, Input> fetch(ref<Store> store, const Input & _input) override
    {
        auto name = "source";

        Input input(_input);

        bool shallow = maybeGetBoolAttr(input.attrs, "shallow").value_or(false);
        bool submodules = maybeGetBoolAttr(input.attrs, "submodules").value_or(false);

        std::string cacheType = "git";
        if (shallow) cacheType += "-shallow";
        if (submodules) cacheType += "-submodules";

        auto ingestionMethod =
            maybeGetBoolAttr(input.attrs, "gitIngestion").value_or((bool) input.getTreeHash())
            ? FileIngestionMethod::Git
            : FileIngestionMethod::Recursive;

        auto getImmutableAttrs = [&]()
        {
            Attrs attrs({
                {"type", cacheType},
                {"name", name},
            });
            if (input.getTreeHash())
                attrs.insert_or_assign("treeHash", input.getTreeHash()->gitRev());
            if (input.getRev())
                attrs.insert_or_assign("rev", input.getRev()->gitRev());
            if (maybeGetBoolAttr(input.attrs, "gitIngestion").value_or((bool) input.getTreeHash()))
                attrs.insert_or_assign("gi", true);
            return attrs;
        };

        auto makeResult = [&](const Attrs & infoAttrs, StorePathDescriptor && storePathDesc)
            -> std::pair<Tree, Input>
        {
            assert(input.getRev() || input.getTreeHash());
            /* If was originally set, that original value must be preserved. */
            assert(!_input.getRev() || _input.getRev() == input.getRev());
            assert(!_input.getTreeHash() || _input.getTreeHash() == input.getTreeHash());
            if (!shallow)
                input.attrs.insert_or_assign("revCount", getIntAttr(infoAttrs, "revCount"));
            input.attrs.insert_or_assign("lastModified", getIntAttr(infoAttrs, "lastModified"));
            return {
                Tree {
                    store->toRealPath(store->makeFixedOutputPathFromCA(storePathDesc)),
                    std::move(storePathDesc),
                },
                input
            };
        };

        if (input.getRev()) {
            if (auto res = getCache()->lookup(store, getImmutableAttrs()))
                return makeResult(res->first, std::move(res->second));
        }

        auto [isLocal, actualUrl_] = getActualUrl(input);
        auto actualUrl = actualUrl_; // work around clang bug

        // If this is a local directory and no ref or revision is
        // given, then allow the use of an unclean working tree.
        if (!input.getRef() && !input.getRev() && !input.getTreeHash() && isLocal) {
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

            bool haveCommits = !readDirectory(gitDir + "/refs/heads").empty();

            try {
                if (haveCommits) {
                    runProgram("git", true, { "-C", actualUrl, "diff-index", "--quiet", "HEAD", "--" });
                    clean = true;
                }
            } catch (ExecError & e) {
                if (!WIFEXITED(e.status) || WEXITSTATUS(e.status) != 1) throw;
            }

            if (!clean) {

                /* This is an unclean working tree. So copy all tracked files. */

                if (!settings.allowDirty)
                    throw Error("Git tree '%s' is dirty", actualUrl);

                if (settings.warnDirty)
                    warn("Git tree '%s' is dirty", actualUrl);

                auto gitOpts = Strings({ "-C", actualUrl, "ls-files", "-z" });
                if (submodules)
                    gitOpts.emplace_back("--recurse-submodules");

                auto files = tokenizeString<std::set<std::string>>(
                    runProgram("git", true, gitOpts), "\0"s);

                PathFilter filter = [&](const Path & p) -> bool {
                    assert(hasPrefix(p, actualUrl));
                    std::string file(p, actualUrl.size() + 1);

                    auto st = lstat(p);

                    if (S_ISDIR(st.st_mode)) {
                        auto prefix = file + "/";
                        auto i = files.lower_bound(prefix);
                        return i != files.end() && hasPrefix(*i, prefix);
                    }

                    return files.count(file);
                };

                auto storePath = store->addToStore("source", actualUrl, ingestionMethod, htSHA256, filter);
                // FIXME: just have Store::addToStore return a StorePathDescriptor, as
                // it has the underlying information.
                auto storePathDesc = store->queryPathInfo(storePath)->fullStorePathDescriptorOpt().value();

                // FIXME: maybe we should use the timestamp of the last
                // modified dirty file?
                input.attrs.insert_or_assign(
                    "lastModified",
                    haveCommits ? std::stoull(runProgram("git", true, { "-C", actualUrl, "log", "-1", "--format=%ct", "--no-show-signature", "HEAD" })) : 0);

                return {
                    Tree {
                        store->printStorePath(storePath),
                        std::move(storePathDesc),
                    },
                    input
                };
            }
        }

        if (!input.getRef()) input.attrs.insert_or_assign("ref", isLocal ? readHead(actualUrl) : "master");

        Attrs mutableAttrs({
            {"type", cacheType},
            {"name", name},
            {"url", actualUrl},
            {"ref", *input.getRef()},
        });

        Path repoDir;

        if (isLocal) {

            if (!input.getRev() && !input.getTreeHash())
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

                auto treeHash2 = Hash::parseNonSRIUnprefixed(getStrAttr(res->first, "treeHash"), htSHA1);
                if (!input.getTreeHash() || input.getTreeHash() == treeHash2) {
                    input.attrs.insert_or_assign("treeHash", rev2.gitRev());
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

            /* If a rev or treeHash is specified, we need to fetch if
               it's not in the repo. */
            if (input.getRev() || input.getTreeHash()) {
                try {
                    auto gitHash = input.getTreeHash() ? input.getTreeHash() : input.getRev();
                    runProgram("git", true, { "-C", repoDir, "cat-file", "-e", gitHash->gitRev() });
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
                Activity act(*logger, lvlTalkative, actUnknown, fmt("fetching Git repository '%s'", actualUrl));

                // FIXME: git stderr messes up our progress indicator, so
                // we're using --quiet for now. Should process its stderr.
                try {
                    auto ref = input.getRef();
                    auto fetchRef = ref->compare(0, 5, "refs/") == 0
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

            if (!input.getRev() && !input.getTreeHash())
                input.attrs.insert_or_assign("rev", Hash::parseAny(chomp(readFile(localRefFile)), htSHA1).gitRev());
        }

        if (input.getTreeHash()) {
            auto type = chomp(runProgram("git", true, { "-C", repoDir, "cat-file", "-t", input.getTreeHash()->gitRev() }));
            if (type != "tree")
                throw Error("Need a tree object, found '%s' object in %s", type, input.getTreeHash()->gitRev());
        }

        bool isShallow = chomp(runProgram("git", true, { "-C", repoDir, "rev-parse", "--is-shallow-repository" })) == "true";

        if (isShallow && !shallow)
            throw Error("'%s' is a shallow Git repository, but a non-shallow repository is needed", actualUrl);

        // FIXME: check whether rev is an ancestor of ref.

        if (input.getRev())
            printTalkative("using revision %s of repo '%s'", input.getRev()->gitRev(), actualUrl);
        else if (input.getTreeHash())
            printTalkative("using tree %s of repo '%s'", input.getTreeHash()->gitRev(), actualUrl);

        /* Now that we know the ref, check again whether we have it in
           the store. */
        if (auto res = getCache()->lookup(store, getImmutableAttrs()))
            return makeResult(res->first, std::move(res->second));

        Path tmpDir = createTempDir();
        AutoDelete delTmpDir(tmpDir, true);
        PathFilter filter = defaultPathFilter;

        if (submodules) {
            if (input.getTreeHash())
                throw Error("Cannot fetch specific tree hashes if there are submodules");
            warn("Nix's computed git tree hash will be different when submodules are converted to regular directories");
        }

        if (submodules) {
            Path tmpGitDir = createTempDir();
            AutoDelete delTmpGitDir(tmpGitDir, true);

            runProgram("git", true, { "init", tmpDir, "--separate-git-dir", tmpGitDir });
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
                RunOptions gitOptions("git", { "-C", repoDir, "archive", input.getTreeHash() ? input.getTreeHash()->gitRev() : input.getRev()->gitRev() });
                gitOptions.standardOut = &sink;
                runProgram2(gitOptions);
            });

            unpackTarfile(*source, tmpDir);
        }

        auto storePath = store->addToStore(name, tmpDir, ingestionMethod, ingestionMethod == FileIngestionMethod::Git ? htSHA1 : htSHA256, filter);
        // FIXME: just have Store::addToStore return a StorePathDescriptor, as
        // it has the underlying information.
        auto storePathDesc = store->queryPathInfo(storePath)->fullStorePathDescriptorOpt().value();

        // verify treeHash is what we actually obtained in the nix store
        if (input.getTreeHash()) {
            auto path = store->toRealPath(store->printStorePath(storePath));
            auto gotHash = dumpGitHash(htSHA1, path);
            if (gotHash != input.getTreeHash())
                throw Error("Git hash mismatch in input '%s' (%s), expected '%s', got '%s'",
                    input.to_string(), path, input.getTreeHash()->gitRev(), gotHash.gitRev());
        }

        Attrs infoAttrs({});
        if (input.getTreeHash()) {
            infoAttrs.insert_or_assign("treeHash", input.getTreeHash()->gitRev());
            infoAttrs.insert_or_assign("revCount", 0);
            infoAttrs.insert_or_assign("lastModified", 0);
        } else {
            auto lastModified = std::stoull(runProgram("git", true, { "-C", repoDir, "log", "-1", "--format=%ct", "--no-show-signature", input.getRev()->gitRev() }));
            infoAttrs.insert_or_assign("rev", input.getRev()->gitRev());
            infoAttrs.insert_or_assign("lastModified", lastModified);

            if (!shallow)
                infoAttrs.insert_or_assign("revCount",
                    std::stoull(runProgram("git", true, { "-C", repoDir, "rev-list", "--count", input.getRev()->gitRev() })));
        }

        if (!_input.getRev() && !_input.getTreeHash())
            getCache()->add(
                store,
                mutableAttrs,
                infoAttrs,
                storePathDesc,
                false);

        getCache()->add(
            store,
            getImmutableAttrs(),
            infoAttrs,
            storePathDesc,
            true);

        return makeResult(infoAttrs, std::move(storePathDesc));
    }
};

static auto r1 = OnStartup([] { registerInputScheme(std::make_unique<GitInputScheme>()); });

}
