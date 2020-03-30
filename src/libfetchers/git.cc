#include "fetchers.hh"
#include "cache.hh"
#include "globals.hh"
#include "tarfile.hh"
#include "store-api.hh"

#include <sys/time.h>

using namespace std::string_literals;

namespace nix::fetchers {

static std::string readHead(const Path & path)
{
    return chomp(runProgram("git", true, { "-C", path, "rev-parse", "--abbrev-ref", "HEAD" }));
}

struct GitInput : Input
{
    ParsedURL url;
    std::optional<std::string> ref;
    std::optional<Hash> rev;
    bool shallow = false;

    GitInput(const ParsedURL & url) : url(url)
    { }

    std::string type() const override { return "git"; }

    bool operator ==(const Input & other) const override
    {
        auto other2 = dynamic_cast<const GitInput *>(&other);
        return
            other2
            && url == other2->url
            && rev == other2->rev
            && ref == other2->ref;
    }

    bool isImmutable() const override
    {
        return (bool) rev;
    }

    std::optional<std::string> getRef() const override { return ref; }

    std::optional<Hash> getRev() const override { return rev; }

    ParsedURL toURL() const override
    {
        ParsedURL url2(url);
        if (url2.scheme != "git") url2.scheme = "git+" + url2.scheme;
        if (rev) url2.query.insert_or_assign("rev", rev->gitRev());
        if (ref) url2.query.insert_or_assign("ref", *ref);
        if (shallow) url2.query.insert_or_assign("shallow", "1");
        return url2;
    }

    Attrs toAttrsInternal() const override
    {
        Attrs attrs;
        attrs.emplace("url", url.to_string());
        if (ref)
            attrs.emplace("ref", *ref);
        if (rev)
            attrs.emplace("rev", rev->gitRev());
        if (shallow)
            attrs.emplace("shallow", true);
        return attrs;
    }

    std::pair<bool, std::string> getActualUrl() const
    {
        // Don't clone file:// URIs (but otherwise treat them the
        // same as remote URIs, i.e. don't use the working tree or
        // HEAD).
        static bool forceHttp = getEnv("_NIX_FORCE_HTTP") == "1"; // for testing
        bool isLocal = url.scheme == "file" && !forceHttp;
        return {isLocal, isLocal ? url.path : url.base};
    }

    std::pair<Tree, std::shared_ptr<const Input>> fetchTreeInternal(nix::ref<Store> store) const override
    {
        auto name = "source";

        auto input = std::make_shared<GitInput>(*this);

        assert(!rev || rev->type == htSHA1);

        auto cacheType = shallow ? "git-shallow" : "git";

        auto getImmutableAttrs = [&]()
        {
            return Attrs({
                {"type", cacheType},
                {"name", name},
                {"rev", input->rev->gitRev()},
            });
        };

        auto makeResult = [&](const Attrs & infoAttrs, StorePath && storePath)
            -> std::pair<Tree, std::shared_ptr<const Input>>
        {
            assert(input->rev);
            assert(!rev || rev == input->rev);
            return {
                Tree {
                    .actualPath = store->toRealPath(storePath),
                    .storePath = std::move(storePath),
                    .info = TreeInfo {
                        .revCount = shallow ? std::nullopt : std::optional(getIntAttr(infoAttrs, "revCount")),
                        .lastModified = getIntAttr(infoAttrs, "lastModified"),
                    },
                },
                input
            };
        };

        if (rev) {
            if (auto res = getCache()->lookup(store, getImmutableAttrs()))
                return makeResult(res->first, std::move(res->second));
        }

        auto [isLocal, actualUrl_] = getActualUrl();
        auto actualUrl = actualUrl_; // work around clang bug

        // If this is a local directory and no ref or revision is
        // given, then allow the use of an unclean working tree.
        if (!input->ref && !input->rev && isLocal) {
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

                auto files = tokenizeString<std::set<std::string>>(
                    runProgram("git", true, { "-C", actualUrl, "ls-files", "-z" }), "\0"s);

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

                auto storePath = store->addToStore("source", actualUrl, true, htSHA256, filter);

                auto tree = Tree {
                    .actualPath = store->printStorePath(storePath),
                    .storePath = std::move(storePath),
                    .info = TreeInfo {
                        // FIXME: maybe we should use the timestamp of the last
                        // modified dirty file?
                        .lastModified = haveCommits ? std::stoull(runProgram("git", true, { "-C", actualUrl, "log", "-1", "--format=%ct", "HEAD" })) : 0,
                    }
                };

                return {std::move(tree), input};
            }
        }

        if (!input->ref) input->ref = isLocal ? readHead(actualUrl) : "master";

        Attrs mutableAttrs({
            {"type", cacheType},
            {"name", name},
            {"url", actualUrl},
            {"ref", *input->ref},
        });

