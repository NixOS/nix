#include "fetchers.hh"
#include "processes.hh"
#include "users.hh"
#include "cache.hh"
#include "globals.hh"
#include "tarfile.hh"
#include "store-api.hh"
#include "url-parts.hh"
#include "fs-input-accessor.hh"
#include "posix-source-accessor.hh"
#include "fetch-settings.hh"

#include <sys/time.h>

using namespace std::string_literals;

namespace nix::fetchers {

static RunOptions hgOptions(const Strings & args)
{
    auto env = getEnv();
    // Set HGPLAIN: this means we get consistent output from hg and avoids leakage from a user or system .hgrc.
    env["HGPLAIN"] = "";

    return {
        .program = "hg",
        .searchPath = true,
        .args = args,
        .environment = env
    };
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
    std::optional<Input> inputFromURL(const ParsedURL & url, bool requireTree) const override
    {
        if (url.scheme != "hg+http" &&
            url.scheme != "hg+https" &&
            url.scheme != "hg+ssh" &&
            url.scheme != "hg+file") return {};

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

    std::optional<Input> inputFromAttrs(const Attrs & attrs) const override
    {
        parseURL(getStrAttr(attrs, "url"));

        if (auto ref = maybeGetStrAttr(attrs, "ref")) {
            if (!std::regex_match(*ref, refRegex))
                throw BadURL("invalid Mercurial branch/tag name '%s'", *ref);
        }

        Input input;
        input.attrs = attrs;
        return input;
    }

    ParsedURL toURL(const Input & input) const override
    {
        auto url = parseURL(getStrAttr(input.attrs, "url"));
        url.scheme = "hg+" + url.scheme;
        if (auto rev = input.getRev()) url.query.insert_or_assign("rev", rev->gitRev());
        if (auto ref = input.getRef()) url.query.insert_or_assign("ref", *ref);
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
        return res;
    }

    std::optional<Path> getSourcePath(const Input & input) const override
    {
        auto url = parseURL(getStrAttr(input.attrs, "url"));
        if (url.scheme == "file" && !input.getRef() && !input.getRev())
            return url.path;
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
            throw Error("cannot commit '%s' to Mercurial repository '%s' because it's not a working tree", path, input.to_string());

        auto absPath = CanonPath(repoPath) / path;

        writeFile(absPath.abs(), contents);

        // FIXME: shut up if file is already tracked.
        runHg(
            { "add", absPath.abs() });

        if (commitMsg)
            runHg(
                { "commit", absPath.abs(), "-m", *commitMsg });
    }

    std::pair<bool, std::string> getActualUrl(const Input & input) const
    {
        auto url = parseURL(getStrAttr(input.attrs, "url"));
        bool isLocal = url.scheme == "file";
        return {isLocal, isLocal ? url.path : url.base};
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

            bool clean = runHg({ "status", "-R", actualUrl, "--modified", "--added", "--removed" }) == "";

            if (!clean) {

                /* This is an unclean working tree. So copy all tracked
                   files. */

                if (!fetchSettings.allowDirty)
                    throw Error("Mercurial tree '%s' is unclean", actualUrl);

                if (fetchSettings.warnDirty)
                    warn("Mercurial tree '%s' is unclean", actualUrl);

                input.attrs.insert_or_assign("ref", chomp(runHg({ "branch", "-R", actualUrl })));

                auto files = tokenizeString<std::set<std::string>>(
                    runHg({ "status", "-R", actualUrl, "--clean", "--modified", "--added", "--no-status", "--print0" }), "\0"s);

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

                PosixSourceAccessor accessor;
                auto storePath = store->addToStore(
                    input.getName(),
                    accessor, CanonPath { actualPath },
                    FileIngestionMethod::Recursive, HashAlgorithm::SHA256, {},
                    filter);

                return storePath;
            }
        }

        if (!input.getRef()) input.attrs.insert_or_assign("ref", "default");

        auto checkHashAlgorithm = [&](const std::optional<Hash> & hash)
        {
            if (hash.has_value() && hash->algo != HashAlgorithm::SHA1)
                throw Error("Hash '%s' is not supported by Mercurial. Only sha1 is supported.", hash->to_string(HashFormat::Base16, true));
        };


        auto getLockedAttrs = [&]()
        {
            checkHashAlgorithm(input.getRev());

            return Attrs({
                {"type", "hg"},
                {"name", name},
                {"rev", input.getRev()->gitRev()},
            });
        };

        auto makeResult = [&](const Attrs & infoAttrs, const StorePath & storePath) -> StorePath
        {
            assert(input.getRev());
            assert(!origRev || origRev == input.getRev());
            input.attrs.insert_or_assign("revCount", getIntAttr(infoAttrs, "revCount"));
            return storePath;
        };

        if (input.getRev()) {
            if (auto res = getCache()->lookup(*store, getLockedAttrs()))
                return makeResult(res->first, std::move(res->second));
        }

        auto revOrRef = input.getRev() ? input.getRev()->gitRev() : *input.getRef();

        Attrs unlockedAttrs({
            {"type", "hg"},
            {"name", name},
            {"url", actualUrl},
            {"ref", *input.getRef()},
        });

        if (auto res = getCache()->lookup(*store, unlockedAttrs)) {
            auto rev2 = Hash::parseAny(getStrAttr(res->first, "rev"), HashAlgorithm::SHA1);
            if (!input.getRev() || input.getRev() == rev2) {
                input.attrs.insert_or_assign("rev", rev2.gitRev());
                return makeResult(res->first, std::move(res->second));
            }
        }

        Path cacheDir = fmt("%s/nix/hg/%s", getCacheDir(), hashString(HashAlgorithm::SHA256, actualUrl).to_string(HashFormat::Nix32, false));

        /* If this is a commit hash that we already have, we don't
           have to pull again. */
        if (!(input.getRev()
                && pathExists(cacheDir)
                && runProgram(hgOptions({ "log", "-R", cacheDir, "-r", input.getRev()->gitRev(), "--template", "1" })).second == "1"))
        {
            Activity act(*logger, lvlTalkative, actUnknown, fmt("fetching Mercurial repository '%s'", actualUrl));

            if (pathExists(cacheDir)) {
                try {
                    runHg({ "pull", "-R", cacheDir, "--", actualUrl });
                }
                catch (ExecError & e) {
                    auto transJournal = cacheDir + "/.hg/store/journal";
                    /* hg throws "abandoned transaction" error only if this file exists */
                    if (pathExists(transJournal)) {
                        runHg({ "recover", "-R", cacheDir });
                        runHg({ "pull", "-R", cacheDir, "--", actualUrl });
                    } else {
                        throw ExecError(e.status, "'hg pull' %s", statusToString(e.status));
                    }
                }
            } else {
                createDirs(dirOf(cacheDir));
                runHg({ "clone", "--noupdate", "--", actualUrl, cacheDir });
            }
        }

        auto tokens = tokenizeString<std::vector<std::string>>(
            runHg({ "log", "-R", cacheDir, "-r", revOrRef, "--template", "{node} {rev} {branch}" }));
        assert(tokens.size() == 3);

        input.attrs.insert_or_assign("rev", Hash::parseAny(tokens[0], HashAlgorithm::SHA1).gitRev());
        auto revCount = std::stoull(tokens[1]);
        input.attrs.insert_or_assign("ref", tokens[2]);

        if (auto res = getCache()->lookup(*store, getLockedAttrs()))
            return makeResult(res->first, std::move(res->second));

        Path tmpDir = createTempDir();
        AutoDelete delTmpDir(tmpDir, true);

        runHg({ "archive", "-R", cacheDir, "-r", input.getRev()->gitRev(), tmpDir });

        deletePath(tmpDir + "/.hg_archival.txt");

        PosixSourceAccessor accessor;
        auto storePath = store->addToStore(name, accessor, CanonPath { tmpDir });

        Attrs infoAttrs({
            {"rev", input.getRev()->gitRev()},
            {"revCount", (uint64_t) revCount},
        });

        if (!origRev)
            getCache()->add(
                *store,
                unlockedAttrs,
                infoAttrs,
                storePath,
                false);

        getCache()->add(
            *store,
            getLockedAttrs(),
            infoAttrs,
            storePath,
            true);

        return makeResult(infoAttrs, std::move(storePath));
    }

    std::pair<ref<InputAccessor>, Input> getAccessor(ref<Store> store, const Input & _input) const override
    {
        Input input(_input);

        auto storePath = fetchToStore(store, input);

        return {makeStorePathAccessor(store, storePath), input};
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

}
