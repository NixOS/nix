#include "flake.hh"
#include "lockfile.hh"
#include "primops.hh"
#include "eval-inline.hh"
#include "fetchGit.hh"
#include "download.hh"
#include "args.hh"

#include <iostream>
#include <queue>
#include <regex>
#include <ctime>
#include <iomanip>
#include <nlohmann/json.hpp>

namespace nix {

using namespace flake;

namespace flake {

/* Read a registry. */
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
    for (auto i = flakes.begin(); i != flakes.end(); ++i)
        registry->entries.emplace(i.key(), FlakeRef(i->value("uri", "")));

    return registry;
}

/* Write a registry to a file. */
void writeRegistry(const FlakeRegistry & registry, const Path & path)
{
    nlohmann::json json;
    json["version"] = 2;
    for (auto elem : registry.entries)
        json["flakes"][elem.first.to_string()] = { {"uri", elem.second.to_string()} };
    createDirs(dirOf(path));
    writeFile(path, json.dump(4)); // The '4' is the number of spaces used in the indentation in the json file.
}

Path getUserRegistryPath()
{
    return getHome() + "/.config/nix/registry.json";
}

std::shared_ptr<FlakeRegistry> getUserRegistry()
{
    return readRegistry(getUserRegistryPath());
}

std::shared_ptr<FlakeRegistry> getFlagRegistry(RegistryOverrides registryOverrides)
{
    auto flagRegistry = std::make_shared<FlakeRegistry>();
    for (auto const & x : registryOverrides) {
        flagRegistry->entries.insert_or_assign(FlakeRef(x.first), FlakeRef(x.second));
    }
    return flagRegistry;
}

static FlakeRef lookupFlake(EvalState & state, const FlakeRef & flakeRef, const Registries & registries,
    std::vector<FlakeRef> pastSearches = {});

FlakeRef updateFlakeRef(EvalState & state, const FlakeRef & newRef, const Registries & registries, std::vector<FlakeRef> pastSearches)
{
    std::string errorMsg = "found cycle in flake registries: ";
    for (FlakeRef oldRef : pastSearches) {
        errorMsg += oldRef.to_string();
        if (oldRef == newRef)
            throw Error(errorMsg);
        errorMsg += " - ";
    }
    pastSearches.push_back(newRef);
    return lookupFlake(state, newRef, registries, pastSearches);
}

static FlakeRef lookupFlake(EvalState & state, const FlakeRef & flakeRef, const Registries & registries,
    std::vector<FlakeRef> pastSearches)
{
    if (registries.empty() && !flakeRef.isDirect())
        throw Error("indirect flake reference '%s' is not allowed", flakeRef);

    for (std::shared_ptr<FlakeRegistry> registry : registries) {
        auto i = registry->entries.find(flakeRef);
        if (i != registry->entries.end()) {
            auto newRef = i->second;
            return updateFlakeRef(state, newRef, registries, pastSearches);
        }

        auto j = registry->entries.find(flakeRef.baseRef());
        if (j != registry->entries.end()) {
            auto newRef = j->second;
            newRef.ref = flakeRef.ref;
            newRef.rev = flakeRef.rev;
            return updateFlakeRef(state, newRef, registries, pastSearches);
        }
    }

    if (!flakeRef.isDirect())
        throw Error("could not resolve flake reference '%s'", flakeRef);

    return flakeRef;
}

// Lookups happen here too
static SourceInfo fetchFlake(EvalState & state, const FlakeRef & flakeRef, bool impureIsAllowed = false)
{
    FlakeRef resolvedRef = lookupFlake(state, flakeRef,
        impureIsAllowed ? state.getFlakeRegistries() : std::vector<std::shared_ptr<FlakeRegistry>>());

    if (evalSettings.pureEval && !impureIsAllowed && !resolvedRef.isImmutable())
        throw Error("requested to fetch mutable flake '%s' in pure mode", resolvedRef);

    auto doGit = [&](const GitInfo & gitInfo) {
        FlakeRef ref(resolvedRef.baseRef());
        ref.ref = gitInfo.ref;
        ref.rev = gitInfo.rev;
        SourceInfo info(ref);
        info.storePath = gitInfo.storePath;
        info.revCount = gitInfo.revCount;
        info.narHash = state.store->queryPathInfo(info.storePath)->narHash;
        info.lastModified = gitInfo.lastModified;
        return info;
    };

    // This only downloads only one revision of the repo, not the entire history.
    if (auto refData = std::get_if<FlakeRef::IsGitHub>(&resolvedRef.data)) {

        // FIXME: use regular /archive URLs instead? api.github.com
        // might have stricter rate limits.

        auto url = fmt("https://api.github.com/repos/%s/%s/tarball/%s",
            refData->owner, refData->repo,
            resolvedRef.rev ? resolvedRef.rev->to_string(Base16, false)
                : resolvedRef.ref ? *resolvedRef.ref : "master");

        std::string accessToken = settings.githubAccessToken.get();
        if (accessToken != "")
            url += "?access_token=" + accessToken;

        CachedDownloadRequest request(url);
        request.unpack = true;
        request.name = "source";
        request.ttl = resolvedRef.rev ? 1000000000 : settings.tarballTtl;
        request.getLastModified = true;
        auto result = getDownloader()->downloadCached(state.store, request);

        if (!result.etag)
            throw Error("did not receive an ETag header from '%s'", url);

        if (result.etag->size() != 42 || (*result.etag)[0] != '"' || (*result.etag)[41] != '"')
            throw Error("ETag header '%s' from '%s' is not a Git revision", *result.etag, url);

        FlakeRef ref(resolvedRef.baseRef());
        ref.rev = Hash(std::string(*result.etag, 1, result.etag->size() - 2), htSHA1);
        SourceInfo info(ref);
        info.storePath = result.storePath;
        info.narHash = state.store->queryPathInfo(info.storePath)->narHash;
        info.lastModified = result.lastModified;

        return info;
    }

    // This downloads the entire git history
    else if (auto refData = std::get_if<FlakeRef::IsGit>(&resolvedRef.data)) {
        return doGit(exportGit(state.store, refData->uri, resolvedRef.ref, resolvedRef.rev, "source"));
    }

    else if (auto refData = std::get_if<FlakeRef::IsPath>(&resolvedRef.data)) {
        if (!pathExists(refData->path + "/.git"))
            throw Error("flake '%s' does not reference a Git repository", refData->path);
        return doGit(exportGit(state.store, refData->path, {}, {}, "source"));
    }

    else abort();
}

// This will return the flake which corresponds to a given FlakeRef. The lookupFlake is done within `fetchFlake`, which is used here.
Flake getFlake(EvalState & state, const FlakeRef & flakeRef, bool impureIsAllowed = false)
{
    SourceInfo sourceInfo = fetchFlake(state, flakeRef, impureIsAllowed);
    debug("got flake source '%s' with flakeref %s", sourceInfo.storePath, sourceInfo.resolvedRef.to_string());

    FlakeRef resolvedRef = sourceInfo.resolvedRef;

    state.store->assertStorePath(sourceInfo.storePath);

    if (state.allowedPaths)
        state.allowedPaths->insert(state.store->toRealPath(sourceInfo.storePath));

    // Guard against symlink attacks.
    Path flakeFile = canonPath(sourceInfo.storePath + "/" + resolvedRef.subdir + "/flake.nix");
    Path realFlakeFile = state.store->toRealPath(flakeFile);
    if (!isInDir(realFlakeFile, state.store->toRealPath(sourceInfo.storePath)))
        throw Error("'flake.nix' file of flake '%s' escapes from '%s'", resolvedRef, sourceInfo.storePath);

    Flake flake(flakeRef, sourceInfo);

    if (!pathExists(realFlakeFile))
        throw Error("source tree referenced by '%s' does not contain a '%s/flake.nix' file", resolvedRef, resolvedRef.subdir);

    Value vInfo;
    state.evalFile(realFlakeFile, vInfo); // FIXME: symlink attack

    state.forceAttrs(vInfo);

    auto sEpoch = state.symbols.create("epoch");

    if (auto epoch = vInfo.attrs->get(sEpoch)) {
        flake.epoch = state.forceInt(*(**epoch).value, *(**epoch).pos);
        if (flake.epoch > 201906)
            throw Error("flake '%s' requires unsupported epoch %d; please upgrade Nix", flakeRef, flake.epoch);
    } else
        throw Error("flake '%s' lacks attribute 'epoch'", flakeRef);

    if (auto name = vInfo.attrs->get(state.sName))
        flake.id = state.forceStringNoCtx(*(**name).value, *(**name).pos);
    else
        throw Error("flake '%s' lacks attribute 'name'", flakeRef);

    if (auto description = vInfo.attrs->get(state.sDescription))
        flake.description = state.forceStringNoCtx(*(**description).value, *(**description).pos);

    auto sInputs = state.symbols.create("inputs");

    if (auto inputs = vInfo.attrs->get(sInputs)) {
        state.forceList(*(**inputs).value, *(**inputs).pos);
        for (unsigned int n = 0; n < (**inputs).value->listSize(); ++n)
            flake.inputs.push_back(FlakeRef(state.forceStringNoCtx(
                *(**inputs).value->listElems()[n], *(**inputs).pos)));
    }

    auto sNonFlakeInputs = state.symbols.create("nonFlakeInputs");

    if (std::optional<Attr *> nonFlakeInputs = vInfo.attrs->get(sNonFlakeInputs)) {
        state.forceAttrs(*(**nonFlakeInputs).value, *(**nonFlakeInputs).pos);
        for (Attr attr : *(*(**nonFlakeInputs).value).attrs) {
            std::string myNonFlakeUri = state.forceStringNoCtx(*attr.value, *attr.pos);
            FlakeRef nonFlakeRef = FlakeRef(myNonFlakeUri);
            flake.nonFlakeInputs.insert_or_assign(attr.name, nonFlakeRef);
        }
    }

    auto sOutputs = state.symbols.create("outputs");

    if (auto outputs = vInfo.attrs->get(sOutputs)) {
        state.forceFunction(*(**outputs).value, *(**outputs).pos);
        flake.vOutputs = (**outputs).value;
    } else
        throw Error("flake '%s' lacks attribute 'outputs'", flakeRef);

    for (auto & attr : *vInfo.attrs) {
        if (attr.name != sEpoch &&
            attr.name != state.sName &&
            attr.name != state.sDescription &&
            attr.name != sInputs &&
            attr.name != sNonFlakeInputs &&
            attr.name != sOutputs)
            throw Error("flake '%s' has an unsupported attribute '%s', at %s",
                flakeRef, attr.name, *attr.pos);
    }

    return flake;
}

// Get the `NonFlake` corresponding to a `FlakeRef`.
NonFlake getNonFlake(EvalState & state, const FlakeRef & flakeRef, bool impureIsAllowed = false)
{
    auto sourceInfo = fetchFlake(state, flakeRef, impureIsAllowed);
    debug("got non-flake source '%s' with flakeref %s", sourceInfo.storePath, sourceInfo.resolvedRef.to_string());

    FlakeRef resolvedRef = sourceInfo.resolvedRef;

    NonFlake nonFlake(flakeRef, sourceInfo);

    state.store->assertStorePath(nonFlake.sourceInfo.storePath);

    if (state.allowedPaths)
        state.allowedPaths->insert(nonFlake.sourceInfo.storePath);

    return nonFlake;
}

bool allowedToWrite(HandleLockFile handle)
{
    return handle == UpdateLockFile || handle == RecreateLockFile;
}

bool recreateLockFile(HandleLockFile handle)
{
    return handle == RecreateLockFile || handle == UseNewLockFile;
}

bool allowedToUseRegistries(HandleLockFile handle, bool isTopRef)
{
    if (handle == AllPure) return false;
    else if (handle == TopRefUsesRegistries) return isTopRef;
    else if (handle == UpdateLockFile) return true;
    else if (handle == UseUpdatedLockFile) return true;
    else if (handle == RecreateLockFile) return true;
    else if (handle == UseNewLockFile) return true;
    else assert(false);
}

static std::pair<Flake, FlakeInput> updateLocks(
    EvalState & state,
    const FlakeRef & flakeRef,
    HandleLockFile handleLockFile,
    const FlakeInputs & oldEntry,
    bool topRef)
{
    auto flake = getFlake(state, flakeRef, allowedToUseRegistries(handleLockFile, topRef));

    FlakeInput newEntry(
        flake.id,
        flake.sourceInfo.resolvedRef,
        flake.sourceInfo.narHash);

    for (auto & input : flake.nonFlakeInputs) {
        auto & id = input.first;
        auto & ref = input.second;
        auto i = oldEntry.nonFlakeInputs.find(id);
        if (i != oldEntry.nonFlakeInputs.end()) {
            newEntry.nonFlakeInputs.insert_or_assign(i->first, i->second);
        } else {
            if (handleLockFile == AllPure || handleLockFile == TopRefUsesRegistries)
                throw Error("cannot update non-flake dependency '%s' in pure mode", id);
            auto nonFlake = getNonFlake(state, ref, allowedToUseRegistries(handleLockFile, false));
            newEntry.nonFlakeInputs.insert_or_assign(id,
                NonFlakeInput(
                    nonFlake.sourceInfo.resolvedRef,
                    nonFlake.sourceInfo.narHash));
        }
    }

    for (auto & inputRef : flake.inputs) {
        auto i = oldEntry.flakeInputs.find(inputRef);
        if (i != oldEntry.flakeInputs.end()) {
            newEntry.flakeInputs.insert_or_assign(inputRef, i->second);
        } else {
            if (handleLockFile == AllPure || handleLockFile == TopRefUsesRegistries)
                throw Error("cannot update flake dependency '%s' in pure mode", inputRef);
            newEntry.flakeInputs.insert_or_assign(inputRef,
                updateLocks(state, inputRef, handleLockFile, {}, false).second);
        }
    }

    return {flake, newEntry};
}

/* Given a flake reference, recursively fetch it and its dependencies.
   FIXME: this should return a graph of flakes.
*/
ResolvedFlake resolveFlake(EvalState & state, const FlakeRef & topRef, HandleLockFile handleLockFile)
{
    auto flake = getFlake(state, topRef, allowedToUseRegistries(handleLockFile, true));

    LockFile oldLockFile;

    if (!recreateLockFile(handleLockFile)) {
        // If recreateLockFile, start with an empty lockfile
        // FIXME: symlink attack
        oldLockFile = LockFile::read(
            state.store->toRealPath(flake.sourceInfo.storePath)
            + "/" + flake.sourceInfo.resolvedRef.subdir + "/flake.lock");
    }

    // FIXME: get rid of duplicate getFlake call
    LockFile lockFile(updateLocks(
            state, topRef, handleLockFile, oldLockFile, true).second);

    if (!(lockFile == oldLockFile)) {
        if (allowedToWrite(handleLockFile)) {
            if (auto refData = std::get_if<FlakeRef::IsPath>(&topRef.data)) {
                lockFile.write(refData->path + (topRef.subdir == "" ? "" : "/" + topRef.subdir) + "/flake.lock");

                // Hack: Make sure that flake.lock is visible to Git, so it ends up in the Nix store.
                runProgram("git", true, { "-C", refData->path, "add",
                                          (topRef.subdir == "" ? "" : topRef.subdir + "/") + "flake.lock" });
            } else
                warn("cannot write lockfile of remote flake '%s'", topRef);
        } else if (handleLockFile != AllPure && handleLockFile != TopRefUsesRegistries)
            warn("using updated lockfile without writing it to file");
    }

    return ResolvedFlake(std::move(flake), std::move(lockFile));
}

void updateLockFile(EvalState & state, const FlakeRef & flakeRef, bool recreateLockFile)
{
    resolveFlake(state, flakeRef, recreateLockFile ? RecreateLockFile : UpdateLockFile);
}

static void emitSourceInfoAttrs(EvalState & state, const SourceInfo & sourceInfo, Value & vAttrs)
{
    auto & path = sourceInfo.storePath;
    assert(state.store->isValidPath(path));
    mkString(*state.allocAttr(vAttrs, state.sOutPath), path, {path});

    if (sourceInfo.resolvedRef.rev) {
        mkString(*state.allocAttr(vAttrs, state.symbols.create("rev")),
            sourceInfo.resolvedRef.rev->gitRev());
        mkString(*state.allocAttr(vAttrs, state.symbols.create("shortRev")),
            sourceInfo.resolvedRef.rev->gitShortRev());
    }

    if (sourceInfo.revCount)
        mkInt(*state.allocAttr(vAttrs, state.symbols.create("revCount")), *sourceInfo.revCount);

    if (sourceInfo.lastModified)
        mkString(*state.allocAttr(vAttrs, state.symbols.create("lastModified")),
            fmt("%s",
                std::put_time(std::gmtime(&*sourceInfo.lastModified), "%Y%m%d%H%M%S")));
}

/* Helper primop to make callFlake (below) fetch/call its inputs
   lazily. Note that this primop cannot be called by user code since
   it doesn't appear in 'builtins'. */
static void prim_callFlake(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    auto lazyFlake = (FlakeInput *) args[0]->attrs;
    auto flake = getFlake(state, lazyFlake->ref, false);

    if (flake.sourceInfo.narHash != lazyFlake->narHash)
        throw Error("the content hash of flake '%s' doesn't match the hash recorded in the referring lockfile", flake.sourceInfo.resolvedRef);

    callFlake(state, flake, *lazyFlake, v);
}

static void prim_callNonFlake(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    auto lazyNonFlake = (NonFlakeInput *) args[0]->attrs;

    auto nonFlake = getNonFlake(state, lazyNonFlake->ref);

    if (nonFlake.sourceInfo.narHash != lazyNonFlake->narHash)
        throw Error("the content hash of repository '%s' doesn't match the hash recorded in the referring lockfile", nonFlake.sourceInfo.resolvedRef);

    state.mkAttrs(v, 8);

    assert(state.store->isValidPath(nonFlake.sourceInfo.storePath));

    mkString(*state.allocAttr(v, state.sOutPath),
        nonFlake.sourceInfo.storePath, {nonFlake.sourceInfo.storePath});

    emitSourceInfoAttrs(state, nonFlake.sourceInfo, v);
}

void callFlake(EvalState & state,
    const Flake & flake,
    const FlakeInputs & inputs,
    Value & v)
{
    // Construct the resulting attrset '{description, outputs,
    // ...}'. This attrset is passed lazily as an argument to 'outputs'.

    state.mkAttrs(v,
        inputs.flakeInputs.size() +
        inputs.nonFlakeInputs.size() + 8);

    for (auto & dep : inputs.flakeInputs) {
        auto vFlake = state.allocAttr(v, dep.second.id);
        auto vPrimOp = state.allocValue();
        static auto primOp = new PrimOp(prim_callFlake, 1, state.symbols.create("callFlake"));
        vPrimOp->type = tPrimOp;
        vPrimOp->primOp = primOp;
        auto vArg = state.allocValue();
        vArg->type = tNull;
        // FIXME: leak
        vArg->attrs = (Bindings *) new FlakeInput(dep.second); // evil! also inefficient
        mkApp(*vFlake, *vPrimOp, *vArg);
    }

    for (auto & dep : inputs.nonFlakeInputs) {
        auto vNonFlake = state.allocAttr(v, dep.first);
        auto vPrimOp = state.allocValue();
        static auto primOp = new PrimOp(prim_callNonFlake, 1, state.symbols.create("callNonFlake"));
        vPrimOp->type = tPrimOp;
        vPrimOp->primOp = primOp;
        auto vArg = state.allocValue();
        vArg->type = tNull;
        // FIXME: leak
        vArg->attrs = (Bindings *) new NonFlakeInput(dep.second); // evil! also inefficient
        mkApp(*vNonFlake, *vPrimOp, *vArg);
    }

    mkString(*state.allocAttr(v, state.sDescription), flake.description);

    emitSourceInfoAttrs(state, flake.sourceInfo, v);

    auto vOutputs = state.allocAttr(v, state.symbols.create("outputs"));
    mkApp(*vOutputs, *flake.vOutputs, v);

    v.attrs->push_back(Attr(state.symbols.create("self"), &v));

    v.attrs->sort();
}

void callFlake(EvalState & state,
    const ResolvedFlake & resFlake,
    Value & v)
{
    callFlake(state, resFlake.flake, resFlake.lockFile, v);
}

// This function is exposed to be used in nix files.
static void prim_getFlake(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    callFlake(state, resolveFlake(state, state.forceStringNoCtx(*args[0], pos),
            evalSettings.pureEval ? AllPure : UseUpdatedLockFile), v);
}

static RegisterPrimOp r2("getFlake", 1, prim_getFlake);

void gitCloneFlake(FlakeRef flakeRef, EvalState & state, Registries registries, const Path & destDir)
{
    flakeRef = lookupFlake(state, flakeRef, registries);

    std::string uri;

    Strings args = {"clone"};

    if (auto refData = std::get_if<FlakeRef::IsGitHub>(&flakeRef.data)) {
        uri = "git@github.com:" + refData->owner + "/" + refData->repo + ".git";
        args.push_back(uri);
        if (flakeRef.ref) {
            args.push_back("--branch");
            args.push_back(*flakeRef.ref);
        }
    } else if (auto refData = std::get_if<FlakeRef::IsGit>(&flakeRef.data)) {
        args.push_back(refData->uri);
        if (flakeRef.ref) {
            args.push_back("--branch");
            args.push_back(*flakeRef.ref);
        }
    }

    if (destDir != "")
        args.push_back(destDir);

    runProgram("git", true, args);
}

}

std::shared_ptr<flake::FlakeRegistry> EvalState::getGlobalFlakeRegistry()
{
    std::call_once(_globalFlakeRegistryInit, [&]() {
        auto path = evalSettings.flakeRegistry;

        if (!hasPrefix(path, "/")) {
            CachedDownloadRequest request(evalSettings.flakeRegistry);
            request.name = "flake-registry.json";
            request.gcRoot = true;
            path = getDownloader()->downloadCached(store, request).path;
        }

        _globalFlakeRegistry = readRegistry(path);
    });

    return _globalFlakeRegistry;
}

// This always returns a vector with flakeReg, userReg, globalReg.
// If one of them doesn't exist, the registry is left empty but does exist.
const Registries EvalState::getFlakeRegistries()
{
    Registries registries;
    registries.push_back(getFlagRegistry(registryOverrides));
    registries.push_back(getUserRegistry());
    registries.push_back(getGlobalFlakeRegistry());
    return registries;
}

}
