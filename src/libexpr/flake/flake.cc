#include "terminal.hh"
#include "flake.hh"
#include "eval.hh"
#include "eval-settings.hh"
#include "lockfile.hh"
#include "primops.hh"
#include "eval-inline.hh"
#include "store-api.hh"
#include "fetchers.hh"
#include "finally.hh"
#include "fetch-settings.hh"
#include "value-to-json.hh"
#include "local-fs-store.hh"

namespace nix {

using namespace flake;

namespace flake {

static void forceTrivialValue(EvalState & state, Value & value, const PosIdx pos)
{
    if (value.isThunk() && value.isTrivial())
        state.forceValue(value, pos);
}

static void expectType(EvalState & state, ValueType type,
    Value & value, const PosIdx pos)
{
    forceTrivialValue(state, value, pos);
    if (value.type() != type)
        throw Error("expected %s but got %s at %s",
            showType(type), showType(value.type()), state.positions[pos]);
}

static std::map<FlakeId, FlakeInput> parseFlakeInputs(
    EvalState & state,
    Value * value,
    const PosIdx pos,
    const InputPath & lockRootPath,
    const SourcePath & flakePath);

static FlakeInput parseFlakeInput(
    EvalState & state,
    const std::string & inputName,
    Value * value,
    const PosIdx pos,
    const InputPath & lockRootPath,
    const SourcePath & flakePath)
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
                expectType(state, nString, *attr.value, attr.pos);
                url = attr.value->string_view();
                attrs.emplace("url", *url);
            } else if (attr.name == sFlake) {
                expectType(state, nBool, *attr.value, attr.pos);
                input.isFlake = attr.value->boolean;
            } else if (attr.name == sInputs) {
                input.overrides = parseFlakeInputs(state, attr.value, attr.pos, lockRootPath, flakePath);
            } else if (attr.name == sFollows) {
                expectType(state, nString, *attr.value, attr.pos);
                auto follows(parseInputPath(attr.value->c_str()));
                follows.insert(follows.begin(), lockRootPath.begin(), lockRootPath.end());
                input.follows = follows;
            } else {
                // Allow selecting a subset of enum values
                #pragma GCC diagnostic push
                #pragma GCC diagnostic ignored "-Wswitch-enum"
                switch (attr.value->type()) {
                    case nString:
                        attrs.emplace(state.symbols[attr.name], attr.value->c_str());
                        break;
                    case nBool:
                        attrs.emplace(state.symbols[attr.name], Explicit<bool> { attr.value->boolean });
                        break;
                    case nInt:
                        attrs.emplace(state.symbols[attr.name], (long unsigned int) attr.value->integer);
                        break;
                    default:
                        if (attr.name == state.symbols.create("publicKeys")) {
                            experimentalFeatureSettings.require(Xp::VerifiedFetches);
                            NixStringContext emptyContext = {};
                            attrs.emplace(state.symbols[attr.name], printValueAsJSON(state, true, *attr.value, pos, emptyContext).dump());
                        } else
                            state.error<TypeError>("flake input attribute '%s' is %s while a string, Boolean, or integer is expected",
                                state.symbols[attr.name], showType(*attr.value)).debugThrow();
                }
                #pragma GCC diagnostic pop
            }
        } catch (Error & e) {
            e.addTrace(
                state.positions[attr.pos],
                HintFmt("while evaluating flake attribute '%s'", state.symbols[attr.name]));
            throw;
        }
    }

    if (attrs.count("type"))
        try {
            input.ref = FlakeRef::fromAttrs(attrs);
        } catch (Error & e) {
            e.addTrace(state.positions[pos], HintFmt("while evaluating flake input"));
            throw;
        }
    else {
        attrs.erase("url");
        if (!attrs.empty())
            throw Error("unexpected flake input attribute '%s', at %s", attrs.begin()->first, state.positions[pos]);
        if (url)
            input.ref = parseFlakeRef(*url, {}, true, input.isFlake);
    }

    if (!input.follows && !input.ref)
        input.ref = FlakeRef::fromAttrs({{"type", "indirect"}, {"id", inputName}});

    return input;
}

