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

static std::map<FlakeId, FlakeInput> parseFlakeInputs(
    EvalState & state, Value * value, const Pos & pos);

static FlakeInput parseFlakeInput(EvalState & state,
    const std::string & inputName, Value * value, const Pos & pos)
{
    expectType(state, tAttrs, *value, pos);

    FlakeInput input {
        .ref = parseFlakeRef(inputName)
    };

    auto sInputs = state.symbols.create("inputs");
    auto sUrl = state.symbols.create("url");
    auto sUri = state.symbols.create("uri"); // FIXME: remove soon
    auto sFlake = state.symbols.create("flake");
    auto sFollows = state.symbols.create("follows");

    for (Attr attr : *(value->attrs)) {
        if (attr.name == sUrl || attr.name == sUri) {
            expectType(state, tString, *attr.value, *attr.pos);
            input.ref = parseFlakeRef(attr.value->string.s);
        } else if (attr.name == sFlake) {
            expectType(state, tBool, *attr.value, *attr.pos);
            input.isFlake = attr.value->boolean;
        } else if (attr.name == sInputs) {
            input.overrides = parseFlakeInputs(state, attr.value, *attr.pos);
        } else if (attr.name == sFollows) {
            expectType(state, tString, *attr.value, *attr.pos);
            try {
                input.follows = parseInputPath(attr.value->string.s);
            } catch (Error & e) {
                e.addPrefix("in flake attribute at '%s':\n");
            }
        } else
            throw Error("flake input '%s' has an unsupported attribute '%s', at %s",
                inputName, attr.name, *attr.pos);
    }

    return input;
}

static std::map<FlakeId, FlakeInput> parseFlakeInputs(
    EvalState & state, Value * value, const Pos & pos)
{
    std::map<FlakeId, FlakeInput> inputs;

    expectType(state, tAttrs, *value, pos);

    for (Attr & inputAttr : *(*value).attrs) {
        inputs.emplace(inputAttr.name,
            parseFlakeInput(state,
                inputAttr.name,
                inputAttr.value,
                *inputAttr.pos));
    }

    return inputs;
}

