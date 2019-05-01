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

    Hash hash = Hash((std::string) json["contentHash"]);
    LockFile::FlakeEntry entry(flakeRef, hash);

    auto nonFlakeRequires = json["nonFlakeRequires"];

    for (auto i = nonFlakeRequires.begin(); i != nonFlakeRequires.end(); ++i) {
        FlakeRef flakeRef(i->value("uri", ""));
        if (!flakeRef.isImmutable())
            throw Error("requested to fetch FlakeRef '%s' purely, which is mutable", flakeRef);
        Hash hash = Hash((std::string) i->value("contentHash", ""));
        LockFile::NonFlakeEntry newEntry(flakeRef, hash);
        entry.nonFlakeEntries.insert_or_assign(i.key(), newEntry);
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
        LockFile::NonFlakeEntry entry(flakeRef, Hash((std::string) json["contentHash"]));
        if (!flakeRef.isImmutable())
            throw Error("requested to fetch FlakeRef '%s' purely, which is mutable", flakeRef);
        lockFile.nonFlakeEntries.insert_or_assign(i.key(), entry);
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
    json["contentHash"] = entry.contentHash.to_string(SRI);
    for (auto & x : entry.nonFlakeEntries) {
        json["nonFlakeRequires"][x.first]["uri"] = x.second.ref.to_string();
        json["nonFlakeRequires"][x.first]["contentHash"] = x.second.contentHash.to_string(SRI);
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
        json["nonFlakeRequires"][x.first]["contentHash"] = x.second.contentHash.to_string(SRI);
    }
    json["requires"] = nlohmann::json::object();
    for (auto & x : lockFile.flakeEntries)
        json["requires"][x.first.to_string()] = flakeEntryToJson(x.second);
    createDirs(dirOf(path));
    writeFile(path, json.dump(4) + "\n"); // '4' = indentation in json file
}

std::shared_ptr<FlakeRegistry> getGlobalRegistry()
{
    return readRegistry(evalSettings.flakeRegistry);
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
    registries.push_back(getGlobalRegistry());
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

        auto result = getDownloader()->downloadCached(state.store, url, true, "source",
            Hash(), nullptr, resolvedRef.rev ? 1000000000 : settings.tarballTtl);

        if (!result.etag)
            throw Error("did not receive an ETag header from '%s'", url);

        if (result.etag->size() != 42 || (*result.etag)[0] != '"' || (*result.etag)[41] != '"')
            throw Error("ETag header '%s' from '%s' is not a Git revision", *result.etag, url);

        FlakeRef ref(resolvedRef.baseRef());
        ref.rev = Hash(std::string(*result.etag, 1, result.etag->size() - 2), htSHA1);
        SourceInfo info(ref);
        info.storePath = result.storePath;

        return info;
    }

    // This downloads the entire git history
    else if (auto refData = std::get_if<FlakeRef::IsGit>(&resolvedRef.data)) {
        auto gitInfo = exportGit(state.store, refData->uri, resolvedRef.ref, resolvedRef.rev, "source");
        FlakeRef ref(resolvedRef.baseRef());
        ref.ref = gitInfo.ref;
        ref.rev = gitInfo.rev;
        SourceInfo info(ref);
        info.storePath = gitInfo.storePath;
        info.revCount = gitInfo.revCount;
        return info;
    }

    else if (auto refData = std::get_if<FlakeRef::IsPath>(&resolvedRef.data)) {
        if (!pathExists(refData->path + "/.git"))
            throw Error("flake '%s' does not reference a Git repository", refData->path);
        auto gitInfo = exportGit(state.store, refData->path, {}, {}, "source");
        FlakeRef ref(resolvedRef.baseRef());
        ref.ref = gitInfo.ref;
        ref.rev = gitInfo.rev;
        SourceInfo info(ref);
        info.storePath = gitInfo.storePath;
        info.revCount = gitInfo.revCount;
        return info;
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
    flake.hash = state.store->queryPathInfo(sourceInfo.storePath)->narHash;

    if (!pathExists(realFlakeFile))
        throw Error("source tree referenced by '%s' does not contain a '%s/flake.nix' file", resolvedRef, resolvedRef.subdir);

    Value vInfo;
    state.evalFile(realFlakeFile, vInfo); // FIXME: symlink attack

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

    return flake;
}

