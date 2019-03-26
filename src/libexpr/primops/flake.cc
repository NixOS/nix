#include "flake.hh"
#include "primops.hh"
#include "eval-inline.hh"
#include "fetchGit.hh"
#include "download.hh"
#include "args.hh"

#include <iostream>
#include <queue>
#include <regex>
#include <nlohmann/json.hpp>

namespace nix {

Path getUserRegistryPath()
{
    return getHome() + "/.config/nix/registry.json";
}

/* Read the registry or a lock file. (Currently they have an identical
   format. */
std::unique_ptr<FlakeRegistry> readRegistry(const Path & path)
{
    auto registry = std::make_unique<FlakeRegistry>();

    try {
        auto json = nlohmann::json::parse(readFile(path));

        auto version = json.value("version", 0);
        if (version != 1)
            throw Error("flake registry '%s' has unsupported version %d", path, version);

        auto flakes = json["flakes"];
        for (auto i = flakes.begin(); i != flakes.end(); ++i) {
            FlakeRegistry::Entry entry{FlakeRef(i->value("uri", ""))};
            registry->entries.emplace(i.key(), entry);
        }
    } catch (SysError & e) {
        if (e.errNo != ENOENT) throw;
    }

    return registry;
}

/* Write the registry or lock file to a file. */
void writeRegistry(FlakeRegistry registry, Path path)
{
    nlohmann::json json = {};
    json["version"] = 1;
    json["flakes"] = {};
    for (auto elem : registry.entries) {
        json["flakes"][elem.first] = { {"uri", elem.second.ref.to_string()} };
    }
    createDirs(dirOf(path));
    writeFile(path, json.dump(4)); // The '4' is the number of spaces used in the indentation in the json file.
}

const FlakeRegistry & EvalState::getFlakeRegistry()
{
    std::call_once(_flakeRegistryInit, [&]()
    {
#if 0
        auto registryUri = "file:///home/eelco/Dev/gists/nix-flakes/registry.json";

        auto registryFile = getDownloader()->download(DownloadRequest(registryUri));
#endif

        auto registryFile = settings.nixDataDir + "/nix/flake-registry.json";

        _flakeRegistry = readRegistry(registryFile);
    });

    return *_flakeRegistry;
}

Value * makeFlakeRegistryValue(EvalState & state)
{
    auto v = state.allocValue();

    auto registry = state.getFlakeRegistry();

    state.mkAttrs(*v, registry.entries.size());

    for (auto & entry : registry.entries) {
        auto vEntry = state.allocAttr(*v, entry.first);
        state.mkAttrs(*vEntry, 2);
        mkString(*state.allocAttr(*vEntry, state.symbols.create("uri")), entry.second.ref.to_string());
        vEntry->attrs->sort();
    }

    v->attrs->sort();

    return v;
}

static FlakeRef lookupFlake(EvalState & state, const FlakeRef & flakeRef,
    std::vector<const FlakeRegistry *> registries)
{
    if (auto refData = std::get_if<FlakeRef::IsFlakeId>(&flakeRef.data)) {
        for (auto registry : registries) {
            auto i = registry->entries.find(refData->id);
            if (i != registry->entries.end()) {
                auto newRef = FlakeRef(i->second.ref);
                if (!newRef.isDirect())
                    throw Error("found indirect flake URI '%s' in the flake registry", i->second.ref.to_string());
                return newRef;
            }
        }
        throw Error("cannot find flake '%s' in the flake registry or in the flake lock file", refData->id);
    } else
        return flakeRef;
}

struct FlakeSourceInfo
{
    Path storePath;
    std::optional<Hash> rev;
};

static FlakeSourceInfo fetchFlake(EvalState & state, const FlakeRef & flakeRef)
{
    FlakeRef directFlakeRef = FlakeRef(flakeRef);
    if (!flakeRef.isDirect())
    {
        std::vector<const FlakeRegistry *> registries;
        // 'pureEval' is a setting which cannot be changed in `nix flake`,
        // but without flagging it off, we can't use any FlakeIds.
        // if (!evalSettings.pureEval) {
            registries.push_back(&state.getFlakeRegistry());
        // }
        directFlakeRef = lookupFlake(state, flakeRef, registries);
    }
    assert(directFlakeRef.isDirect());
    // NOTE FROM NICK: I don't see why one wouldn't fetch FlakeId flakes..

    if (auto refData = std::get_if<FlakeRef::IsGitHub>(&directFlakeRef.data)) {
        // FIXME: require hash in pure mode.

        // FIXME: use regular /archive URLs instead? api.github.com
        // might have stricter rate limits.

        // FIXME: support passing auth tokens for private repos.

        auto url = fmt("https://api.github.com/repos/%s/%s/tarball/%s",
            refData->owner, refData->repo,
            refData->rev
                ? refData->rev->to_string(Base16, false)
                : refData->ref
                  ? *refData->ref
                  : "master");

        auto result = getDownloader()->downloadCached(state.store, url, true, "source",
            Hash(), nullptr, refData->rev ? 1000000000 : settings.tarballTtl);

        if (!result.etag)
            throw Error("did not receive an ETag header from '%s'", url);

        if (result.etag->size() != 42 || (*result.etag)[0] != '"' || (*result.etag)[41] != '"')
            throw Error("ETag header '%s' from '%s' is not a Git revision", *result.etag, url);

        FlakeSourceInfo info;
        info.storePath = result.path;
        info.rev = Hash(std::string(*result.etag, 1, result.etag->size() - 2), htSHA1);

        return info;
    }

    else if (auto refData = std::get_if<FlakeRef::IsGit>(&directFlakeRef.data)) {
        auto gitInfo = exportGit(state.store, refData->uri, refData->ref,
            refData->rev ? refData->rev->to_string(Base16, false) : "", "source");
        FlakeSourceInfo info;
        info.storePath = gitInfo.storePath;
        info.rev = Hash(gitInfo.rev, htSHA1);
        return info;
    }

    else abort();
}

Flake getFlake(EvalState & state, const FlakeRef & flakeRef)
{
    auto sourceInfo = fetchFlake(state, flakeRef);
    debug("got flake source '%s' with revision %s",
        sourceInfo.storePath, sourceInfo.rev.value_or(Hash(htSHA1)).to_string(Base16, false));

    auto flakePath = sourceInfo.storePath;
    state.store->assertStorePath(flakePath);

    if (state.allowedPaths)
        state.allowedPaths->insert(flakePath);

    FlakeRef newFlakeRef(flakeRef);
    if (std::get_if<FlakeRef::IsGitHub>(&newFlakeRef.data)) {
        FlakeSourceInfo srcInfo = fetchFlake(state, newFlakeRef);
        if (srcInfo.rev) {
            std::string uri = flakeRef.baseRef().to_string();
            newFlakeRef = FlakeRef(uri + "/" + srcInfo.rev->to_string(Base16, false));
        }
    }

    Flake flake(newFlakeRef);

    Value vInfo;
    state.evalFile(flakePath + "/flake.nix", vInfo); // FIXME: symlink attack

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
            flake.requires.push_back(FlakeRef(state.forceStringNoCtx(
                *(**requires).value->listElems()[n], *(**requires).pos)));
    }

