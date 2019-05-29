#include "flake.hh"
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
    json["version"] = 1;
    for (auto elem : registry.entries)
        json["flakes"][elem.first.to_string()] = { {"uri", elem.second.to_string()} };
    createDirs(dirOf(path));
    writeFile(path, json.dump(4)); // The '4' is the number of spaces used in the indentation in the json file.
}

LockFile::FlakeEntry readFlakeEntry(nlohmann::json json)
{
    FlakeRef flakeRef(json["uri"]);
    if (!flakeRef.isImmutable())
        throw Error("cannot use mutable flake '%s' in pure mode", flakeRef);

    LockFile::FlakeEntry entry(flakeRef, Hash((std::string) json["contentHash"]));

    auto nonFlakeRequires = json["nonFlakeRequires"];

    for (auto i = nonFlakeRequires.begin(); i != nonFlakeRequires.end(); ++i) {
        FlakeRef flakeRef(i->value("uri", ""));
        if (!flakeRef.isImmutable())
            throw Error("requested to fetch FlakeRef '%s' purely, which is mutable", flakeRef);
        LockFile::NonFlakeEntry nonEntry(flakeRef, Hash(i->value("contentHash", "")));
        entry.nonFlakeEntries.insert_or_assign(i.key(), nonEntry);
    }

    auto requires = json["requires"];

    for (auto i = requires.begin(); i != requires.end(); ++i)
        entry.flakeEntries.insert_or_assign(i.key(), readFlakeEntry(*i));

    return entry;
}

LockFile readLockFile(const Path & path)
{
    LockFile lockFile;

    if (!pathExists(path))
        return lockFile;

    auto json = nlohmann::json::parse(readFile(path));

    auto version = json.value("version", 0);
    if (version != 1)
        throw Error("lock file '%s' has unsupported version %d", path, version);

    auto nonFlakeRequires = json["nonFlakeRequires"];

    for (auto i = nonFlakeRequires.begin(); i != nonFlakeRequires.end(); ++i) {
        FlakeRef flakeRef(i->value("uri", ""));
        LockFile::NonFlakeEntry nonEntry(flakeRef, Hash(i->value("contentHash", "")));
        if (!flakeRef.isImmutable())
            throw Error("found mutable FlakeRef '%s' in lockfile at path %s", flakeRef, path);
        lockFile.nonFlakeEntries.insert_or_assign(i.key(), nonEntry);
    }

    auto requires = json["requires"];

    for (auto i = requires.begin(); i != requires.end(); ++i)
        lockFile.flakeEntries.insert_or_assign(i.key(), readFlakeEntry(*i));

    return lockFile;
}

nlohmann::json flakeEntryToJson(const LockFile::FlakeEntry & entry)
{
    nlohmann::json json;
    json["uri"] = entry.ref.to_string();
    json["contentHash"] = entry.narHash.to_string(SRI);
    for (auto & x : entry.nonFlakeEntries) {
        json["nonFlakeRequires"][x.first]["uri"] = x.second.ref.to_string();
        json["nonFlakeRequires"][x.first]["contentHash"] = x.second.narHash.to_string(SRI);
    }
    for (auto & x : entry.flakeEntries)
        json["requires"][x.first.to_string()] = flakeEntryToJson(x.second);
    return json;
}

void writeLockFile(const LockFile & lockFile, const Path & path)
{
    nlohmann::json json;
    json["version"] = 1;
    json["nonFlakeRequires"] = nlohmann::json::object();
    for (auto & x : lockFile.nonFlakeEntries) {
        json["nonFlakeRequires"][x.first]["uri"] = x.second.ref.to_string();
        json["nonFlakeRequires"][x.first]["contentHash"] = x.second.narHash.to_string(SRI);
    }
    json["requires"] = nlohmann::json::object();
    for (auto & x : lockFile.flakeEntries)
        json["requires"][x.first.to_string()] = flakeEntryToJson(x.second);
    createDirs(dirOf(path));
    writeFile(path, json.dump(4) + "\n"); // '4' = indentation in json file
}

