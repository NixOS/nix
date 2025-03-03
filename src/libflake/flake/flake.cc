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
#include "flake/settings.hh"
#include "value-to-json.hh"
#include "local-fs-store.hh"
#include "fetch-to-store.hh"

#include <nlohmann/json.hpp>

namespace nix {

using namespace flake;

namespace flake {

struct FetchedFlake
{
    FlakeRef lockedRef;
    ref<SourceAccessor> accessor;
};

typedef std::map<FlakeRef, FetchedFlake> FlakeCache;

static std::optional<FetchedFlake> lookupInFlakeCache(
    const FlakeCache & flakeCache,
    const FlakeRef & flakeRef)
{
    auto i = flakeCache.find(flakeRef);
    if (i == flakeCache.end()) return std::nullopt;
    debug("mapping '%s' to previously seen input '%s' -> '%s",
        flakeRef, i->first, i->second.lockedRef);
    return i->second;
}

static std::tuple<ref<SourceAccessor>, FlakeRef, FlakeRef> fetchOrSubstituteTree(
    EvalState & state,
    const FlakeRef & originalRef,
    bool useRegistries,
    FlakeCache & flakeCache)
{
    auto fetched = lookupInFlakeCache(flakeCache, originalRef);
    FlakeRef resolvedRef = originalRef;

    if (!fetched) {
        if (originalRef.input.isDirect()) {
            auto [accessor, lockedRef] = originalRef.lazyFetch(state.store);
            fetched.emplace(FetchedFlake{.lockedRef = lockedRef, .accessor = accessor});
        } else {
            if (useRegistries) {
                resolvedRef = originalRef.resolve(
                    state.store,
                    [](fetchers::Registry::RegistryType type) {
                        /* Only use the global registry and CLI flags
                           to resolve indirect flakerefs. */
                        return type == fetchers::Registry::Flag || type == fetchers::Registry::Global;
                    });
                fetched = lookupInFlakeCache(flakeCache, originalRef);
                if (!fetched) {
                    auto [accessor, lockedRef] = resolvedRef.lazyFetch(state.store);
                    fetched.emplace(FetchedFlake{.lockedRef = lockedRef, .accessor = accessor});
                }
                flakeCache.insert_or_assign(resolvedRef, *fetched);
            }
            else {
                throw Error("'%s' is an indirect flake reference, but registry lookups are not allowed", originalRef);
            }
        }
        flakeCache.insert_or_assign(originalRef, *fetched);
    }

    debug("got tree '%s' from '%s'", fetched->accessor, fetched->lockedRef);

    return {fetched->accessor, resolvedRef, fetched->lockedRef};
}

static StorePath copyInputToStore(
    EvalState & state,
    fetchers::Input & input,
    const fetchers::Input & originalInput,
    ref<SourceAccessor> accessor)
{
    auto storePath = fetchToStore(*state.store, accessor, FetchMode::Copy, input.getName());

    state.allowPath(storePath);

    auto narHash = state.store->queryPathInfo(storePath)->narHash;
    input.attrs.insert_or_assign("narHash", narHash.to_string(HashFormat::SRI, true));

    assert(!originalInput.getNarHash() || storePath == originalInput.computeStorePath(*state.store));

    return storePath;
}

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

static std::pair<std::map<FlakeId, FlakeInput>, fetchers::Attrs> parseFlakeInputs(
    EvalState & state,
    Value * value,
    const PosIdx pos,
    const InputAttrPath & lockRootAttrPath,
    const SourcePath & flakeDir,
    bool allowSelf);

static void parseFlakeInputAttr(
    EvalState & state,
    const Attr & attr,
    fetchers::Attrs & attrs)
{
    // Allow selecting a subset of enum values
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wswitch-enum"
    switch (attr.value->type()) {
        case nString:
            attrs.emplace(state.symbols[attr.name], attr.value->c_str());
            break;
        case nBool:
            attrs.emplace(state.symbols[attr.name], Explicit<bool> { attr.value->boolean() });
            break;
        case nInt: {
            auto intValue = attr.value->integer().value;
            if (intValue < 0)
                state.error<EvalError>("negative value given for flake input attribute %1%: %2%", state.symbols[attr.name], intValue).debugThrow();
            attrs.emplace(state.symbols[attr.name], uint64_t(intValue));
            break;
        }
        default:
            if (attr.name == state.symbols.create("publicKeys")) {
                experimentalFeatureSettings.require(Xp::VerifiedFetches);
                NixStringContext emptyContext = {};
                attrs.emplace(state.symbols[attr.name], printValueAsJSON(state, true, *attr.value, attr.pos, emptyContext).dump());
            } else
                state.error<TypeError>("flake input attribute '%s' is %s while a string, Boolean, or integer is expected",
                    state.symbols[attr.name], showType(*attr.value)).debugThrow();
    }
    #pragma GCC diagnostic pop
}

static FlakeInput parseFlakeInput(
    EvalState & state,
    std::string_view inputName,
    Value * value,
    const PosIdx pos,
    const InputAttrPath & lockRootAttrPath,
    const SourcePath & flakeDir)
{
    expectType(state, nAttrs, *value, pos);

    FlakeInput input;

    auto sInputs = state.symbols.create("inputs");
    auto sUrl = state.symbols.create("url");
    auto sFlake = state.symbols.create("flake");
    auto sFollows = state.symbols.create("follows");

    fetchers::Attrs attrs;
    std::optional<std::string> url;

    for (auto & attr : *value->attrs()) {
        try {
            if (attr.name == sUrl) {
                forceTrivialValue(state, *attr.value, pos);
                if (attr.value->type() == nString)
                    url = attr.value->string_view();
                else if (attr.value->type() == nPath) {
                    auto path = attr.value->path();
                    if (path.accessor != flakeDir.accessor)
                        throw Error("input attribute path '%s' at %s must be in the same source tree as %s",
                            path, state.positions[attr.pos], flakeDir);
                    url = "path:" + flakeDir.path.makeRelative(path.path);
                }
                else
                    throw Error("expected a string or a path but got %s at %s",
                        showType(attr.value->type()), state.positions[attr.pos]);
                attrs.emplace("url", *url);
            } else if (attr.name == sFlake) {
                expectType(state, nBool, *attr.value, attr.pos);
                input.isFlake = attr.value->boolean();
            } else if (attr.name == sInputs) {
                input.overrides = parseFlakeInputs(state, attr.value, attr.pos, lockRootAttrPath, flakeDir, false).first;
            } else if (attr.name == sFollows) {
                expectType(state, nString, *attr.value, attr.pos);
                auto follows(parseInputAttrPath(attr.value->c_str()));
                follows.insert(follows.begin(), lockRootAttrPath.begin(), lockRootAttrPath.end());
                input.follows = follows;
            } else
                parseFlakeInputAttr(state, attr, attrs);
        } catch (Error & e) {
            e.addTrace(
                state.positions[attr.pos],
                HintFmt("while evaluating flake attribute '%s'", state.symbols[attr.name]));
            throw;
        }
    }

    if (attrs.count("type"))
        try {
            input.ref = FlakeRef::fromAttrs(state.fetchSettings, attrs);
        } catch (Error & e) {
            e.addTrace(state.positions[pos], HintFmt("while evaluating flake input"));
            throw;
        }
    else {
        attrs.erase("url");
        if (!attrs.empty())
            throw Error("unexpected flake input attribute '%s', at %s", attrs.begin()->first, state.positions[pos]);
        if (url)
            input.ref = parseFlakeRef(state.fetchSettings, *url, {}, true, input.isFlake, true);
    }

    if (!input.follows && !input.ref)
        input.ref = FlakeRef::fromAttrs(state.fetchSettings, {{"type", "indirect"}, {"id", std::string(inputName)}});

    return input;
}

static std::pair<std::map<FlakeId, FlakeInput>, fetchers::Attrs> parseFlakeInputs(
    EvalState & state,
    Value * value,
    const PosIdx pos,
    const InputAttrPath & lockRootAttrPath,
    const SourcePath & flakeDir,
    bool allowSelf)
{
    std::map<FlakeId, FlakeInput> inputs;
    fetchers::Attrs selfAttrs;

    expectType(state, nAttrs, *value, pos);

    for (auto & inputAttr : *value->attrs()) {
        auto inputName = state.symbols[inputAttr.name];
        if (inputName == "self") {
            if (!allowSelf)
                throw Error("'self' input attribute not allowed at %s", state.positions[inputAttr.pos]);
            expectType(state, nAttrs, *inputAttr.value, inputAttr.pos);
            for (auto & attr : *inputAttr.value->attrs())
                parseFlakeInputAttr(state, attr, selfAttrs);
        } else {
            inputs.emplace(inputName,
                parseFlakeInput(state,
                    inputName,
                    inputAttr.value,
                    inputAttr.pos,
                    lockRootAttrPath,
                    flakeDir));
        }
    }

    return {inputs, selfAttrs};
}

static Flake readFlake(
    EvalState & state,
    const FlakeRef & originalRef,
    const FlakeRef & resolvedRef,
    const FlakeRef & lockedRef,
    const SourcePath & rootDir,
    const InputAttrPath & lockRootAttrPath)
{
    auto flakeDir = rootDir / CanonPath(resolvedRef.subdir);
    auto flakePath = flakeDir / "flake.nix";

    // NOTE evalFile forces vInfo to be an attrset because mustBeTrivial is true.
    Value vInfo;
    state.evalFile(flakePath, vInfo, true);

    Flake flake {
        .originalRef = originalRef,
        .resolvedRef = resolvedRef,
        .lockedRef = lockedRef,
        .path = flakePath,
    };

    if (auto description = vInfo.attrs()->get(state.sDescription)) {
        expectType(state, nString, *description->value, description->pos);
        flake.description = description->value->c_str();
    }

    auto sInputs = state.symbols.create("inputs");

    if (auto inputs = vInfo.attrs()->get(sInputs)) {
        auto [flakeInputs, selfAttrs] = parseFlakeInputs(state, inputs->value, inputs->pos, lockRootAttrPath, flakeDir, true);
        flake.inputs = std::move(flakeInputs);
        flake.selfAttrs = std::move(selfAttrs);
    }

    auto sOutputs = state.symbols.create("outputs");

    if (auto outputs = vInfo.attrs()->get(sOutputs)) {
        expectType(state, nFunction, *outputs->value, outputs->pos);

        if (outputs->value->isLambda() && outputs->value->payload.lambda.fun->hasFormals()) {
            for (auto & formal : outputs->value->payload.lambda.fun->formals->formals) {
                if (formal.name != state.sSelf)
                    flake.inputs.emplace(state.symbols[formal.name], FlakeInput {
                        .ref = parseFlakeRef(state.fetchSettings, std::string(state.symbols[formal.name]))
                    });
            }
        }

    } else
        throw Error("flake '%s' lacks attribute 'outputs'", resolvedRef);

    auto sNixConfig = state.symbols.create("nixConfig");

    if (auto nixConfig = vInfo.attrs()->get(sNixConfig)) {
        expectType(state, nAttrs, *nixConfig->value, nixConfig->pos);

        for (auto & setting : *nixConfig->value->attrs()) {
            forceTrivialValue(state, *setting.value, setting.pos);
            if (setting.value->type() == nString)
                flake.config.settings.emplace(
                    state.symbols[setting.name],
                    std::string(state.forceStringNoCtx(*setting.value, setting.pos, "")));
            else if (setting.value->type() == nPath) {
                auto storePath = fetchToStore(*state.store, setting.value->path(), FetchMode::Copy);
                flake.config.settings.emplace(
                    state.symbols[setting.name],
                    state.store->printStorePath(storePath));
            }
            else if (setting.value->type() == nInt)
                flake.config.settings.emplace(
                    state.symbols[setting.name],
                    state.forceInt(*setting.value, setting.pos, "").value);
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

    for (auto & attr : *vInfo.attrs()) {
        if (attr.name != state.sDescription &&
            attr.name != sInputs &&
            attr.name != sOutputs &&
            attr.name != sNixConfig)
            throw Error("flake '%s' has an unsupported attribute '%s', at %s",
                resolvedRef, state.symbols[attr.name], state.positions[attr.pos]);
    }

    return flake;
}

static FlakeRef applySelfAttrs(
    const FlakeRef & ref,
    const Flake & flake)
{
    auto newRef(ref);

    std::set<std::string> allowedAttrs{"submodules", "lfs"};

    for (auto & attr : flake.selfAttrs) {
        if (!allowedAttrs.contains(attr.first))
            throw Error("flake 'self' attribute '%s' is not supported", attr.first);
        newRef.input.attrs.insert_or_assign(attr.first, attr.second);
    }

    return newRef;
}

static Flake getFlake(
    EvalState & state,
    const FlakeRef & originalRef,
    bool useRegistries,
    FlakeCache & flakeCache,
    const InputAttrPath & lockRootAttrPath)
{
    // Fetch a lazy tree first.
    auto [accessor, resolvedRef, lockedRef] = fetchOrSubstituteTree(
        state, originalRef, useRegistries, flakeCache);

    // Parse/eval flake.nix to get at the input.self attributes.
    auto flake = readFlake(state, originalRef, resolvedRef, lockedRef, {accessor}, lockRootAttrPath);

    // Re-fetch the tree if necessary.
    auto newLockedRef = applySelfAttrs(lockedRef, flake);

    if (lockedRef != newLockedRef) {
        debug("refetching input '%s' due to self attribute", newLockedRef);
        // FIXME: need to remove attrs that are invalidated by the changed input attrs, such as 'narHash'.
        newLockedRef.input.attrs.erase("narHash");
        auto [accessor2, resolvedRef2, lockedRef2] = fetchOrSubstituteTree(
            state, newLockedRef, false, flakeCache);
        accessor = accessor2;
        lockedRef = lockedRef2;
    }

    // Copy the tree to the store.
    auto storePath = copyInputToStore(state, lockedRef.input, originalRef.input, accessor);

    // Re-parse flake.nix from the store.
    return readFlake(state, originalRef, resolvedRef, lockedRef, state.storePath(storePath), lockRootAttrPath);
}

Flake getFlake(EvalState & state, const FlakeRef & originalRef, bool useRegistries)
{
    FlakeCache flakeCache;
    return getFlake(state, originalRef, useRegistries, flakeCache, {});
}

static LockFile readLockFile(
    const fetchers::Settings & fetchSettings,
    const SourcePath & lockFilePath)
{
    return lockFilePath.pathExists()
        ? LockFile(fetchSettings, lockFilePath.readFile(), fmt("%s", lockFilePath))
        : LockFile();
}

/* Compute an in-memory lock file for the specified top-level flake,
   and optionally write it to file, if the flake is writable. */
LockedFlake lockFlake(
    const Settings & settings,
    EvalState & state,
    const FlakeRef & topRef,
    const LockFlags & lockFlags)
{
    experimentalFeatureSettings.require(Xp::Flakes);

    FlakeCache flakeCache;

    auto useRegistries = lockFlags.useRegistries.value_or(settings.useRegistries);

    auto flake = getFlake(state, topRef, useRegistries, flakeCache, {});

    if (lockFlags.applyNixConfig) {
        flake.config.apply(settings);
        state.store->setOptions();
    }

    try {
        if (!state.fetchSettings.allowDirty && lockFlags.referenceLockFilePath) {
            throw Error("reference lock file was provided, but the `allow-dirty` setting is set to false");
        }

        auto oldLockFile = readLockFile(
            state.fetchSettings,
            lockFlags.referenceLockFilePath.value_or(
                flake.lockFilePath()));

        debug("old lock file: %s", oldLockFile);

        struct OverrideTarget
        {
            FlakeInput input;
            SourcePath sourcePath;
            std::optional<InputAttrPath> parentInputAttrPath; // FIXME: rename to inputAttrPathPrefix?
        };

        std::map<InputAttrPath, OverrideTarget> overrides;
        std::set<InputAttrPath> explicitCliOverrides;
        std::set<InputAttrPath> overridesUsed, updatesUsed;
        std::map<ref<Node>, SourcePath> nodePaths;

        for (auto & i : lockFlags.inputOverrides) {
            overrides.emplace(
                i.first,
                OverrideTarget {
                    .input = FlakeInput { .ref = i.second },
                    /* Note: any relative overrides
                       (e.g. `--override-input B/C "path:./foo/bar"`)
                       are interpreted relative to the top-level
                       flake. */
                    .sourcePath = flake.path,
                });
            explicitCliOverrides.insert(i.first);
        }

        LockFile newLockFile;

        std::vector<FlakeRef> parents;

        std::function<void(
            const FlakeInputs & flakeInputs,
            ref<Node> node,
            const InputAttrPath & inputAttrPathPrefix,
            std::shared_ptr<const Node> oldNode,
            const InputAttrPath & followsPrefix,
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
            const InputAttrPath & inputAttrPathPrefix,
            /* The old node, if any, from which locks can be
               copied. */
            std::shared_ptr<const Node> oldNode,
            /* The prefix relative to which 'follows' should be
               interpreted. When a node is initially locked, it's
               relative to the node's flake; when it's already locked,
               it's relative to the root of the lock file. */
            const InputAttrPath & followsPrefix,
            /* The source path of this node's flake. */
            const SourcePath & sourcePath,
            bool trustLock)
        {
            debug("computing lock file node '%s'", printInputAttrPath(inputAttrPathPrefix));

            /* Get the overrides (i.e. attributes of the form
               'inputs.nixops.inputs.nixpkgs.url = ...'). */
            for (auto & [id, input] : flakeInputs) {
                for (auto & [idOverride, inputOverride] : input.overrides) {
                    auto inputAttrPath(inputAttrPathPrefix);
                    inputAttrPath.push_back(id);
                    inputAttrPath.push_back(idOverride);
                    overrides.emplace(inputAttrPath,
                        OverrideTarget {
                            .input = inputOverride,
                            .sourcePath = sourcePath,
                            .parentInputAttrPath = inputAttrPathPrefix
                        });
                }
            }

            /* Check whether this input has overrides for a
               non-existent input. */
            for (auto [inputAttrPath, inputOverride] : overrides) {
                auto inputAttrPath2(inputAttrPath);
                auto follow = inputAttrPath2.back();
                inputAttrPath2.pop_back();
                if (inputAttrPath2 == inputAttrPathPrefix && !flakeInputs.count(follow))
                    warn(
                        "input '%s' has an override for a non-existent input '%s'",
                        printInputAttrPath(inputAttrPathPrefix), follow);
            }

            /* Go over the flake inputs, resolve/fetch them if
               necessary (i.e. if they're new or the flakeref changed
               from what's in the lock file). */
            for (auto & [id, input2] : flakeInputs) {
                auto inputAttrPath(inputAttrPathPrefix);
                inputAttrPath.push_back(id);
                auto inputAttrPathS = printInputAttrPath(inputAttrPath);
                debug("computing input '%s'", inputAttrPathS);

                try {

                    /* Do we have an override for this input from one of the
                       ancestors? */
                    auto i = overrides.find(inputAttrPath);
                    bool hasOverride = i != overrides.end();
                    bool hasCliOverride = explicitCliOverrides.contains(inputAttrPath);
                    if (hasOverride)
                        overridesUsed.insert(inputAttrPath);
                    auto input = hasOverride ? i->second.input : input2;

                    /* Resolve relative 'path:' inputs relative to
                       the source path of the overrider. */
                    auto overridenSourcePath = hasOverride ? i->second.sourcePath : sourcePath;

                    /* Respect the "flakeness" of the input even if we
                       override it. */
                    if (hasOverride)
                        input.isFlake = input2.isFlake;

                    /* Resolve 'follows' later (since it may refer to an input
                       path we haven't processed yet. */
                    if (input.follows) {
                        InputAttrPath target;

                        target.insert(target.end(), input.follows->begin(), input.follows->end());

                        debug("input '%s' follows '%s'", inputAttrPathS, printInputAttrPath(target));
                        node->inputs.insert_or_assign(id, target);
                        continue;
                    }

                    assert(input.ref);

                    auto overridenParentPath =
                        input.ref->input.isRelative()
                        ? std::optional<InputAttrPath>(hasOverride ? i->second.parentInputAttrPath : inputAttrPathPrefix)
                        : std::nullopt;

                    auto resolveRelativePath = [&]() -> std::optional<SourcePath>
                    {
                        if (auto relativePath = input.ref->input.isRelative()) {
                            return SourcePath {
                                overridenSourcePath.accessor,
                                CanonPath(*relativePath, overridenSourcePath.path.parent().value())
                            };
                        } else
                            return std::nullopt;
                    };

                    /* Get the input flake, resolve 'path:./...'
                       flakerefs relative to the parent flake. */
                    auto getInputFlake = [&](const FlakeRef & ref)
                    {
                        if (auto resolvedPath = resolveRelativePath()) {
                            return readFlake(state, ref, ref, ref, *resolvedPath, inputAttrPath);
                        } else {
                            return getFlake(state, ref, useRegistries, flakeCache, inputAttrPath);
                        }
                    };

                    /* Do we have an entry in the existing lock file?
                       And the input is not in updateInputs? */
                    std::shared_ptr<LockedNode> oldLock;

                    updatesUsed.insert(inputAttrPath);

                    if (oldNode && !lockFlags.inputUpdates.count(inputAttrPath))
                        if (auto oldLock2 = get(oldNode->inputs, id))
                            if (auto oldLock3 = std::get_if<0>(&*oldLock2))
                                oldLock = *oldLock3;

                    if (oldLock
                        && oldLock->originalRef == *input.ref
                        && oldLock->parentInputAttrPath == overridenParentPath
                        && !hasCliOverride)
                    {
                        debug("keeping existing input '%s'", inputAttrPathS);

                        /* Copy the input from the old lock since its flakeref
                           didn't change and there is no override from a
                           higher level flake. */
                        auto childNode = make_ref<LockedNode>(
                            oldLock->lockedRef,
                            oldLock->originalRef,
                            oldLock->isFlake,
                            oldLock->parentInputAttrPath);

                        node->inputs.insert_or_assign(id, childNode);

                        /* If we have this input in updateInputs, then we
                           must fetch the flake to update it. */
                        auto lb = lockFlags.inputUpdates.lower_bound(inputAttrPath);

                        auto mustRefetch =
                            lb != lockFlags.inputUpdates.end()
                            && lb->size() > inputAttrPath.size()
                            && std::equal(inputAttrPath.begin(), inputAttrPath.end(), lb->begin());

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
                                        auto overridePath(inputAttrPath);
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
                            auto inputFlake = getInputFlake(oldLock->lockedRef);
                            nodePaths.emplace(childNode, inputFlake.path.parent());
                            computeLocks(inputFlake.inputs, childNode, inputAttrPath, oldLock, followsPrefix,
                                inputFlake.path, false);
                        } else {
                            computeLocks(fakeInputs, childNode, inputAttrPath, oldLock, followsPrefix, sourcePath, true);
                        }

                    } else {
                        /* We need to create a new lock file entry. So fetch
                           this input. */
                        debug("creating new input '%s'", inputAttrPathS);

                        if (!lockFlags.allowUnlocked
                            && !input.ref->input.isLocked()
                            && !input.ref->input.isRelative())
                            throw Error("cannot update unlocked flake input '%s' in pure mode", inputAttrPathS);

                        /* Note: in case of an --override-input, we use
                            the *original* ref (input2.ref) for the
                            "original" field, rather than the
                            override. This ensures that the override isn't
                            nuked the next time we update the lock
                            file. That is, overrides are sticky unless you
                            use --no-write-lock-file. */
                        auto ref = (input2.ref && explicitCliOverrides.contains(inputAttrPath)) ? *input2.ref : *input.ref;

                        if (input.isFlake) {
                            auto inputFlake = getInputFlake(*input.ref);

                            auto childNode = make_ref<LockedNode>(
                                inputFlake.lockedRef,
                                ref,
                                true,
                                overridenParentPath);

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
                                inputFlake.inputs, childNode, inputAttrPath,
                                oldLock
                                ? std::dynamic_pointer_cast<const Node>(oldLock)
                                : readLockFile(state.fetchSettings, inputFlake.lockFilePath()).root.get_ptr(),
                                oldLock ? followsPrefix : inputAttrPath,
                                inputFlake.path,
                                false);
                        }

                        else {
                            auto [path, lockedRef] = [&]() -> std::tuple<SourcePath, FlakeRef>
                            {
                                // Handle non-flake 'path:./...' inputs.
                                if (auto resolvedPath = resolveRelativePath()) {
                                    return {*resolvedPath, *input.ref};
                                } else {
                                    auto [accessor, resolvedRef, lockedRef] = fetchOrSubstituteTree(
                                        state, *input.ref, useRegistries, flakeCache);

                                    // FIXME: allow input to be lazy.
                                    auto storePath = copyInputToStore(state, lockedRef.input, input.ref->input, accessor);

                                    return {state.storePath(storePath), lockedRef};
                                }
                            }();

                            auto childNode = make_ref<LockedNode>(lockedRef, ref, false, overridenParentPath);

                            nodePaths.emplace(childNode, path);

                            node->inputs.insert_or_assign(id, childNode);
                        }
                    }

                } catch (Error & e) {
                    e.addTrace({}, "while updating the flake input '%s'", inputAttrPathS);
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
                    printInputAttrPath(i.first), i.second);

        for (auto & i : lockFlags.inputUpdates)
            if (!updatesUsed.count(i))
                warn("'%s' does not match any input of this flake", printInputAttrPath(i));

        /* Check 'follows' inputs. */
        newLockFile.check();

        debug("new lock file: %s", newLockFile);

        auto sourcePath = topRef.input.getSourcePath();

        /* Check whether we need to / can write the new lock file. */
        if (newLockFile != oldLockFile || lockFlags.outputLockFilePath) {

            auto diff = LockFile::diff(oldLockFile, newLockFile);

            if (lockFlags.writeLockFile) {
                if (sourcePath || lockFlags.outputLockFilePath) {
                    if (auto unlockedInput = newLockFile.isUnlocked(state.fetchSettings)) {
                        if (lockFlags.failOnUnlocked)
                            throw Error(
                                "Will not write lock file of flake '%s' because it has an unlocked input ('%s'). "
                                "Use '--allow-dirty-locks' to allow this anyway.", topRef, *unlockedInput);
                        if (state.fetchSettings.warnDirty)
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
                            auto relPath = (topRef.subdir == "" ? "" : topRef.subdir + "/") + "flake.lock";
                            auto outputLockFilePath = *sourcePath / relPath;

                            bool lockFileExists = fs::symlink_exists(outputLockFilePath);

                            auto s = chomp(diff);
                            if (lockFileExists) {
                                if (s.empty())
                                    warn("updating lock file '%s'", outputLockFilePath);
                                else
                                    warn("updating lock file '%s':\n%s", outputLockFilePath, s);
                            } else
                                warn("creating lock file '%s': \n%s", outputLockFilePath, s);

                            std::optional<std::string> commitMessage = std::nullopt;

                            if (lockFlags.commitLockFile) {
                                std::string cm;

                                cm = settings.commitLockFileSummary.get();

                                if (cm == "") {
                                    cm = fmt("%s: %s", relPath, lockFileExists ? "Update" : "Add");
                                }

                                cm += "\n\nFlake lock file updates:\n\n";
                                cm += filterANSIEscapes(diff, true);
                                commitMessage = cm;
                            }

                            topRef.input.putFile(
                                CanonPath((topRef.subdir == "" ? "" : topRef.subdir + "/") + "flake.lock"),
                                newLockFileS, commitMessage);
                        }

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
                } else
                    throw Error("cannot write modified lock file of flake '%s' (use '--no-write-lock-file' to ignore)", topRef);
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

        auto [storePath, subdir] = state.store->toStorePath(sourcePath.path.abs());

        emitTreeAttrs(
            state,
            storePath,
            lockedNode ? lockedNode->lockedRef.input : lockedFlake.flake.lockedRef.input,
            vSourceInfo,
            false,
            !lockedNode && lockedFlake.flake.forceDirty);

        auto key = keyMap.find(node);
        assert(key != keyMap.end());

        override
            .alloc(state.symbols.create("dir"))
            .mkString(CanonPath(subdir).rel());

        overrides.alloc(state.symbols.create(key->second)).mkAttrs(override);
    }

    auto & vOverrides = state.allocValue()->mkAttrs(overrides);

    auto vCallFlake = state.allocValue();
    state.evalFile(state.callFlakeInternal, *vCallFlake);

    auto vLocks = state.allocValue();
    vLocks->mkString(lockFileStr);

    auto vFetchFinalTree = get(state.internalPrimOps, "fetchFinalTree");
    assert(vFetchFinalTree);

    Value * args[] = {vLocks, &vOverrides, *vFetchFinalTree};
    state.callFunction(*vCallFlake, args, vRes, noPos);
}

void initLib(const Settings & settings)
{
    auto prim_getFlake = [&settings](EvalState & state, const PosIdx pos, Value * * args, Value & v)
    {
        std::string flakeRefS(state.forceStringNoCtx(*args[0], pos, "while evaluating the argument passed to builtins.getFlake"));
        auto flakeRef = parseFlakeRef(state.fetchSettings, flakeRefS, {}, true);
        if (state.settings.pureEval && !flakeRef.input.isLocked())
            throw Error("cannot call 'getFlake' on unlocked flake reference '%s', at %s (use --impure to override)", flakeRefS, state.positions[pos]);

        callFlake(state,
            lockFlake(settings, state, flakeRef,
                LockFlags {
                    .updateLockFile = false,
                    .writeLockFile = false,
                    .useRegistries = !state.settings.pureEval && settings.useRegistries,
                    .allowUnlocked = !state.settings.pureEval,
                }),
            v);
    };

    RegisterPrimOp::primOps->push_back({
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
}

static void prim_parseFlakeRef(
    EvalState & state,
    const PosIdx pos,
    Value * * args,
    Value & v)
{
    std::string flakeRefS(state.forceStringNoCtx(*args[0], pos,
        "while evaluating the argument passed to builtins.parseFlakeRef"));
    auto attrs = parseFlakeRef(state.fetchSettings, flakeRefS, {}, true).toAttrs();
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
    for (const auto & attr : *args[0]->attrs()) {
        auto t = attr.value->type();
        if (t == nInt) {
            auto intValue = attr.value->integer().value;

            if (intValue < 0) {
                state.error<EvalError>("negative value given for flake ref attr %1%: %2%", state.symbols[attr.name], intValue).atPos(pos).debugThrow();
            }

            attrs.emplace(state.symbols[attr.name], uint64_t(intValue));
        } else if (t == nBool) {
            attrs.emplace(state.symbols[attr.name],
                Explicit<bool> { attr.value->boolean() });
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
    auto flakeRef = FlakeRef::fromAttrs(state.fetchSettings, attrs);
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

std::optional<Fingerprint> LockedFlake::getFingerprint(
    ref<Store> store,
    const fetchers::Settings & fetchSettings) const
{
    if (lockFile.isUnlocked(fetchSettings)) return std::nullopt;

    auto fingerprint = flake.lockedRef.input.getFingerprint(store);
    if (!fingerprint) return std::nullopt;

    *fingerprint += fmt(";%s;%s", flake.lockedRef.subdir, lockFile);

    /* Include revCount and lastModified because they're not
       necessarily implied by the content fingerprint (e.g. for
       tarball flakes) but can influence the evaluation result. */
    if (auto revCount = flake.lockedRef.input.getRevCount())
        *fingerprint += fmt(";revCount=%d", *revCount);
    if (auto lastModified = flake.lockedRef.input.getLastModified())
        *fingerprint += fmt(";lastModified=%d", *lastModified);

    // FIXME: as an optimization, if the flake contains a lock file
    // and we haven't changed it, then it's sufficient to use
    // flake.sourceInfo.storePath for the fingerprint.
    return hashString(HashAlgorithm::SHA256, *fingerprint);
}

Flake::~Flake() { }

}
