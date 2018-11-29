#include "primops.hh"
#include "eval-inline.hh"
#include "fetchGit.hh"
#include "download.hh"

#include <queue>
#include <nlohmann/json.hpp>

namespace nix {

const EvalState::FlakeRegistry & EvalState::getFlakeRegistry()
{
    std::call_once(_flakeRegistryInit, [&]()
    {
        _flakeRegistry = std::make_unique<FlakeRegistry>();

        if (!evalSettings.pureEval) {

            auto registryUri = "file:///home/eelco/Dev/gists/nix-flakes/registry.json";

            auto registryFile = getDownloader()->download(DownloadRequest(registryUri));

            auto json = nlohmann::json::parse(*registryFile.data);

            auto version = json.value("version", 0);
            if (version != 1)
                throw Error("flake registry '%s' has unsupported version %d", registryUri, version);

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

static Flake fetchFlake(EvalState & state, const std::string & flakeUri)
{
    Flake flake;

    auto gitInfo = exportGit(state.store, flakeUri, {}, "", "source");

    state.store->assertStorePath(gitInfo.storePath);

    Value vInfo;
    state.evalFile(gitInfo.storePath + "/flake.nix", vInfo);

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
    auto registry = state.getFlakeRegistry();

    std::map<std::string, Flake> done;
    std::queue<std::string> todo;
    for (auto & i : flakeUris) todo.push(i);

    while (!todo.empty()) {
        auto flakeUri = todo.front();
        todo.pop();
        if (done.count(flakeUri)) continue;

        auto flake = fetchFlake(state, flakeUri);

        for (auto & require : flake.requires) {
            auto i = registry.entries.find(require);
            if (i == registry.entries.end())
                throw Error("unknown flake '%s'", require);
            todo.push(i->second.uri);
        }

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
