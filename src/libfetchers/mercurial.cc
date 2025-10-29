#include "nix/fetchers/fetchers.hh"
#include "nix/util/processes.hh"
#include "nix/util/users.hh"
#include "nix/fetchers/cache.hh"
#include "nix/store/globals.hh"
#include "nix/util/tarfile.hh"
#include "nix/store/store-api.hh"
#include "nix/util/url-parts.hh"
#include "nix/fetchers/fetch-settings.hh"

#include <sys/time.h>

using namespace std::string_literals;

namespace nix::fetchers {

static RunOptions hgOptions(const Strings & args)
{
    auto env = getEnv();
    // Set HGPLAIN: this means we get consistent output from hg and avoids leakage from a user or system .hgrc.
    env["HGPLAIN"] = "";

    return {.program = "hg", .lookupPath = true, .args = args, .environment = env};
}

// runProgram wrapper that uses hgOptions instead of stock RunOptions.
static std::string runHg(const Strings & args, const std::optional<std::string> & input = {})
{
    RunOptions opts = hgOptions(args);
    opts.input = input;

    auto res = runProgram(std::move(opts));

    if (!statusOk(res.first))
        throw ExecError(res.first, "hg %1%", statusToString(res.first));

    return res.second;
}

struct MercurialInputScheme : InputScheme
{
    std::optional<Input> inputFromURL(const Settings & settings, const ParsedURL & url, bool requireTree) const override
    {
        if (url.scheme != "hg+http" && url.scheme != "hg+https" && url.scheme != "hg+ssh" && url.scheme != "hg+file")
            return {};

        auto url2(url);
        url2.scheme = std::string(url2.scheme, 3);
        url2.query.clear();

        Attrs attrs;
        attrs.emplace("type", "hg");

        for (auto & [name, value] : url.query) {
            if (name == "rev" || name == "ref")
                attrs.emplace(name, value);
            else
                url2.query.emplace(name, value);
        }

        attrs.emplace("url", url2.to_string());

        return inputFromAttrs(settings, attrs);
    }

    std::string_view schemeName() const override
    {
        return "hg";
    }

    StringSet allowedAttrs() const override
    {
        return {
            "url",
            "ref",
            "rev",
            "revCount",
            "narHash",
            "name",
        };
    }

    std::optional<Input> inputFromAttrs(const Settings & settings, const Attrs & attrs) const override
    {
        parseURL(getStrAttr(attrs, "url"));

        if (auto ref = maybeGetStrAttr(attrs, "ref")) {
            if (!std::regex_match(*ref, refRegex))
                throw BadURL("invalid Mercurial branch/tag name '%s'", *ref);
        }

        Input input{settings};
        input.attrs = attrs;
        return input;
    }

    ParsedURL toURL(const Input & input) const override
    {
        auto url = parseURL(getStrAttr(input.attrs, "url"));
        url.scheme = "hg+" + url.scheme;
        if (auto rev = input.getRev())
            url.query.insert_or_assign("rev", rev->gitRev());
        if (auto ref = input.getRef())
            url.query.insert_or_assign("ref", *ref);
        return url;
    }

    Input applyOverrides(const Input & input, std::optional<std::string> ref, std::optional<Hash> rev) const override
    {
        auto res(input);
        if (rev)
            res.attrs.insert_or_assign("rev", rev->gitRev());
        if (ref)
            res.attrs.insert_or_assign("ref", *ref);
        return res;
    }

    std::optional<std::filesystem::path> getSourcePath(const Input & input) const override
    {
        auto url = parseURL(getStrAttr(input.attrs, "url"));
        if (url.scheme == "file" && !input.getRef() && !input.getRev())
            return renderUrlPathEnsureLegal(url.path);
        return {};
    }

    void putFile(
        const Input & input,
        const CanonPath & path,
        std::string_view contents,
        std::optional<std::string> commitMsg) const override
    {
        auto [isLocal, repoPath] = getActualUrl(input);
        if (!isLocal)
            throw Error(
                "cannot commit '%s' to Mercurial repository '%s' because it's not a working tree",
                path,
                input.to_string());

        auto absPath = CanonPath(repoPath) / path;

        writeFile(absPath.abs(), contents);

        // FIXME: shut up if file is already tracked.
        runHg({"add", absPath.abs()});

        if (commitMsg)
            runHg({"commit", absPath.abs(), "-m", *commitMsg});
    }

    std::pair<bool, std::string> getActualUrl(const Input & input) const
    {
        auto url = parseURL(getStrAttr(input.attrs, "url"));
        bool isLocal = url.scheme == "file";
        return {isLocal, isLocal ? renderUrlPathEnsureLegal(url.path) : url.to_string()};
    }

