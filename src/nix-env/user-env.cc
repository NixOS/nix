#include "user-env.hh"
#include "util.hh"
#include "derivations.hh"
#include "store-api.hh"
#include "path-with-outputs.hh"
#include "local-fs-store.hh"
#include "globals.hh"
#include "shared.hh"
#include "eval.hh"
#include "eval-inline.hh"
#include "profiles.hh"


namespace nix {


DrvInfos queryInstalled(EvalState & state, const Path & userEnv)
{
    DrvInfos elems;
    if (pathExists(userEnv + "/manifest.json"))
        throw Error("profile '%s' is incompatible with 'nix-env'; please use 'nix profile' instead", userEnv);
    Path manifestFile = userEnv + "/manifest.nix";
    if (pathExists(manifestFile)) {
        Value v;
        state.evalFile(manifestFile, v);
        Bindings & bindings(*state.allocBindings(0));
        getDerivations(state, v, "", bindings, elems, false);
    }
    return elems;
}


bool createUserEnv(EvalState & state, DrvInfos & elems,
    const Path & profile, bool keepDerivations,
    const string & lockToken)
{
    /* Build the components in the user environment, if they don't
       exist already. */
    std::vector<StorePathWithOutputs> drvsToBuild;
    for (auto & i : elems)
        if (i.queryDrvPath() != "")
            drvsToBuild.push_back({state.store->parseStorePath(i.queryDrvPath())});

    debug(format("building user environment dependencies"));
    state.store->buildPaths(
        toDerivedPaths(drvsToBuild),
        state.repair ? bmRepair : bmNormal);

    /* Construct the whole top level derivation. */
    StorePathSet references;
    Value manifest;
    state.mkList(manifest, elems.size());
    size_t n = 0;
    for (auto & i : elems) {
        /* Create a pseudo-derivation containing the name, system,
           output paths, and optionally the derivation path, as well
           as the meta attributes. */
        Path drvPath = keepDerivations ? i.queryDrvPath() : "";
        DrvInfo::Outputs outputs = i.queryOutputs(true);
        StringSet metaNames = i.queryMetaNames();

        auto attrs = state.buildBindings(7 + outputs.size());

        attrs.alloc(state.sType).mkString("derivation");
        attrs.alloc(state.sName).mkString(i.queryName());
        auto system = i.querySystem();
        if (!system.empty())
            attrs.alloc(state.sSystem).mkString(system);
        attrs.alloc(state.sOutPath).mkString(i.queryOutPath());
        if (drvPath != "")
            attrs.alloc(state.sDrvPath).mkString(i.queryDrvPath());

        // Copy each output meant for installation.
        auto & vOutputs = attrs.alloc(state.sOutputs);
        state.mkList(vOutputs, outputs.size());
        for (const auto & [m, j] : enumerate(outputs)) {
            (vOutputs.listElems()[m] = state.allocValue())->mkString(j.first);
            auto outputAttrs = state.buildBindings(2);
            outputAttrs.alloc(state.sOutPath).mkString(j.second);
            attrs.alloc(j.first).mkAttrs(outputAttrs);

            /* This is only necessary when installing store paths, e.g.,
               `nix-env -i /nix/store/abcd...-foo'. */
            state.store->addTempRoot(state.store->parseStorePath(j.second));
            state.store->ensurePath(state.store->parseStorePath(j.second));

            references.insert(state.store->parseStorePath(j.second));
        }

        // Copy the meta attributes.
        auto meta = state.buildBindings(metaNames.size());
        for (auto & j : metaNames) {
            Value * v = i.queryMeta(j);
            if (!v) continue;
            meta.insert(state.symbols.create(j), v);
        }

        attrs.alloc(state.sMeta).mkAttrs(meta);

        (manifest.listElems()[n++] = state.allocValue())->mkAttrs(attrs);

        if (drvPath != "") references.insert(state.store->parseStorePath(drvPath));
    }

    /* Also write a copy of the list of user environment elements to
       the store; we need it for future modifications of the
       environment. */
    auto manifestFile = state.store->addTextToStore("env-manifest.nix",
        fmt("%s", manifest), references);

    /* Get the environment builder expression. */
    Value envBuilder;
    state.eval(state.parseExprFromString(
        #include "buildenv.nix.gen.hh"
            , "/"), envBuilder);

    /* Construct a Nix expression that calls the user environment
       builder with the manifest as argument. */
    auto attrs = state.buildBindings(3);
    attrs.alloc("manifest").mkString(
        state.store->printStorePath(manifestFile),
        {state.store->printStorePath(manifestFile)});
    attrs.insert(state.symbols.create("derivations"), &manifest);
    Value args;
    args.mkAttrs(attrs);

    Value topLevel;
    topLevel.mkApp(&envBuilder, &args);

    /* Evaluate it. */
    debug("evaluating user environment builder");
    state.forceValue(topLevel);
    PathSet context;
    Attr & aDrvPath(*topLevel.attrs->find(state.sDrvPath));
    auto topLevelDrv = state.store->parseStorePath(state.coerceToPath(*aDrvPath.pos, *aDrvPath.value, context));
    Attr & aOutPath(*topLevel.attrs->find(state.sOutPath));
    Path topLevelOut = state.coerceToPath(*aOutPath.pos, *aOutPath.value, context);

    /* Realise the resulting store expression. */
    debug("building user environment");
    std::vector<StorePathWithOutputs> topLevelDrvs;
    topLevelDrvs.push_back({topLevelDrv});
    state.store->buildPaths(
        toDerivedPaths(topLevelDrvs),
        state.repair ? bmRepair : bmNormal);

    /* Switch the current user environment to the output path. */
    auto store2 = state.store.dynamic_pointer_cast<LocalFSStore>();

    if (store2) {
        PathLocks lock;
        lockProfile(lock, profile);

        Path lockTokenCur = optimisticLockProfile(profile);
        if (lockToken != lockTokenCur) {
            printInfo("profile '%1%' changed while we were busy; restarting", profile);
            return false;
        }

        debug(format("switching to new user environment"));
        Path generation = createGeneration(ref<LocalFSStore>(store2), profile,
            store2->parseStorePath(topLevelOut));
        switchLink(profile, generation);
    }

    return true;
}


}
