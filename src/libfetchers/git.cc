#include "fetchers.hh"
#include "cache.hh"
#include "globals.hh"
#include "tarfile.hh"
#include "store-api.hh"
#include "url-parts.hh"

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
            if (name != "type" && name != "url" && name != "ref" && name != "rev" && name != "shallow" && name != "submodules" && name != "lastModified" && name != "revCount" && name != "narHash")
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

                auto storePath = store->addToStore("source", actualUrl, FileIngestionMethod::Recursive, htSHA256, filter);

                // FIXME: maybe we should use the timestamp of the last
                // modified dirty file?
                input.attrs.insert_or_assign(
                    "lastModified",
                    haveCommits ? std::stoull(runProgram("git", true, { "-C", actualUrl, "log", "-1", "--format=%ct", "--no-show-signature", "HEAD" })) : 0);

                return {
                    Tree(store->printStorePath(storePath), std::move(storePath)),
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

        if (submodules) {
            Path tmpGitDir = createTempDir();
            AutoDelete delTmpGitDir(tmpGitDir, true);

            runProgram("git", true, { "init", tmpDir, "--separate-git-dir", tmpGitDir });
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
                RunOptions gitOptions("git", { "-C", repoDir, "archive", input.getRev()->gitRev() });
                gitOptions.standardOut = &sink;
                runProgram2(gitOptions);
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
