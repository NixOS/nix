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
        throw Error("requested to fetch FlakeRef '%s' purely, which is mutable", flakeRef);

    LockFile::FlakeEntry entry(flakeRef);

    auto nonFlakeRequires = json["nonFlakeRequires"];

    for (auto i = nonFlakeRequires.begin(); i != nonFlakeRequires.end(); ++i) {
        FlakeRef flakeRef(i->value("uri", ""));
        if (!flakeRef.isImmutable())
            throw Error("requested to fetch FlakeRef '%s' purely, which is mutable", flakeRef);
        entry.nonFlakeEntries.insert_or_assign(i.key(), flakeRef);
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
        if (!flakeRef.isImmutable())
            throw Error("requested to fetch FlakeRef '%s' purely, which is mutable", flakeRef);
        lockFile.nonFlakeEntries.insert_or_assign(i.key(), flakeRef);
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
    for (auto & x : entry.nonFlakeEntries)
        json["nonFlakeRequires"][x.first]["uri"] = x.second.to_string();
    for (auto & x : entry.flakeEntries)
        json["requires"][x.first.to_string()] = flakeEntryToJson(x.second);
    return json;
}

void writeLockFile(const LockFile & lockFile, const Path & path)
{
    nlohmann::json json;
    json["version"] = 1;
    json["nonFlakeRequires"] = nlohmann::json::object();
    for (auto & x : lockFile.nonFlakeEntries)
        json["nonFlakeRequires"][x.first]["uri"] = x.second.to_string();
    json["requires"] = nlohmann::json::object();
    for (auto & x : lockFile.flakeEntries)
        json["requires"][x.first.to_string()] = flakeEntryToJson(x.second);
    createDirs(dirOf(path));
    writeFile(path, json.dump(4)); // '4' = indentation in json file
}

std::shared_ptr<FlakeRegistry> getGlobalRegistry()
{
    Path registryFile = settings.nixDataDir + "/nix/flake-registry.json";
    return readRegistry(registryFile);
}

Path getUserRegistryPath()
{
    return getHome() + "/.config/nix/registry.json";
}

std::shared_ptr<FlakeRegistry> getUserRegistry()
{
    return readRegistry(getUserRegistryPath());
}

std::shared_ptr<FlakeRegistry> getFlagRegistry()
{
    // TODO (Nick): Implement this.
    return std::make_shared<FlakeRegistry>();
}

// This always returns a vector with globalReg, userReg, localReg, flakeReg.
// If one of them doesn't exist, the registry is left empty but does exist.
const Registries EvalState::getFlakeRegistries()
{
    Registries registries;
    registries.push_back(getGlobalRegistry()); // TODO (Nick): Doesn't this break immutability?
    registries.push_back(getUserRegistry());
    registries.push_back(std::make_shared<FlakeRegistry>()); // local
    registries.push_back(getFlagRegistry());
    return registries;
}

