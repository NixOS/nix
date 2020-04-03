#include "flake.hh"
#include "lockfile.hh"
#include "primops.hh"
#include "eval-inline.hh"
#include "store-api.hh"
#include "fetchers.hh"
#include "finally.hh"

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

    fetchers::Attrs attrs;
    std::optional<std::string> url;

    for (nix::Attr attr : *(value->attrs)) {
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
                    throw TypeError("flake input attribute '%s' is %s while a string is expected",
                        attr.name, showType(*attr.value));
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

    for (nix::Attr & inputAttr : *(*value).attrs) {
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
        expectType(state, tInt, *edition->value, *edition->pos);
        flake.edition = edition->value->integer;
        if (flake.edition > 201909)
            throw Error("flake '%s' requires unsupported edition %d; please upgrade Nix", lockedRef, flake.edition);
        if (flake.edition < 201909)
            throw Error("flake '%s' has illegal edition %d", lockedRef, flake.edition);
    } else
        throw Error("flake '%s' lacks attribute 'edition'", lockedRef);

    if (auto description = vInfo.attrs->get(state.sDescription)) {
        expectType(state, tString, *description->value, *description->pos);
        flake.description = description->value->string.s;
    }

    auto sInputs = state.symbols.create("inputs");

    if (auto inputs = vInfo.attrs->get(sInputs))
        flake.inputs = parseFlakeInputs(state, inputs->value, *inputs->pos);

    auto sOutputs = state.symbols.create("outputs");

    if (auto outputs = vInfo.attrs->get(sOutputs)) {
        expectType(state, tLambda, *outputs->value, *outputs->pos);
        flake.vOutputs = outputs->value;

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

    // FIXME: symlink attack
    auto oldLockFile = LockFile::read(
        flake.sourceInfo->actualPath + "/" + flake.lockedRef.subdir + "/flake.lock");

    debug("old lock file: %s", oldLockFile);

    // FIXME: check whether all overrides are used.
    std::map<InputPath, FlakeInput> overrides;
    std::set<InputPath> overridesUsed, updatesUsed;

    for (auto & i : lockFlags.inputOverrides)
        overrides.insert_or_assign(i.first, FlakeInput { .ref = i.second });

    LockFile newLockFile;

    std::vector<FlakeRef> parents;
    std::map<InputPath, InputPath> follows;

    std::function<void(
        const FlakeInputs & flakeInputs,
        std::shared_ptr<Node> node,
        const InputPath & inputPathPrefix,
        std::shared_ptr<const Node> oldNode)>
        computeLocks;

    computeLocks = [&](
        const FlakeInputs & flakeInputs,
        std::shared_ptr<Node> node,
        const InputPath & inputPathPrefix,
        std::shared_ptr<const Node> oldNode)
    {
        debug("computing lock file node '%s'", concatStringsSep("/", inputPathPrefix));

        /* Get the overrides (i.e. attributes of the form
           'inputs.nixops.inputs.nixpkgs.url = ...'). */
        // FIXME: check this
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
            debug("computing input '%s'", concatStringsSep("/", inputPath));

            /* Do we have an override for this input from one of the
               ancestors? */
            auto i = overrides.find(inputPath);
            bool hasOverride = i != overrides.end();
            if (hasOverride) overridesUsed.insert(inputPath);
            auto & input = hasOverride ? i->second : input2;

            /* Resolve 'follows' later (since it may refer to an input
               path we haven't processed yet. */
            if (input.follows) {
                if (hasOverride)
                    /* 'follows' from an override is relative to the
                       root of the graph. */
                    follows.insert_or_assign(inputPath, *input.follows);
                else {
                    /* Otherwise, it's relative to the current flake. */
                    InputPath path(inputPathPrefix);
                    for (auto & i : *input.follows) path.push_back(i);
                    follows.insert_or_assign(inputPath, path);
                }
                continue;
            }

            /* Do we have an entry in the existing lock file? And we
               don't have a --update-input flag for this input? */
            std::shared_ptr<const LockedNode> oldLock;

            updatesUsed.insert(inputPath);

            if (oldNode && !lockFlags.inputUpdates.count(inputPath)) {
                auto oldLockIt = oldNode->inputs.find(id);
                if (oldLockIt != oldNode->inputs.end())
                    oldLock = std::dynamic_pointer_cast<const LockedNode>(oldLockIt->second);
            }

            if (oldLock
                && oldLock->originalRef == input.ref
                && !hasOverride)
            {
                debug("keeping existing input '%s'", inputPathS);

                /* Copy the input from the old lock since its flakeref
                   didn't change and there is no override from a
                   higher level flake. */
                auto childNode = std::make_shared<LockedNode>(
                    oldLock->lockedRef, oldLock->originalRef, oldLock->info, oldLock->isFlake);

                node->inputs.insert_or_assign(id, childNode);

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
                        state, oldLock->lockedRef, oldLock->info, false, flakeCache);
                    computeLocks(inputFlake.inputs, childNode, inputPath, oldLock);
                } else {
                    /* No need to fetch this flake, we can be
                       lazy. However there may be new overrides on the
                       inputs of this flake, so we need to check
                       those. */
                    FlakeInputs fakeInputs;

                    for (auto & i : oldLock->inputs) {
                        auto lockedNode = std::dynamic_pointer_cast<LockedNode>(i.second);
                        // Note: this node is not locked in case
                        // of a circular reference back to the root.
                        if (lockedNode)
                            fakeInputs.emplace(i.first, FlakeInput {
                                .ref = lockedNode->originalRef
                            });
                        else {
                            InputPath path(inputPath);
                            path.push_back(i.first);
                            follows.insert_or_assign(path, InputPath());
                        }
                    }

                    computeLocks(fakeInputs, childNode, inputPath, oldLock);
                }

            } else {
                /* We need to create a new lock file entry. So fetch
                   this input. */

                if (!lockFlags.allowMutable && !input.ref.input->isImmutable())
                    throw Error("cannot update flake input '%s' in pure mode", inputPathS);

                if (input.isFlake) {
                    auto inputFlake = getFlake(state, input.ref, {}, lockFlags.useRegistries, flakeCache);

                    auto childNode = std::make_shared<LockedNode>(
                        inputFlake.lockedRef, inputFlake.originalRef, inputFlake.sourceInfo->info);

                    node->inputs.insert_or_assign(id, childNode);

                    /* Guard against circular flake imports. */
                    for (auto & parent : parents)
                        if (parent == input.ref)
                            throw Error("found circular import of flake '%s'", parent);
                    parents.push_back(input.ref);
                    Finally cleanup([&]() { parents.pop_back(); });

                    /* Recursively process the inputs of this
                       flake. Also, unless we already have this flake
                       in the top-level lock file, use this flake's
                       own lock file. */
                    computeLocks(
                        inputFlake.inputs, childNode, inputPath,
                        oldLock
                        ? std::dynamic_pointer_cast<const Node>(oldLock)
                        : LockFile::read(
                            inputFlake.sourceInfo->actualPath + "/" + inputFlake.lockedRef.subdir + "/flake.lock").root);
                }

                else {
                    auto [sourceInfo, lockedRef] = fetchOrSubstituteTree(
                        state, input.ref, {}, lockFlags.useRegistries, flakeCache);
                    node->inputs.insert_or_assign(id,
                        std::make_shared<LockedNode>(lockedRef, input.ref, sourceInfo.info, false));
                }
            }
        }
    };

    computeLocks(
        flake.inputs, newLockFile.root, {},
        lockFlags.recreateLockFile ? nullptr : oldLockFile.root);

    /* Insert edges for 'follows' overrides. */
    for (auto & [from, to] : follows) {
        debug("adding 'follows' node from '%s' to '%s'",
            concatStringsSep("/", from),
            concatStringsSep("/", to));

        assert(!from.empty());

        InputPath fromParent(from);
        fromParent.pop_back();

        auto fromParentNode = newLockFile.root->findInput(fromParent);
        assert(fromParentNode);

        auto toNode = newLockFile.root->findInput(to);
        if (!toNode)
            throw Error("flake input '%s' follows non-existent flake input '%s'",
                concatStringsSep("/", from),
                concatStringsSep("/", to));

        fromParentNode->inputs.insert_or_assign(from.back(), toNode);
    }

    for (auto & i : lockFlags.inputOverrides)
        if (!overridesUsed.count(i.first))
            warn("the flag '--override-input %s %s' does not match any input",
                concatStringsSep("/", i.first), i.second);

    for (auto & i : lockFlags.inputUpdates)
        if (!updatesUsed.count(i))
            warn("the flag '--update-input %s' does not match any input", concatStringsSep("/", i));

    debug("new lock file: %s", newLockFile);

    /* Check whether we need to / can write the new lock file. */
    if (!(newLockFile == oldLockFile)) {

        auto diff = diffLockFiles(oldLockFile, newLockFile);

        if (!(oldLockFile == LockFile()))
            printInfo("inputs of flake '%s' changed:\n%s", topRef, chomp(diff));

        if (lockFlags.writeLockFile) {
            if (auto sourcePath = topRef.input->getSourcePath()) {
                if (!newLockFile.isImmutable()) {
                    if (settings.warnDirty)
                        warn("will not write lock file of flake '%s' because it has a mutable input", topRef);
                } else {
                    if (!lockFlags.updateLockFile)
                        throw Error("flake '%s' requires lock file changes but they're not allowed due to '--no-update-lock-file'", topRef);

                    auto relPath = (topRef.subdir == "" ? "" : topRef.subdir + "/") + "flake.lock";

                    auto path = *sourcePath + "/" + relPath;

                    bool lockFileExists = pathExists(path);

                    if (lockFileExists)
                        warn("updating lock file '%s'", path);
                    else
                        warn("creating lock file '%s'", path);

                    newLockFile.write(path);

                    topRef.input->markChangedFile(
                        (topRef.subdir == "" ? "" : topRef.subdir + "/") + "flake.lock",
                        lockFlags.commitLockFile
                        ? std::optional<std::string>(fmt("%s: %s\n\nFlake input changes:\n\n%s",
                                relPath, lockFileExists ? "Update" : "Add", diff))
                        : std::nullopt);

                    /* Rewriting the lockfile changed the top-level
                       repo, so we should re-read it. FIXME: we could
                       also just clear the 'rev' field... */
                    auto prevLockedRef = flake.lockedRef;
                    FlakeCache dummyCache;
                    flake = getFlake(state, topRef, {}, lockFlags.useRegistries, dummyCache);

                    if (lockFlags.commitLockFile &&
                        flake.lockedRef.input->getRev() &&
                        prevLockedRef.input->getRev() != flake.lockedRef.input->getRev())
                        warn("committed new revision '%s'", flake.lockedRef.input->getRev()->gitRev());

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

void callFlake(EvalState & state,
    const LockedFlake & lockedFlake,
    Value & vRes)
{
    auto vLocks = state.allocValue();
    auto vRootSrc = state.allocValue();
    auto vRootSubdir = state.allocValue();
    auto vTmp1 = state.allocValue();
    auto vTmp2 = state.allocValue();

    mkString(*vLocks, lockedFlake.lockFile.to_string());

    emitTreeAttrs(state, *lockedFlake.flake.sourceInfo, lockedFlake.flake.lockedRef.input, *vRootSrc);

    mkString(*vRootSubdir, lockedFlake.flake.lockedRef.subdir);

    static Value * vCallFlake = nullptr;

    if (!vCallFlake) {
        vCallFlake = state.allocValue();
        state.eval(state.parseExprFromString(
            #include "call-flake.nix.gen.hh"
            , "/"), *vCallFlake);
    }

    state.callFunction(*vCallFlake, *vLocks, *vTmp1, noPos);
    state.callFunction(*vTmp1, *vRootSrc, *vTmp2, noPos);
    state.callFunction(*vTmp2, *vRootSubdir, vRes, noPos);
}

static void prim_getFlake(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    auto flakeRefS = state.forceStringNoCtx(*args[0], pos);
    auto flakeRef = parseFlakeRef(flakeRefS);
    if (evalSettings.pureEval && !flakeRef.input->isImmutable())
        throw Error("cannot call 'getFlake' on mutable flake reference '%s', at %s (use --impure to override)", flakeRefS, pos);

    callFlake(state,
        lockFlake(state, flakeRef,
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
