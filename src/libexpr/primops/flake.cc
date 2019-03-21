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

/* Read the registry or a lock file. (Currently they have an identical
   format. */
std::shared_ptr<FlakeRegistry> readRegistry(const Path & path)
{
    auto registry = std::make_shared<FlakeRegistry>();

    if (!pathExists(path))
        return std::make_shared<FlakeRegistry>();

    auto json = nlohmann::json::parse(readFile(path));

    auto version = json.value("version", 0);
    if (version != 1)
        throw Error("flake registry '%s' has unsupported version %d", path, version);

    auto flakes = json["flakes"];
    for (auto i = flakes.begin(); i != flakes.end(); ++i) {
        FlakeRegistry::Entry entry{FlakeRef(i->value("uri", ""))};
        registry->entries.emplace(i.key(), entry);
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

Path getUserRegistryPath()
{
    return getHome() + "/.config/nix/registry.json";
}
std::shared_ptr<FlakeRegistry> getGlobalRegistry()
{
    // FIXME: get from nixos.org.
    Path registryFile = settings.nixDataDir + "/nix/flake-registry.json";
    return readRegistry(registryFile);
}

std::shared_ptr<FlakeRegistry> getUserRegistry()
{
    return readRegistry(getUserRegistryPath());
}

std::shared_ptr<FlakeRegistry> getFlagRegistry()
{
    return std::make_shared<FlakeRegistry>();
    // TODO: Implement this once the right flags are implemented.
}

const std::vector<std::shared_ptr<FlakeRegistry>> EvalState::getFlakeRegistries()
{
    std::vector<std::shared_ptr<FlakeRegistry>> registries;
    if (evalSettings.pureEval) {
        registries.push_back(std::make_shared<FlakeRegistry>()); // global
        registries.push_back(std::make_shared<FlakeRegistry>()); // user
        registries.push_back(std::make_shared<FlakeRegistry>()); // local
    } else {
        registries.push_back(getGlobalRegistry());
        registries.push_back(getUserRegistry());
        registries.push_back(getLocalRegistry());
    }
    registries.push_back(getFlagRegistry());
    return registries;
}

// Creates a Nix attribute set value listing all dependencies, so they can be used in `provides`.
Value * makeFlakeRegistryValue(EvalState & state)
{
    auto v = state.allocValue();

    auto registries = state.getFlakeRegistries();

    int size = 0;
    for (auto registry : registries)
        size += registry->entries.size();
    state.mkAttrs(*v, size);

    for (auto & registry : registries) {
        for (auto & entry : registry->entries) {
            auto vEntry = state.allocAttr(*v, entry.first);
            state.mkAttrs(*vEntry, 2);
            mkString(*state.allocAttr(*vEntry, state.symbols.create("uri")), entry.second.ref.to_string());
            vEntry->attrs->sort();
        }
    }

    v->attrs->sort();

    return v;
}

static FlakeRef lookupFlake(EvalState & state, const FlakeRef & flakeRef,
    std::vector<std::shared_ptr<FlakeRegistry>> registries)
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
    std::optional<uint64_t> revCount;
};

static FlakeSourceInfo fetchFlake(EvalState & state, const FlakeRef & flakeRef)
{
    FlakeRef directFlakeRef = FlakeRef(flakeRef);
    if (!flakeRef.isDirect()) {
        directFlakeRef = lookupFlake(state, flakeRef, state.getFlakeRegistries());
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
        info.revCount = gitInfo.revCount;
        return info;
    }

    else if (auto refData = std::get_if<FlakeRef::IsPath>(&directFlakeRef.data)) {
        if (!pathExists(refData->path + "/.git"))
            throw Error("flake '%s' does not reference a Git repository", refData->path);
        auto gitInfo = exportGit(state.store, refData->path, {}, "", "source");
        FlakeSourceInfo info;
        info.storePath = gitInfo.storePath;
        info.rev = Hash(gitInfo.rev, htSHA1);
        info.revCount = gitInfo.revCount;
        return info;
    }

    else abort();
}

Flake getFlake(EvalState & state, const FlakeRef & flakeRef)
{
    FlakeSourceInfo sourceInfo = fetchFlake(state, flakeRef);
    debug("got flake source '%s' with revision %s",
        sourceInfo.storePath, sourceInfo.rev.value_or(Hash(htSHA1)).to_string(Base16, false));

    auto flakePath = sourceInfo.storePath;
    state.store->assertStorePath(flakePath);

    if (state.allowedPaths)
        state.allowedPaths->insert(flakePath);

    Flake flake(flakeRef);
    if (std::get_if<FlakeRef::IsGitHub>(&flakeRef.data)) {
        if (sourceInfo.rev)
            flake.ref = FlakeRef(flakeRef.baseRef().to_string()
                + "/" + sourceInfo.rev->to_string(Base16, false));
    }

    flake.path = flakePath;
    flake.revCount = sourceInfo.revCount;
    flake.path = flakePath;

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

    if (std::optional<Attr *> nonFlakeRequires = vInfo.attrs->get(state.symbols.create("nonFlakeRequires"))) {
        state.forceAttrs(*(**nonFlakeRequires).value, *(**nonFlakeRequires).pos);
        for (Attr attr : *(*(**nonFlakeRequires).value).attrs) {
            std::string myNonFlakeUri = state.forceStringNoCtx(*attr.value, *attr.pos);
            FlakeRef nonFlakeRef = FlakeRef(myNonFlakeUri);
            flake.nonFlakeRequires.insert_or_assign(attr.name, nonFlakeRef);
        }
    }

    if (auto provides = vInfo.attrs->get(state.symbols.create("provides"))) {
        state.forceFunction(*(**provides).value, *(**provides).pos);
        flake.vProvides = (**provides).value;
    } else
        throw Error("flake lacks attribute 'provides'");

    auto lockFile = flakePath + "/flake.lock"; // FIXME: symlink attack

    flake.lockFile = readRegistry(lockFile);
    for (auto & entry : flake.lockFile->entries)
        if (!entry.second.ref.isImmutable())
            throw Error("flake lock file '%s' contains mutable entry '%s'",
                lockFile, entry.second.ref.to_string());


    return flake;
}

// Get the `NonFlake` corresponding to a `FlakeRef`.
NonFlake getNonFlake(EvalState & state, const FlakeRef & flakeRef, FlakeId flakeId)
{
    FlakeSourceInfo sourceInfo = fetchFlake(state, flakeRef);
    debug("got non-flake source '%s' with revision %s",
        sourceInfo.storePath, sourceInfo.rev.value_or(Hash(htSHA1)).to_string(Base16, false));

    auto flakePath = sourceInfo.storePath;
    state.store->assertStorePath(flakePath);

    if (state.allowedPaths)
        state.allowedPaths->insert(flakePath);

    NonFlake nonFlake(flakeRef);
    if (std::get_if<FlakeRef::IsGitHub>(&flakeRef.data)) {
        if (sourceInfo.rev)
            nonFlake.ref = FlakeRef(flakeRef.baseRef().to_string()
                + "/" + sourceInfo.rev->to_string(Base16, false));
    }

    nonFlake.path = flakePath;

    nonFlake.id = flakeId;

    return nonFlake;
}

/* Given a flake reference, recursively fetch it and its
   dependencies.
   FIXME: this should return a graph of flakes.
*/
Dependencies resolveFlake(EvalState & state, const FlakeRef & topRef, bool impureTopRef)
{
    Dependencies deps;
    std::queue<FlakeRef> todo;
    bool isTopLevel = true;
    todo.push(topRef);

    auto registries = state.getFlakeRegistries();

    while (!todo.empty()) {
        auto flakeRef = todo.front();
        todo.pop();

        if (auto refData = std::get_if<FlakeRef::IsFlakeId>(&flakeRef.data)) {
            if (done.count(refData->id)) continue; // optimization
            flakeRef = lookupFlake(state, flakeRef, registries);
        if (evalSettings.pureEval && !flakeRef.isImmutable() && (!isTopLevel || !impureTopRef))
            throw Error("mutable flake '%s' is not allowed in pure mode; use --impure to disable", flakeRef.to_string());

        auto flake = getFlake(state, flakeRef);

        if (isTopLevel) {
            deps.topFlakeId = flake.id;
            isTopLevel = false;
        }

        for (auto & flakeRef : flake.requires)
            todo.push(flakeRef);

        for (auto & x : flake.nonFlakeRequires)
            deps.nonFlakes.push_back(getNonFlake(state, x.second, x.first));
            // TODO (Nick): If there are 2 non-flake dependencies with the same
            // FlakeId, this will lead to trouble! One of the dependencies won't
            // be used!

        deps.flakes.push_back(flake);
    }

    return deps;
}

FlakeRegistry updateLockFile(EvalState & evalState, FlakeRef & flakeRef)
{
    FlakeRegistry newLockFile;
    Dependencies deps = resolveFlake(evalState, flakeRef, false);
    // Nick assumed that "topRefPure" means that the Flake for flakeRef can be
    // fetched purely.
    for (auto const& require : deps.flakes) {
        FlakeRegistry::Entry entry = FlakeRegistry::Entry(require.ref);
        // The FlakeRefs are immutable because they come out of the Flake objects.
        if (require.id != deps.topFlakeId)
            newLockFile.entries.insert_or_assign(require.id, entry);
        // TODO (Nick): If there are 2 flake dependencies with the same FlakeId,
        // one of them gets ignored!
    }
    for (auto const& nonFlake : deps.nonFlakes) {
        FlakeRegistry::Entry entry = FlakeRegistry::Entry(nonFlake.ref);
        newLockFile.entries.insert_or_assign(nonFlake.id, entry);
        // We are assuming the sets of FlakeIds for flakes and non-flakes
        // are disjoint.
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

// Return the `provides` of the top flake, while assigning to `v` the provides
// of the dependencies as well.
Value * makeFlakeValue(EvalState & state, FlakeUri flakeUri, bool impureTopRef, Value & v)
{
    FlakeRef flakeRef = FlakeRef(flakeUri);

    Dependencies deps = resolveFlake(state, flakeRef, impure);

    // FIXME: we should call each flake with only its dependencies
    // (rather than the closure of the top-level flake).

    auto vResult = state.allocValue();
    // This will store the attribute set of the `nonFlakeRequires` and the `requires.provides`.

    state.mkAttrs(*vResult, dependencies.flakes.size());

    Value * vTop = 0;

    for (auto & flake : deps.flakes) {
        auto vFlake = state.allocAttr(*vResult, flake.id);
        if (topFlakeId == flake.second.id) vTop = vFlake;

        state.mkAttrs(*vFlake, 4);

        mkString(*state.allocAttr(*vFlake, state.sDescription), flake.second.description);

        state.store->assertStorePath(flake.second.path);
        mkString(*state.allocAttr(*vFlake, state.sOutPath), flake.second.path, {flake.second.path});

        if (flake.second.revCount)
            mkInt(*state.allocAttr(*vFlake, state.symbols.create("revCount")), *flake.second.revCount);

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
    makeFlakeValue(state, state.forceStringNoCtx(*args[0], pos), false, v);
}

static RegisterPrimOp r2("getFlake", 1, prim_getFlake);

}
