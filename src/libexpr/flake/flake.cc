#include "flake.hh"
#include "lockfile.hh"
#include "primops.hh"
#include "eval-inline.hh"
#include "store-api.hh"
#include "fetchers/fetchers.hh"

#include <iostream>
#include <ctime>
#include <iomanip>

namespace nix {

using namespace flake;

namespace flake {

/* If 'allowLookup' is true, then resolve 'flakeRef' using the
   registries. */
static FlakeRef maybeLookupFlake(
    EvalState & state,
    const FlakeRef & flakeRef,
    bool allowLookup)
{
    if (!flakeRef.isDirect()) {
        if (allowLookup)
            return flakeRef.resolve(state.store);
        else
            throw Error("'%s' is an indirect flake reference, but registry lookups are not allowed", flakeRef);
    } else
        return flakeRef;
}

typedef std::vector<std::pair<FlakeRef, FlakeRef>> RefMap;

static FlakeRef lookupInRefMap(
    const RefMap & refMap,
    const FlakeRef & flakeRef)
{
#if 0
    // FIXME: inefficient.
    for (auto & i : refMap) {
        if (flakeRef.contains(i.first)) {
            debug("mapping '%s' to previously seen input '%s' -> '%s",
                flakeRef, i.first, i.second);
            return i.second;
        }
    }
#endif

    return flakeRef;
}

static void expectType(EvalState & state, ValueType type,
    Value & value, const Pos & pos)
{
    if (value.type == tThunk && value.isTrivial())
        state.forceValue(value, pos);
    if (value.type != type)
        throw Error("expected %s but got %s at %s",
            showType(type), showType(value.type), pos);
}

static Flake getFlake(EvalState & state, const FlakeRef & originalRef,
    bool allowLookup, RefMap & refMap)
{
    auto flakeRef = lookupInRefMap(refMap,
        maybeLookupFlake(state,
            lookupInRefMap(refMap, originalRef), allowLookup));

    auto [sourceInfo, resolvedInput] = flakeRef.input->fetchTree(state.store);

    FlakeRef resolvedRef(resolvedInput, flakeRef.subdir);

    debug("got flake source '%s' from flake URL '%s'",
        state.store->printStorePath(sourceInfo.storePath), resolvedRef);

    refMap.push_back({originalRef, resolvedRef});
    refMap.push_back({flakeRef, resolvedRef});

    if (state.allowedPaths)
        state.allowedPaths->insert(sourceInfo.actualPath);

    // Guard against symlink attacks.
    auto flakeFile = canonPath(sourceInfo.actualPath + "/" + resolvedRef.subdir + "/flake.nix");
    if (!isInDir(flakeFile, sourceInfo.actualPath))
        throw Error("'flake.nix' file of flake '%s' escapes from '%s'",
            resolvedRef, state.store->printStorePath(sourceInfo.storePath));

    Flake flake {
        .originalRef = originalRef,
        .resolvedRef = resolvedRef,
        .sourceInfo = std::make_shared<fetchers::Tree>(std::move(sourceInfo))
    };

    if (!pathExists(flakeFile))
        throw Error("source tree referenced by '%s' does not contain a '%s/flake.nix' file", resolvedRef, resolvedRef.subdir);

    Value vInfo;
    state.evalFile(flakeFile, vInfo, true); // FIXME: symlink attack

    expectType(state, tAttrs, vInfo, Pos(state.symbols.create(flakeFile), 0, 0));

    auto sEdition = state.symbols.create("edition");
    auto sEpoch = state.symbols.create("epoch"); // FIXME: remove soon

    auto edition = vInfo.attrs->get(sEdition);
    if (!edition)
        edition = vInfo.attrs->get(sEpoch);

    if (edition) {
        expectType(state, tInt, *(**edition).value, *(**edition).pos);
        flake.edition = (**edition).value->integer;
        if (flake.edition > 201909)
            throw Error("flake '%s' requires unsupported edition %d; please upgrade Nix", flakeRef, flake.edition);
        if (flake.edition < 201909)
            throw Error("flake '%s' has illegal edition %d", flakeRef, flake.edition);
    } else
        throw Error("flake '%s' lacks attribute 'edition'", flakeRef);

    if (auto description = vInfo.attrs->get(state.sDescription)) {
        expectType(state, tString, *(**description).value, *(**description).pos);
        flake.description = (**description).value->string.s;
    }

    auto sInputs = state.symbols.create("inputs");
    auto sUrl = state.symbols.create("url");
    auto sUri = state.symbols.create("uri"); // FIXME: remove soon
    auto sFlake = state.symbols.create("flake");

    if (std::optional<Attr *> inputs = vInfo.attrs->get(sInputs)) {
        expectType(state, tAttrs, *(**inputs).value, *(**inputs).pos);

        for (Attr inputAttr : *(*(**inputs).value).attrs) {
            expectType(state, tAttrs, *inputAttr.value, *inputAttr.pos);

            FlakeInput input(parseFlakeRef(inputAttr.name));

            for (Attr attr : *(inputAttr.value->attrs)) {
                if (attr.name == sUrl || attr.name == sUri) {
                    expectType(state, tString, *attr.value, *attr.pos);
                    input.ref = parseFlakeRef(attr.value->string.s);
                } else if (attr.name == sFlake) {
                    expectType(state, tBool, *attr.value, *attr.pos);
                    input.isFlake = attr.value->boolean;
                } else
                    throw Error("flake input '%s' has an unsupported attribute '%s', at %s",
                        inputAttr.name, attr.name, *attr.pos);
            }

            flake.inputs.emplace(inputAttr.name, input);
        }
    }

    auto sOutputs = state.symbols.create("outputs");

    if (auto outputs = vInfo.attrs->get(sOutputs)) {
        expectType(state, tLambda, *(**outputs).value, *(**outputs).pos);
        flake.vOutputs = (**outputs).value;

        if (flake.vOutputs->lambda.fun->matchAttrs) {
            for (auto & formal : flake.vOutputs->lambda.fun->formals->formals) {
                if (formal.name != state.sSelf)
                    flake.inputs.emplace(formal.name, FlakeInput(parseFlakeRef(formal.name)));
            }
        }

    } else
        throw Error("flake '%s' lacks attribute 'outputs'", flakeRef);

    for (auto & attr : *vInfo.attrs) {
        if (attr.name != sEdition &&
            attr.name != sEpoch &&
            attr.name != state.sDescription &&
            attr.name != sInputs &&
            attr.name != sOutputs)
            throw Error("flake '%s' has an unsupported attribute '%s', at %s",
                flakeRef, attr.name, *attr.pos);
    }

    return flake;
}

Flake getFlake(EvalState & state, const FlakeRef & originalRef, bool allowLookup)
{
    RefMap refMap;
    return getFlake(state, originalRef, allowLookup, refMap);
}

static std::pair<fetchers::Tree, FlakeRef> getNonFlake(
    EvalState & state,
    const FlakeRef & originalRef,
    bool allowLookup,
    RefMap & refMap)
{
    auto flakeRef = lookupInRefMap(refMap,
        maybeLookupFlake(state,
            lookupInRefMap(refMap, originalRef), allowLookup));

    auto [sourceInfo, resolvedInput] = flakeRef.input->fetchTree(state.store);

    FlakeRef resolvedRef(resolvedInput, flakeRef.subdir);

    debug("got non-flake source '%s' with flakeref %s",
        state.store->printStorePath(sourceInfo.storePath), resolvedRef);

    refMap.push_back({originalRef, resolvedRef});
    refMap.push_back({flakeRef, resolvedRef});

    if (state.allowedPaths)
        state.allowedPaths->insert(sourceInfo.actualPath);

    return std::make_pair(std::move(sourceInfo), resolvedRef);
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

/* Given a flakeref and its subtree of the lockfile, return an updated
   subtree of the lockfile. That is, if the 'flake.nix' of the
   referenced flake has inputs that don't have a corresponding entry
   in the lockfile, they're added to the lockfile; conversely, any
   lockfile entries that don't have a corresponding entry in flake.nix
   are removed.

   Note that this is lazy: we only recursively fetch inputs that are
   not in the lockfile yet. */
static std::pair<Flake, LockedInput> updateLocks(
    RefMap & refMap,
    const std::string & inputPath,
    EvalState & state,
    const Flake & flake,
    HandleLockFile handleLockFile,
    const LockedInputs & oldEntry,
    bool topRef)
{
    LockedInput newEntry(
        flake.resolvedRef,
        flake.originalRef,
        flake.sourceInfo->narHash);

    std::vector<std::function<void()>> postponed;

    for (auto & [id, input] : flake.inputs) {
        auto inputPath2 = (inputPath.empty() ? "" : inputPath + "/") + id;
        auto i = oldEntry.inputs.find(id);
        if (i != oldEntry.inputs.end() && i->second.originalRef == input.ref) {
            newEntry.inputs.insert_or_assign(id, i->second);
        } else {
            if (handleLockFile == AllPure || handleLockFile == TopRefUsesRegistries)
                throw Error("cannot update flake input '%s' in pure mode", id);

            auto warn = [&](const FlakeRef & resolvedRef, const fetchers::Tree & sourceInfo) {
                if (i == oldEntry.inputs.end())
                    printInfo("mapped flake input '%s' to '%s'",
                        inputPath2, resolvedRef);
                else
                    printMsg(lvlWarn, "updated flake input '%s' from '%s' to '%s'",
                        inputPath2, i->second.originalRef, resolvedRef);
            };

            if (input.isFlake) {
                auto actualInput = getFlake(state, input.ref,
                    allowedToUseRegistries(handleLockFile, false), refMap);
                warn(actualInput.resolvedRef, *actualInput.sourceInfo);
                postponed.push_back([&, id{id}, inputPath2, actualInput]() {
                    newEntry.inputs.insert_or_assign(id,
                        updateLocks(refMap, inputPath2, state, actualInput, handleLockFile, {}, false).second);
                });
            } else {
                auto [sourceInfo, resolvedRef] = getNonFlake(state, input.ref,
                    allowedToUseRegistries(handleLockFile, false), refMap);
                warn(resolvedRef, sourceInfo);
                newEntry.inputs.insert_or_assign(id,
                    LockedInput(resolvedRef, input.ref, sourceInfo.narHash));
            }
        }
    }

    for (auto & f : postponed) f();

    return {flake, newEntry};
}

/* Compute an in-memory lockfile for the specified top-level flake,
   and optionally write it to file, it the flake is writable. */
ResolvedFlake resolveFlake(EvalState & state, const FlakeRef & topRef, HandleLockFile handleLockFile)
{
    settings.requireExperimentalFeature("flakes");

    auto flake = getFlake(state, topRef,
        allowedToUseRegistries(handleLockFile, true));

    LockFile oldLockFile;

    if (!recreateLockFile(handleLockFile)) {
        // If recreateLockFile, start with an empty lockfile
        // FIXME: symlink attack
        oldLockFile = LockFile::read(
            flake.sourceInfo->actualPath + "/" + flake.resolvedRef.subdir + "/flake.lock");
    }

    debug("old lock file: %s", oldLockFile);

    RefMap refMap;

    LockFile lockFile(updateLocks(
            refMap, "", state, flake, handleLockFile, oldLockFile, true).second);

    debug("new lock file: %s", lockFile);

    if (!(lockFile == oldLockFile)) {
        if (allowedToWrite(handleLockFile)) {
            if (auto sourcePath = topRef.input->getSourcePath()) {
                if (!lockFile.isImmutable()) {
                    if (settings.warnDirty)
                        warn("will not write lock file of flake '%s' because it has a mutable input", topRef);
                } else {
                    warn("updated lock file of flake '%s'", topRef);

                    lockFile.write(*sourcePath + (topRef.subdir == "" ? "" : "/" + topRef.subdir) + "/flake.lock");

                    // FIXME: rewriting the lockfile changed the
                    // top-level repo, so we should re-read it.

                    #if 0
                    // Hack: Make sure that flake.lock is visible to Git, so it ends up in the Nix store.
                    runProgram("git", true,
                        { "-C", *sourcePath, "add",
                          "--force",
                          "--intent-to-add",
                          (topRef.subdir == "" ? "" : topRef.subdir + "/") + "flake.lock" });
                    #endif
                }
            } else
                warn("cannot write lock file of remote flake '%s'", topRef);
        } else if (handleLockFile != AllPure && handleLockFile != TopRefUsesRegistries)
            warn("using updated lock file without writing it to file");
    }

    return ResolvedFlake { .flake = std::move(flake), .lockFile = std::move(lockFile) };
}

void updateLockFile(EvalState & state, const FlakeRef & flakeRef, bool recreateLockFile)
{
    resolveFlake(state, flakeRef, recreateLockFile ? RecreateLockFile : UpdateLockFile);
}

static void emitSourceInfoAttrs(EvalState & state, const fetchers::Tree & sourceInfo, Value & vAttrs)
{
    assert(state.store->isValidPath(sourceInfo.storePath));
    auto pathS = state.store->printStorePath(sourceInfo.storePath);
    mkString(*state.allocAttr(vAttrs, state.sOutPath), pathS, {pathS});

    if (sourceInfo.rev) {
        mkString(*state.allocAttr(vAttrs, state.symbols.create("rev")),
            sourceInfo.rev->gitRev());
        mkString(*state.allocAttr(vAttrs, state.symbols.create("shortRev")),
            sourceInfo.rev->gitShortRev());
    }

    if (sourceInfo.revCount)
        mkInt(*state.allocAttr(vAttrs, state.symbols.create("revCount")), *sourceInfo.revCount);

    if (sourceInfo.lastModified)
        mkString(*state.allocAttr(vAttrs, state.symbols.create("lastModified")),
            fmt("%s", std::put_time(std::gmtime(&*sourceInfo.lastModified), "%Y%m%d%H%M%S")));
}

struct LazyInput
{
    bool isFlake;
    LockedInput lockedInput;
};

/* Helper primop to make callFlake (below) fetch/call its inputs
   lazily. Note that this primop cannot be called by user code since
   it doesn't appear in 'builtins'. */
static void prim_callFlake(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    auto lazyInput = (LazyInput *) args[0]->attrs;

    if (lazyInput->isFlake) {
        auto flake = getFlake(state, lazyInput->lockedInput.ref, false);

        if (flake.sourceInfo->narHash != lazyInput->lockedInput.narHash)
            throw Error("the content hash of flake '%s' doesn't match the hash recorded in the referring lockfile",
                lazyInput->lockedInput.ref);

        callFlake(state, flake, lazyInput->lockedInput, v);
    } else {
        RefMap refMap;
        auto [sourceInfo, resolvedRef] = getNonFlake(state, lazyInput->lockedInput.ref, false, refMap);

        if (sourceInfo.narHash != lazyInput->lockedInput.narHash)
            throw Error("the content hash of repository '%s' doesn't match the hash recorded in the referring lockfile",
                lazyInput->lockedInput.ref);

        state.mkAttrs(v, 8);

        assert(state.store->isValidPath(sourceInfo.storePath));

        auto pathS = state.store->printStorePath(sourceInfo.storePath);

        mkString(*state.allocAttr(v, state.sOutPath), pathS, {pathS});

        emitSourceInfoAttrs(state, sourceInfo, v);

        v.attrs->sort();
    }
}

void callFlake(EvalState & state,
    const Flake & flake,
    const LockedInputs & lockedInputs,
    Value & vResFinal)
{
    auto & vRes = *state.allocValue();
    auto & vInputs = *state.allocValue();

    state.mkAttrs(vInputs, flake.inputs.size() + 1);

    for (auto & [inputId, input] : flake.inputs) {
        auto vFlake = state.allocAttr(vInputs, inputId);
        auto vPrimOp = state.allocValue();
        static auto primOp = new PrimOp(prim_callFlake, 1, state.symbols.create("callFlake"));
        vPrimOp->type = tPrimOp;
        vPrimOp->primOp = primOp;
        auto vArg = state.allocValue();
        vArg->type = tNull;
        auto lockedInput = lockedInputs.inputs.find(inputId);
        assert(lockedInput != lockedInputs.inputs.end());
        // FIXME: leak
        vArg->attrs = (Bindings *) new LazyInput{input.isFlake, lockedInput->second};
        mkApp(*vFlake, *vPrimOp, *vArg);
    }

    auto & vSourceInfo = *state.allocValue();
    state.mkAttrs(vSourceInfo, 8);
    emitSourceInfoAttrs(state, *flake.sourceInfo, vSourceInfo);
    vSourceInfo.attrs->sort();

    vInputs.attrs->push_back(Attr(state.sSelf, &vRes));

    vInputs.attrs->sort();

    /* For convenience, put the outputs directly in the result, so you
       can refer to an output of an input as 'inputs.foo.bar' rather
       than 'inputs.foo.outputs.bar'. */
    auto vCall = *state.allocValue();
    state.eval(state.parseExprFromString(
            "outputsFun: inputs: sourceInfo: let outputs = outputsFun inputs; in "
            "outputs // sourceInfo // { inherit inputs; inherit outputs; inherit sourceInfo; }", "/"), vCall);

    auto vCall2 = *state.allocValue();
    auto vCall3 = *state.allocValue();
    state.callFunction(vCall, *flake.vOutputs, vCall2, noPos);
    state.callFunction(vCall2, vInputs, vCall3, noPos);
    state.callFunction(vCall3, vSourceInfo, vRes, noPos);

    vResFinal = vRes;
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
    callFlake(state, resolveFlake(state, parseFlakeRef(state.forceStringNoCtx(*args[0], pos)),
            evalSettings.pureEval ? AllPure : UseUpdatedLockFile), v);
}

static RegisterPrimOp r2("getFlake", 1, prim_getFlake);

}

Fingerprint ResolvedFlake::getFingerprint() const
{
    // FIXME: as an optimization, if the flake contains a lock file
    // and we haven't changed it, then it's sufficient to use
    // flake.sourceInfo.storePath for the fingerprint.
    return hashString(htSHA256,
        fmt("%s;%d;%d;%s",
            flake.sourceInfo->storePath.to_string(),
            flake.sourceInfo->revCount.value_or(0),
            flake.sourceInfo->lastModified.value_or(0),
            lockFile));
}

Flake::~Flake() { }

}