static std::map<FlakeId, FlakeInput> parseFlakeInputs(
    EvalState & state,
    Value * value,
    const PosIdx pos,
    const InputPath & lockRootPath,
    const SourcePath & flakePath)
{
    std::map<FlakeId, FlakeInput> inputs;

    expectType(state, nAttrs, *value, pos);

    for (nix::Attr & inputAttr : *(*value).attrs) {
        inputs.emplace(state.symbols[inputAttr.name],
            parseFlakeInput(state,
                state.symbols[inputAttr.name],
                inputAttr.value,
                inputAttr.pos,
                lockRootPath,
                flakePath));
    }

    return inputs;
}

static Flake readFlake(
    EvalState & state,
    const FlakeRef & originalRef,
    const FlakeRef & resolvedRef,
    const FlakeRef & lockedRef,
    const SourcePath & rootDir,
    const InputPath & lockRootPath)
{
    auto flakePath = rootDir / CanonPath(resolvedRef.subdir) / "flake.nix";

    Value vInfo;
    state.evalFile(flakePath, vInfo, true);

    // FIXME: unclear how to port this from lazy-trees
    // expectType(state, nAttrs, vInfo, state.positions.add(Pos::Origin(rootDir), 1, 1));

    Flake flake {
        .originalRef = originalRef,
        .resolvedRef = resolvedRef,
        .lockedRef = lockedRef,
        .path = flakePath,
    };

    if (auto description = vInfo.attrs->get(state.sDescription)) {
        expectType(state, nString, *description->value, description->pos);
        flake.description = description->value->c_str();
    }

    auto sInputs = state.symbols.create("inputs");

    if (auto inputs = vInfo.attrs->get(sInputs))
        flake.inputs = parseFlakeInputs(state, inputs->value, inputs->pos, lockRootPath, flakePath);

    auto sOutputs = state.symbols.create("outputs");

    if (auto outputs = vInfo.attrs->get(sOutputs)) {
        expectType(state, nFunction, *outputs->value, outputs->pos);

        if (outputs->value->isLambda() && outputs->value->lambda.fun->hasFormals()) {
            for (auto & formal : outputs->value->lambda.fun->formals->formals) {
                if (formal.name != state.sSelf)
                    flake.inputs.emplace(state.symbols[formal.name], FlakeInput {
                        .ref = parseFlakeRef(state.symbols[formal.name])
                    });
            }
        }

    } else
        throw Error("flake '%s' lacks attribute 'outputs'", resolvedRef);

    auto sNixConfig = state.symbols.create("nixConfig");

    if (auto nixConfig = vInfo.attrs->get(sNixConfig)) {
        expectType(state, nAttrs, *nixConfig->value, nixConfig->pos);

        for (auto & setting : *nixConfig->value->attrs) {
            forceTrivialValue(state, *setting.value, setting.pos);
            if (setting.value->type() == nString)
                flake.config.settings.emplace(
                    state.symbols[setting.name],
                    std::string(state.forceStringNoCtx(*setting.value, setting.pos, "")));
            else if (setting.value->type() == nPath) {
                NixStringContext emptyContext = {};
                flake.config.settings.emplace(
                    state.symbols[setting.name],
                    state.coerceToString(setting.pos, *setting.value, emptyContext, "", false, true).toOwned());
            }
            else if (setting.value->type() == nInt)
                flake.config.settings.emplace(
                    state.symbols[setting.name],
                    state.forceInt(*setting.value, setting.pos, ""));
            else if (setting.value->type() == nBool)
                flake.config.settings.emplace(
                    state.symbols[setting.name],
                    Explicit<bool> { state.forceBool(*setting.value, setting.pos, "") });
            else if (setting.value->type() == nList) {
                std::vector<std::string> ss;
                for (auto elem : setting.value->listItems()) {
                    if (elem->type() != nString)
                        state.error<TypeError>("list element in flake configuration setting '%s' is %s while a string is expected",
                            state.symbols[setting.name], showType(*setting.value)).debugThrow();
                    ss.emplace_back(state.forceStringNoCtx(*elem, setting.pos, ""));
                }
                flake.config.settings.emplace(state.symbols[setting.name], ss);
            }
            else
                state.error<TypeError>("flake configuration setting '%s' is %s",
                    state.symbols[setting.name], showType(*setting.value)).debugThrow();
        }
    }

    for (auto & attr : *vInfo.attrs) {
        if (attr.name != state.sDescription &&
            attr.name != sInputs &&
            attr.name != sOutputs &&
            attr.name != sNixConfig)
            throw Error("flake '%s' has an unsupported attribute '%s', at %s",
                resolvedRef, state.symbols[attr.name], state.positions[attr.pos]);
    }

    return flake;
}

