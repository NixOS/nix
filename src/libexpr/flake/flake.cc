#include "flake.hh"
#include "eval.hh"
#include "lockfile.hh"
#include "primops.hh"
#include "eval-inline.hh"
#include "store-api.hh"
#include "fetchers.hh"
#include "finally.hh"

namespace nix {

using namespace flake;

namespace flake {

typedef std::pair<fetchers::Tree, FlakeRef> FetchedFlake;
typedef std::vector<std::pair<FlakeRef, FetchedFlake>> FlakeCache;

static std::optional<FetchedFlake> lookupInFlakeCache(
    const FlakeCache & flakeCache,
    const FlakeRef & flakeRef)
{
    // FIXME: inefficient.
    for (auto & i : flakeCache) {
        if (flakeRef == i.first) {
            debug("mapping '%s' to previously seen input '%s' -> '%s",
                flakeRef, i.first, i.second.second);
            return i.second;
        }
    }

    return std::nullopt;
}

static std::tuple<fetchers::Tree, FlakeRef, FlakeRef> fetchOrSubstituteTree(
    EvalState & state,
    const FlakeRef & originalRef,
    bool allowLookup,
    FlakeCache & flakeCache)
{
    auto fetched = lookupInFlakeCache(flakeCache, originalRef);
    FlakeRef resolvedRef = originalRef;

    if (!fetched) {
        if (originalRef.input.isDirect()) {
            fetched.emplace(originalRef.fetchTree(state.store));
        } else {
            if (allowLookup) {
                resolvedRef = originalRef.resolve(state.store);
                auto fetchedResolved = lookupInFlakeCache(flakeCache, originalRef);
                if (!fetchedResolved) fetchedResolved.emplace(resolvedRef.fetchTree(state.store));
                flakeCache.push_back({resolvedRef, *fetchedResolved});
                fetched.emplace(*fetchedResolved);
            }
            else {
                throw Error("'%s' is an indirect flake reference, but registry lookups are not allowed", originalRef);
            }
        }
        flakeCache.push_back({originalRef, *fetched});
    }

    auto [tree, lockedRef] = *fetched;

    debug("got tree '%s' from '%s'",
        state.store->printStorePath(tree.storePath), lockedRef);

    if (state.allowedPaths)
        state.allowedPaths->insert(tree.actualPath);

    assert(!originalRef.input.getNarHash() || tree.storePath == originalRef.input.computeStorePath(*state.store));

    return {std::move(tree), resolvedRef, lockedRef};
}

static void forceTrivialValue(EvalState & state, Value & value, const Pos & pos)
{
    if (value.isThunk() && value.isTrivial())
        state.forceValue(value, pos);
}


static void expectType(EvalState & state, ValueType type,
    Value & value, const Pos & pos)
{
    forceTrivialValue(state, value, pos);
    if (value.type() != type)
        throw Error("expected %s but got %s at %s",
            showType(type), showType(value.type()), pos);
}

static std::map<FlakeId, FlakeInput> parseFlakeInputs(
    EvalState & state, Value * value, const Pos & pos);

static FlakeInput parseFlakeInput(EvalState & state,
    const std::string & inputName, Value * value, const Pos & pos)
{
    expectType(state, nAttrs, *value, pos);

    FlakeInput input;

    auto sInputs = state.symbols.create("inputs");
    auto sUrl = state.symbols.create("url");
    auto sFlake = state.symbols.create("flake");
    auto sFollows = state.symbols.create("follows");

    fetchers::Attrs attrs;
    std::optional<std::string> url;

    for (nix::Attr attr : *(value->attrs)) {
        try {
            if (attr.name == sUrl) {
                expectType(state, nString, *attr.value, *attr.pos);
                url = attr.value->string.s;
                attrs.emplace("url", *url);
            } else if (attr.name == sFlake) {
                expectType(state, nBool, *attr.value, *attr.pos);
                input.isFlake = attr.value->boolean;
            } else if (attr.name == sInputs) {
                input.overrides = parseFlakeInputs(state, attr.value, *attr.pos);
            } else if (attr.name == sFollows) {
                expectType(state, nString, *attr.value, *attr.pos);
                input.follows = parseInputPath(attr.value->string.s);
            } else {
                switch (attr.value->type()) {
                    case nString:
                        attrs.emplace(attr.name, attr.value->string.s);
                        break;
                    case nBool:
                        attrs.emplace(attr.name, Explicit<bool> { attr.value->boolean });
                        break;
                    case nInt:
                        attrs.emplace(attr.name, (long unsigned int)attr.value->integer);
                        break;
                    default:
                        throw TypeError("flake input attribute '%s' is %s while a string, Boolean, or integer is expected",
                            attr.name, showType(*attr.value));
                }
            }
        } catch (Error & e) {
            e.addTrace(*attr.pos, hintfmt("in flake attribute '%s'", attr.name));
            throw;
        }
    }

    if (attrs.count("type"))
        try {
            input.ref = FlakeRef::fromAttrs(attrs);
        } catch (Error & e) {
            e.addTrace(pos, hintfmt("in flake input"));
            throw;
        }
    else {
        attrs.erase("url");
        if (!attrs.empty())
            throw Error("unexpected flake input attribute '%s', at %s", attrs.begin()->first, pos);
        if (url)
            input.ref = parseFlakeRef(*url, {}, true);
    }

    if (!input.follows && !input.ref)
        input.ref = FlakeRef::fromAttrs({{"type", "indirect"}, {"id", inputName}});

    return input;
}

static std::map<FlakeId, FlakeInput> parseFlakeInputs(
    EvalState & state, Value * value, const Pos & pos)
{
    std::map<FlakeId, FlakeInput> inputs;

    expectType(state, nAttrs, *value, pos);

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
    bool allowLookup,
    FlakeCache & flakeCache)
{
    auto [sourceInfo, resolvedRef, lockedRef] = fetchOrSubstituteTree(
        state, originalRef, allowLookup, flakeCache);

    // Guard against symlink attacks.
    auto flakeFile = canonPath(sourceInfo.actualPath + "/" + lockedRef.subdir + "/flake.nix");
    if (!isInDir(flakeFile, sourceInfo.actualPath))
        throw Error("'flake.nix' file of flake '%s' escapes from '%s'",
            lockedRef, state.store->printStorePath(sourceInfo.storePath));

    Flake flake {
        .originalRef = originalRef,
        .resolvedRef = resolvedRef,
        .lockedRef = lockedRef,
        .sourceInfo = std::make_shared<fetchers::Tree>(std::move(sourceInfo))
    };

    if (!pathExists(flakeFile))
        throw Error("source tree referenced by '%s' does not contain a '%s/flake.nix' file", lockedRef, lockedRef.subdir);

    Value vInfo;
    state.evalFile(flakeFile, vInfo, true); // FIXME: symlink attack

    expectType(state, nAttrs, vInfo, Pos(foFile, state.symbols.create(flakeFile), 0, 0));

    if (auto description = vInfo.attrs->get(state.sDescription)) {
        expectType(state, nString, *description->value, *description->pos);
        flake.description = description->value->string.s;
    }

    auto sInputs = state.symbols.create("inputs");

    if (auto inputs = vInfo.attrs->get(sInputs))
        flake.inputs = parseFlakeInputs(state, inputs->value, *inputs->pos);

    auto sOutputs = state.symbols.create("outputs");

    if (auto outputs = vInfo.attrs->get(sOutputs)) {
        expectType(state, nFunction, *outputs->value, *outputs->pos);

        if (outputs->value->isLambda() && outputs->value->lambda.fun->matchAttrs) {
            for (auto & formal : outputs->value->lambda.fun->formals->formals) {
                if (formal.name != state.sSelf)
                    flake.inputs.emplace(formal.name, FlakeInput {
                        .ref = parseFlakeRef(formal.name)
                    });
            }
        }

    } else
        throw Error("flake '%s' lacks attribute 'outputs'", lockedRef);

    auto sNixConfig = state.symbols.create("nixConfig");

    if (auto nixConfig = vInfo.attrs->get(sNixConfig)) {
        expectType(state, nAttrs, *nixConfig->value, *nixConfig->pos);

        for (auto & setting : *nixConfig->value->attrs) {
            forceTrivialValue(state, *setting.value, *setting.pos);
            if (setting.value->type() == nString)
                flake.config.settings.insert({setting.name, state.forceStringNoCtx(*setting.value, *setting.pos)});
            else if (setting.value->type() == nInt)
                flake.config.settings.insert({setting.name, state.forceInt(*setting.value, *setting.pos)});
            else if (setting.value->type() == nBool)
                flake.config.settings.insert({setting.name, state.forceBool(*setting.value, *setting.pos)});
            else if (setting.value->type() == nList) {
                std::vector<std::string> ss;
                for (unsigned int n = 0; n < setting.value->listSize(); ++n) {
                    auto elem = setting.value->listElems()[n];
                    if (elem->type() != nString)
                        throw TypeError("list element in flake configuration setting '%s' is %s while a string is expected",
                            setting.name, showType(*setting.value));
                    ss.push_back(state.forceStringNoCtx(*elem, *setting.pos));
                }
                flake.config.settings.insert({setting.name, ss});
            }
            else
                throw TypeError("flake configuration setting '%s' is %s",
                    setting.name, showType(*setting.value));
        }
    }

    for (auto & attr : *vInfo.attrs) {
        if (attr.name != state.sDescription &&
            attr.name != sInputs &&
            attr.name != sOutputs &&
            attr.name != sNixConfig)
            throw Error("flake '%s' has an unsupported attribute '%s', at %s",
                lockedRef, attr.name, *attr.pos);
    }

    return flake;
}

Flake getFlake(EvalState & state, const FlakeRef & originalRef, bool allowLookup)
{
    FlakeCache flakeCache;
    return getFlake(state, originalRef, allowLookup, flakeCache);
}

/* Compute an in-memory lock file for the specified top-level flake,
   and optionally write it to file, if the flake is writable. */
LockedFlake lockFlake(
    EvalState & state,
    const FlakeRef & topRef,
    const LockFlags & lockFlags)
{
    settings.requireExperimentalFeature("flakes");

    FlakeCache flakeCache;

    auto useRegistries = lockFlags.useRegistries.value_or(settings.useRegistries);

    auto flake = getFlake(state, topRef, useRegistries, flakeCache);

    if (lockFlags.applyNixConfig) {
        flake.config.apply();
        // FIXME: send new config to the daemon.
    }

    try {

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

        struct LockParent {
            /* The path to this parent. */
            InputPath path;

            /* Whether we are currently inside a top-level lockfile
               (inputs absolute) or subordinate lockfile (inputs
               relative). */
            bool absolute;
        };

        std::function<void(
            const FlakeInputs & flakeInputs,
            std::shared_ptr<Node> node,
            const InputPath & inputPathPrefix,
            std::shared_ptr<const Node> oldNode,
            const LockParent & parent,
            const Path & parentPath)>
            computeLocks;

        computeLocks = [&](
            const FlakeInputs & flakeInputs,
            std::shared_ptr<Node> node,
            const InputPath & inputPathPrefix,
            std::shared_ptr<const Node> oldNode,
            const LockParent & parent,
            const Path & parentPath)
        {
            debug("computing lock file node '%s'", printInputPath(inputPathPrefix));

            /* Get the overrides (i.e. attributes of the form
               'inputs.nixops.inputs.nixpkgs.url = ...'). */
            for (auto & [id, input] : flakeInputs) {
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
                auto inputPathS = printInputPath(inputPath);
                debug("computing input '%s'", inputPathS);

                try {

                    /* Do we have an override for this input from one of the
                       ancestors? */
                    auto i = overrides.find(inputPath);
                    bool hasOverride = i != overrides.end();
                    if (hasOverride) {
                        overridesUsed.insert(inputPath);
                        // Respect the “flakeness” of the input even if we
                        // override it
                        i->second.isFlake = input2.isFlake;
                    }
                    auto & input = hasOverride ? i->second : input2;

                    /* Resolve 'follows' later (since it may refer to an input
                       path we haven't processed yet. */
                    if (input.follows) {
                        InputPath target;

                        if (parent.absolute && !hasOverride) {
                            target = *input.follows;
                        } else {
                            if (hasOverride) {
                                target = inputPathPrefix;
                                target.pop_back();
                            } else
                                target = parent.path;

                            for (auto & i : *input.follows) target.push_back(i);
                        }

                        debug("input '%s' follows '%s'", inputPathS, printInputPath(target));
                        node->inputs.insert_or_assign(id, target);
                        continue;
                    }

                    assert(input.ref);

                    /* Do we have an entry in the existing lock file? And we
                       don't have a --update-input flag for this input? */
                    std::shared_ptr<LockedNode> oldLock;

                    updatesUsed.insert(inputPath);

                    if (oldNode && !lockFlags.inputUpdates.count(inputPath))
                        if (auto oldLock2 = get(oldNode->inputs, id))
                            if (auto oldLock3 = std::get_if<0>(&*oldLock2))
                                oldLock = *oldLock3;

                    if (oldLock
                        && oldLock->originalRef == *input.ref
                        && !hasOverride)
                    {
                        debug("keeping existing input '%s'", inputPathS);

                        /* Copy the input from the old lock since its flakeref
                           didn't change and there is no override from a
                           higher level flake. */
                        auto childNode = std::make_shared<LockedNode>(
                            oldLock->lockedRef, oldLock->originalRef, oldLock->isFlake);

                        node->inputs.insert_or_assign(id, childNode);

                        /* If we have an --update-input flag for an input
                           of this input, then we must fetch the flake to
                           update it. */
                        auto lb = lockFlags.inputUpdates.lower_bound(inputPath);

                        auto hasChildUpdate =
                            lb != lockFlags.inputUpdates.end()
                            && lb->size() > inputPath.size()
                            && std::equal(inputPath.begin(), inputPath.end(), lb->begin());

                        if (hasChildUpdate) {
                            auto inputFlake = getFlake(
                                state, oldLock->lockedRef, false, flakeCache);
                            computeLocks(inputFlake.inputs, childNode, inputPath, oldLock, parent, parentPath);
                        } else {
                            /* No need to fetch this flake, we can be
                               lazy. However there may be new overrides on the
                               inputs of this flake, so we need to check
                               those. */
                            FlakeInputs fakeInputs;

                            for (auto & i : oldLock->inputs) {
                                if (auto lockedNode = std::get_if<0>(&i.second)) {
                                    fakeInputs.emplace(i.first, FlakeInput {
                                        .ref = (*lockedNode)->originalRef,
                                        .isFlake = (*lockedNode)->isFlake,
                                    });
                                } else if (auto follows = std::get_if<1>(&i.second)) {
                                    fakeInputs.emplace(i.first, FlakeInput {
                                        .follows = *follows,
                                    });
                                }
                            }

                            computeLocks(fakeInputs, childNode, inputPath, oldLock, parent, parentPath);
                        }

                    } else {
                        /* We need to create a new lock file entry. So fetch
                           this input. */
                        debug("creating new input '%s'", inputPathS);

                        if (!lockFlags.allowMutable && !input.ref->input.isImmutable())
                            throw Error("cannot update flake input '%s' in pure mode", inputPathS);

                        if (input.isFlake) {
                            Path localPath = parentPath;
                            FlakeRef localRef = *input.ref;

                            // If this input is a path, recurse it down.
                            // This allows us to resolve path inputs relative to the current flake.
                            if (localRef.input.getType() == "path") {
                                localRef.input.parent = parentPath;
                                localPath = canonPath(parentPath + "/" + *input.ref->input.getSourcePath());
                            }

                            auto inputFlake = getFlake(state, localRef, useRegistries, flakeCache);

                            /* Note: in case of an --override-input, we use
                               the *original* ref (input2.ref) for the
                               "original" field, rather than the
                               override. This ensures that the override isn't
                               nuked the next time we update the lock
                               file. That is, overrides are sticky unless you
                               use --no-write-lock-file. */
                            auto childNode = std::make_shared<LockedNode>(
                                inputFlake.lockedRef, input2.ref ? *input2.ref : *input.ref);

                            node->inputs.insert_or_assign(id, childNode);

                            /* Guard against circular flake imports. */
                            for (auto & parent : parents)
                                if (parent == *input.ref)
                                    throw Error("found circular import of flake '%s'", parent);
                            parents.push_back(*input.ref);
                            Finally cleanup([&]() { parents.pop_back(); });

                            // Follows paths from existing inputs in the top-level lockfile are absolute,
                            // whereas paths in subordinate lockfiles are relative to those lockfiles.
                            LockParent newParent {
                                .path = inputPath,
                                .absolute = oldLock ? true : false
                            };

                            /* Recursively process the inputs of this
                               flake. Also, unless we already have this flake
                               in the top-level lock file, use this flake's
                               own lock file. */
                            computeLocks(
                                inputFlake.inputs, childNode, inputPath,
                                oldLock
                                ? std::dynamic_pointer_cast<const Node>(oldLock)
                                : LockFile::read(
                                    inputFlake.sourceInfo->actualPath + "/" + inputFlake.lockedRef.subdir + "/flake.lock").root,
                                newParent, localPath);
                        }

                        else {
                            auto [sourceInfo, resolvedRef, lockedRef] = fetchOrSubstituteTree(
                                state, *input.ref, useRegistries, flakeCache);
                            node->inputs.insert_or_assign(id,
                                std::make_shared<LockedNode>(lockedRef, *input.ref, false));
                        }
                    }

                } catch (Error & e) {
                    e.addTrace({}, "while updating the flake input '%s'", inputPathS);
                    throw;
                }
            }
        };

        LockParent parent {
            .path = {},
            .absolute = true
        };

        // Bring in the current ref for relative path resolution if we have it
        auto parentPath = canonPath(flake.sourceInfo->actualPath + "/" + flake.lockedRef.subdir);

        computeLocks(
            flake.inputs, newLockFile.root, {},
            lockFlags.recreateLockFile ? nullptr : oldLockFile.root, parent, parentPath);

        for (auto & i : lockFlags.inputOverrides)
            if (!overridesUsed.count(i.first))
                warn("the flag '--override-input %s %s' does not match any input",
                    printInputPath(i.first), i.second);

        for (auto & i : lockFlags.inputUpdates)
            if (!updatesUsed.count(i))
                warn("the flag '--update-input %s' does not match any input", printInputPath(i));

        /* Check 'follows' inputs. */
        newLockFile.check();

        debug("new lock file: %s", newLockFile);

        /* Check whether we need to / can write the new lock file. */
        if (!(newLockFile == oldLockFile)) {

            auto diff = LockFile::diff(oldLockFile, newLockFile);

            if (lockFlags.writeLockFile) {
                if (auto sourcePath = topRef.input.getSourcePath()) {
                    if (!newLockFile.isImmutable()) {
                        if (settings.warnDirty)
                            warn("will not write lock file of flake '%s' because it has a mutable input", topRef);
                    } else {
                        if (!lockFlags.updateLockFile)
                            throw Error("flake '%s' requires lock file changes but they're not allowed due to '--no-update-lock-file'", topRef);

                        auto relPath = (topRef.subdir == "" ? "" : topRef.subdir + "/") + "flake.lock";

                        auto path = *sourcePath + "/" + relPath;

                        bool lockFileExists = pathExists(path);

                        if (lockFileExists) {
                            auto s = chomp(diff);
                            if (s.empty())
                                warn("updating lock file '%s'", path);
                            else
                                warn("updating lock file '%s':\n%s", path, s);
                        } else
                            warn("creating lock file '%s'", path);

                        newLockFile.write(path);

                        topRef.input.markChangedFile(
                            (topRef.subdir == "" ? "" : topRef.subdir + "/") + "flake.lock",
                            lockFlags.commitLockFile
                            ? std::optional<std::string>(fmt("%s: %s\n\nFlake lock file changes:\n\n%s",
                                    relPath, lockFileExists ? "Update" : "Add", filterANSIEscapes(diff, true)))
                            : std::nullopt);

                        /* Rewriting the lockfile changed the top-level
                           repo, so we should re-read it. FIXME: we could
                           also just clear the 'rev' field... */
                        auto prevLockedRef = flake.lockedRef;
                        FlakeCache dummyCache;
                        flake = getFlake(state, topRef, useRegistries, dummyCache);

                        if (lockFlags.commitLockFile &&
                            flake.lockedRef.input.getRev() &&
                            prevLockedRef.input.getRev() != flake.lockedRef.input.getRev())
                            warn("committed new revision '%s'", flake.lockedRef.input.getRev()->gitRev());

                        /* Make sure that we picked up the change,
                           i.e. the tree should usually be dirty
                           now. Corner case: we could have reverted from a
                           dirty to a clean tree! */
                        if (flake.lockedRef.input == prevLockedRef.input
                            && !flake.lockedRef.input.isImmutable())
                            throw Error("'%s' did not change after I updated its 'flake.lock' file; is 'flake.lock' under version control?", flake.originalRef);
                    }
                } else
                    throw Error("cannot write modified lock file of flake '%s' (use '--no-write-lock-file' to ignore)", topRef);
            } else
                warn("not writing modified lock file of flake '%s':\n%s", topRef, chomp(diff));
        }

        return LockedFlake { .flake = std::move(flake), .lockFile = std::move(newLockFile) };

    } catch (Error & e) {
        e.addTrace({}, "while updating the lock file of flake '%s'", flake.lockedRef.to_string());
        throw;
    }
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

    if (!state.vCallFlake) {
        state.vCallFlake = allocRootValue(state.allocValue());
        state.eval(state.parseExprFromString(
            #include "call-flake.nix.gen.hh"
            , "/"), **state.vCallFlake);
    }

    state.callFunction(**state.vCallFlake, *vLocks, *vTmp1, noPos);
    state.callFunction(*vTmp1, *vRootSrc, *vTmp2, noPos);
    state.callFunction(*vTmp2, *vRootSubdir, vRes, noPos);
}

static void prim_getFlake(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    auto flakeRefS = state.forceStringNoCtx(*args[0], pos);
    auto flakeRef = parseFlakeRef(flakeRefS, {}, true);
    if (evalSettings.pureEval && !flakeRef.input.isImmutable())
        throw Error("cannot call 'getFlake' on mutable flake reference '%s', at %s (use --impure to override)", flakeRefS, pos);

    callFlake(state,
        lockFlake(state, flakeRef,
            LockFlags {
                .updateLockFile = false,
                .useRegistries = !evalSettings.pureEval && !settings.useRegistries,
                .allowMutable  = !evalSettings.pureEval,
            }),
        v);
}

static RegisterPrimOp r2("__getFlake", 1, prim_getFlake, "flakes");

}

Fingerprint LockedFlake::getFingerprint() const
{
    // FIXME: as an optimization, if the flake contains a lock file
    // and we haven't changed it, then it's sufficient to use
    // flake.sourceInfo.storePath for the fingerprint.
    return hashString(htSHA256,
        fmt("%s;%d;%d;%s",
            flake.sourceInfo->storePath.to_string(),
            flake.lockedRef.input.getRevCount().value_or(0),
            flake.lockedRef.input.getLastModified().value_or(0),
            lockFile));
}

Flake::~Flake() { }

}
