#include "primops.hh"
#include "eval-inline.hh"
#include "download.hh"
#include "store-api.hh"
#include "pathlocks.hh"

#include <sys/time.h>

#include <regex>

#include <nlohmann/json.hpp>

using namespace std::string_literals;

namespace nix {

struct HgInfo
{
    Path storePath;
    std::string branch;
    std::string rev;
    uint64_t revCount = 0;
};

std::regex commitHashRegex("^[0-9a-fA-F]{40}$");

HgInfo exportMercurial(ref<Store> store, const std::string & uri,
    std::string rev, const std::string & name)
{
    if (evalSettings.pureEval && rev == "")
        throw Error("in pure evaluation mode, 'fetchMercurial' requires a Mercurial revision");

    if (rev == "" && hasPrefix(uri, "/") && pathExists(uri + "/.hg")) {

        bool clean = runProgram("hg", true, { "status", "-R", uri, "--modified", "--added", "--removed" }) == "";

        if (!clean) {

            /* This is an unclean working tree. So copy all tracked
               files. */

            printTalkative("copying unclean Mercurial working tree '%s'", uri);

            HgInfo hgInfo;
            hgInfo.rev = "0000000000000000000000000000000000000000";
            hgInfo.branch = chomp(runProgram("hg", true, { "branch", "-R", uri }));

            auto files = tokenizeString<std::set<std::string>>(
                runProgram("hg", true, { "status", "-R", uri, "--clean", "--modified", "--added", "--no-status", "--print0" }), "\0"s);

            PathFilter filter = [&](const Path & p) -> bool {
                assert(hasPrefix(p, uri));
                std::string file(p, uri.size() + 1);

                auto st = lstat(p);

                if (S_ISDIR(st.st_mode)) {
                    auto prefix = file + "/";
                    auto i = files.lower_bound(prefix);
                    return i != files.end() && hasPrefix(*i, prefix);
                }

                return files.count(file);
            };

            hgInfo.storePath = store->addToStore("source", uri, true, htSHA256, filter);

            return hgInfo;
        }
    }

    if (rev == "") rev = "default";

    Path cacheDir = fmt("%s/nix/hg/%s", getCacheDir(), hashString(htSHA256, uri).to_string(Base32, false));

    Path stampFile = fmt("%s/.hg/%s.stamp", cacheDir, hashString(htSHA512, rev).to_string(Base32, false));

    /* If we haven't pulled this repo less than ‘tarball-ttl’ seconds,
       do so now. */
    time_t now = time(0);
    struct stat st;
    if (stat(stampFile.c_str(), &st) != 0 ||
        (uint64_t) st.st_mtime + settings.tarballTtl <= (uint64_t) now)
    {
        /* Except that if this is a commit hash that we already have,
           we don't have to pull again. */
        if (!(std::regex_match(rev, commitHashRegex)
                && pathExists(cacheDir)
                && runProgram(
                    RunOptions("hg", { "log", "-R", cacheDir, "-r", rev, "--template", "1" })
                    .killStderr(true)).second == "1"))
        {
            Activity act(*logger, lvlTalkative, actUnknown, fmt("fetching Mercurial repository '%s'", uri));

            if (pathExists(cacheDir)) {
                try {
                    runProgram("hg", true, { "pull", "-R", cacheDir, "--", uri });
                }
                catch (ExecError & e) {
                    string transJournal = cacheDir + "/.hg/store/journal";
                    /* hg throws "abandoned transaction" error only if this file exists */
                    if (pathExists(transJournal)) {
                        runProgram("hg", true, { "recover", "-R", cacheDir });
                        runProgram("hg", true, { "pull", "-R", cacheDir, "--", uri });
                    } else {
                        throw ExecError(e.status, fmt("'hg pull' %s", statusToString(e.status)));
                    }
                }
            } else {
                createDirs(dirOf(cacheDir));
                runProgram("hg", true, { "clone", "--noupdate", "--", uri, cacheDir });
            }
        }

        writeFile(stampFile, "");
    }

    auto tokens = tokenizeString<std::vector<std::string>>(
        runProgram("hg", true, { "log", "-R", cacheDir, "-r", rev, "--template", "{node} {rev} {branch}" }));
    assert(tokens.size() == 3);

    HgInfo hgInfo;
    hgInfo.rev = tokens[0];
    hgInfo.revCount = std::stoull(tokens[1]);
    hgInfo.branch = tokens[2];

    std::string storeLinkName = hashString(htSHA512, name + std::string("\0"s) + hgInfo.rev).to_string(Base32, false);
    Path storeLink = fmt("%s/.hg/%s.link", cacheDir, storeLinkName);

    try {
        auto json = nlohmann::json::parse(readFile(storeLink));

        assert(json["name"] == name && json["rev"] == hgInfo.rev);

        hgInfo.storePath = json["storePath"];

        if (store->isValidPath(hgInfo.storePath)) {
            printTalkative("using cached Mercurial store path '%s'", hgInfo.storePath);
            return hgInfo;
        }

    } catch (SysError & e) {
        if (e.errNo != ENOENT) throw;
    }

    Path tmpDir = createTempDir();
    AutoDelete delTmpDir(tmpDir, true);

    runProgram("hg", true, { "archive", "-R", cacheDir, "-r", rev, tmpDir });

    deletePath(tmpDir + "/.hg_archival.txt");

    hgInfo.storePath = store->addToStore(name, tmpDir);

    nlohmann::json json;
    json["storePath"] = hgInfo.storePath;
    json["uri"] = uri;
    json["name"] = name;
    json["branch"] = hgInfo.branch;
    json["rev"] = hgInfo.rev;
    json["revCount"] = hgInfo.revCount;

    writeFile(storeLink, json.dump());

    return hgInfo;
}

static void prim_fetchMercurial(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    std::string url;
    std::string rev;
    std::string name = "source";
    PathSet context;

    state.forceValue(*args[0]);

    if (args[0]->type == tAttrs) {

        state.forceAttrs(*args[0], pos);

        for (auto & attr : *args[0]->attrs) {
            string n(attr.name);
            if (n == "url")
                url = state.coerceToString(*attr.pos, *attr.value, context, false, false);
            else if (n == "rev")
                rev = state.forceStringNoCtx(*attr.value, *attr.pos);
            else if (n == "name")
                name = state.forceStringNoCtx(*attr.value, *attr.pos);
            else
                throw EvalError("unsupported argument '%s' to 'fetchMercurial', at %s", attr.name, *attr.pos);
        }

        if (url.empty())
            throw EvalError(format("'url' argument required, at %1%") % pos);

    } else
        url = state.coerceToString(pos, *args[0], context, false, false);

    // FIXME: git externals probably can be used to bypass the URI
    // whitelist. Ah well.
    state.checkURI(url);

    auto hgInfo = exportMercurial(state.store, url, rev, name);

    state.mkAttrs(v, 8);
    mkString(*state.allocAttr(v, state.sOutPath), hgInfo.storePath, PathSet({hgInfo.storePath}));
    mkString(*state.allocAttr(v, state.symbols.create("branch")), hgInfo.branch);
    mkString(*state.allocAttr(v, state.symbols.create("rev")), hgInfo.rev);
    mkString(*state.allocAttr(v, state.symbols.create("shortRev")), std::string(hgInfo.rev, 0, 12));
    mkInt(*state.allocAttr(v, state.symbols.create("revCount")), hgInfo.revCount);
    v.attrs->sort();

    if (state.allowedPaths)
        state.allowedPaths->insert(state.store->toRealPath(hgInfo.storePath));
}

static RegisterPrimOp r("fetchMercurial", 1, prim_fetchMercurial);

}