static FlakeRef maybeResolve(
    EvalState & state,
    const FlakeRef & originalRef,
    bool useRegistries)
{
    if (!originalRef.input.isDirect()) {
        if (!useRegistries)
            throw Error("'%s' is an indirect flake reference, but registry lookups are not allowed", originalRef);
        return originalRef.resolve(state.store);
    } else
        return originalRef;
}

static Flake getFlake(
    EvalState & state,
    const FlakeRef & originalRef,
    bool useRegistries,
    const InputPath & lockRootPath,
    const std::vector<std::string> & patches)
{
    auto resolvedRef = maybeResolve(state, originalRef, useRegistries);

    auto [accessor, lockedRef] = resolvedRef.lazyFetch(state.store);

    state.registerAccessor(accessor);

    return readFlake(state, originalRef, resolvedRef, lockedRef, SourcePath {accessor, CanonPath::root}, lockRootPath);
}

Flake getFlake(EvalState & state, const FlakeRef & originalRef, bool useRegistries)
{
    return getFlake(state, originalRef, useRegistries, {}, {});
}

static LockFile readLockFile(const SourcePath & lockFilePath)
{
    return lockFilePath.pathExists()
        ? LockFile(lockFilePath.readFile(), fmt("%s", lockFilePath))
        : LockFile();
}

/* Compute an in-memory lock file for the specified top-level flake,
   and optionally write it to file, if the flake is writable. */
