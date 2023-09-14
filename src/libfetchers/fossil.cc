#include "fetchers.hh"
#include "cache.hh"
#include "globals.hh"
#include "tarfile.hh"
#include "store-api.hh"
#include "url-parts.hh"

#include "fetch-settings.hh"

#include <sys/time.h>
#include <nlohmann/json.hpp>
#include <iostream>

using namespace std::string_literals;

namespace nix::fetchers {

static RunOptions fslOptions(const Strings & args)
{
    auto env = getEnv();

    return {
        .program = "fossil",
        .searchPath = true,
        .args = args,
        .environment = env
    };
}

static std::string runFsl(const Strings & args, const std::optional<std::string> & input = {})
{
    RunOptions opts = fslOptions(args);
    opts.input = input;

    auto res = runProgram(std::move(opts));

    if (!statusOk(res.first))
        throw ExecError(res.first, fmt("fossil %1%", statusToString(res.first)));

    return res.second;
}

struct FossilInputScheme : InputScheme
{
    std::optional<Input> inputFromURL(const ParsedURL & url) override
    {
        if (url.scheme != "fsl+http" &&
            url.scheme != "fsl+https" &&
            url.scheme != "fsl+ssh" &&
            url.scheme != "fsl+file") return {};

        auto url2(url);
        url2.scheme = std::string(url2.scheme, 4);
        url2.query.clear();

        Attrs attrs;
        attrs.emplace("type", "fsl");

        for (auto &[name, value] : url.query) {
            if (name == "rev" || name == "ref")
                attrs.emplace(name, value);
            else
                url2.query.emplace(name, value);
        }

        attrs.emplace("url", url2.to_string());

        return inputFromAttrs(attrs);
    }

    std::optional<Input> inputFromAttrs(const Attrs & attrs) override
    {
        if (maybeGetStrAttr(attrs, "type") != "fsl") return {};

        for (auto & [name, value] : attrs)
            if (name != "type" && name != "url" && name != "ref" && name != "rev"  && name != "narHash" && name != "name")
                throw Error("unsupported Fossil input attribute '%s'", name);

        parseURL(getStrAttr(attrs, "url"));

        Input input;
        input.attrs = attrs;
        return input;
    }

    ParsedURL toURL(const Input & input) override
    {
        auto url = parseURL(getStrAttr(input.attrs, "url"));
        url.scheme = "fsl+" + url.scheme;
        if (auto rev = input.getRev()) url.query.insert_or_assign("rev", rev->to_string(Base16, false));
        if (auto ref = input.getRef()) url.query.insert_or_assign("ref", *ref);
        return url;
    }

    bool hasAllInfo(const Input & input) override
    {
        return true;
    }

    Input applyOverrides(
        const Input & input,
        std::optional<std::string> ref,
        std::optional<Hash> rev) override
    {
        auto res(input);
        if (rev) res.attrs.insert_or_assign("rev", rev->to_string(Base16, false));
        if (ref) res.attrs.insert_or_assign("ref", *ref);
        return res;
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

        // FIXME: shut up if file is already tracked.
        runFsl(
            { "add", *sourcePath + "/" + std::string(file) });

        if (commitMsg)
            runFsl(
                { "commit", *sourcePath + "/" + std::string(file), "-m", *commitMsg });
    }

    std::pair<bool, std::string> getActualUrl(const Input & input) const
    {
        auto url = parseURL(getStrAttr(input.attrs, "url"));
        bool isLocal = url.scheme == "file";
        return {isLocal, isLocal ? url.path : url.base};
    }

    void clone(const Input & input, const Path & destDir) override
    {
        auto [isLocal, actualUrl] = getActualUrl(input);

        runFsl({ "clone", actualUrl, destDir });
    }

