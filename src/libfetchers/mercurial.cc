#include "fetchers.hh"
#include "cache.hh"
#include "globals.hh"
#include "tarfile.hh"
#include "store-api.hh"

#include <sys/time.h>

using namespace std::string_literals;

namespace nix::fetchers {

struct MercurialInput : Input
{
    ParsedURL url;
    std::optional<std::string> ref;
    std::optional<Hash> rev;

    MercurialInput(const ParsedURL & url) : url(url)
    { }

    std::string type() const override { return "hg"; }

    bool operator ==(const Input & other) const override
    {
        auto other2 = dynamic_cast<const MercurialInput *>(&other);
        return
            other2
            && url == other2->url
            && rev == other2->rev
            && ref == other2->ref;
    }

    bool isImmutable() const override
    {
        return (bool) rev || narHash;
    }

    std::optional<std::string> getRef() const override { return ref; }

    std::optional<Hash> getRev() const override { return rev; }

    ParsedURL toURL() const override
    {
        ParsedURL url2(url);
        url2.scheme = "hg+" + url2.scheme;
        if (rev) url2.query.insert_or_assign("rev", rev->gitRev());
        if (ref) url2.query.insert_or_assign("ref", *ref);
        return url;
    }

    Attrs toAttrsInternal() const override
    {
        Attrs attrs;
        attrs.emplace("url", url.to_string());
        if (ref)
            attrs.emplace("ref", *ref);
        if (rev)
            attrs.emplace("rev", rev->gitRev());
        return attrs;
    }

    std::pair<bool, std::string> getActualUrl() const
    {
        bool isLocal = url.scheme == "file";
        return {isLocal, isLocal ? url.path : url.base};
    }