LockedFlake lockFlake(
    EvalState & state,
    const FlakeRef & topRef,
    const LockFlags & lockFlags)
{
    experimentalFeatureSettings.require(Xp::Flakes);

    auto useRegistries = lockFlags.useRegistries.value_or(fetchSettings.useRegistries);

    auto flake = getFlake(state, topRef, useRegistries, {}, {});

    if (lockFlags.applyNixConfig) {
        flake.config.apply();
        state.store->setOptions();
    }

    try {
        if (!fetchSettings.allowDirty && lockFlags.referenceLockFilePath) {
            throw Error("reference lock file was provided, but the `allow-dirty` setting is set to false");
        }

        auto oldLockFile = readLockFile(
            lockFlags.referenceLockFilePath.value_or(
                flake.lockFilePath()));

        debug("old lock file: %s", oldLockFile);

        std::map<InputPath, std::tuple<FlakeInput, SourcePath, std::optional<InputPath>>> overrides;
        std::set<InputPath> explicitCliOverrides;
        std::set<InputPath> overridesUsed, updatesUsed;
        std::map<ref<Node>, SourcePath> nodePaths;

        for (auto & i : lockFlags.inputOverrides) {
            overrides.emplace(
                i.first,
                std::make_tuple(
                    FlakeInput { .ref = i.second },
                    // Note: any relative overrides
                    // (e.g. `--override-input B/C "path:./foo/bar"`)
                    // are interpreted relative to the top-level
                    // flake.
                    flake.path,
                    std::nullopt));
            explicitCliOverrides.insert(i.first);
        }

        LockFile newLockFile;

        std::vector<FlakeRef> parents;

        std::function<void(
            const FlakeInputs & flakeInputs,
            ref<Node> node,
            const InputPath & inputPathPrefix,
            std::shared_ptr<const Node> oldNode,
            const InputPath & followsPrefix,
            const SourcePath & sourcePath,
            bool trustLock)>
            computeLocks;

        computeLocks = [&](
            /* The inputs of this node, either from flake.nix or
               flake.lock. */
            const FlakeInputs & flakeInputs,
            /* The node whose locks are to be updated.*/
            ref<Node> node,
            /* The path to this node in the lock file graph. */
            const InputPath & inputPathPrefix,
            /* The old node, if any, from which locks can be
               copied. */
            std::shared_ptr<const Node> oldNode,
            /* The prefix relative to which 'follows' should be
               interpreted. When a node is initially locked, it's
               relative to the node's flake; when it's already locked,
               it's relative to the root of the lock file. */
            const InputPath & followsPrefix,
            /* The source path of this node's flake. */
            const SourcePath & sourcePath,
            bool trustLock)
        {
            debug("computing lock file node '%s'", printInputPath(inputPathPrefix));

            /* Get the overrides (i.e. attributes of the form
               'inputs.nixops.inputs.nixpkgs.url = ...'). */
            for (auto & [id, input] : flakeInputs) {
                for (auto & [idOverride, inputOverride] : input.overrides) {
                    auto inputPath(inputPathPrefix);
                    inputPath.push_back(id);
                    inputPath.push_back(idOverride);
                    overrides.emplace(inputPath,
                        std::make_tuple(inputOverride, sourcePath, inputPathPrefix));
                }
            }

            /* Check whether this input has overrides for a
               non-existent input. */
            for (auto [inputPath, inputOverride] : overrides) {
                auto inputPath2(inputPath);
                auto follow = inputPath2.back();
                inputPath2.pop_back();
                if (inputPath2 == inputPathPrefix && !flakeInputs.count(follow))
                    warn(
                        "input '%s' has an override for a non-existent input '%s'",
                        printInputPath(inputPathPrefix), follow);
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
                    bool hasCliOverride = explicitCliOverrides.contains(inputPath);
                    if (hasOverride)
                        overridesUsed.insert(inputPath);
                    auto input = hasOverride ? std::get<0>(i->second) : input2;

                    /* Resolve relative 'path:' inputs relative to
                       the source path of the overrider. */
                    auto overridenSourcePath = hasOverride ? std::get<1>(i->second) : sourcePath;

                    /* Respect the "flakeness" of the input even if we
                       override it. */
                    if (hasOverride)
                        input.isFlake = input2.isFlake;

                    /* Resolve 'follows' later (since it may refer to an input
                       path we haven't processed yet. */
                    if (input.follows) {
                        InputPath target;

                        target.insert(target.end(), input.follows->begin(), input.follows->end());

                        debug("input '%s' follows '%s'", inputPathS, printInputPath(target));
                        node->inputs.insert_or_assign(id, target);
                        continue;
                    }

                    assert(input.ref);

                    /* Get the input flake, resolve 'path:./...'
                       flakerefs relative to the parent flake. */
                    auto getInputFlake = [&]()
                    {
                        std::vector<std::string> patches;
                        return getFlake(state, *input.ref, useRegistries, inputPath, patches);
                    };

                    /* Do we have an entry in the existing lock file?
                       And the input is not in updateInputs? */
                    std::shared_ptr<LockedNode> oldLock;

                    updatesUsed.insert(inputPath);

                    if (oldNode && !lockFlags.inputUpdates.count(inputPath))
                        if (auto oldLock2 = get(oldNode->inputs, id))
                            if (auto oldLock3 = std::get_if<0>(&*oldLock2))
                                oldLock = *oldLock3;

                    if (oldLock
                        && oldLock->originalRef == *input.ref
                        && !hasCliOverride)
                    {
                        debug("keeping existing input '%s'", inputPathS);

                        /* Copy the input from the old lock since its flakeref
                           didn't change and there is no override from a
                           higher level flake. */
                        auto childNode = make_ref<LockedNode>(
                            oldLock->lockedRef, oldLock->originalRef, oldLock->isFlake);

                        node->inputs.insert_or_assign(id, childNode);

                        /* If we have this input in updateInputs, then we
                           must fetch the flake to update it. */
                        auto lb = lockFlags.inputUpdates.lower_bound(inputPath);

                        auto mustRefetch =
                            lb != lockFlags.inputUpdates.end()
                            && lb->size() > inputPath.size()
                            && std::equal(inputPath.begin(), inputPath.end(), lb->begin());

                        FlakeInputs fakeInputs;

                        if (!mustRefetch) {
                            /* No need to fetch this flake, we can be
                               lazy. However there may be new overrides on the
                               inputs of this flake, so we need to check
                               those. */
                            for (auto & i : oldLock->inputs) {
                                if (auto lockedNode = std::get_if<0>(&i.second)) {
                                    fakeInputs.emplace(i.first, FlakeInput {
                                        .ref = (*lockedNode)->originalRef,
                                        .isFlake = (*lockedNode)->isFlake,
                                    });
                                } else if (auto follows = std::get_if<1>(&i.second)) {
                                    if (!trustLock) {
                                        // It is possible that the flake has changed,
                                        // so we must confirm all the follows that are in the lock file are also in the flake.
                                        auto overridePath(inputPath);
                                        overridePath.push_back(i.first);
                                        auto o = overrides.find(overridePath);
                                        // If the override disappeared, we have to refetch the flake,
                                        // since some of the inputs may not be present in the lock file.
                                        if (o == overrides.end()) {
                                            mustRefetch = true;
                                            // There's no point populating the rest of the fake inputs,
                                            // since we'll refetch the flake anyways.
                                            break;
                                        }
                                    }
                                    auto absoluteFollows(followsPrefix);
                                    absoluteFollows.insert(absoluteFollows.end(), follows->begin(), follows->end());
                                    fakeInputs.emplace(i.first, FlakeInput {
                                        .follows = absoluteFollows,
                                    });
                                }
                            }
                        }

                        if (mustRefetch) {
                            auto inputFlake = getFlake(state, oldLock->lockedRef, false);
                            nodePaths.emplace(childNode, inputFlake.path.parent());
                            computeLocks(inputFlake.inputs, childNode, inputPath, oldLock, followsPrefix,
                                inputFlake.path, false);
                        } else {
                             // FIXME: sourcePath is wrong here, we
                             // should pass a lambda that lazily
                             // fetches the parent flake if needed
                             // (i.e. getInputFlake()).
                            computeLocks(fakeInputs, childNode, inputPath, oldLock, followsPrefix, sourcePath, true);
                        }

                    } else {
                        /* We need to create a new lock file entry. So fetch
                           this input. */
                        debug("creating new input '%s'", inputPathS);

                        if (!lockFlags.allowUnlocked && !input.ref->input.isLocked())
                            throw Error("cannot update unlocked flake input '%s' in pure mode", inputPathS);

                        /* Note: in case of an --override-input, we use
                            the *original* ref (input2.ref) for the
                            "original" field, rather than the
                            override. This ensures that the override isn't
                            nuked the next time we update the lock
                            file. That is, overrides are sticky unless you
                            use --no-write-lock-file. */
                        auto ref = (input2.ref && explicitCliOverrides.contains(inputPath)) ? *input2.ref : *input.ref;

                        if (input.isFlake) {
                            auto inputFlake = getInputFlake();

                            auto childNode = make_ref<LockedNode>(
                                inputFlake.lockedRef, ref, true);

                            node->inputs.insert_or_assign(id, childNode);

                            /* Guard against circular flake imports. */
                            for (auto & parent : parents)
                                if (parent == *input.ref)
                                    throw Error("found circular import of flake '%s'", parent);
                            parents.push_back(*input.ref);
                            Finally cleanup([&]() { parents.pop_back(); });

                            /* Recursively process the inputs of this
                               flake. Also, unless we already have this flake
                               in the top-level lock file, use this flake's
                               own lock file. */
                            nodePaths.emplace(childNode, inputFlake.path.parent());
                            computeLocks(
                                inputFlake.inputs, childNode, inputPath,
                                oldLock
                                ? std::dynamic_pointer_cast<const Node>(oldLock)
                                : readLockFile(inputFlake.lockFilePath()).root.get_ptr(),
                                oldLock ? followsPrefix : inputPath,
                                inputFlake.path,
                                false);
                        }

                        else {
                            auto [path, lockedRef] = [&]() -> std::tuple<SourcePath, FlakeRef>
                            {
                                auto resolvedRef = maybeResolve(state, *input.ref, useRegistries);
                                auto [accessor, lockedRef] = resolvedRef.lazyFetch(state.store);
                                state.registerAccessor(accessor);
                                return {SourcePath(accessor), lockedRef};
                            }();

                            auto childNode = make_ref<LockedNode>(lockedRef, ref, false);

                            nodePaths.emplace(childNode, path);

                            node->inputs.insert_or_assign(id, childNode);
                        }
                    }

                } catch (Error & e) {
                    e.addTrace({}, "while updating the flake input '%s'", inputPathS);
                    throw;
                }
            }
        };

        nodePaths.emplace(newLockFile.root, flake.path.parent());

        computeLocks(
            flake.inputs,
            newLockFile.root,
            {},
            lockFlags.recreateLockFile ? nullptr : oldLockFile.root.get_ptr(),
            {},
            flake.path,
            false);

        for (auto & i : lockFlags.inputOverrides)
            if (!overridesUsed.count(i.first))
                warn("the flag '--override-input %s %s' does not match any input",
                    printInputPath(i.first), i.second);

        for (auto & i : lockFlags.inputUpdates)
            if (!updatesUsed.count(i))
                warn("'%s' does not match any input of this flake", printInputPath(i));

        /* Check 'follows' inputs. */
        newLockFile.check();

        debug("new lock file: %s", newLockFile);

        /* Check whether we need to / can write the new lock file. */
        if (newLockFile != oldLockFile || lockFlags.outputLockFilePath) {

            auto diff = LockFile::diff(oldLockFile, newLockFile);

            if (lockFlags.writeLockFile) {
                if (auto unlockedInput = newLockFile.isUnlocked()) {
                    if (fetchSettings.warnDirty)
                        warn("will not write lock file of flake '%s' because it has an unlocked input ('%s')", topRef, *unlockedInput);
                } else {
                    if (!lockFlags.updateLockFile)
                        throw Error("flake '%s' requires lock file changes but they're not allowed due to '--no-update-lock-file'", topRef);

                    auto newLockFileS = fmt("%s\n", newLockFile);

                    if (lockFlags.outputLockFilePath) {
                        if (lockFlags.commitLockFile)
                            throw Error("'--commit-lock-file' and '--output-lock-file' are incompatible");
                        writeFile(*lockFlags.outputLockFilePath, newLockFileS);
                    } else {
                        bool lockFileExists = flake.lockFilePath().pathExists();

                        if (lockFileExists) {
                            auto s = chomp(diff);
                            if (s.empty())
                                warn("updating lock file '%s'", flake.lockFilePath());
                            else
                                warn("updating lock file '%s':\n%s", flake.lockFilePath(), s);
                        } else
                            warn("creating lock file '%s'", flake.lockFilePath());

                        std::optional<std::string> commitMessage = std::nullopt;

                        if (lockFlags.commitLockFile) {
                            std::string cm;

                            cm = fetchSettings.commitLockFileSummary.get();

                            if (cm == "") {
                                cm = fmt("%s: %s", flake.lockFilePath().path.rel(), lockFileExists ? "Update" : "Add");
                            }

                            cm += "\n\nFlake lock file updates:\n\n";
                            cm += filterANSIEscapes(diff, true);
                            commitMessage = cm;
                        }

                        topRef.input.putFile(flake.lockFilePath().path, newLockFileS, commitMessage);

                        /* Rewriting the lockfile changed the top-level
                           repo, so we should re-read it. FIXME: we could
                           also just clear the 'rev' field... */
                        auto prevLockedRef = flake.lockedRef;
                        flake = getFlake(state, topRef, useRegistries);

                        if (lockFlags.commitLockFile &&
                            flake.lockedRef.input.getRev() &&
                            prevLockedRef.input.getRev() != flake.lockedRef.input.getRev())
                            warn("committed new revision '%s'", flake.lockedRef.input.getRev()->gitRev());
                    }
                }
            } else {
                warn("not writing modified lock file of flake '%s':\n%s", topRef, chomp(diff));
                flake.forceDirty = true;
            }
        }

        return LockedFlake {
            .flake = std::move(flake),
            .lockFile = std::move(newLockFile),
            .nodePaths = std::move(nodePaths)
        };

    } catch (Error & e) {
        e.addTrace({}, "while updating the lock file of flake '%s'", flake.lockedRef.to_string());
        throw;
    }
}

