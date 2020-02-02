#include "flake.hh"
#include "lockfile.hh"
#include "primops.hh"
#include "eval-inline.hh"
#include "store-api.hh"
#include "fetchers/fetchers.hh"
#include "finally.hh"

#include <iostream>
#include <ctime>
#include <iomanip>

namespace nix {

using namespace flake;

namespace flake {

/* If 'allowLookup' is true, then resolve 'flakeRef' using the
   registries. */
static FlakeRef maybeLookupFlake(
    ref<Store> store,
    const FlakeRef & flakeRef,
    bool allowLookup)
{
    if (!flakeRef.input->isDirect()) {
        if (allowLookup)
            return flakeRef.resolve(store);
        else
            throw Error("'%s' is an indirect flake reference, but registry lookups are not allowed", flakeRef);
    } else
        return flakeRef;
}

typedef std::vector<std::pair<FlakeRef, FlakeRef>> FlakeCache;

static FlakeRef lookupInFlakeCache(
    const FlakeCache & flakeCache,
    const FlakeRef & flakeRef)
{
    // FIXME: inefficient.
    for (auto & i : flakeCache) {
        if (flakeRef == i.first) {
            debug("mapping '%s' to previously seen input '%s' -> '%s",
                flakeRef, i.first, i.second);
            return i.second;
        }
    }

    return flakeRef;
}

static std::pair<fetchers::Tree, FlakeRef> fetchOrSubstituteTree(
    EvalState & state,
    const FlakeRef & originalRef,
    std::optional<TreeInfo> treeInfo,
    bool allowLookup,
    FlakeCache & flakeCache)
{
    /* The tree may already be in the Nix store, or it could be
       substituted (which is often faster than fetching from the
       original source). So check that. */
    if (treeInfo && originalRef.input->isDirect() && originalRef.input->isImmutable()) {
        try {
            auto storePath = treeInfo->computeStorePath(*state.store);

            state.store->ensurePath(storePath);

            debug("using substituted/cached input '%s' in '%s'",
                originalRef, state.store->printStorePath(storePath));

            auto actualPath = state.store->toRealPath(storePath);

            if (state.allowedPaths)
                state.allowedPaths->insert(actualPath);

            return {
                Tree {
                    .actualPath = actualPath,
                    .storePath = std::move(storePath),
                    .info = *treeInfo,
                },
                originalRef
            };
        } catch (Error & e) {
            debug("substitution of input '%s' failed: %s", originalRef, e.what());
        }
    }

    auto resolvedRef = lookupInFlakeCache(flakeCache,
        maybeLookupFlake(state.store,
            lookupInFlakeCache(flakeCache, originalRef), allowLookup));

    auto [tree, lockedRef] = resolvedRef.fetchTree(state.store);

    debug("got tree '%s' from '%s'",
        state.store->printStorePath(tree.storePath), lockedRef);

    flakeCache.push_back({originalRef, lockedRef});
    flakeCache.push_back({resolvedRef, lockedRef});

    if (state.allowedPaths)
        state.allowedPaths->insert(tree.actualPath);

    if (treeInfo)
        assert(tree.storePath == treeInfo->computeStorePath(*state.store));

    return {std::move(tree), lockedRef};
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
        .ref = FlakeRef::fromAttrs({{"type", "indirect"}, {"id", inputName}})
    };

    auto sInputs = state.symbols.create("inputs");
    auto sUrl = state.symbols.create("url");
    auto sUri = state.symbols.create("uri"); // FIXME: remove soon
    auto sFlake = state.symbols.create("flake");
    auto sFollows = state.symbols.create("follows");

    fetchers::Input::Attrs attrs;
    std::optional<std::string> url;

    for (Attr attr : *(value->attrs)) {
        try {
            if (attr.name == sUrl || attr.name == sUri) {
                expectType(state, tString, *attr.value, *attr.pos);
                url = attr.value->string.s;
                attrs.emplace("url", *url);
            } else if (attr.name == sFlake) {
                expectType(state, tBool, *attr.value, *attr.pos);
                input.isFlake = attr.value->boolean;
            } else if (attr.name == sInputs) {
                input.overrides = parseFlakeInputs(state, attr.value, *attr.pos);
            } else if (attr.name == sFollows) {
                expectType(state, tString, *attr.value, *attr.pos);
                input.follows = parseInputPath(attr.value->string.s);
            } else {
                state.forceValue(*attr.value);
                if (attr.value->type == tString)
                    attrs.emplace(attr.name, attr.value->string.s);
                else
                    throw Error("unsupported attribute type");
            }
        } catch (Error & e) {
            e.addPrefix(fmt("in flake attribute '%s' at '%s':\n", attr.name, *attr.pos));
            throw;
        }
    }