std::shared_ptr<FlakeRegistry> EvalState::getGlobalFlakeRegistry()
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
        if (flake.epoch > 2019)
            throw Error("flake '%s' requires unsupported epoch %d; please upgrade Nix", flakeRef, flake.epoch);
    } else
        throw Error("flake '%s' lacks attribute 'epoch'", flakeRef);

    if (auto name = vInfo.attrs->get(state.sName))
        flake.id = state.forceStringNoCtx(*(**name).value, *(**name).pos);
    else
        throw Error("flake '%s' lacks attribute 'name'", flakeRef);

    if (auto description = vInfo.attrs->get(state.sDescription))
        flake.description = state.forceStringNoCtx(*(**description).value, *(**description).pos);

    auto sRequires = state.symbols.create("requires");

    if (auto requires = vInfo.attrs->get(sRequires)) {
        state.forceList(*(**requires).value, *(**requires).pos);
        for (unsigned int n = 0; n < (**requires).value->listSize(); ++n)
            flake.requires.push_back(FlakeRef(state.forceStringNoCtx(
                *(**requires).value->listElems()[n], *(**requires).pos)));
    }

    auto sNonFlakeRequires = state.symbols.create("nonFlakeRequires");

    if (std::optional<Attr *> nonFlakeRequires = vInfo.attrs->get(sNonFlakeRequires)) {
        state.forceAttrs(*(**nonFlakeRequires).value, *(**nonFlakeRequires).pos);
        for (Attr attr : *(*(**nonFlakeRequires).value).attrs) {
            std::string myNonFlakeUri = state.forceStringNoCtx(*attr.value, *attr.pos);
            FlakeRef nonFlakeRef = FlakeRef(myNonFlakeUri);
            flake.nonFlakeRequires.insert_or_assign(attr.name, nonFlakeRef);
        }
    }

    auto sProvides = state.symbols.create("provides");

    if (auto provides = vInfo.attrs->get(sProvides)) {
        state.forceFunction(*(**provides).value, *(**provides).pos);
        flake.vProvides = (**provides).value;
    } else
        throw Error("flake '%s' lacks attribute 'provides'", flakeRef);

    for (auto & attr : *vInfo.attrs) {
        if (attr.name != sEpoch &&
            attr.name != state.sName &&
            attr.name != state.sDescription &&
            attr.name != sRequires &&
            attr.name != sNonFlakeRequires &&
            attr.name != sProvides)
            throw Error("flake '%s' has an unsupported attribute '%s', at %s",
                flakeRef, attr.name, *attr.pos);
    }

    return flake;
}

// Get the `NonFlake` corresponding to a `FlakeRef`.
NonFlake getNonFlake(EvalState & state, const FlakeRef & flakeRef, FlakeAlias alias, bool impureIsAllowed = false)
{
    auto sourceInfo = fetchFlake(state, flakeRef, impureIsAllowed);
    debug("got non-flake source '%s' with flakeref %s", sourceInfo.storePath, sourceInfo.resolvedRef.to_string());

    FlakeRef resolvedRef = sourceInfo.resolvedRef;

    NonFlake nonFlake(flakeRef, sourceInfo);

    state.store->assertStorePath(nonFlake.sourceInfo.storePath);

    if (state.allowedPaths)
        state.allowedPaths->insert(nonFlake.sourceInfo.storePath);

    nonFlake.alias = alias;

    return nonFlake;
}

LockFile entryToLockFile(const LockFile::FlakeEntry & entry)
{
    LockFile lockFile;
    lockFile.flakeEntries = entry.flakeEntries;
    lockFile.nonFlakeEntries = entry.nonFlakeEntries;
    return lockFile;
}