void callFlake(EvalState & state,
    const LockedFlake & lockedFlake,
    Value & vRes)
{
    experimentalFeatureSettings.require(Xp::Flakes);

    auto [lockFileStr, keyMap] = lockedFlake.lockFile.to_string();

    auto overrides = state.buildBindings(lockedFlake.nodePaths.size());

    for (auto & [node, sourcePath] : lockedFlake.nodePaths) {
        auto override = state.buildBindings(2);

        auto & vSourceInfo = override.alloc(state.symbols.create("sourceInfo"));

        auto lockedNode = node.dynamic_pointer_cast<const LockedNode>();
        emitTreeAttrs(
            state,
            SourcePath(sourcePath.accessor),
            lockedNode ? lockedNode->lockedRef.input : lockedFlake.flake.lockedRef.input,
            vSourceInfo,
            false,
            !lockedNode && lockedFlake.flake.forceDirty);

        auto key = keyMap.find(node);
        assert(key != keyMap.end());

        override
            .alloc(state.symbols.create("dir"))
            .mkString(sourcePath.path.rel());

        overrides.alloc(state.symbols.create(key->second)).mkAttrs(override);
    }

    auto & vOverrides = state.allocValue()->mkAttrs(overrides);

    auto vCallFlake = state.allocValue();
    state.evalFile(state.callFlakeInternal, *vCallFlake);

    auto vTmp1 = state.allocValue();
    auto vLocks = state.allocValue();
    vLocks->mkString(lockFileStr);
    state.callFunction(*vCallFlake, *vLocks, *vTmp1, noPos);

    state.callFunction(*vTmp1, vOverrides, vRes, noPos);
}