    StorePath fetchToStore(ref<Store> store, Input & input) const
    {
        auto origRev = input.getRev();

        auto name = input.getName();

        auto [isLocal, actualUrl_] = getActualUrl(input);
        auto actualUrl = actualUrl_; // work around clang bug

        // FIXME: return lastModified.

        // FIXME: don't clone local repositories.

        if (!input.getRef() && !input.getRev() && isLocal && pathExists(actualUrl + "/.hg")) {

            bool clean = runHg({"status", "-R", actualUrl, "--modified", "--added", "--removed"}) == "";

            if (!clean) {

                /* This is an unclean working tree. So copy all tracked
                   files. */

                if (!input.settings->allowDirty)
                    throw Error("Mercurial tree '%s' is unclean", actualUrl);

                if (input.settings->warnDirty)
                    warn("Mercurial tree '%s' is unclean", actualUrl);

                input.attrs.insert_or_assign("ref", chomp(runHg({"branch", "-R", actualUrl})));

                auto files = tokenizeString<StringSet>(
                    runHg({"status", "-R", actualUrl, "--clean", "--modified", "--added", "--no-status", "--print0"}),
                    "\0"s);

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

                auto storePath = store->addToStore(
                    input.getName(),
                    {getFSSourceAccessor(), CanonPath(actualPath)},
                    ContentAddressMethod::Raw::NixArchive,
                    HashAlgorithm::SHA256,
                    {},
                    filter);

                return storePath;
            }
        }

        if (!input.getRef())
            input.attrs.insert_or_assign("ref", "default");

        auto revInfoKey = [&](const Hash & rev) {
            if (rev.algo != HashAlgorithm::SHA1)
                throw Error(
                    "Hash '%s' is not supported by Mercurial. Only sha1 is supported.",
                    rev.to_string(HashFormat::Base16, true));

            return Cache::Key{"hgRev", {{"store", store->storeDir}, {"name", name}, {"rev", input.getRev()->gitRev()}}};
        };

        auto makeResult = [&](const Attrs & infoAttrs, const StorePath & storePath) -> StorePath {
            assert(input.getRev());
            assert(!origRev || origRev == input.getRev());
            input.attrs.insert_or_assign("revCount", getIntAttr(infoAttrs, "revCount"));
            return storePath;
        };

        /* Check the cache for the most recent rev for this URL/ref. */
        Cache::Key refToRevKey{"hgRefToRev", {{"url", actualUrl}, {"ref", *input.getRef()}}};

        if (!input.getRev()) {
            if (auto res = input.settings->getCache()->lookupWithTTL(refToRevKey))
                input.attrs.insert_or_assign("rev", getRevAttr(*res, "rev").gitRev());
        }

        /* If we have a rev, check if we have a cached store path. */
        if (auto rev = input.getRev()) {
            if (auto res = input.settings->getCache()->lookupStorePath(revInfoKey(*rev), *store))
                return makeResult(res->value, res->storePath);
        }

        Path cacheDir =
            fmt("%s/hg/%s",
                getCacheDir(),
                hashString(HashAlgorithm::SHA256, actualUrl).to_string(HashFormat::Nix32, false));

        /* If this is a commit hash that we already have, we don't
           have to pull again. */
        if (!(input.getRev() && pathExists(cacheDir)
              && runProgram(hgOptions({"log", "-R", cacheDir, "-r", input.getRev()->gitRev(), "--template", "1"}))
                         .second
                     == "1")) {
            Activity act(*logger, lvlTalkative, actUnknown, fmt("fetching Mercurial repository '%s'", actualUrl));

            if (pathExists(cacheDir)) {
                try {
                    runHg({"pull", "-R", cacheDir, "--", actualUrl});
                } catch (ExecError & e) {
                    auto transJournal = cacheDir + "/.hg/store/journal";
                    /* hg throws "abandoned transaction" error only if this file exists */
                    if (pathExists(transJournal)) {
                        runHg({"recover", "-R", cacheDir});
                        runHg({"pull", "-R", cacheDir, "--", actualUrl});
                    } else {
                        throw ExecError(e.status, "'hg pull' %s", statusToString(e.status));
                    }
                }
            } else {
                createDirs(dirOf(cacheDir));
                runHg({"clone", "--noupdate", "--", actualUrl, cacheDir});
            }
        }

        /* Fetch the remote rev or ref. */
        auto tokens = tokenizeString<std::vector<std::string>>(runHg(
            {"log",
             "-R",
             cacheDir,
             "-r",
             input.getRev() ? input.getRev()->gitRev() : *input.getRef(),
             "--template",
             "{node} {rev} {branch}"}));
        assert(tokens.size() == 3);

        auto rev = Hash::parseAny(tokens[0], HashAlgorithm::SHA1);
        input.attrs.insert_or_assign("rev", rev.gitRev());
        auto revCount = std::stoull(tokens[1]);
        input.attrs.insert_or_assign("ref", tokens[2]);

        /* Now that we have the rev, check the cache again for a
           cached store path. */
        if (auto res = input.settings->getCache()->lookupStorePath(revInfoKey(rev), *store))
            return makeResult(res->value, res->storePath);

        Path tmpDir = createTempDir();
        AutoDelete delTmpDir(tmpDir, true);

        runHg({"archive", "-R", cacheDir, "-r", rev.gitRev(), tmpDir});

        deletePath(tmpDir + "/.hg_archival.txt");

        auto storePath = store->addToStore(name, {getFSSourceAccessor(), CanonPath(tmpDir)});

        Attrs infoAttrs({
            {"revCount", (uint64_t) revCount},
        });

        if (!origRev)
            input.settings->getCache()->upsert(refToRevKey, {{"rev", rev.gitRev()}});

        input.settings->getCache()->upsert(revInfoKey(rev), *store, infoAttrs, storePath);

        return makeResult(infoAttrs, std::move(storePath));
    }

    std::pair<ref<SourceAccessor>, Input> getAccessor(ref<Store> store, const Input & _input) const override
    {
        Input input(_input);

        auto storePath = fetchToStore(store, input);
        auto accessor = store->requireStoreObjectAccessor(storePath);

        accessor->setPathDisplay("«" + input.to_string() + "»");

        return {accessor, input};
    }

    bool isLocked(const Input & input) const override
    {
        return (bool) input.getRev();
    }

    std::optional<std::string> getFingerprint(ref<Store> store, const Input & input) const override
    {
        if (auto rev = input.getRev())
            return rev->gitRev();
        else
            return std::nullopt;
    }
};

static auto rMercurialInputScheme = OnStartup([] { registerInputScheme(std::make_unique<MercurialInputScheme>()); });

} // namespace nix::fetchers