    if (auto provides = vInfo.attrs->get(state.symbols.create("provides"))) {
        state.forceFunction(*(**provides).value, *(**provides).pos);
        flake.vProvides = (**provides).value;
    } else
        throw Error("flake lacks attribute 'provides'");

    auto lockFile = flakePath + "/flake.lock"; // FIXME: symlink attack

    if (pathExists(lockFile)) {
        flake.lockFile = readRegistry(lockFile);
        for (auto & entry : flake.lockFile->entries)
            if (!entry.second.ref.isImmutable())
                throw Error("flake lock file '%s' contains mutable entry '%s'",
                    lockFile, entry.second.ref.to_string());
    }

    return flake;
}

/* Given a flake reference, recursively fetch it and its
   dependencies.
   FIXME: this should return a graph of flakes.
*/
static std::tuple<FlakeId, std::map<FlakeId, Flake>> resolveFlake(EvalState & state,
    const FlakeRef & topRef, bool impureTopRef)
{
    std::map<FlakeId, Flake> done;
    std::queue<std::tuple<FlakeRef, bool>> todo;
    std::optional<FlakeId> topFlakeId; /// FIXME: ambiguous
    todo.push({topRef, true});

    std::vector<const FlakeRegistry *> registries;
    FlakeRegistry localRegistry;
    registries.push_back(&localRegistry);
    if (!evalSettings.pureEval)
        registries.push_back(&state.getFlakeRegistry());

    while (!todo.empty()) {
        auto [flakeRef, toplevel] = todo.front();
        todo.pop();

        if (auto refData = std::get_if<FlakeRef::IsFlakeId>(&flakeRef.data)) {
            if (done.count(refData->id)) continue; // optimization
            flakeRef = lookupFlake(state, flakeRef, registries);
        }

        if (evalSettings.pureEval && !flakeRef.isImmutable() && (!toplevel || !impureTopRef))
            throw Error("mutable flake '%s' is not allowed in pure mode; use --no-pure-eval to disable", flakeRef.to_string());

        auto flake = getFlake(state, flakeRef);

        if (done.count(flake.id)) continue;

        if (toplevel) topFlakeId = flake.id;

        for (auto & require : flake.requires)
            todo.push({require, false});

        if (flake.lockFile)
            for (auto & entry : flake.lockFile->entries) {
                if (localRegistry.entries.count(entry.first)) continue;
                localRegistry.entries.emplace(entry.first, entry.second);
            }

        done.emplace(flake.id, std::move(flake));
    }

    assert(topFlakeId);
    return {*topFlakeId, std::move(done)};
}