static void prim_getFlake(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    std::string flakeRefS(state.forceStringNoCtx(*args[0], pos, "while evaluating the argument passed to builtins.getFlake"));
    auto flakeRef = parseFlakeRef(flakeRefS, {}, true);
    if (evalSettings.pureEval && !flakeRef.input.isLocked())
        throw Error("cannot call 'getFlake' on unlocked flake reference '%s', at %s (use --impure to override)", flakeRefS, state.positions[pos]);

    callFlake(state,
        lockFlake(state, flakeRef,
            LockFlags {
                .updateLockFile = false,
                .writeLockFile = false,
                .useRegistries = !evalSettings.pureEval && fetchSettings.useRegistries,
                .allowUnlocked = !evalSettings.pureEval,
            }),
        v);
}

static RegisterPrimOp r2({
    .name =  "__getFlake",
    .args = {"args"},
    .doc = R"(
      Fetch a flake from a flake reference, and return its output attributes and some metadata. For example:

      ```nix
      (builtins.getFlake "nix/55bc52401966fbffa525c574c14f67b00bc4fb3a").packages.x86_64-linux.nix
      ```

      Unless impure evaluation is allowed (`--impure`), the flake reference
      must be "locked", e.g. contain a Git revision or content hash. An
      example of an unlocked usage is:

      ```nix
      (builtins.getFlake "github:edolstra/dwarffs").rev
      ```
    )",
    .fun = prim_getFlake,
    .experimentalFeature = Xp::Flakes,
});