        Path repoDir;

        if (isLocal) {

            if (!input->rev)
                input->rev = Hash(chomp(runProgram("git", true, { "-C", actualUrl, "rev-parse", *input->ref })), htSHA1);

            repoDir = actualUrl;

        } else {

            if (auto res = getCache()->lookup(store, mutableAttrs)) {
                auto rev2 = Hash(getStrAttr(res->first, "rev"), htSHA1);
                if (!rev || rev == rev2) {
                    input->rev = rev2;
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
                input->ref->compare(0, 5, "refs/") == 0
                ? cacheDir + "/" + *input->ref
                : cacheDir + "/refs/heads/" + *input->ref;

            bool doFetch;
            time_t now = time(0);

            /* If a rev was specified, we need to fetch if it's not in the
               repo. */
            if (input->rev) {
                try {
                    runProgram("git", true, { "-C", repoDir, "cat-file", "-e", input->rev->gitRev() });
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
                    runProgram("git", true, { "-C", repoDir, "fetch", "--quiet", "--force", "--", actualUrl, fmt("%s:%s", *input->ref, *input->ref) });
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

            if (!input->rev)
                input->rev = Hash(chomp(readFile(localRefFile)), htSHA1);
        }

        bool isShallow = chomp(runProgram("git", true, { "-C", repoDir, "rev-parse", "--is-shallow-repository" })) == "true";

        if (isShallow && !shallow)
            throw Error("'%s' is a shallow Git repository, but a non-shallow repository is needed", actualUrl);

        // FIXME: check whether rev is an ancestor of ref.

        printTalkative("using revision %s of repo '%s'", input->rev->gitRev(), actualUrl);

        /* Now that we know the ref, check again whether we have it in
           the store. */
        if (auto res = getCache()->lookup(store, getImmutableAttrs()))
            return makeResult(res->first, std::move(res->second));

        // FIXME: should pipe this, or find some better way to extract a
        // revision.
        auto source = sinkToSource([&](Sink & sink) {
            RunOptions gitOptions("git", { "-C", repoDir, "archive", input->rev->gitRev() });
            gitOptions.standardOut = &sink;
            runProgram2(gitOptions);
        });

        Path tmpDir = createTempDir();
        AutoDelete delTmpDir(tmpDir, true);

        unpackTarfile(*source, tmpDir);

        auto storePath = store->addToStore(name, tmpDir);

        auto lastModified = std::stoull(runProgram("git", true, { "-C", repoDir, "log", "-1", "--format=%ct", input->rev->gitRev() }));

        Attrs infoAttrs({
            {"rev", input->rev->gitRev()},
            {"lastModified", lastModified},
        });

        if (!shallow)
            infoAttrs.insert_or_assign("revCount",
                std::stoull(runProgram("git", true, { "-C", repoDir, "rev-list", "--count", input->rev->gitRev() })));

        if (!this->rev)
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

struct GitInputScheme : InputScheme
{
    std::unique_ptr<Input> inputFromURL(const ParsedURL & url) override
    {
        if (url.scheme != "git" &&
            url.scheme != "git+http" &&
            url.scheme != "git+https" &&
            url.scheme != "git+ssh" &&
            url.scheme != "git+file") return nullptr;

        auto url2(url);
        if (hasPrefix(url2.scheme, "git+")) url2.scheme = std::string(url2.scheme, 4);
        url2.query.clear();

        Attrs attrs;
        attrs.emplace("type", "git");

        for (auto &[name, value] : url.query) {
            if (name == "rev" || name == "ref")
                attrs.emplace(name, value);
            else
                url2.query.emplace(name, value);
        }

        attrs.emplace("url", url2.to_string());

        return inputFromAttrs(attrs);
    }

    std::unique_ptr<Input> inputFromAttrs(const Attrs & attrs) override
    {
        if (maybeGetStrAttr(attrs, "type") != "git") return {};

        for (auto & [name, value] : attrs)
            if (name != "type" && name != "url" && name != "ref" && name != "rev" && name != "shallow")
                throw Error("unsupported Git input attribute '%s'", name);

        auto input = std::make_unique<GitInput>(parseURL(getStrAttr(attrs, "url")));
        if (auto ref = maybeGetStrAttr(attrs, "ref")) {
            if (!std::regex_match(*ref, refRegex))
                throw BadURL("invalid Git branch/tag name '%s'", *ref);
            input->ref = *ref;
        }
        if (auto rev = maybeGetStrAttr(attrs, "rev"))
            input->rev = Hash(*rev, htSHA1);

        input->shallow = maybeGetBoolAttr(attrs, "shallow").value_or(false);

        return input;
    }
};

static auto r1 = OnStartup([] { registerInputScheme(std::make_unique<GitInputScheme>()); });

}