FlakeRegistry updateLockFile(EvalState & evalState, FlakeRef & flakeRef)
{
    FlakeRegistry newLockFile;
    std::map<FlakeId, Flake> myDependencyMap = get<1>(resolveFlake(evalState, flakeRef, false));
    // Nick assumed that "topRefPure" means that the Flake for flakeRef can be
    // fetched purely.
    for (auto const& require : myDependencyMap) {
        FlakeRegistry::Entry entry = FlakeRegistry::Entry(require.second.ref);
        // The FlakeRefs are immutable because they come out of the Flake objects,
        // not from the requires.
        newLockFile.entries.insert(std::pair<FlakeId, FlakeRegistry::Entry>(require.first, entry));
    }
    return newLockFile;
}

void updateLockFile(EvalState & state, std::string path)
{
    // 'path' is the path to the local flake repo.
    FlakeRef flakeRef = FlakeRef("file://" + path);
    if (std::get_if<FlakeRef::IsGit>(&flakeRef.data)) {
        FlakeRegistry newLockFile = updateLockFile(state, flakeRef);
        writeRegistry(newLockFile, path + "/flake.lock");
    } else if (std::get_if<FlakeRef::IsGitHub>(&flakeRef.data)) {
        throw UsageError("you can only update local flakes, not flakes on GitHub");
    } else {
        throw UsageError("you can only update local flakes, not flakes through their FlakeId");
    }
}

Value * makeFlakeValue(EvalState & state, std::string flakeUri, Value & v)
{
    // FIXME: temporary hack to make the default installation source
    // work.
    bool impure = false;
    if (hasPrefix(flakeUri, "impure:")) {
        flakeUri = std::string(flakeUri, 7);
        impure = true;
    }

    auto flakeRef = FlakeRef(flakeUri);

    auto [topFlakeId, flakes] = resolveFlake(state, flakeUri, impure);

    // FIXME: we should call each flake with only its dependencies
    // (rather than the closure of the top-level flake).

    auto vResult = state.allocValue();

    state.mkAttrs(*vResult, flakes.size());

    Value * vTop = 0;

    for (auto & flake : flakes) {
        auto vFlake = state.allocAttr(*vResult, flake.second.id);
        if (topFlakeId == flake.second.id) vTop = vFlake;
        state.mkAttrs(*vFlake, 2);
        mkString(*state.allocAttr(*vFlake, state.sDescription), flake.second.description);
        auto vProvides = state.allocAttr(*vFlake, state.symbols.create("provides"));
        mkApp(*vProvides, *flake.second.vProvides, *vResult);
        vFlake->attrs->sort();
    }

    vResult->attrs->sort();

    v = *vResult;

    assert(vTop);
    return vTop;
}

static void prim_getFlake(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    makeFlakeValue(state, state.forceStringNoCtx(*args[0], pos), v);
}

static RegisterPrimOp r2("getFlake", 1, prim_getFlake);

}