static void prim_parseFlakeRef(
    EvalState & state,
    const PosIdx pos,
    Value * * args,
    Value & v)
{
    std::string flakeRefS(state.forceStringNoCtx(*args[0], pos,
        "while evaluating the argument passed to builtins.parseFlakeRef"));
    auto attrs = parseFlakeRef(flakeRefS, {}, true).toAttrs();
    auto binds = state.buildBindings(attrs.size());
    for (const auto & [key, value] : attrs) {
        auto s = state.symbols.create(key);
        auto & vv = binds.alloc(s);
        std::visit(overloaded {
            [&vv](const std::string    & value) { vv.mkString(value); },
            [&vv](const uint64_t       & value) { vv.mkInt(value);    },
            [&vv](const Explicit<bool> & value) { vv.mkBool(value.t); }
        }, value);
    }
    v.mkAttrs(binds);
}

static RegisterPrimOp r3({
    .name =  "__parseFlakeRef",
    .args = {"flake-ref"},
    .doc = R"(
      Parse a flake reference, and return its exploded form.

      For example:

      ```nix
      builtins.parseFlakeRef "github:NixOS/nixpkgs/23.05?dir=lib"
      ```

      evaluates to:

      ```nix
      { dir = "lib"; owner = "NixOS"; ref = "23.05"; repo = "nixpkgs"; type = "github"; }
      ```
    )",
    .fun = prim_parseFlakeRef,
    .experimentalFeature = Xp::Flakes,
});


