#include "nix/util/terminal.hh"
#include "nix/flake/flake.hh"
#include "nix/expr/eval.hh"
#include "nix/expr/eval-settings.hh"
#include "nix/flake/lockfile.hh"
#include "nix/expr/primops.hh"
#include "nix/expr/eval-inline.hh"
#include "nix/store/store-api.hh"
#include "nix/fetchers/fetchers.hh"
#include "nix/util/finally.hh"
#include "nix/fetchers/fetch-settings.hh"
#include "nix/flake/settings.hh"
#include "nix/expr/value-to-json.hh"
#include "nix/store/local-fs-store.hh"
#include "nix/fetchers/fetch-to-store.hh"
#include "nix/util/memory-source-accessor.hh"
#include "nix/fetchers/input-cache.hh"

#include <nlohmann/json.hpp>

namespace nix {

using namespace flake;

namespace flake {

static StorePath copyInputToStore(
    EvalState & state, fetchers::Input & input, const fetchers::Input & originalInput, ref<SourceAccessor> accessor)
{
    auto storePath = fetchToStore(*input.settings, *state.store, accessor, FetchMode::Copy, input.getName());

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

static void expectType(EvalState & state, ValueType type, Value & value, const PosIdx pos)
{
    forceTrivialValue(state, value, pos);
    if (value.type() != type)
        throw Error("expected %s but got %s at %s", showType(type), showType(value.type()), state.positions[pos]);
}

static std::pair<std::map<FlakeId, FlakeInput>, fetchers::Attrs> parseFlakeInputs(
    EvalState & state,
    Value * value,
    const PosIdx pos,
    const InputAttrPath & lockRootAttrPath,
    const SourcePath & flakeDir,
    bool allowSelf);

static void parseFlakeInputAttr(EvalState & state, const Attr & attr, fetchers::Attrs & attrs)
{
// Allow selecting a subset of enum values
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch-enum"
    switch (attr.value->type()) {
    case nString:
        attrs.emplace(state.symbols[attr.name], attr.value->c_str());
        break;
    case nBool:
        attrs.emplace(state.symbols[attr.name], Explicit<bool>{attr.value->boolean()});
        break;
    case nInt: {
        auto intValue = attr.value->integer().value;
        if (intValue < 0)
            state
                .error<EvalError>(
                    "negative value given for flake input attribute %1%: %2%", state.symbols[attr.name], intValue)
                .debugThrow();
        attrs.emplace(state.symbols[attr.name], uint64_t(intValue));
        break;
    }
    default:
        if (attr.name == state.symbols.create("publicKeys")) {
            experimentalFeatureSettings.require(Xp::VerifiedFetches);
            NixStringContext emptyContext = {};
            attrs.emplace(
                state.symbols[attr.name], printValueAsJSON(state, true, *attr.value, attr.pos, emptyContext).dump());
        } else
            state
                .error<TypeError>(
                    "flake input attribute '%s' is %s while a string, Boolean, or integer is expected",
                    state.symbols[attr.name],
                    showType(*attr.value))
                .debugThrow();
    }
#pragma GCC diagnostic pop
}

static FlakeInput parseFlakeInput(
    EvalState & state,
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
                        throw Error(
                            "input attribute path '%s' at %s must be in the same source tree as %s",
                            path,
                            state.positions[attr.pos],
                            flakeDir);
                    url = "path:" + flakeDir.path.makeRelative(path.path);
                } else
                    throw Error(
                        "expected a string or a path but got %s at %s",
                        showType(attr.value->type()),
                        state.positions[attr.pos]);
                attrs.emplace("url", *url);
            } else if (attr.name == sFlake) {
                expectType(state, nBool, *attr.value, attr.pos);
                input.isFlake = attr.value->boolean();
            } else if (attr.name == sInputs) {
                input.overrides =
                    parseFlakeInputs(state, attr.value, attr.pos, lockRootAttrPath, flakeDir, false).first;
            } else if (attr.name == sFollows) {
                expectType(state, nString, *attr.value, attr.pos);
                auto follows(parseInputAttrPath(attr.value->c_str()));
                follows.insert(follows.begin(), lockRootAttrPath.begin(), lockRootAttrPath.end());
                input.follows = follows;
            } else
                parseFlakeInputAttr(state, attr, attrs);
        } catch (Error & e) {
            e.addTrace(
                state.positions[attr.pos], HintFmt("while evaluating flake attribute '%s'", state.symbols[attr.name]));
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

    if (input.ref && input.follows)
        throw Error("flake input has both a flake reference and a follows attribute, at %s", state.positions[pos]);

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
            inputs.emplace(
                inputName, parseFlakeInput(state, inputAttr.value, inputAttr.pos, lockRootAttrPath, flakeDir));
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

    Flake flake{
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
        auto [flakeInputs, selfAttrs] =
            parseFlakeInputs(state, inputs->value, inputs->pos, lockRootAttrPath, flakeDir, true);
        flake.inputs = std::move(flakeInputs);
        flake.selfAttrs = std::move(selfAttrs);
    }

    auto sOutputs = state.symbols.create("outputs");

    if (auto outputs = vInfo.attrs()->get(sOutputs)) {
        expectType(state, nFunction, *outputs->value, outputs->pos);

        if (outputs->value->isLambda() && outputs->value->lambda().fun->hasFormals()) {
            for (auto & formal : outputs->value->lambda().fun->formals->formals) {
                if (formal.name != state.sSelf)
                    flake.inputs.emplace(
                        state.symbols[formal.name],
                        FlakeInput{.ref = parseFlakeRef(state.fetchSettings, std::string(state.symbols[formal.name]))});
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
                    state.symbols[setting.name], std::string(state.forceStringNoCtx(*setting.value, setting.pos, "")));
            else if (setting.value->type() == nPath) {
                auto storePath =
                    fetchToStore(state.fetchSettings, *state.store, setting.value->path(), FetchMode::Copy);
                flake.config.settings.emplace(state.symbols[setting.name], state.store->printStorePath(storePath));
            } else if (setting.value->type() == nInt)
                flake.config.settings.emplace(
                    state.symbols[setting.name], state.forceInt(*setting.value, setting.pos, "").value);
            else if (setting.value->type() == nBool)
                flake.config.settings.emplace(
                    state.symbols[setting.name], Explicit<bool>{state.forceBool(*setting.value, setting.pos, "")});
            else if (setting.value->type() == nList) {
                std::vector<std::string> ss;
                for (auto elem : setting.value->listView()) {
                    if (elem->type() != nString)
                        state
                            .error<TypeError>(
                                "list element in flake configuration setting '%s' is %s while a string is expected",
                                state.symbols[setting.name],
                                showType(*setting.value))
                            .debugThrow();
                    ss.emplace_back(state.forceStringNoCtx(*elem, setting.pos, ""));
                }
                flake.config.settings.emplace(state.symbols[setting.name], ss);
            } else
                state
                    .error<TypeError>(
                        "flake configuration setting '%s' is %s", state.symbols[setting.name], showType(*setting.value))
                    .debugThrow();
        }
    }

    for (auto & attr : *vInfo.attrs()) {
        if (attr.name != state.sDescription && attr.name != sInputs && attr.name != sOutputs && attr.name != sNixConfig)
            throw Error(
                "flake '%s' has an unsupported attribute '%s', at %s",
                resolvedRef,
                state.symbols[attr.name],
                state.positions[attr.pos]);
    }

    return flake;
}

static FlakeRef applySelfAttrs(const FlakeRef & ref, const Flake & flake)
{
    auto newRef(ref);

    StringSet allowedAttrs{"submodules", "lfs"};

    for (auto & attr : flake.selfAttrs) {
        if (!allowedAttrs.contains(attr.first))
            throw Error("flake 'self' attribute '%s' is not supported", attr.first);
        newRef.input.attrs.try_emplace(attr.first, attr.second);
    }

    return newRef;
}

static Flake getFlake(
    EvalState & state,
    const FlakeRef & originalRef,
    fetchers::UseRegistries useRegistries,
    const InputAttrPath & lockRootAttrPath)
{
    // Fetch a lazy tree first.
    auto cachedInput = state.inputCache->getAccessor(state.store, originalRef.input, useRegistries);

    auto resolvedRef = FlakeRef(std::move(cachedInput.resolvedInput), originalRef.subdir);
    auto lockedRef = FlakeRef(std::move(cachedInput.lockedInput), originalRef.subdir);

    // Parse/eval flake.nix to get at the input.self attributes.
    auto flake = readFlake(state, originalRef, resolvedRef, lockedRef, {cachedInput.accessor}, lockRootAttrPath);

    // Re-fetch the tree if necessary.
    auto newLockedRef = applySelfAttrs(lockedRef, flake);

    if (lockedRef != newLockedRef) {
        debug("refetching input '%s' due to self attribute", newLockedRef);
        // FIXME: need to remove attrs that are invalidated by the changed input attrs, such as 'narHash'.
        newLockedRef.input.attrs.erase("narHash");
        auto cachedInput2 = state.inputCache->getAccessor(state.store, newLockedRef.input, fetchers::UseRegistries::No);
        cachedInput.accessor = cachedInput2.accessor;
        lockedRef = FlakeRef(std::move(cachedInput2.lockedInput), newLockedRef.subdir);
    }

    // Copy the tree to the store.
    auto storePath = copyInputToStore(state, lockedRef.input, originalRef.input, cachedInput.accessor);

    // Re-parse flake.nix from the store.
    return readFlake(state, originalRef, resolvedRef, lockedRef, state.storePath(storePath), lockRootAttrPath);
}

Flake getFlake(EvalState & state, const FlakeRef & originalRef, fetchers::UseRegistries useRegistries)
{
    return getFlake(state, originalRef, useRegistries, {});
}

static LockFile readLockFile(const fetchers::Settings & fetchSettings, const SourcePath & lockFilePath)
{
    return lockFilePath.pathExists() ? LockFile(fetchSettings, lockFilePath.readFile(), fmt("%s", lockFilePath))
                                     : LockFile();
}

/* Compute an in-memory lock file for the specified top-level flake,
   and optionally write it to file, if the flake is writable. */
LockedFlake
lockFlake(const Settings & settings, EvalState & state, const FlakeRef & topRef, const LockFlags & lockFlags)
{
    experimentalFeatureSettings.require(Xp::Flakes);

    auto useRegistries = lockFlags.useRegistries.value_or(settings.useRegistries);
    auto useRegistriesTop = useRegistries ? fetchers::UseRegistries::All : fetchers::UseRegistries::No;
    auto useRegistriesInputs = useRegistries ? fetchers::UseRegistries::Limited : fetchers::UseRegistries::No;

    auto flake = getFlake(state, topRef, useRegistriesTop, {});

    if (lockFlags.applyNixConfig) {
        flake.config.apply(settings);
        state.store->setOptions();
    }

    try {
        if (!state.fetchSettings.allowDirty && lockFlags.referenceLockFilePath) {
            throw Error("reference lock file was provided, but the `allow-dirty` setting is set to false");
        }

        auto oldLockFile =
            readLockFile(state.fetchSettings, lockFlags.referenceLockFilePath.value_or(flake.lockFilePath()));

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
                OverrideTarget{
                    .input = FlakeInput{.ref = i.second},
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
                           bool trustLock) {
            debug("computing lock file node '%s'", printInputAttrPath(inputAttrPathPrefix));

            /* Get the overrides (i.e. attributes of the form
               'inputs.nixops.inputs.nixpkgs.url = ...'). */
            std::function<void(const FlakeInput & input, const InputAttrPath & prefix)> addOverrides;
            addOverrides = [&](const FlakeInput & input, const InputAttrPath & prefix) {
                for (auto & [idOverride, inputOverride] : input.overrides) {
                    auto inputAttrPath(prefix);
                    inputAttrPath.push_back(idOverride);
                    if (inputOverride.ref || inputOverride.follows)
                        overrides.emplace(
                            inputAttrPath,
                            OverrideTarget{
                                .input = inputOverride,
                                .sourcePath = sourcePath,
                                .parentInputAttrPath = inputAttrPathPrefix});
                    addOverrides(inputOverride, inputAttrPath);
                }
            };

            for (auto & [id, input] : flakeInputs) {
                auto inputAttrPath(inputAttrPathPrefix);
                inputAttrPath.push_back(id);
                addOverrides(input, inputAttrPath);
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
                        printInputAttrPath(inputAttrPathPrefix),
                        follow);
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
                    auto overriddenSourcePath = hasOverride ? i->second.sourcePath : sourcePath;

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

                    if (!input.ref)
                        input.ref =
                            FlakeRef::fromAttrs(state.fetchSettings, {{"type", "indirect"}, {"id", std::string(id)}});

                    auto overriddenParentPath =
                        input.ref->input.isRelative()
                            ? std::optional<InputAttrPath>(
                                  hasOverride ? i->second.parentInputAttrPath : inputAttrPathPrefix)
                            : std::nullopt;

                    auto resolveRelativePath = [&]() -> std::optional<SourcePath> {
                        if (auto relativePath = input.ref->input.isRelative()) {
                            return SourcePath{
                                overriddenSourcePath.accessor,
                                CanonPath(*relativePath, overriddenSourcePath.path.parent().value())};
                        } else
                            return std::nullopt;
                    };

                    /* Get the input flake, resolve 'path:./...'
                       flakerefs relative to the parent flake. */
                    auto getInputFlake = [&](const FlakeRef & ref, const fetchers::UseRegistries useRegistries) {
                        if (auto resolvedPath = resolveRelativePath()) {
                            return readFlake(state, ref, ref, ref, *resolvedPath, inputAttrPath);
                        } else {
                            return getFlake(state, ref, useRegistries, inputAttrPath);
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

                    if (oldLock && oldLock->originalRef.canonicalize() == input.ref->canonicalize()
                        && oldLock->parentInputAttrPath == overriddenParentPath && !hasCliOverride) {
                        debug("keeping existing input '%s'", inputAttrPathS);

                        /* Copy the input from the old lock since its flakeref
                           didn't change and there is no override from a
                           higher level flake. */
                        auto childNode = make_ref<LockedNode>(
                            oldLock->lockedRef, oldLock->originalRef, oldLock->isFlake, oldLock->parentInputAttrPath);

                        node->inputs.insert_or_assign(id, childNode);

                        /* If we have this input in updateInputs, then we
                           must fetch the flake to update it. */
                        auto lb = lockFlags.inputUpdates.lower_bound(inputAttrPath);

                        auto mustRefetch = lb != lockFlags.inputUpdates.end() && lb->size() > inputAttrPath.size()
                                           && std::equal(inputAttrPath.begin(), inputAttrPath.end(), lb->begin());

                        FlakeInputs fakeInputs;

                        if (!mustRefetch) {
                            /* No need to fetch this flake, we can be
                               lazy. However there may be new overrides on the
                               inputs of this flake, so we need to check
                               those. */
                            for (auto & i : oldLock->inputs) {
                                if (auto lockedNode = std::get_if<0>(&i.second)) {
                                    fakeInputs.emplace(
                                        i.first,
                                        FlakeInput{
                                            .ref = (*lockedNode)->originalRef,
                                            .isFlake = (*lockedNode)->isFlake,
                                        });
                                } else if (auto follows = std::get_if<1>(&i.second)) {
                                    if (!trustLock) {
                                        // It is possible that the flake has changed,
                                        // so we must confirm all the follows that are in the lock file are also in the
                                        // flake.
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
                                    fakeInputs.emplace(
                                        i.first,
                                        FlakeInput{
                                            .follows = absoluteFollows,
                                        });
                                }
                            }
                        }

                        if (mustRefetch) {
                            auto inputFlake = getInputFlake(oldLock->lockedRef, useRegistriesInputs);
                            nodePaths.emplace(childNode, inputFlake.path.parent());
                            computeLocks(
                                inputFlake.inputs,
                                childNode,
                                inputAttrPath,
                                oldLock,
                                followsPrefix,
                                inputFlake.path,
                                false);
                        } else {
                            computeLocks(
                                fakeInputs, childNode, inputAttrPath, oldLock, followsPrefix, sourcePath, true);
                        }

                    } else {
                        /* We need to create a new lock file entry. So fetch
                           this input. */
                        debug("creating new input '%s'", inputAttrPathS);

                        if (!lockFlags.allowUnlocked && !input.ref->input.isLocked() && !input.ref->input.isRelative())
                            throw Error("cannot update unlocked flake input '%s' in pure mode", inputAttrPathS);

                        /* Note: in case of an --override-input, we use
                            the *original* ref (input2.ref) for the
                            "original" field, rather than the
                            override. This ensures that the override isn't
                            nuked the next time we update the lock
                            file. That is, overrides are sticky unless you
                            use --no-write-lock-file. */
                        auto inputIsOverride = explicitCliOverrides.contains(inputAttrPath);
                        auto ref = (input2.ref && inputIsOverride) ? *input2.ref : *input.ref;

                        if (input.isFlake) {
                            auto inputFlake = getInputFlake(
                                *input.ref, inputIsOverride ? fetchers::UseRegistries::All : useRegistriesInputs);

                            auto childNode =
                                make_ref<LockedNode>(inputFlake.lockedRef, ref, true, overriddenParentPath);

                            node->inputs.insert_or_assign(id, childNode);

                            /* Guard against circular flake imports. */
                            for (auto & parent : parents)
                                if (parent == *input.ref)
                                    throw Error("found circular import of flake '%s'", parent);
                            parents.push_back(*input.ref);
                            Finally cleanup([&]() { parents.pop_back(); });

                            /* Recursively process the inputs of this
                               flake, using its own lock file. */
                            nodePaths.emplace(childNode, inputFlake.path.parent());
                            computeLocks(
                                inputFlake.inputs,
                                childNode,
                                inputAttrPath,
                                readLockFile(state.fetchSettings, inputFlake.lockFilePath()).root.get_ptr(),
                                inputAttrPath,
                                inputFlake.path,
                                false);
                        }

                        else {
                            auto [path, lockedRef] = [&]() -> std::tuple<SourcePath, FlakeRef> {
                                // Handle non-flake 'path:./...' inputs.
                                if (auto resolvedPath = resolveRelativePath()) {
                                    return {*resolvedPath, *input.ref};
                                } else {
                                    auto cachedInput = state.inputCache->getAccessor(
                                        state.store, input.ref->input, useRegistriesInputs);

                                    auto lockedRef = FlakeRef(std::move(cachedInput.lockedInput), input.ref->subdir);

                                    // FIXME: allow input to be lazy.
                                    auto storePath = copyInputToStore(
                                        state, lockedRef.input, input.ref->input, cachedInput.accessor);

                                    return {state.storePath(storePath), lockedRef};
                                }
                            }();

                            auto childNode = make_ref<LockedNode>(lockedRef, ref, false, overriddenParentPath);

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
                warn(
                    "the flag '--override-input %s %s' does not match any input",
                    printInputAttrPath(i.first),
                    i.second);

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
                                "Not writing lock file of flake '%s' because it has an unlocked input ('%s'). "
                                "Use '--allow-dirty-locks' to allow this anyway.",
                                topRef,
                                *unlockedInput);
                        if (state.fetchSettings.warnDirty)
                            warn(
                                "not writing lock file of flake '%s' because it has an unlocked input ('%s')",
                                topRef,
                                *unlockedInput);
                    } else {
                        if (!lockFlags.updateLockFile)
                            throw Error(
                                "flake '%s' requires lock file changes but they're not allowed due to '--no-update-lock-file'",
                                topRef);

                        auto newLockFileS = fmt("%s\n", newLockFile);

                        if (lockFlags.outputLockFilePath) {
                            if (lockFlags.commitLockFile)
                                throw Error("'--commit-lock-file' and '--output-lock-file' are incompatible");
                            writeFile(*lockFlags.outputLockFilePath, newLockFileS);
                        } else {
                            auto relPath = (topRef.subdir == "" ? "" : topRef.subdir + "/") + "flake.lock";
                            auto outputLockFilePath = *sourcePath / relPath;

                            bool lockFileExists = pathExists(outputLockFilePath);

                            auto s = chomp(diff);
                            if (lockFileExists) {
                                if (s.empty())
                                    warn("updating lock file %s", outputLockFilePath);
                                else
                                    warn("updating lock file %s:\n%s", outputLockFilePath, s);
                            } else
                                warn("creating lock file %s: \n%s", outputLockFilePath, s);

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
                                newLockFileS,
                                commitMessage);
                        }

                        /* Rewriting the lockfile changed the top-level
                           repo, so we should re-read it. FIXME: we could
                           also just clear the 'rev' field... */
                        auto prevLockedRef = flake.lockedRef;
                        flake = getFlake(state, topRef, useRegistriesTop);

                        if (lockFlags.commitLockFile && flake.lockedRef.input.getRev()
                            && prevLockedRef.input.getRev() != flake.lockedRef.input.getRev())
                            warn("committed new revision '%s'", flake.lockedRef.input.getRev()->gitRev());
                    }
                } else
                    throw Error(
                        "cannot write modified lock file of flake '%s' (use '--no-write-lock-file' to ignore)", topRef);
            } else {
                warn("not writing modified lock file of flake '%s':\n%s", topRef, chomp(diff));
                flake.forceDirty = true;
            }
        }

        return LockedFlake{
            .flake = std::move(flake), .lockFile = std::move(newLockFile), .nodePaths = std::move(nodePaths)};

    } catch (Error & e) {
        e.addTrace({}, "while updating the lock file of flake '%s'", flake.lockedRef.to_string());
        throw;
    }
}

static ref<SourceAccessor> makeInternalFS()
{
    auto internalFS = make_ref<MemorySourceAccessor>(MemorySourceAccessor{});
    internalFS->setPathDisplay("«flakes-internal»", "");
    internalFS->addFile(
        CanonPath("call-flake.nix"),
#include "call-flake.nix.gen.hh"
    );
    return internalFS;
}

static auto internalFS = makeInternalFS();

static Value * requireInternalFile(EvalState & state, CanonPath path)
{
    SourcePath p{internalFS, path};
    auto v = state.allocValue();
    state.evalFile(p, *v); // has caching
    return v;
}

void callFlake(EvalState & state, const LockedFlake & lockedFlake, Value & vRes)
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

        override.alloc(state.symbols.create("dir")).mkString(CanonPath(subdir).rel());

        overrides.alloc(state.symbols.create(key->second)).mkAttrs(override);
    }

    auto & vOverrides = state.allocValue()->mkAttrs(overrides);

    Value * vCallFlake = requireInternalFile(state, CanonPath("call-flake.nix"));

    auto vLocks = state.allocValue();
    vLocks->mkString(lockFileStr);

    auto vFetchFinalTree = get(state.internalPrimOps, "fetchFinalTree");
    assert(vFetchFinalTree);

    Value * args[] = {vLocks, &vOverrides, *vFetchFinalTree};
    state.callFunction(*vCallFlake, args, vRes, noPos);
}

} // namespace flake

std::optional<Fingerprint> LockedFlake::getFingerprint(ref<Store> store, const fetchers::Settings & fetchSettings) const
{
    if (lockFile.isUnlocked(fetchSettings))
        return std::nullopt;

    auto fingerprint = flake.lockedRef.input.getFingerprint(store);
    if (!fingerprint)
        return std::nullopt;

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

Flake::~Flake() {}

} // namespace nix
