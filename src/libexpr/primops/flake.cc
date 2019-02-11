#include "primops.hh"
#include "eval-inline.hh"
#include "fetchGit.hh"
#include "download.hh"

#include <queue>
#include <regex>
#include <nlohmann/json.hpp>

namespace nix {

const EvalState::FlakeRegistry & EvalState::getFlakeRegistry()
{
    std::call_once(_flakeRegistryInit, [&]()
    {
        _flakeRegistry = std::make_unique<FlakeRegistry>();

        if (!evalSettings.pureEval) {

#if 0
            auto registryUri = "file:///home/eelco/Dev/gists/nix-flakes/registry.json";

            auto registryFile = getDownloader()->download(DownloadRequest(registryUri));
#endif

            auto registryFile = readFile(settings.nixDataDir + "/nix/flake-registry.json");

            auto json = nlohmann::json::parse(registryFile);

            auto version = json.value("version", 0);
            if (version != 1)
                throw Error("flake registry '%s' has unsupported version %d", registryFile, version);

            auto flakes = json["flakes"];
            for (auto i = flakes.begin(); i != flakes.end(); ++i) {
                FlakeRegistry::Entry entry;
                entry.uri = i->value("uri", "");
                if (entry.uri.empty())
                    throw Error("invalid flake registry entry");
                _flakeRegistry->entries.emplace(i.key(), entry);
            }
        }
    });

    return *_flakeRegistry;
}

static void prim_flakeRegistry(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    auto registry = state.getFlakeRegistry();

    state.mkAttrs(v, registry.entries.size());

    for (auto & entry : registry.entries) {
        auto vEntry = state.allocAttr(v, entry.first);
        state.mkAttrs(*vEntry, 2);
        mkString(*state.allocAttr(*vEntry, state.symbols.create("uri")), entry.second.uri);
        vEntry->attrs->sort();
    }

    v.attrs->sort();
}

static RegisterPrimOp r1("__flakeRegistry", 0, prim_flakeRegistry);

struct Flake
{
    std::string name;
    std::string description;
    Path path;
    std::set<std::string> requires;
    Value * vProvides; // FIXME: gc
};

std::regex flakeRegex("^flake:([a-zA-Z][a-zA-Z0-9_-]*)(/[a-zA-Z][a-zA-Z0-9_.-]*)?$");
std::regex githubRegex("^github:([a-zA-Z][a-zA-Z0-9_-]*)/([a-zA-Z][a-zA-Z0-9_-]*)(/([a-zA-Z][a-zA-Z0-9_-]*))?$");

static Path fetchFlake(EvalState & state, const std::string & flakeUri)
{
    std::smatch match;

    if (std::regex_match(flakeUri, match, flakeRegex)) {
        auto flakeName = match[1];
        auto revOrRef = match[2];
        auto registry = state.getFlakeRegistry();
        auto i = registry.entries.find(flakeName);
        if (i == registry.entries.end())
            throw Error("unknown flake '%s'", flakeName);
        return fetchFlake(state, i->second.uri);
    }

    else if (std::regex_match(flakeUri, match, githubRegex)) {
        auto owner = match[1];
        auto repo = match[2];
        auto revOrRef = match[4].str();
        if (revOrRef.empty()) revOrRef = "master";

        // FIXME: require hash in pure mode.

        // FIXME: use regular /archive URLs instead? api.github.com
        // might have stricter rate limits.
        auto storePath = getDownloader()->downloadCached(state.store,
            fmt("https://api.github.com/repos/%s/%s/tarball/%s", owner, repo, revOrRef),
            true, "source");

        // FIXME: extract revision hash from ETag.

        return storePath;
    }

    else if (hasPrefix(flakeUri, "/") || hasPrefix(flakeUri, "git://")) {
        auto gitInfo = exportGit(state.store, flakeUri, {}, "", "source");
        return gitInfo.storePath;
    }

    else
        throw Error("unsupported flake URI '%s'", flakeUri);
}

static Flake getFlake(EvalState & state, const std::string & flakeUri)
{
    auto flakePath = fetchFlake(state, flakeUri);
    state.store->assertStorePath(flakePath);

    Flake flake;

    Value vInfo;
    state.evalFile(flakePath + "/flake.nix", vInfo);

    state.forceAttrs(vInfo);

    if (auto name = vInfo.attrs->get(state.sName))
        flake.name = state.forceStringNoCtx(*(**name).value, *(**name).pos);
    else
        throw Error("flake lacks attribute 'name'");

    if (auto description = vInfo.attrs->get(state.sDescription))
        flake.description = state.forceStringNoCtx(*(**description).value, *(**description).pos);

    if (auto requires = vInfo.attrs->get(state.symbols.create("requires"))) {
        state.forceList(*(**requires).value, *(**requires).pos);
        for (unsigned int n = 0; n < (**requires).value->listSize(); ++n)
            flake.requires.insert(state.forceStringNoCtx(
                    *(**requires).value->listElems()[n], *(**requires).pos));
    }

    if (auto provides = vInfo.attrs->get(state.symbols.create("provides"))) {
        state.forceFunction(*(**provides).value, *(**provides).pos);
        flake.vProvides = (**provides).value;
    } else
        throw Error("flake lacks attribute 'provides'");

    return flake;
}

static std::map<std::string, Flake> resolveFlakes(EvalState & state, const StringSet & flakeUris)
{
    std::map<std::string, Flake> done;
    std::queue<std::string> todo;
    for (auto & i : flakeUris) todo.push(i);

    while (!todo.empty()) {
        auto flakeUri = todo.front();
        todo.pop();
        if (done.count(flakeUri)) continue;

        auto flake = getFlake(state, flakeUri);

        for (auto & require : flake.requires)
            todo.push(require);

        done.emplace(flake.name, flake);
    }

    return done;
}

static void prim_getFlake(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    std::string flakeUri = state.forceStringNoCtx(*args[0], pos);

    auto flakes = resolveFlakes(state, {flakeUri});

    auto vResult = state.allocValue();

    state.mkAttrs(*vResult, flakes.size());

    for (auto & flake : flakes) {
        auto vFlake = state.allocAttr(*vResult, flake.second.name);
        state.mkAttrs(*vFlake, 2);
        mkString(*state.allocAttr(*vFlake, state.sDescription), flake.second.description);
        auto vProvides = state.allocAttr(*vFlake, state.symbols.create("provides"));
        mkApp(*vProvides, *flake.second.vProvides, *vResult);
        vFlake->attrs->sort();
    }

    vResult->attrs->sort();

    v = *vResult;
}

static RegisterPrimOp r2("getFlake", 1, prim_getFlake);

}