// Get the `NonFlake` corresponding to a `FlakeRef`.
NonFlake getNonFlake(EvalState & state, const FlakeRef & flakeRef, FlakeAlias alias)
{
    SourceInfo sourceInfo = fetchFlake(state, flakeRef);
    debug("got non-flake source '%s' with flakeref %s", sourceInfo.storePath, sourceInfo.resolvedRef.to_string());

    FlakeRef resolvedRef = sourceInfo.resolvedRef;

    NonFlake nonFlake(flakeRef, sourceInfo);

    state.store->assertStorePath(nonFlake.storePath);

    if (state.allowedPaths)
        state.allowedPaths->insert(nonFlake.storePath);

    nonFlake.hash = state.store->queryPathInfo(sourceInfo.storePath)->narHash;

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

ResolvedFlake resolveFlakeFromLockFile(EvalState & state, const FlakeRef & flakeRef, RegistryAccess registryAccess,
    LockFile lockFile, bool isTopFlake = false)
{
    bool allowRegistries = registryAccess == AllowRegistry || (registryAccess == AllowRegistryAtTop && isTopFlake);
    Flake flake = getFlake(state, flakeRef, allowRegistries);

    ResolvedFlake deps(flake);

    for (auto & nonFlakeInfo : flake.nonFlakeRequires) {
        FlakeRef ref = nonFlakeInfo.second;
        auto i = lockFile.nonFlakeEntries.find(nonFlakeInfo.first);
        if (i != lockFile.nonFlakeEntries.end()) ref = i->second.ref;
        deps.nonFlakeDeps.push_back(getNonFlake(state, ref, nonFlakeInfo.first));
    }

    for (auto newFlakeRef : flake.requires) {
        FlakeRef ref = newFlakeRef;
        LockFile newLockFile;
        auto i = lockFile.flakeEntries.find(newFlakeRef);
        if (i != lockFile.flakeEntries.end()) { // Propagate lockFile downwards if possible
            ref = i->second.ref;
            newLockFile = entryToLockFile(i->second);
        }
        deps.flakeDeps.push_back(resolveFlakeFromLockFile(state, ref, registryAccess, newLockFile));
    }

    return deps;
}

/* Given a flake reference, recursively fetch it and its dependencies.
   FIXME: this should return a graph of flakes.
*/
ResolvedFlake resolveFlake(EvalState & state, const FlakeRef & topRef, RegistryAccess registryAccess,
    bool recreateLockFile)
{
    bool allowRegistries = registryAccess == AllowRegistry || registryAccess == AllowRegistryAtTop;
    Flake flake = getFlake(state, topRef, allowRegistries);
    LockFile lockFile;

    if (!recreateLockFile) // If recreateLockFile, start with an empty lockfile
        lockFile = readLockFile(flake.storePath + "/flake.lock"); // FIXME: symlink attack

    return resolveFlakeFromLockFile(state, topRef, registryAccess, lockFile, true);
}

LockFile::FlakeEntry dependenciesToFlakeEntry(const ResolvedFlake & resolvedFlake)
{
    LockFile::FlakeEntry entry(resolvedFlake.flake.resolvedRef, resolvedFlake.flake.hash);

    for (auto & newResFlake : resolvedFlake.flakeDeps)
        entry.flakeEntries.insert_or_assign(newResFlake.flake.originalRef, dependenciesToFlakeEntry(newResFlake));

    for (auto & nonFlake : resolvedFlake.nonFlakeDeps)
        entry.nonFlakeEntries.insert_or_assign(nonFlake.alias, LockFile::NonFlakeEntry(nonFlake.resolvedRef, nonFlake.hash));

    return entry;
}

static LockFile makeLockFile(EvalState & evalState, FlakeRef & flakeRef, bool recreateLockFile)
{
    ResolvedFlake resFlake = resolveFlake(evalState, flakeRef, AllowRegistry, recreateLockFile);
    return entryToLockFile(dependenciesToFlakeEntry(resFlake));
}

void updateLockFile(EvalState & state, const FlakeUri & uri, bool recreateLockFile)
{
    FlakeRef flakeRef = FlakeRef(uri);
    auto lockFile = makeLockFile(state, flakeRef, recreateLockFile);
    if (auto refData = std::get_if<FlakeRef::IsPath>(&flakeRef.data)) {
        writeLockFile(lockFile, refData->path + (flakeRef.subdir == "" ? "" : "/" + flakeRef.subdir) + "/flake.lock");

        // Hack: Make sure that flake.lock is visible to Git. Otherwise,
        // exportGit will fail to copy it to the Nix store.
        runProgram("git", true, { "-C", refData->path, "add",
                  (flakeRef.subdir == "" ? "" : flakeRef.subdir + "/") + "flake.lock" });
    } else
        throw Error("flakeUri %s can't be updated because it is not a path", uri);
}

void callFlake(EvalState & state, const ResolvedFlake & resFlake, Value & v)
{
    // Construct the resulting attrset '{description, provides,
    // ...}'. This attrset is passed lazily as an argument to 'provides'.

    state.mkAttrs(v, resFlake.flakeDeps.size() + resFlake.nonFlakeDeps.size() + 8);

    for (const ResolvedFlake newResFlake : resFlake.flakeDeps) {
        auto vFlake = state.allocAttr(v, newResFlake.flake.id);
        callFlake(state, newResFlake, *vFlake);
    }

    for (const NonFlake nonFlake : resFlake.nonFlakeDeps) {
        auto vNonFlake = state.allocAttr(v, nonFlake.alias);
        state.mkAttrs(*vNonFlake, 4);

        state.store->isValidPath(nonFlake.storePath);
        mkString(*state.allocAttr(*vNonFlake, state.sOutPath), nonFlake.storePath, {nonFlake.storePath});

        // FIXME: add rev, shortRev, revCount, ...
    }

    mkString(*state.allocAttr(v, state.sDescription), resFlake.flake.description);

    auto & path = resFlake.flake.storePath;
    state.store->isValidPath(path);
    mkString(*state.allocAttr(v, state.sOutPath), path, {path});

    if (resFlake.flake.resolvedRef.rev) {
        mkString(*state.allocAttr(v, state.symbols.create("rev")),
            resFlake.flake.resolvedRef.rev->gitRev());
        mkString(*state.allocAttr(v, state.symbols.create("shortRev")),
            resFlake.flake.resolvedRef.rev->gitShortRev());
    }

    if (resFlake.flake.revCount)
        mkInt(*state.allocAttr(v, state.symbols.create("revCount")), *resFlake.flake.revCount);

    auto vProvides = state.allocAttr(v, state.symbols.create("provides"));
    mkApp(*vProvides, *resFlake.flake.vProvides, v);

    v.attrs->push_back(Attr(state.symbols.create("self"), &v));

    v.attrs->sort();
}

// Return the `provides` of the top flake, while assigning to `v` the provides
// of the dependencies as well.
void makeFlakeValue(EvalState & state, const FlakeRef & flakeRef, RegistryAccess registryAccess, Value & v, bool recreateLockFile)
{
    callFlake(state, resolveFlake(state, flakeRef, registryAccess, recreateLockFile), v);
}

// This function is exposed to be used in nix files.
static void prim_getFlake(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    makeFlakeValue(state, state.forceStringNoCtx(*args[0], pos),
        evalSettings.pureEval ? DisallowRegistry : AllowRegistryAtTop, v, false);
    // `recreateLockFile == false` because this is the evaluation stage, which should be pure, and hence not recreate lockfiles.
}

static RegisterPrimOp r2("getFlake", 1, prim_getFlake);

void gitCloneFlake (std::string flakeUri, EvalState & state, Registries registries,
    Path endDirectory)
{
    FlakeRef flakeRef(flakeUri);
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

    if (endDirectory != "")
        args.push_back(endDirectory);

    runProgram("git", true, args);
}

}