    if (attrs.count("type"))
        try {
            input.ref = FlakeRef::fromAttrs(attrs);
        } catch (Error & e) {
            e.addPrefix(fmt("in flake input at '%s':\n", pos));
            throw;
        }
    else {
        attrs.erase("url");
        if (!attrs.empty())
            throw Error("unexpected flake input attribute '%s', at %s", attrs.begin()->first, pos);
        if (url)
            input.ref = parseFlakeRef(*url);
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

static Flake getFlake(
    EvalState & state,
    const FlakeRef & originalRef,
    std::optional<TreeInfo> treeInfo,
    bool allowLookup,
    FlakeCache & flakeCache)
{
    auto [sourceInfo, lockedRef] = fetchOrSubstituteTree(
        state, originalRef, treeInfo, allowLookup, flakeCache);

    // Guard against symlink attacks.
    auto flakeFile = canonPath(sourceInfo.actualPath + "/" + lockedRef.subdir + "/flake.nix");
    if (!isInDir(flakeFile, sourceInfo.actualPath))
        throw Error("'flake.nix' file of flake '%s' escapes from '%s'",
            lockedRef, state.store->printStorePath(sourceInfo.storePath));

    Flake flake {
        .originalRef = originalRef,
        .lockedRef = lockedRef,
        .sourceInfo = std::make_shared<fetchers::Tree>(std::move(sourceInfo))
    };

    if (!pathExists(flakeFile))
        throw Error("source tree referenced by '%s' does not contain a '%s/flake.nix' file", lockedRef, lockedRef.subdir);

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
            throw Error("flake '%s' requires unsupported edition %d; please upgrade Nix", lockedRef, flake.edition);
        if (flake.edition < 201909)
            throw Error("flake '%s' has illegal edition %d", lockedRef, flake.edition);
    } else
        throw Error("flake '%s' lacks attribute 'edition'", lockedRef);

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
        throw Error("flake '%s' lacks attribute 'outputs'", lockedRef);

    for (auto & attr : *vInfo.attrs) {
        if (attr.name != sEdition &&
            attr.name != sEpoch &&
            attr.name != state.sDescription &&
            attr.name != sInputs &&
            attr.name != sOutputs)
            throw Error("flake '%s' has an unsupported attribute '%s', at %s",
                lockedRef, attr.name, *attr.pos);
    }

    return flake;
}

Flake getFlake(EvalState & state, const FlakeRef & originalRef, bool allowLookup)
{
    FlakeCache flakeCache;
    return getFlake(state, originalRef, {}, allowLookup, flakeCache);
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
            res += fmt("  added '%s': '%s'\n", concatStringsSep("/", j->first), j->second->lockedRef);
            ++j;
        } else if (i != oldFlat.end() && (j == newFlat.end() || i->first < j->first)) {
            res += fmt("  removed '%s'\n", concatStringsSep("/", i->first));
            ++i;
        } else {
            if (!(i->second->lockedRef == j->second->lockedRef)) {
                assert(i->second->lockedRef.to_string() != j->second->lockedRef.to_string());
                res += fmt("  updated '%s': '%s' -> '%s'\n",
                    concatStringsSep("/", i->first),
                    i->second->lockedRef,
                    j->second->lockedRef);
            }
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
    const LockFlags & lockFlags)
{
    settings.requireExperimentalFeature("flakes");

    FlakeCache flakeCache;

    auto flake = getFlake(state, topRef, {}, lockFlags.useRegistries, flakeCache);

    LockFile oldLockFile;

    if (!lockFlags.recreateLockFile) {
        // FIXME: symlink attack
        oldLockFile = LockFile::read(
            flake.sourceInfo->actualPath + "/" + flake.lockedRef.subdir + "/flake.lock");
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

        std::vector<FlakeRef> parents;

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

                /* Do we have an entry in the existing lock file? And
                   we don't have a --update-input flag for this
                   input? */
                auto oldLock =
                    lockFlags.inputUpdates.count(inputPath)
                    ? oldLocks.inputs.end()
                    : oldLocks.inputs.find(id);

                if (oldLock != oldLocks.inputs.end() && oldLock->second.originalRef == input.ref && !hasOverride) {
                    /* Copy the input from the old lock file if its
                       flakeref didn't change and there is no override
                       from a higher level flake. */
                    newLocks.inputs.insert_or_assign(id, oldLock->second);

                    /* If we have an --update-input flag for an input
                       of this input, then we must fetch the flake to
                       to update it. */
                    auto lb = lockFlags.inputUpdates.lower_bound(inputPath);

                    auto hasChildUpdate =
                        lb != lockFlags.inputUpdates.end()
                        && lb->size() > inputPath.size()
                        && std::equal(inputPath.begin(), inputPath.end(), lb->begin());

                    if (hasChildUpdate) {
                        auto inputFlake = getFlake(
                            state, oldLock->second.lockedRef, oldLock->second.info, false, flakeCache);

                        updateLocks(inputFlake.inputs,
                            (const LockedInputs &) oldLock->second,
                            newLocks.inputs.find(id)->second,
                            inputPath);

                    } else {
                        /* No need to fetch this flake, we can be
                           lazy. However there may be new overrides on
                           the inputs of this flake, so we need to
                           check those. */
                        FlakeInputs fakeInputs;

                        for (auto & i : oldLock->second.inputs)
                            fakeInputs.emplace(i.first, FlakeInput { .ref = i.second.originalRef });

                        updateLocks(fakeInputs,
                            oldLock->second,
                            newLocks.inputs.find(id)->second,
                            inputPath);
                    }

                } else {
                    /* We need to update/create a new lock file
                       entry. So fetch the flake/non-flake. */

                    if (!lockFlags.allowMutable && !input.ref.input->isImmutable())
                        throw Error("cannot update flake input '%s' in pure mode", inputPathS);

                    if (input.isFlake) {
                        auto inputFlake = getFlake(state, input.ref, {}, lockFlags.useRegistries, flakeCache);

                        newLocks.inputs.insert_or_assign(id,
                            LockedInput(inputFlake.lockedRef, inputFlake.originalRef, inputFlake.sourceInfo->info));

                        /* Recursively process the inputs of this
                           flake. Also, unless we already have this
                           flake in the top-level lock file, use this
                           flake's own lock file. */

                        /* Guard against circular flake imports. */
                        for (auto & parent : parents)
                            if (parent == input.ref)
                                throw Error("found circular import of flake '%s'", parent);
                        parents.push_back(input.ref);
                        Finally cleanup([&]() { parents.pop_back(); });

                        updateLocks(inputFlake.inputs,
                            oldLock != oldLocks.inputs.end()
                            ? (const LockedInputs &) oldLock->second
                            : LockFile::read(
                                inputFlake.sourceInfo->actualPath + "/" + inputFlake.lockedRef.subdir + "/flake.lock"),
                            newLocks.inputs.find(id)->second,
                            inputPath);
                    }

                    else {
                        auto [sourceInfo, lockedRef] = fetchOrSubstituteTree(
                            state, input.ref, {}, lockFlags.useRegistries, flakeCache);
                        newLocks.inputs.insert_or_assign(id,
                            LockedInput(lockedRef, input.ref, sourceInfo.info));
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

        if (lockFlags.writeLockFile) {
            if (auto sourcePath = topRef.input->getSourcePath()) {
                if (!newLockFile.isImmutable()) {
                    if (settings.warnDirty)
                        warn("will not write lock file of flake '%s' because it has a mutable input", topRef);
                } else {
                    if (!lockFlags.updateLockFile)
                        throw Error("flake '%s' requires lock file changes but they're not allowed due to '--no-update-lock-file'", topRef);

                    auto path = *sourcePath + (topRef.subdir == "" ? "" : "/" + topRef.subdir) + "/flake.lock";

                    if (pathExists(path))
                        warn("updating lock file '%s'", path);
                    else
                        warn("creating lock file '%s'", path);

                    newLockFile.write(path);

                    topRef.input->markChangedFile(
                        (topRef.subdir == "" ? "" : topRef.subdir + "/") + "flake.lock");

                    /* Rewriting the lockfile changed the top-level
                       repo, so we should re-read it. FIXME: we could
                       also just clear the 'rev' field... */
                    auto prevLockedRef = flake.lockedRef;
                    FlakeCache dummyCache;
                    flake = getFlake(state, topRef, {}, lockFlags.useRegistries, dummyCache);

                    /* Make sure that we picked up the change,
                       i.e. the tree should usually be dirty
                       now. Corner case: we could have reverted from a
                       dirty to a clean tree! */
                    if (flake.lockedRef.input == prevLockedRef.input
                        && !flake.lockedRef.input->isImmutable())
                        throw Error("'%s' did not change after I updated its 'flake.lock' file; is 'flake.lock' under version control?", flake.originalRef);
                }
            } else
                throw Error("cannot write modified lock file of flake '%s' (use '--no-write-lock-file' to ignore)", topRef);
        } else
            warn("not writing modified lock file of flake '%s'", topRef);
    }

    return LockedFlake { .flake = std::move(flake), .lockFile = std::move(newLockFile) };
}

static void emitSourceInfoAttrs(
    EvalState & state,
    const FlakeRef & flakeRef,
    const fetchers::Tree & sourceInfo,
    Value & vAttrs)
{
    assert(state.store->isValidPath(sourceInfo.storePath));
    auto pathS = state.store->printStorePath(sourceInfo.storePath);
    mkString(*state.allocAttr(vAttrs, state.sOutPath), pathS, {pathS});

    if (auto rev = flakeRef.input->getRev()) {
        mkString(*state.allocAttr(vAttrs, state.symbols.create("rev")),
            rev->gitRev());
        mkString(*state.allocAttr(vAttrs, state.symbols.create("shortRev")),
            rev->gitShortRev());
    }

    if (sourceInfo.info.revCount)
        mkInt(*state.allocAttr(vAttrs, state.symbols.create("revCount")), *sourceInfo.info.revCount);

    if (sourceInfo.info.lastModified)
        mkString(*state.allocAttr(vAttrs, state.symbols.create("lastModified")),
            fmt("%s", std::put_time(std::gmtime(&*sourceInfo.info.lastModified), "%Y%m%d%H%M%S")));
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
        FlakeCache flakeCache;
        auto flake = getFlake(state, lazyInput->lockedInput.lockedRef, lazyInput->lockedInput.info, false, flakeCache);

        if (flake.sourceInfo->info.narHash != lazyInput->lockedInput.info.narHash)
            throw Error("the content hash of flake '%s' (%s) doesn't match the hash recorded in the referring lock file (%s)",
                lazyInput->lockedInput.lockedRef,
                flake.sourceInfo->info.narHash.to_string(SRI),
                lazyInput->lockedInput.info.narHash.to_string(SRI));

        // FIXME: check all the other attributes in lockedInput.info
        // once we've dropped support for lock file version 4.

        assert(flake.sourceInfo->storePath == lazyInput->lockedInput.computeStorePath(*state.store));

        callFlake(state, flake, lazyInput->lockedInput, v);
    } else {
        FlakeCache flakeCache;
        auto [sourceInfo, lockedRef] = fetchOrSubstituteTree(
            state, lazyInput->lockedInput.lockedRef, {}, false, flakeCache);

        if (sourceInfo.info.narHash != lazyInput->lockedInput.info.narHash)
            throw Error("the content hash of repository '%s' (%s) doesn't match the hash recorded in the referring lock file (%s)",
                lazyInput->lockedInput.lockedRef,
                sourceInfo.info.narHash.to_string(SRI),
                lazyInput->lockedInput.info.narHash.to_string(SRI));

        // FIXME: check all the other attributes in lockedInput.info
        // once we've dropped support for lock file version 4.

        assert(sourceInfo.storePath == lazyInput->lockedInput.computeStorePath(*state.store));

        state.mkAttrs(v, 8);

        assert(state.store->isValidPath(sourceInfo.storePath));

        auto pathS = state.store->printStorePath(sourceInfo.storePath);

        mkString(*state.allocAttr(v, state.sOutPath), pathS, {pathS});

        emitSourceInfoAttrs(state, lockedRef, sourceInfo, v);

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
    emitSourceInfoAttrs(state, flake.lockedRef, *flake.sourceInfo, vSourceInfo);
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
    callFlake(state,
        lockFlake(state, parseFlakeRef(state.forceStringNoCtx(*args[0], pos)),
            LockFlags {
                .updateLockFile = false,
                .useRegistries = !evalSettings.pureEval,
                .allowMutable  = !evalSettings.pureEval,
            }),
        v);
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
            flake.sourceInfo->info.revCount.value_or(0),
            flake.sourceInfo->info.lastModified.value_or(0),
            lockFile));
}

Flake::~Flake() { }

}