    std::pair<Tree, std::shared_ptr<const Input>> fetchTreeInternal(nix::ref<Store> store) const override
    {
        auto name = "source";

        auto input = std::make_shared<MercurialInput>(*this);

        auto [isLocal, actualUrl_] = getActualUrl();
        auto actualUrl = actualUrl_; // work around clang bug

        // FIXME: return lastModified.

        // FIXME: don't clone local repositories.

        if (!input->ref && !input->rev && isLocal && pathExists(actualUrl + "/.hg")) {

            bool clean = runProgram("hg", true, { "status", "-R", actualUrl, "--modified", "--added", "--removed" }) == "";

            if (!clean) {

                /* This is an unclean working tree. So copy all tracked
                   files. */

                if (!settings.allowDirty)
                    throw Error("Mercurial tree '%s' is unclean", actualUrl);

                if (settings.warnDirty)
                    warn("Mercurial tree '%s' is unclean", actualUrl);

                input->ref = chomp(runProgram("hg", true, { "branch", "-R", actualUrl }));

                auto files = tokenizeString<std::set<std::string>>(
                    runProgram("hg", true, { "status", "-R", actualUrl, "--clean", "--modified", "--added", "--no-status", "--print0" }), "\0"s);

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

                return {Tree {
                    .actualPath = store->printStorePath(storePath),
                    .storePath = std::move(storePath),
                }, input};
            }
        }

        if (!input->ref) input->ref = "default";

        auto getImmutableAttrs = [&]()
        {
            return Attrs({
                {"type", "hg"},
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
                Tree{
                    .actualPath = store->toRealPath(storePath),
                    .storePath = std::move(storePath),
                    .info = TreeInfo {
                        .revCount = getIntAttr(infoAttrs, "revCount"),
                    },
                },
                input
            };
        };

        if (input->rev) {
            if (auto res = getCache()->lookup(store, getImmutableAttrs()))
                return makeResult(res->first, std::move(res->second));
        }

        assert(input->rev || input->ref);
        auto revOrRef = input->rev ? input->rev->gitRev() : *input->ref;

        Attrs mutableAttrs({
            {"type", "hg"},
            {"name", name},
            {"url", actualUrl},
            {"ref", *input->ref},
        });

        if (auto res = getCache()->lookup(store, mutableAttrs)) {
            auto rev2 = Hash(getStrAttr(res->first, "rev"), htSHA1);
            if (!rev || rev == rev2) {
                input->rev = rev2;
                return makeResult(res->first, std::move(res->second));
            }
        }

        Path cacheDir = fmt("%s/nix/hg/%s", getCacheDir(), hashString(htSHA256, actualUrl).to_string(Base32, false));

        /* If this is a commit hash that we already have, we don't
           have to pull again. */
        if (!(input->rev
                && pathExists(cacheDir)
                && runProgram(
                    RunOptions("hg", { "log", "-R", cacheDir, "-r", input->rev->gitRev(), "--template", "1" })
                    .killStderr(true)).second == "1"))
        {
            Activity act(*logger, lvlTalkative, actUnknown, fmt("fetching Mercurial repository '%s'", actualUrl));

            if (pathExists(cacheDir)) {
                try {
                    runProgram("hg", true, { "pull", "-R", cacheDir, "--", actualUrl });
                }
                catch (ExecError & e) {
                    string transJournal = cacheDir + "/.hg/store/journal";
                    /* hg throws "abandoned transaction" error only if this file exists */
                    if (pathExists(transJournal)) {
                        runProgram("hg", true, { "recover", "-R", cacheDir });
                        runProgram("hg", true, { "pull", "-R", cacheDir, "--", actualUrl });
                    } else {
                        throw ExecError(e.status, fmt("'hg pull' %s", statusToString(e.status)));
                    }
                }
            } else {
                createDirs(dirOf(cacheDir));
                runProgram("hg", true, { "clone", "--noupdate", "--", actualUrl, cacheDir });
            }
        }

        auto tokens = tokenizeString<std::vector<std::string>>(
            runProgram("hg", true, { "log", "-R", cacheDir, "-r", revOrRef, "--template", "{node} {rev} {branch}" }));
        assert(tokens.size() == 3);

        input->rev = Hash(tokens[0], htSHA1);
        auto revCount = std::stoull(tokens[1]);
        input->ref = tokens[2];

        if (auto res = getCache()->lookup(store, getImmutableAttrs()))
            return makeResult(res->first, std::move(res->second));

        Path tmpDir = createTempDir();
        AutoDelete delTmpDir(tmpDir, true);

        runProgram("hg", true, { "archive", "-R", cacheDir, "-r", input->rev->gitRev(), tmpDir });

        deletePath(tmpDir + "/.hg_archival.txt");

        auto storePath = store->addToStore(name, tmpDir);

        Attrs infoAttrs({
            {"rev", input->rev->gitRev()},
            {"revCount", (int64_t) revCount},
        });

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

struct MercurialInputScheme : InputScheme
{
    std::unique_ptr<Input> inputFromURL(const ParsedURL & url) override
    {
        if (url.scheme != "hg+http" &&
            url.scheme != "hg+https" &&
            url.scheme != "hg+ssh" &&
            url.scheme != "hg+file") return nullptr;

        auto url2(url);
        url2.scheme = std::string(url2.scheme, 3);
        url2.query.clear();

        Attrs attrs;
        attrs.emplace("type", "hg");

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
        if (maybeGetStrAttr(attrs, "type") != "hg") return {};

        for (auto & [name, value] : attrs)
            if (name != "type" && name != "url" && name != "ref" && name != "rev")
                throw Error("unsupported Mercurial input attribute '%s'", name);

        auto input = std::make_unique<MercurialInput>(parseURL(getStrAttr(attrs, "url")));
        if (auto ref = maybeGetStrAttr(attrs, "ref")) {
            if (!std::regex_match(*ref, refRegex))
                throw BadURL("invalid Mercurial branch/tag name '%s'", *ref);
            input->ref = *ref;
        }
        if (auto rev = maybeGetStrAttr(attrs, "rev"))
            input->rev = Hash(*rev, htSHA1);
        return input;
    }
};

static auto r1 = OnStartup([] { registerInputScheme(std::make_unique<MercurialInputScheme>()); });

}