    std::pair<StorePath, Input> fetch(ref<Store> store, const Input & _input) override
    {
        Input input(_input);

        auto name = input.getName();

        auto getLockedAttrs = [&]()
        {

            auto rev = input.getRev();

            auto revstr = rev->to_string(Base16, false);

            return Attrs({
                {"type", "fsl"},
                {"name", name},
                {"rev", revstr},
            });
        };

        auto makeResult = [&](const Attrs & infoAttrs, StorePath && storePath)
            -> std::pair<StorePath, Input>
        {
            assert(input.getRev());
            assert(!_input.getRev() || _input.getRev() == input.getRev());
            return {std::move(storePath), input};
        };

        if (input.getRev()) {
            if (auto res = getCache()->lookup(store, getLockedAttrs()))
                return makeResult(res->first, std::move(res->second));
        }

        auto [isLocal, actualUrl_] = getActualUrl(input);
        auto actualUrl = actualUrl_; // work around clang bug

        if (!input.getRef() && !input.getRev() && isLocal) {
            bool clean = false;

            auto changes = runFsl({ "--chdir", actualUrl, "changes", "--extra", "--merge", "--dotfiles" });

            std::cout << "CHanges: " << changes << std::endl;
            if (changes.length() == 0) clean = true;

            if (!clean) {
                if (!fetchSettings.allowDirty)
                    throw Error("Fossil tree '%s' is dirty", actualUrl);

                if (fetchSettings.warnDirty)
                    warn("Fossil tree '%s' is dirty", actualUrl);
            }


            auto files = tokenizeString<std::set<std::string>>(runFsl({ "--chdir", actualUrl, "ls" }), "\n"s);

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

            auto storePath = store->addToStore(input.getName(), actualUrl, FileIngestionMethod::Recursive, htSHA256, filter);

            return {std::move(storePath), input};

        }

        auto checkHashType = [&](const std::optional<Hash> & hash)
        {
            if (hash.has_value() && !(hash->type == htSHA1 || hash->type == htSHA256))
                throw Error("Hash '%s' is not supported by fossil. Supported types are sha1 and sha256.", hash->to_string(Base16, true));
        };

        checkHashType(input.getRev());

        if (!input.getRef()) input.attrs.insert_or_assign("ref", "trunk");

        auto revOrRef = input.getRev() ? input.getRev()->to_string(Base16, false) : *input.getRef();

        auto revHashType = input.getRev()->type;

        Attrs unlockedAttrs({
            {"type", "fsl"},
            {"name", name},
            {"url", actualUrl},
            {"ref", *input.getRef()},
        });

        if (auto res = getCache()->lookup(store, unlockedAttrs)) {
            auto rev2 = Hash::parseAny(getStrAttr(res->first, "rev"), revHashType);
            if (!input.getRev() || input.getRev() == rev2) {
                input.attrs.insert_or_assign("rev", rev2.to_string(Base16, false));
                return makeResult(res->first, std::move(res->second));
            }
        }

        Path repo = fmt("%s/nix/fsl/repos/%s", getCacheDir(), hashString(revHashType, actualUrl).to_string(Base32, false));
        Path ckout = fmt("%s/nix/fsl/ckouts/%s", getCacheDir(), hashString(revHashType, actualUrl).to_string(Base32, false));

        Activity act(*logger, lvlTalkative, actUnknown, fmt("fetching Fossil repository '%s'", actualUrl));

        if (!pathExists(ckout)) {
            createDirs(dirOf(repo));
            createDirs(dirOf(ckout));
            runFsl({ "clone", actualUrl, repo, "--workdir", ckout });
        }
        runFsl({ "--chdir", ckout, "up", revOrRef });

        auto json = nlohmann::json::parse(runFsl({ "--chdir", ckout, "json", "status"}));
        input.attrs.insert_or_assign("rev",
            Hash::parseAny(std::string { json["payload"]["checkout"]["uuid"] }, revHashType).to_string(Base16, false));

        auto json2 = nlohmann::json::parse(runFsl({ "--chdir", ckout, "json", "branch", "list"}));
        input.attrs.insert_or_assign("ref", std::string { json2["payload"]["current"] });

        auto cache = getCache();

        auto theattrs = getLockedAttrs();

        auto res = cache->lookup(store, theattrs);

        if (res) {
            return makeResult(res->first, std::move(res->second));
        }

        auto storePath = store->addToStore(name, ckout);

        Attrs infoAttrs({
            {"rev", input.getRev()->to_string(Base16, false)},
        });

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

static auto rFossilInputScheme = OnStartup([] { registerInputScheme(std::make_unique<FossilInputScheme>()); });

}
