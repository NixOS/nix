#include "fetchers.hh"
#include "parse.hh"
#include "globals.hh"
#include "tarfile.hh"
#include "store-api.hh"
#include "regex.hh"

#include <sys/time.h>

#include <nlohmann/json.hpp>

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
        return (bool) rev;
    }

    std::optional<std::string> getRef() const override { return ref; }

    std::optional<Hash> getRev() const override { return rev; }

    std::string to_string() const override
    {
        ParsedURL url2(url);
        url2.scheme = "hg+" + url2.scheme;
        if (rev) url2.query.insert_or_assign("rev", rev->gitRev());
        if (ref) url2.query.insert_or_assign("ref", *ref);
        return url2.to_string();
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

    std::shared_ptr<const Input> applyOverrides(
        std::optional<std::string> ref,
        std::optional<Hash> rev) const override
    {
        if (!ref && !rev) return shared_from_this();

        auto res = std::make_shared<MercurialInput>(*this);

        if (ref) res->ref = ref;
        if (rev) res->rev = rev;

        return res;
    }

    std::optional<Path> getSourcePath() const
    {
        if (url.scheme == "file" && !ref && !rev)
            return url.path;
        return {};
    }

    void markChangedFile(std::string_view file, std::optional<std::string> commitMsg) const override
    {
        auto sourcePath = getSourcePath();
        assert(sourcePath);

        // FIXME: shut up if file is already tracked.
        runProgram("hg", true,
            { "add", *sourcePath + "/" + std::string(file) });

        if (commitMsg)
            runProgram("hg", true,
                { "commit", *sourcePath + "/" + std::string(file), "-m", *commitMsg });
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

                auto storePath = store->addToStore("source", actualUrl, true, htSHA256, filter);

                return {Tree {
                    .actualPath = store->printStorePath(storePath),
                    .storePath = std::move(storePath),
                }, input};
            }
        }

        if (!input->ref) input->ref = "default";

        Path cacheDir = fmt("%s/nix/hg/%s", getCacheDir(), hashString(htSHA256, actualUrl).to_string(Base32, false));

        assert(input->rev || input->ref);
        auto revOrRef = input->rev ? input->rev->gitRev() : *input->ref;

        Path stampFile = fmt("%s/.hg/%s.stamp", cacheDir, hashString(htSHA512, revOrRef).to_string(Base32, false));

        /* If we haven't pulled this repo less than ‘tarball-ttl’ seconds,
           do so now. */
        time_t now = time(0);
        struct stat st;
        if (stat(stampFile.c_str(), &st) != 0 ||
            (uint64_t) st.st_mtime + settings.tarballTtl <= (uint64_t) now)
        {
            /* Except that if this is a commit hash that we already have,
               we don't have to pull again. */
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

            writeFile(stampFile, "");
        }

        auto tokens = tokenizeString<std::vector<std::string>>(
            runProgram("hg", true, { "log", "-R", cacheDir, "-r", revOrRef, "--template", "{node} {rev} {branch}" }));
        assert(tokens.size() == 3);

        input->rev = Hash(tokens[0], htSHA1);
        auto revCount = std::stoull(tokens[1]);
        input->ref = tokens[2];

        std::string storeLinkName = hashString(htSHA512, name + std::string("\0"s) + input->rev->gitRev()).to_string(Base32, false);
        Path storeLink = fmt("%s/.hg/%s.link", cacheDir, storeLinkName);

        try {
            auto json = nlohmann::json::parse(readFile(storeLink));

            assert(json["name"] == name && json["rev"] == input->rev->gitRev());

            auto storePath = store->parseStorePath((std::string) json["storePath"]);

            if (store->isValidPath(storePath)) {
                printTalkative("using cached Mercurial store path '%s'", store->printStorePath(storePath));
                return {
                    Tree {
                        .actualPath = store->printStorePath(storePath),
                        .storePath = std::move(storePath),
                        .info = TreeInfo {
                            .revCount = revCount,
                        },
                    },
                    input
                };
            }

        } catch (SysError & e) {
            if (e.errNo != ENOENT) throw;
        }

        Path tmpDir = createTempDir();
        AutoDelete delTmpDir(tmpDir, true);

        runProgram("hg", true, { "archive", "-R", cacheDir, "-r", input->rev->gitRev(), tmpDir });

        deletePath(tmpDir + "/.hg_archival.txt");

        auto storePath = store->addToStore(name, tmpDir);

        nlohmann::json json;
        json["storePath"] = store->printStorePath(storePath);
        json["uri"] = actualUrl;
        json["name"] = name;
        json["branch"] = *input->ref;
        json["rev"] = input->rev->gitRev();
        json["revCount"] = revCount;

        writeFile(storeLink, json.dump());

        return {
            Tree {
                .actualPath = store->printStorePath(storePath),
                .storePath = std::move(storePath),
                .info = TreeInfo {
                    .revCount = revCount
                }
            },
            input
        };
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

        Input::Attrs attrs;
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

    std::unique_ptr<Input> inputFromAttrs(const Input::Attrs & attrs) override
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