static Flake getFlake(EvalState & state, const FlakeRef & originalRef,
    bool allowLookup, RefMap & refMap)
{
    auto flakeRef = lookupInRefMap(refMap,
        maybeLookupFlake(state,
            lookupInRefMap(refMap, originalRef), allowLookup));

    auto [sourceInfo, resolvedInput] = flakeRef.input->fetchTree(state.store);

    FlakeRef resolvedRef(resolvedInput, flakeRef.subdir);

    debug("got flake source '%s' from '%s'",
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

    if (std::optional<Attr *> inputs = vInfo.attrs->get(sInputs))
        flake.inputs = parseFlakeInputs(state, (**inputs).value, *(**inputs).pos);

    auto sOutputs = state.symbols.create("outputs");

    if (auto outputs = vInfo.attrs->get(sOutputs)) {
        expectType(state, tLambda, *(**outputs).value, *(**outputs).pos);
        flake.vOutputs = (**outputs).value;

        if (flake.vOutputs->lambda.fun->matchAttrs) {
            for (auto & formal : flake.vOutputs->lambda.fun->formals->formals) {
                if (formal.name != state.sSelf)
                    flake.inputs.emplace(formal.name, FlakeInput {
                        .ref = parseFlakeRef(formal.name)
                    });
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

    debug("got non-flake source '%s' from '%s'",
        state.store->printStorePath(sourceInfo.storePath), resolvedRef);

    refMap.push_back({originalRef, resolvedRef});
    refMap.push_back({flakeRef, resolvedRef});

    if (state.allowedPaths)
        state.allowedPaths->insert(sourceInfo.actualPath);

    return std::make_pair(std::move(sourceInfo), resolvedRef);
}

bool allowedToUseRegistries(LockFileMode handle, bool isTopRef)
{
    if (handle == AllPure) return false;
    else if (handle == TopRefUsesRegistries) return isTopRef;
    else if (handle == UpdateLockFile) return true;
    else if (handle == UseUpdatedLockFile) return true;
    else if (handle == RecreateLockFile) return true;
    else if (handle == UseNewLockFile) return true;
    else assert(false);
}

static void flattenLockFile(
    const LockedInputs & inputs,
    const InputPath & prefix,
    std::map<InputPath, const LockedInput *> & res)
{
    for (auto &[id, input] : inputs.inputs) {
        auto inputPath(prefix);
        inputPath.push_back(id);
        res.emplace(inputPath, &input);
        flattenLockFile(input, inputPath, res);
    }
}

static std::string diffLockFiles(const LockedInputs & oldLocks, const LockedInputs & newLocks)
{
    std::map<InputPath, const LockedInput *> oldFlat, newFlat;
    flattenLockFile(oldLocks, {}, oldFlat);
    flattenLockFile(newLocks, {}, newFlat);

    auto i = oldFlat.begin();
    auto j = newFlat.begin();
    std::string res;

    while (i != oldFlat.end() || j != newFlat.end()) {
        if (j != newFlat.end() && (i == oldFlat.end() || i->first > j->first)) {
            res += fmt("  added '%s': '%s'\n", concatStringsSep("/", j->first), j->second->ref);
            ++j;
        } else if (i != oldFlat.end() && (j == newFlat.end() || i->first < j->first)) {
            res += fmt("  removed '%s'\n", concatStringsSep("/", i->first));
            ++i;
        } else {
            if (!(i->second->ref == j->second->ref))
                res += fmt("  updated '%s': '%s' -> '%s'\n",
                    concatStringsSep("/", i->first),
                    i->second->ref,
                    j->second->ref);
            ++i;
            ++j;
        }
    }

    return res;
}

/* Compute an in-memory lock file for the specified top-level flake,
   and optionally write it to file, it the flake is writable. */
LockedFlake lockFlake(
    EvalState & state,
    const FlakeRef & topRef,
    LockFileMode lockFileMode,
    const LockFlags & lockFlags)
{
    settings.requireExperimentalFeature("flakes");

    RefMap refMap;

    auto flake = getFlake(state, topRef,
        allowedToUseRegistries(lockFileMode, true), refMap);

    LockFile oldLockFile;

    if (lockFileMode != RecreateLockFile && lockFileMode != UseNewLockFile) {
        // If recreateLockFile, start with an empty lockfile
        // FIXME: symlink attack
        oldLockFile = LockFile::read(
            flake.sourceInfo->actualPath + "/" + flake.resolvedRef.subdir + "/flake.lock");
    }

    debug("old lock file: %s", oldLockFile);

    LockFile newLockFile, prevLockFile;
    std::vector<InputPath> prevUnresolved;

    // FIXME: check whether all overrides are used.
    std::map<InputPath, FlakeInput> overrides;

    for (auto & i : lockFlags.inputOverrides)
        overrides.insert_or_assign(i.first, FlakeInput { .ref = i.second });

    /* Compute the new lock file. This is dones as a fixpoint
       iteration: we repeat until the new lock file no longer changes
       and there are no unresolved "follows" inputs. */
    while (true) {
        std::vector<InputPath> unresolved;

        /* Recurse into the flake inputs. */
        std::function<void(
            const FlakeInputs & flakeInputs,
            const LockedInputs & oldLocks,
            LockedInputs & newLocks,
            const InputPath & inputPathPrefix)>
            updateLocks;

        updateLocks = [&](
            const FlakeInputs & flakeInputs,
            const LockedInputs & oldLocks,
            LockedInputs & newLocks,
            const InputPath & inputPathPrefix)
        {
            /* Get the overrides (i.e. attributes of the form
               'inputs.nixops.inputs.nixpkgs.url = ...'). */
            for (auto & [id, input] : flake.inputs) {
                for (auto & [idOverride, inputOverride] : input.overrides) {
                    auto inputPath(inputPathPrefix);
                    inputPath.push_back(id);
                    inputPath.push_back(idOverride);
                    overrides.insert_or_assign(inputPath, inputOverride);
                }
            }

            /* Go over the flake inputs, resolve/fetch them if
               necessary (i.e. if they're new or the flakeref changed
               from what's in the lock file). */
            for (auto & [id, input2] : flakeInputs) {
                auto inputPath(inputPathPrefix);
                inputPath.push_back(id);
                auto inputPathS = concatStringsSep("/", inputPath);

                /* Do we have an override for this input from one of
                   the ancestors? */
                auto i = overrides.find(inputPath);
                bool hasOverride = i != overrides.end();
                auto & input = hasOverride ? i->second : input2;

                if (input.follows) {
                    /* This is a "follows" input
                       (i.e. 'inputs.nixpkgs.follows =
                       "dwarffs/nixpkgs"). Resolve the source and copy
                       its inputs. Note that the source is normally
                       relative to the current node of the lock file
                       (e.g. "dwarffs/nixpkgs" refers to the nixpkgs
                       input of the dwarffs input of the root flake),
                       but if it's from an override, it's relative to
                       the *root* of the lock file. */
                    auto follows = (hasOverride ? newLockFile : newLocks).findInput(*input.follows);
                    if (follows)
                        newLocks.inputs.insert_or_assign(id, **follows);
                    else
                        /* We haven't processed the source of the
                           "follows" yet (e.g. "dwarffs/nixpkgs"). So
                           we'll need another round of the fixpoint
                           iteration. */
                        unresolved.push_back(inputPath);
                    continue;
                }

                auto oldLock = oldLocks.inputs.find(id);

                if (oldLock != oldLocks.inputs.end() && oldLock->second.originalRef == input.ref && !hasOverride) {
                    /* Copy the input from the old lock file if its
                       flakeref didn't change and there is no override
                       from a higher level flake. */
                    newLocks.inputs.insert_or_assign(id, oldLock->second);

                    /* However there may be new overrides on the
                       inputs of this flake, so we need to check those
                       (without fetching this flake - we need to be
                       lazy). */
                    FlakeInputs fakeInputs;

                    for (auto & i : oldLock->second.inputs) {
                        fakeInputs.emplace(i.first, FlakeInput {
                            .ref = i.second.originalRef
                        });
                    }

                    updateLocks(fakeInputs,
                        oldLock->second,
                        newLocks.inputs.find(id)->second,
                        inputPath);

                } else {
                    /* We need to update/create a new lock file
                       entry. So fetch the flake/non-flake. */
                    if (lockFileMode == AllPure || lockFileMode == TopRefUsesRegistries)
                        throw Error("cannot update flake input '%s' in pure mode", inputPathS);

                    if (input.isFlake) {
                        auto inputFlake = getFlake(state, input.ref,
                            allowedToUseRegistries(lockFileMode, false), refMap);

                        newLocks.inputs.insert_or_assign(id,
                            LockedInput(inputFlake.resolvedRef, inputFlake.originalRef, inputFlake.sourceInfo->narHash));

                        /* Recursively process the inputs of this
                           flake. Also, unless we already have this
                           flake in the top-level lock file, use this
                           flake's own lock file. */
                        updateLocks(inputFlake.inputs,
                            oldLock != oldLocks.inputs.end()
                            ? (const LockedInputs &) oldLock->second
                            : LockFile::read(
                                inputFlake.sourceInfo->actualPath + "/" + inputFlake.resolvedRef.subdir + "/flake.lock"),
                            newLocks.inputs.find(id)->second,
                            inputPath);
                    }

                    else {
                        auto [sourceInfo, resolvedRef] = getNonFlake(state, input.ref,
                            allowedToUseRegistries(lockFileMode, false), refMap);
                        newLocks.inputs.insert_or_assign(id,
                            LockedInput(resolvedRef, input.ref, sourceInfo.narHash));
                    }
                }
            }
        };

        updateLocks(flake.inputs, oldLockFile, newLockFile, {});

        /* Check if there is a cycle in the "follows" inputs. */
        if (!unresolved.empty() && unresolved == prevUnresolved) {
            std::vector<std::string> ss;
            for (auto & i : unresolved)
                ss.push_back(concatStringsSep("/", i));
            throw Error("cycle or missing input detected in flake inputs: %s", concatStringsSep(", ", ss));
        }

        std::swap(unresolved, prevUnresolved);

        /* Done with the fixpoint iteration? */
        if (newLockFile == prevLockFile) break;
        prevLockFile = newLockFile;
    };

    debug("new lock file: %s", newLockFile);

    /* Check whether we need to / can write the new lock file. */
    if (!(newLockFile == oldLockFile)) {

        if (!(oldLockFile == LockFile()))
            printInfo("inputs of flake '%s' changed:\n%s", topRef, chomp(diffLockFiles(oldLockFile, newLockFile)));

        if (lockFileMode == UpdateLockFile || lockFileMode == RecreateLockFile) {
            if (auto sourcePath = topRef.input->getSourcePath()) {
                if (!newLockFile.isImmutable()) {
                    if (settings.warnDirty)
                        warn("will not write lock file of flake '%s' because it has a mutable input", topRef);
                } else {
                    auto path = *sourcePath + (topRef.subdir == "" ? "" : "/" + topRef.subdir) + "/flake.lock";

                    if (pathExists(path))
                        warn("updating lock file '%s'", path);
                    else
                        warn("creating lock file '%s'", path);

                    newLockFile.write(path);

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
        } else if (lockFileMode != AllPure && lockFileMode != TopRefUsesRegistries)
            warn("using updated lock file without writing it to file");
    }

    return LockedFlake { .flake = std::move(flake), .lockFile = std::move(newLockFile) };
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
    const LockedFlake & lockedFlake,
    Value & v)
{
    callFlake(state, lockedFlake.flake, lockedFlake.lockFile, v);
}

// This function is exposed to be used in nix files.
static void prim_getFlake(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    LockFlags lockFlags;
    callFlake(state, lockFlake(state, parseFlakeRef(state.forceStringNoCtx(*args[0], pos)),
            evalSettings.pureEval ? AllPure : UseUpdatedLockFile, lockFlags), v);
}

static RegisterPrimOp r2("getFlake", 1, prim_getFlake);

}

Fingerprint LockedFlake::getFingerprint() const
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