static void prim_flakeRefToString(
    EvalState & state,
    const PosIdx pos,
    Value * * args,
    Value & v)
{
    state.forceAttrs(*args[0], noPos,
        "while evaluating the argument passed to builtins.flakeRefToString");
    fetchers::Attrs attrs;
    for (const auto & attr : *args[0]->attrs) {
        auto t = attr.value->type();
        if (t == nInt) {
            attrs.emplace(state.symbols[attr.name],
                          (uint64_t) attr.value->integer);
        } else if (t == nBool) {
            attrs.emplace(state.symbols[attr.name],
                          Explicit<bool> { attr.value->boolean });
        } else if (t == nString) {
            attrs.emplace(state.symbols[attr.name],
                          std::string(attr.value->string_view()));
        } else {
            state.error<EvalError>(
                "flake reference attribute sets may only contain integers, Booleans, "
                "and strings, but attribute '%s' is %s",
                state.symbols[attr.name],
                showType(*attr.value)).debugThrow();
        }
    }
    auto flakeRef = FlakeRef::fromAttrs(attrs);
    v.mkString(flakeRef.to_string());
}

static RegisterPrimOp r4({
    .name =  "__flakeRefToString",
    .args = {"attrs"},
    .doc = R"(
      Convert a flake reference from attribute set format to URL format.

      For example:

      ```nix
      builtins.flakeRefToString {
        dir = "lib"; owner = "NixOS"; ref = "23.05"; repo = "nixpkgs"; type = "github";
      }
      ```

      evaluates to

      ```nix
      "github:NixOS/nixpkgs/23.05?dir=lib"
      ```
    )",
    .fun = prim_flakeRefToString,
    .experimentalFeature = Xp::Flakes,
});

}

std::optional<Fingerprint> LockedFlake::getFingerprint(ref<Store> store) const
{
    if (lockFile.isUnlocked()) return std::nullopt;

    auto fingerprint = flake.lockedRef.input.getFingerprint(store);
    if (!fingerprint) return std::nullopt;

    // FIXME: as an optimization, if the flake contains a lock file
    // and we haven't changed it, then it's sufficient to use
    // flake.sourceInfo.storePath for the fingerprint.
    return hashString(HashAlgorithm::SHA256, fmt("%s;%s;%s", *fingerprint, flake.lockedRef.subdir, lockFile));
}

Flake::~Flake() { }

}