static FlakeRef lookupFlake(EvalState & state, const FlakeRef & flakeRef, const Registries & registries,
    std::vector<FlakeRef> pastSearches = {})
{
    if (registries.empty() && !flakeRef.isDirect())
        throw Error("indirect flake reference '%s' is not allowed", flakeRef);

    for (std::shared_ptr<FlakeRegistry> registry : registries) {
        auto i = registry->entries.find(flakeRef);
        if (i != registry->entries.end()) {
            auto newRef = i->second;
            if (std::get_if<FlakeRef::IsAlias>(&flakeRef.data)) {
                if (flakeRef.ref) newRef.ref = flakeRef.ref;
                if (flakeRef.rev) newRef.rev = flakeRef.rev;
            }
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
    }

    if (!flakeRef.isDirect())
        throw Error("could not resolve flake reference '%s'", flakeRef);

    return flakeRef;
}

static FlakeSourceInfo fetchFlake(EvalState & state, const FlakeRef flakeRef, bool impureIsAllowed = false)
{
    FlakeRef fRef = lookupFlake(state, flakeRef,
        impureIsAllowed ? state.getFlakeRegistries() : std::vector<std::shared_ptr<FlakeRegistry>>());

    if (evalSettings.pureEval && !impureIsAllowed && !fRef.isImmutable())
        throw Error("requested to fetch mutable flake '%s' in pure mode", fRef);

    // This only downloads only one revision of the repo, not the entire history.
    if (auto refData = std::get_if<FlakeRef::IsGitHub>(&fRef.data)) {

        // FIXME: use regular /archive URLs instead? api.github.com
        // might have stricter rate limits.

        auto url = fmt("https://api.github.com/repos/%s/%s/tarball/%s",
            refData->owner, refData->repo,
            fRef.rev ? fRef.rev->to_string(Base16, false)
                : fRef.ref ? *fRef.ref : "master");

        std::string accessToken = settings.githubAccessToken.get();
        if (accessToken != "")
            url += "?access_token=" + accessToken;

        auto result = getDownloader()->downloadCached(state.store, url, true, "source",
            Hash(), nullptr, fRef.rev ? 1000000000 : settings.tarballTtl);

        if (!result.etag)
            throw Error("did not receive an ETag header from '%s'", url);

        if (result.etag->size() != 42 || (*result.etag)[0] != '"' || (*result.etag)[41] != '"')
            throw Error("ETag header '%s' from '%s' is not a Git revision", *result.etag, url);

        FlakeSourceInfo info(fRef);
        info.storePath = result.path;
        info.rev = Hash(std::string(*result.etag, 1, result.etag->size() - 2), htSHA1);
        info.flakeRef.rev = info.rev;
        info.flakeRef.ref = {};

        return info;
    }

    // This downloads the entire git history
    else if (auto refData = std::get_if<FlakeRef::IsGit>(&fRef.data)) {
        auto gitInfo = exportGit(state.store, refData->uri, fRef.ref, fRef.rev, "source");
        FlakeSourceInfo info(fRef);
        info.storePath = gitInfo.storePath;
        info.rev = gitInfo.rev;
        info.revCount = gitInfo.revCount;
        info.flakeRef.ref = gitInfo.ref;
        info.flakeRef.rev = info.rev;
        return info;
    }

    else if (auto refData = std::get_if<FlakeRef::IsPath>(&fRef.data)) {
        if (!pathExists(refData->path + "/.git"))
            throw Error("flake '%s' does not reference a Git repository", refData->path);
        auto gitInfo = exportGit(state.store, refData->path, {}, {}, "source");
        FlakeSourceInfo info(fRef);
        info.storePath = gitInfo.storePath;
        info.rev = gitInfo.rev;
        info.revCount = gitInfo.revCount;
        info.flakeRef.ref = gitInfo.ref;
        info.flakeRef.rev = info.rev;
        return info;
    }

    else abort();
}

// This will return the flake which corresponds to a given FlakeRef. The lookupFlake is done within this function.
Flake getFlake(EvalState & state, const FlakeRef & flakeRef, bool impureIsAllowed = false)
{
    FlakeSourceInfo sourceInfo = fetchFlake(state, flakeRef, impureIsAllowed);
    debug("got flake source '%s' with revision %s",
        sourceInfo.storePath, sourceInfo.rev.value_or(Hash(htSHA1)).to_string(Base16, false));

    state.store->assertStorePath(sourceInfo.storePath);

    if (state.allowedPaths)
        state.allowedPaths->insert(sourceInfo.storePath);

    Flake flake(flakeRef, std::move(sourceInfo));
    if (std::get_if<FlakeRef::IsGitHub>(&flakeRef.data)) {
        // FIXME: ehm?
        if (flake.sourceInfo.rev)
            flake.ref = FlakeRef(flakeRef.baseRef().to_string()
                + "/" + flake.sourceInfo.rev->to_string(Base16, false));
    }

    Path flakeFile = sourceInfo.storePath + "/flake.nix";
    if (!pathExists(flakeFile))
        throw Error("source tree referenced by '%s' does not contain a 'flake.nix' file", flakeRef);

    Value vInfo;
    state.evalFile(flakeFile, vInfo); // FIXME: symlink attack

    state.forceAttrs(vInfo);

    // FIXME: change to "id"?
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

    nonFlake.alias = alias;

    return nonFlake;
}

/* Given a flake reference, recursively fetch it and its
   dependencies.
   FIXME: this should return a graph of flakes.
*/
ResolvedFlake resolveFlake(EvalState & state, const FlakeRef & topRef,
    RegistryAccess registryAccess, bool isTopFlake)
{
    Flake flake = getFlake(state, topRef,
        registryAccess == AllowRegistry || (registryAccess == AllowRegistryAtTop && isTopFlake));

    LockFile lockFile;

    if (isTopFlake)
        lockFile = readLockFile(flake.sourceInfo.storePath + "/flake.lock"); // FIXME: symlink attack

    ResolvedFlake deps(flake);

    for (auto & nonFlakeInfo : flake.nonFlakeRequires)
        deps.nonFlakeDeps.push_back(getNonFlake(state, nonFlakeInfo.second, nonFlakeInfo.first));

    for (auto newFlakeRef : flake.requires) {
        auto i = lockFile.flakeEntries.find(newFlakeRef);
        if (i != lockFile.flakeEntries.end()) newFlakeRef = i->second.ref;
        // FIXME: propagate lockFile downwards
        deps.flakeDeps.push_back(resolveFlake(state, newFlakeRef, registryAccess, false));
    }

    return deps;
}

LockFile::FlakeEntry dependenciesToFlakeEntry(const ResolvedFlake & resolvedFlake)
{
    LockFile::FlakeEntry entry(resolvedFlake.flake.sourceInfo.flakeRef);

    for (auto & newResFlake : resolvedFlake.flakeDeps)
        entry.flakeEntries.insert_or_assign(newResFlake.flake.id, dependenciesToFlakeEntry(newResFlake));

    for (auto & nonFlake : resolvedFlake.nonFlakeDeps)
        entry.nonFlakeEntries.insert_or_assign(nonFlake.alias, nonFlake.ref);

    return entry;
}

static LockFile makeLockFile(EvalState & evalState, FlakeRef & flakeRef)
{
    ResolvedFlake resFlake = resolveFlake(evalState, flakeRef, AllowRegistry);
    LockFile::FlakeEntry entry = dependenciesToFlakeEntry(resFlake);
    LockFile lockFile;
    lockFile.flakeEntries = entry.flakeEntries;
    lockFile.nonFlakeEntries = entry.nonFlakeEntries;
    return lockFile;
}

void updateLockFile(EvalState & state, const Path & path)
{
    // FIXME: don't copy 'path' to the store (especially since we
    // dirty it immediately afterwards).

    FlakeRef flakeRef = FlakeRef(path); // FIXME: ugly
    auto lockFile = makeLockFile(state, flakeRef);
    writeLockFile(lockFile, path + "/flake.lock");

    // Hack: Make sure that flake.lock is visible to Git. Otherwise,
    // exportGit will fail to copy it to the Nix store.
    runProgram("git", true, { "-C", path, "add", "flake.lock" });
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

        state.store->isValidPath(nonFlake.path);
        mkString(*state.allocAttr(*vNonFlake, state.sOutPath), nonFlake.path, {nonFlake.path});
    }

    mkString(*state.allocAttr(v, state.sDescription), resFlake.flake.description);

    auto & path = resFlake.flake.sourceInfo.storePath;
    state.store->isValidPath(path);
    mkString(*state.allocAttr(v, state.sOutPath), path, {path});

    if (resFlake.flake.sourceInfo.revCount)
        mkInt(*state.allocAttr(v, state.symbols.create("revCount")), *resFlake.flake.sourceInfo.revCount);

    auto vProvides = state.allocAttr(v, state.symbols.create("provides"));
    mkApp(*vProvides, *resFlake.flake.vProvides, v);

    v.attrs->push_back(Attr(state.symbols.create("self"), &v));

    v.attrs->sort();
}

// Return the `provides` of the top flake, while assigning to `v` the provides
// of the dependencies as well.
void makeFlakeValue(EvalState & state, const FlakeRef & flakeRef, RegistryAccess registryAccess, Value & v)
{
    callFlake(state, resolveFlake(state, flakeRef, registryAccess), v);
}

// This function is exposed to be used in nix files.
static void prim_getFlake(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    makeFlakeValue(state, state.forceStringNoCtx(*args[0], pos),
        evalSettings.pureEval ? DisallowRegistry : AllowRegistryAtTop, v);
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