LockFile::FlakeEntry dependenciesToFlakeEntry(const ResolvedFlake & resolvedFlake)
{
    LockFile::FlakeEntry entry(
        resolvedFlake.flake.sourceInfo.resolvedRef,
        resolvedFlake.flake.sourceInfo.narHash);

    for (auto & info : resolvedFlake.flakeDeps)
        entry.flakeEntries.insert_or_assign(info.first.to_string(), dependenciesToFlakeEntry(info.second));

    for (auto & nonFlake : resolvedFlake.nonFlakeDeps) {
        LockFile::NonFlakeEntry nonEntry(
            nonFlake.sourceInfo.resolvedRef,
            nonFlake.sourceInfo.narHash);
        entry.nonFlakeEntries.insert_or_assign(nonFlake.alias, nonEntry);
    }

    return entry;
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

ResolvedFlake resolveFlakeFromLockFile(EvalState & state, const FlakeRef & flakeRef,
    HandleLockFile handleLockFile, LockFile lockFile = {}, bool topRef = false)
{
    Flake flake = getFlake(state, flakeRef, allowedToUseRegistries(handleLockFile, topRef));

    ResolvedFlake deps(flake);

    for (auto & nonFlakeInfo : flake.nonFlakeRequires) {
        FlakeRef ref = nonFlakeInfo.second;
        auto i = lockFile.nonFlakeEntries.find(nonFlakeInfo.first);
        if (i != lockFile.nonFlakeEntries.end()) {
            NonFlake nonFlake = getNonFlake(state, i->second.ref, nonFlakeInfo.first);
            if (nonFlake.sourceInfo.narHash != i->second.narHash)
                throw Error("the content hash of flakeref '%s' doesn't match", i->second.ref.to_string());
            deps.nonFlakeDeps.push_back(nonFlake);
        } else {
            if (handleLockFile == AllPure || handleLockFile == TopRefUsesRegistries)
                throw Error("cannot update non-flake dependency '%s' in pure mode", nonFlakeInfo.first);
            deps.nonFlakeDeps.push_back(getNonFlake(state, nonFlakeInfo.second, nonFlakeInfo.first, allowedToUseRegistries(handleLockFile, false)));
        }
    }

    for (auto newFlakeRef : flake.requires) {
        auto i = lockFile.flakeEntries.find(newFlakeRef);
        if (i != lockFile.flakeEntries.end()) { // Propagate lockFile downwards if possible
            ResolvedFlake newResFlake = resolveFlakeFromLockFile(state, i->second.ref, handleLockFile, entryToLockFile(i->second));
            if (newResFlake.flake.sourceInfo.narHash != i->second.narHash)
                throw Error("the content hash of flakeref '%s' doesn't match", i->second.ref.to_string());
            deps.flakeDeps.insert_or_assign(newFlakeRef, newResFlake);
        } else {
            if (handleLockFile == AllPure || handleLockFile == TopRefUsesRegistries)
                throw Error("cannot update flake dependency '%s' in pure mode", newFlakeRef.to_string());
            deps.flakeDeps.insert_or_assign(newFlakeRef, resolveFlakeFromLockFile(state, newFlakeRef, handleLockFile));
        }
    }

    return deps;
}

/* Given a flake reference, recursively fetch it and its dependencies.
   FIXME: this should return a graph of flakes.
*/
ResolvedFlake resolveFlake(EvalState & state, const FlakeRef & topRef, HandleLockFile handleLockFile)
{
    Flake flake = getFlake(state, topRef, allowedToUseRegistries(handleLockFile, true));
    LockFile oldLockFile;

    if (!recreateLockFile (handleLockFile)) {
        // If recreateLockFile, start with an empty lockfile
        oldLockFile = readLockFile(flake.sourceInfo.storePath + "/flake.lock"); // FIXME: symlink attack
    }

    LockFile lockFile(oldLockFile);

    ResolvedFlake resFlake = resolveFlakeFromLockFile(state, topRef, handleLockFile, lockFile, true);
    lockFile = entryToLockFile(dependenciesToFlakeEntry(resFlake));

    if (!(lockFile == oldLockFile)) {
        if (allowedToWrite(handleLockFile)) {
            if (auto refData = std::get_if<FlakeRef::IsPath>(&topRef.data)) {
                writeLockFile(lockFile, refData->path + (topRef.subdir == "" ? "" : "/" + topRef.subdir) + "/flake.lock");

                // Hack: Make sure that flake.lock is visible to Git, so it ends up in the Nix store.
                runProgram("git", true, { "-C", refData->path, "add",
                                          (topRef.subdir == "" ? "" : topRef.subdir + "/") + "flake.lock" });
            } else
                warn("cannot write lockfile of remote flake '%s'", topRef);
        } else if (handleLockFile != AllPure && handleLockFile != TopRefUsesRegistries)
            warn("using updated lockfile without writing it to file");
    }

    return resFlake;
}

void updateLockFile(EvalState & state, const FlakeRef & flakeRef, bool recreateLockFile)
{
    resolveFlake(state, flakeRef, recreateLockFile ? RecreateLockFile : UpdateLockFile);
}

static void emitSourceInfoAttrs(EvalState & state, const SourceInfo & sourceInfo, Value & vAttrs)
{
    auto & path = sourceInfo.storePath;
    state.store->isValidPath(path);
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

void callFlake(EvalState & state, const ResolvedFlake & resFlake, Value & v)
{
    // Construct the resulting attrset '{description, provides,
    // ...}'. This attrset is passed lazily as an argument to 'provides'.

    state.mkAttrs(v, resFlake.flakeDeps.size() + resFlake.nonFlakeDeps.size() + 8);

    for (auto info : resFlake.flakeDeps) {
        const ResolvedFlake newResFlake = info.second;
        auto vFlake = state.allocAttr(v, newResFlake.flake.id);
        callFlake(state, newResFlake, *vFlake);
    }

    for (const NonFlake nonFlake : resFlake.nonFlakeDeps) {
        auto vNonFlake = state.allocAttr(v, nonFlake.alias);
        state.mkAttrs(*vNonFlake, 8);

        state.store->isValidPath(nonFlake.sourceInfo.storePath);
        mkString(*state.allocAttr(*vNonFlake, state.sOutPath),
            nonFlake.sourceInfo.storePath, {nonFlake.sourceInfo.storePath});

        emitSourceInfoAttrs(state, nonFlake.sourceInfo, *vNonFlake);
    }

    mkString(*state.allocAttr(v, state.sDescription), resFlake.flake.description);

    emitSourceInfoAttrs(state, resFlake.flake.sourceInfo, v);

    auto vProvides = state.allocAttr(v, state.symbols.create("provides"));
    mkApp(*vProvides, *resFlake.flake.vProvides, v);

    v.attrs->push_back(Attr(state.symbols.create("self"), &v));

    v.attrs->sort();
}

// Return the `provides` of the top flake, while assigning to `v` the provides
// of the dependencies as well.
void makeFlakeValue(EvalState & state, const FlakeRef & flakeRef, HandleLockFile handle, Value & v)
{
    callFlake(state, resolveFlake(state, flakeRef, handle), v);
}

// This function is exposed to be used in nix files.
static void prim_getFlake(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    makeFlakeValue(state, state.forceStringNoCtx(*args[0], pos),
        evalSettings.pureEval ? AllPure : UseUpdatedLockFile, v);
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
