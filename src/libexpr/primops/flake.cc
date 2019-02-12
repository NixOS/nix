#include "flake.hh"
#include "primops.hh"
#include "eval-inline.hh"
#include "fetchGit.hh"
#include "download.hh"

#include <queue>
#include <regex>
#include <nlohmann/json.hpp>

namespace nix {

const FlakeRegistry & EvalState::getFlakeRegistry()
{
    std::call_once(_flakeRegistryInit, [&]()
    {
        _flakeRegistry = std::make_unique<FlakeRegistry>();

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
            FlakeRegistry::Entry entry{FlakeRef(i->value("uri", ""))};
            _flakeRegistry->entries.emplace(i.key(), entry);
        }
    });

    return *_flakeRegistry;
}

Value * EvalState::makeFlakeRegistryValue()
{
    auto v = allocValue();

    auto registry = getFlakeRegistry();

    mkAttrs(*v, registry.entries.size());

    for (auto & entry : registry.entries) {
        auto vEntry = allocAttr(*v, entry.first);
        mkAttrs(*vEntry, 2);
        mkString(*allocAttr(*vEntry, symbols.create("uri")), entry.second.ref.to_string());
        vEntry->attrs->sort();
    }

    v->attrs->sort();

    return v;
}

static FlakeRef lookupFlake(EvalState & state, const FlakeRef & flakeRef)
{
    if (auto refData = std::get_if<FlakeRef::IsFlakeId>(&flakeRef.data)) {
        auto registry = state.getFlakeRegistry();
        auto i = registry.entries.find(refData->id);
        if (i == registry.entries.end())
            throw Error("cannot find flake '%s' in the flake registry", refData->id);
        auto newRef = FlakeRef(i->second.ref);
        if (!newRef.isDirect())
            throw Error("found indirect flake URI '%s' in the flake registry", i->second.ref.to_string());
        return newRef;
    } else
        return flakeRef;
}

struct Flake
{
    FlakeId id;
    std::string description;
    Path path;
    std::set<std::string> requires;
    Value * vProvides; // FIXME: gc
    // commit hash
    // date
    // content hash
};

static Path fetchFlake(EvalState & state, const FlakeRef & flakeRef)
{
    assert(flakeRef.isDirect());

    if (auto refData = std::get_if<FlakeRef::IsGitHub>(&flakeRef.data)) {
        // FIXME: require hash in pure mode.

        // FIXME: use regular /archive URLs instead? api.github.com
        // might have stricter rate limits.

        // FIXME: support passing auth tokens for private repos.

        auto storePath = getDownloader()->downloadCached(state.store,
            fmt("https://api.github.com/repos/%s/%s/tarball/%s",
                refData->owner, refData->repo,
                refData->rev
                  ? refData->rev->to_string(Base16, false)
                  : refData->ref
                    ? *refData->ref
                    : "master"),
            true, "source");

        // FIXME: extract revision hash from ETag.

        return storePath;
    }

    else if (auto refData = std::get_if<FlakeRef::IsGit>(&flakeRef.data)) {
        auto gitInfo = exportGit(state.store, refData->uri, refData->ref,
            refData->rev ? refData->rev->to_string(Base16, false) : "", "source");
        return gitInfo.storePath;
    }

    else abort();
}

static Flake getFlake(EvalState & state, const FlakeRef & flakeRef)
{
    auto flakePath = fetchFlake(state, flakeRef);
    state.store->assertStorePath(flakePath);

    if (state.allowedPaths)
        state.allowedPaths->insert(flakePath);

    Flake flake;

    Value vInfo;
    state.evalFile(flakePath + "/flake.nix", vInfo);

    state.forceAttrs(vInfo);

    if (auto name = vInfo.attrs->get(state.sName))
        flake.id = state.forceStringNoCtx(*(**name).value, *(**name).pos);
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

/* Given a set of flake references, recursively fetch them and their
   dependencies. */
static std::map<FlakeId, Flake> resolveFlakes(EvalState & state, const std::vector<FlakeRef> & flakeRefs)
{
    std::map<FlakeId, Flake> done;
    std::queue<FlakeRef> todo;
    for (auto & i : flakeRefs) todo.push(i);

    while (!todo.empty()) {
        auto flakeRef = todo.front();
        todo.pop();

        if (auto refData = std::get_if<FlakeRef::IsFlakeId>(&flakeRef.data)) {
            if (done.count(refData->id)) continue; // optimization
            flakeRef = lookupFlake(state, flakeRef);
        }

        auto flake = getFlake(state, flakeRef);

        if (done.count(flake.id)) continue;

        for (auto & require : flake.requires)
            todo.push(require);

        done.emplace(flake.id, flake);
    }

    return done;
}

static void prim_getFlake(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    auto flakeUri = FlakeRef(state.forceStringNoCtx(*args[0], pos));

    auto flakes = resolveFlakes(state, {flakeUri});

    auto vResult = state.allocValue();

    state.mkAttrs(*vResult, flakes.size());

    for (auto & flake : flakes) {
        auto vFlake = state.allocAttr(*vResult, flake.second.id);
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
