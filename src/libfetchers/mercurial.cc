#include "nix/fetchers/fetchers.hh"
#include "nix/util/file-system.hh"
#include "nix/util/fmt.hh"
#include "nix/util/os-string.hh"
#include "nix/util/processes.hh"
#include "nix/util/util.hh"
#include "nix/util/environment-variables.hh"
#include "nix/util/users.hh"
#include "nix/fetchers/cache.hh"
#include "nix/store/globals.hh"
#include "nix/util/tarfile.hh"
#include "nix/store/store-api.hh"
#include "nix/util/url-parts.hh"
#include "nix/fetchers/fetch-settings.hh"

#include <sys/time.h>
#include <variant>

using namespace std::string_literals;

namespace nix::fetchers {

static RunOptions hgOptions(OsStrings args)
{
    auto env = getEnvOs();
    // Set HGPLAIN: this means we get consistent output from hg and avoids leakage from a user or system .hgrc.
    env[OS_STR("HGPLAIN")] = OS_STR("");

    return {.program = "hg", .lookupPath = true, .args = std::move(args), .environment = env};
}

// runProgram wrapper that uses hgOptions instead of stock RunOptions.
static std::string runHg(OsStrings args, const std::optional<std::string> & input = {})
{
    RunOptions opts = hgOptions(std::move(args));
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

    std::string schemeDescription() const override
    {
        // TODO
        return "";
    }

    const std::map<std::string, AttributeInfo> & allowedAttrs() const override
    {
        static const std::map<std::string, AttributeInfo> attrs = {
            {
                "url",
                {},
            },
            {
                "ref",
                {},
            },
            {
                "rev",
                {},
            },
            {
                "revCount",
                {},
            },
            {
                "narHash",
                {},
            },
            {
                "name",
                {},
            },
        };
        return attrs;
    }

    std::optional<Input> inputFromAttrs(const Settings & settings, const Attrs & attrs) const override
    {
        parseURL(getStrAttr(attrs, "url"));

        if (auto ref = maybeGetStrAttr(attrs, "ref")) {
            if (!std::regex_match(*ref, refRegex))
                throw BadURL("invalid Mercurial branch/tag name '%s'", *ref);
        }

        Input input{};
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
            return urlPathToPath(url.path);
        return {};
    }

    void putFile(
        const Input & input,
        const CanonPath & path,
        std::string_view contents,
        std::optional<std::string> commitMsg) const override
    {
        std::visit(
            overloaded{
                [&](const std::filesystem::path & repoPath) {
                    auto absPath = repoPath / path.rel();

                    writeFile(absPath, contents);

                    // FIXME: shut up if file is already tracked.
                    runHg({OS_STR("add"), absPath.native()});

                    if (commitMsg)
                        runHg({OS_STR("commit"), absPath.native(), OS_STR("-m"), string_to_os_string(*commitMsg)});
                },
                [&](const std::string &) {
                    throw Error(
                        "cannot commit '%s' to Mercurial repository '%s' because it's not a working tree",
                        path,
                        input.to_string());
                },
            },
            getActualUrl(input));
    }

    std::variant<std::filesystem::path, std::string> getActualUrl(const Input & input) const
    {
        auto url = parseURL(getStrAttr(input.attrs, "url"));
        if (url.scheme == "file")
            return urlPathToPath(url.path);
        else
            return url.to_string();
    }

    StorePath fetchToStore(const Settings & settings, Store & store, Input & input) const
    {
        auto origRev = input.getRev();

        auto name = input.getName();

        auto actualUrl_ = getActualUrl(input);

        // FIXME: return lastModified.

        // FIXME: don't clone local repositories.

        if (auto * localPathP = std::get_if<std::filesystem::path>(&actualUrl_)) {
            auto & localPath = *localPathP;
            auto unlocked = !input.getRef() && !input.getRev();
            auto isValidLocalRepo = pathExists(localPath / ".hg");
            // short circuiting to not bother checking if locked / no repo is important.
            bool dirty = unlocked && isValidLocalRepo
                         && runHg({
                                OS_STR("status"),
                                OS_STR("-R"),
                                localPath.native(),
                                OS_STR("--modified"),
                                OS_STR("--added"),
                                OS_STR("--removed"),
                            }) != "";
            if (dirty) {
                /* This is an unclean working tree. So copy all tracked
                   files. */

                if (!settings.allowDirty)
                    throw Error("Mercurial tree '%s' is unclean", PathFmt{localPath});

                if (settings.warnDirty)
                    warn("Mercurial tree '%s' is unclean", PathFmt{localPath});

                input.attrs.insert_or_assign("ref", chomp(runHg({OS_STR("branch"), OS_STR("-R"), localPath.native()})));

                auto files = tokenizeString<StringSet>(
                    runHg({
                        OS_STR("status"),
                        OS_STR("-R"),
                        localPath.native(),
                        OS_STR("--clean"),
                        OS_STR("--modified"),
                        OS_STR("--added"),
                        OS_STR("--no-status"),
                        OS_STR("--print0"),
                    }),
                    "\0"s);

                auto actualPath = absPath(localPath);

                PathFilter filter = [&](const std::string & p) -> bool {
                    assert(hasPrefix(p, actualPath.string()));
                    std::string file(p, actualPath.string().size() + 1);

                    auto st = lstat(p);

                    if (S_ISDIR(st.st_mode)) {
                        auto prefix = file + "/";
                        auto i = files.lower_bound(prefix);
                        return i != files.end() && hasPrefix(*i, prefix);
                    }

                    return files.count(file);
                };

                return store.addToStore(
                    input.getName(),
                    {getFSSourceAccessor(), CanonPath(actualPath.string())},
                    ContentAddressMethod::Raw::NixArchive,
                    HashAlgorithm::SHA256,
                    {},
                    filter);
            }
        }

        auto [actualUrl, actualUrlOs] = std::visit(
            overloaded{
                [&](const std::filesystem::path & p) { return std::make_pair(p.string(), p.native()); },
                [&](const std::string & s) { return std::make_pair(s, string_to_os_string(s)); },
            },
            actualUrl_);

        if (!input.getRef())
            input.attrs.insert_or_assign("ref", "default");

        auto revInfoKey = [&](const Hash & rev) {
            if (rev.algo != HashAlgorithm::SHA1)
                throw Error(
                    "Hash '%s' is not supported by Mercurial. Only sha1 is supported.",
                    rev.to_string(HashFormat::Base16, true));

            return Cache::Key{"hgRev", {{"store", store.storeDir}, {"name", name}, {"rev", input.getRev()->gitRev()}}};
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
            if (auto res = settings.getCache()->lookupWithTTL(refToRevKey))
                input.attrs.insert_or_assign("rev", getRevAttr(*res, "rev").gitRev());
        }

        /* If we have a rev, check if we have a cached store path. */
        if (auto rev = input.getRev()) {
            if (auto res = settings.getCache()->lookupStorePath(revInfoKey(*rev), store))
                return makeResult(res->value, res->storePath);
        }

        std::filesystem::path cacheDir =
            getCacheDir() / "hg" / hashString(HashAlgorithm::SHA256, actualUrl).to_string(HashFormat::Nix32, false);

        /* If this is a commit hash that we already have, we don't
           have to pull again. */
        if (!(input.getRev() && pathExists(cacheDir)
              && runProgram(hgOptions({
                                OS_STR("log"),
                                OS_STR("-R"),
                                cacheDir.native(),
                                OS_STR("-r"),
                                string_to_os_string(input.getRev()->gitRev()),
                                OS_STR("--template"),
                                OS_STR("1"),
                            }))
                         .second
                     == "1")) {
            Activity act(*logger, lvlTalkative, actUnknown, fmt("fetching Mercurial repository '%s'", actualUrl));

            if (pathExists(cacheDir)) {
                try {
                    runHg({OS_STR("pull"), OS_STR("-R"), cacheDir.native(), OS_STR("--"), actualUrlOs});
                } catch (ExecError & e) {
                    auto transJournal = cacheDir / ".hg" / "store" / "journal";
                    /* hg throws "abandoned transaction" error only if this file exists */
                    if (pathExists(transJournal)) {
                        runHg({OS_STR("recover"), OS_STR("-R"), cacheDir.native()});
                        runHg({OS_STR("pull"), OS_STR("-R"), cacheDir.native(), OS_STR("--"), actualUrlOs});
                    } else {
                        throw ExecError(e.status, "'hg pull' %s", statusToString(e.status));
                    }
                }
            } else {
                createDirs(cacheDir.parent_path());
                runHg({OS_STR("clone"), OS_STR("--noupdate"), OS_STR("--"), actualUrlOs, cacheDir.native()});
            }
        }

        /* Fetch the remote rev or ref. */
        auto tokens = tokenizeString<std::vector<std::string>>(runHg({
            OS_STR("log"),
            OS_STR("-R"),
            cacheDir.native(),
            OS_STR("-r"),
            string_to_os_string(input.getRev() ? input.getRev()->gitRev() : *input.getRef()),
            OS_STR("--template"),
            OS_STR("{node} {rev} {branch}"),
        }));
        assert(tokens.size() == 3);

        auto rev = Hash::parseAny(tokens[0], HashAlgorithm::SHA1);
        input.attrs.insert_or_assign("rev", rev.gitRev());
        auto revCount = std::stoull(tokens[1]);
        input.attrs.insert_or_assign("ref", tokens[2]);

        /* Now that we have the rev, check the cache again for a
           cached store path. */
        if (auto res = settings.getCache()->lookupStorePath(revInfoKey(rev), store))
            return makeResult(res->value, res->storePath);

        std::filesystem::path tmpDir = createTempDir();
        AutoDelete delTmpDir(tmpDir, true);

        runHg({
            OS_STR("archive"),
            OS_STR("-R"),
            cacheDir.native(),
            OS_STR("-r"),
            string_to_os_string(rev.gitRev()),
            tmpDir.native(),
        });

        deletePath(tmpDir / ".hg_archival.txt");

        auto storePath = store.addToStore(name, {getFSSourceAccessor(), CanonPath(tmpDir.string())});

        Attrs infoAttrs({
            {"revCount", (uint64_t) revCount},
        });

        if (!origRev)
            settings.getCache()->upsert(refToRevKey, {{"rev", rev.gitRev()}});

        settings.getCache()->upsert(revInfoKey(rev), store, infoAttrs, storePath);

        return makeResult(infoAttrs, std::move(storePath));
    }

    std::pair<ref<SourceAccessor>, Input>
    getAccessor(const Settings & settings, Store & store, const Input & _input) const override
    {
        Input input(_input);

        auto storePath = fetchToStore(settings, store, input);
        auto accessor = store.requireStoreObjectAccessor(storePath);

        accessor->setPathDisplay("«" + input.to_string() + "»");

        return {accessor, input};
    }

    bool isLocked(const Settings & settings, const Input & input) const override
    {
        return (bool) input.getRev();
    }

    std::optional<std::string> getFingerprint(Store & store, const Input & input) const override
    {
        if (auto rev = input.getRev())
            return rev->gitRev();
        else
            return std::nullopt;
    }
};

static auto rMercurialInputScheme = OnStartup([] { registerInputScheme(std::make_unique<MercurialInputScheme>()); });

} // namespace nix::fetchers
