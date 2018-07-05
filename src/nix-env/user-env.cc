#include "user-env.hh"
#include "util.hh"
#include "derivations.hh"
#include "store-api.hh"
#include "globals.hh"
#include "shared.hh"
#include "eval.hh"
#include "eval-inline.hh"
#include "profiles.hh"


namespace nix {


DrvInfos queryInstalled(EvalState & state, const Path & userEnv)
{
    DrvInfos elems;
    Path manifestFile = userEnv + "/manifest.nix";
    if (pathExists(manifestFile)) {
        Value v;
        state.evalFile(manifestFile, v);
        Bindings * bindings = BindingsBuilder(0).result();
        getDerivations(state, v, "", *bindings, elems, false);
    }
    return elems;
}


bool createUserEnv(EvalState & state, DrvInfos & elems,
    const Path & profile, bool keepDerivations,
    const string & lockToken)
{
    /* Build the components in the user environment, if they don't
       exist already. */
    PathSet drvsToBuild;
    for (auto & i : elems)
        if (i.queryDrvPath() != "")
            drvsToBuild.insert(i.queryDrvPath());

    debug(format("building user environment dependencies"));
    state.store->buildPaths(drvsToBuild, state.repair ? bmRepair : bmNormal);

    /* Construct the whole top level derivation. */
    PathSet references;
    Value * manifest = state.allocValue();
    state.mkList(*manifest, elems.size());
    unsigned int n = 0;
    for (auto & i : elems) {
        /* Create a pseudo-derivation containing the name, system,
           output paths, and optionally the derivation path, as well
           as the meta attributes. */
        Path drvPath = keepDerivations ? i.queryDrvPath() : "";

        Value * v = state.allocValue();
        manifest->listElems()[n++] = v;

        BindingsBuilder bb(16);

        Value * vv1 = state.allocValue();
        mkString(*vv1, "derivation");
        bb.push_back(state.sType, vv1, &noPos);

        Value * vv2 = state.allocValue();
        mkString(*vv2, i.queryName());
        bb.push_back(state.sName, vv2, &noPos);

        auto system = i.querySystem();
        if (!system.empty()) {
            Value * vv3 = state.allocValue();
            mkString(*vv3, system);
            bb.push_back(state.sSystem, vv3, &noPos);
        }

        Value * vv4 = state.allocValue();
        mkString(*vv4, i.queryOutPath());
        bb.push_back(state.sOutPath, vv4, &noPos);

        if (drvPath != "") {
            Value * vv5 = state.allocValue();
            mkString(*vv5, i.queryDrvPath());
            bb.push_back(state.sDrvPath, vv5, &noPos);
        }

        // Copy each output meant for installation.
        DrvInfo::Outputs outputs = i.queryOutputs(true);

        Value * vOutputs = state.allocValue();
        state.mkList(*vOutputs, outputs.size());
        bb.push_back(state.sOutputs, vOutputs, &noPos);

        unsigned int m = 0;
        for (auto & j : outputs) {
            mkString(*(vOutputs->listElems()[m++] = state.allocValue()), j.first);
            Value * vOutputs = state.allocValue();
            bb.push_back(state.symbols.create(j.first), vOutputs, &noPos);

            BindingsBuilder bbOutputs(2);
            Value * vv6 = state.allocValue();
            mkString(*vv6, j.second);
            bbOutputs.push_back(state.sOutPath, vv6, &noPos);
            state.mkAttrs(*vOutputs, bbOutputs);

            /* This is only necessary when installing store paths, e.g.,
               `nix-env -i /nix/store/abcd...-foo'. */
            state.store->addTempRoot(j.second);
            state.store->ensurePath(j.second);

            references.insert(j.second);
        }

        // Copy the meta attributes.
        Value * vMeta = state.allocValue();
        bb.push_back(state.sMeta, vMeta, &noPos);

        BindingsBuilder bbMeta(16);
        StringSet metaNames = i.queryMetaNames();
        for (auto & j : metaNames) {
            Value * v = i.queryMeta(j);
            if (!v) continue;
            bbMeta.push_back(state.symbols.create(j), v, &noPos);
        }
        state.mkAttrs(*vMeta, bbMeta);
        state.mkAttrs(*v, bb);

        if (drvPath != "") references.insert(drvPath);
    }

    /* Also write a copy of the list of user environment elements to
       the store; we need it for future modifications of the
       environment. */
    Path manifestFile = state.store->addTextToStore("env-manifest.nix",
        (format("%1%") % *manifest).str(), references);

    /* Get the environment builder expression. */
    Value envBuilder;
    state.evalFile(state.findFile("nix/buildenv.nix"), envBuilder);

    /* Construct a Nix expression that calls the user environment
       builder with the manifest as argument. */
    Value args, topLevel;
    BindingsBuilder bb(3);
    Value * vManifest = state.allocValue();
    mkString(*vManifest, manifestFile, {manifestFile});
    bb.push_back(state.symbols.create("manifest"), vManifest, &noPos);
    bb.push_back(state.symbols.create("derivations"), manifest, &noPos);
    state.mkAttrs(args, bb);

    mkApp(topLevel, envBuilder, args);

    /* Evaluate it. */
    debug("evaluating user environment builder");
    state.forceValue(topLevel);
    PathSet context;
    Bindings::find_iterator aDrvPath = topLevel.attrs->find(state.sDrvPath);
    Path topLevelDrv = state.coerceToPath(aDrvPath.pos() ? *aDrvPath.pos() : noPos, *aDrvPath.value(), context);
    Bindings::find_iterator aOutPath = topLevel.attrs->find(state.sOutPath);
    Path topLevelOut = state.coerceToPath(aOutPath.pos() ? *aOutPath.pos() : noPos, *aOutPath.value(), context);

    /* Realise the resulting store expression. */
    debug("building user environment");
    state.store->buildPaths({topLevelDrv}, state.repair ? bmRepair : bmNormal);

    /* Switch the current user environment to the output path. */
    auto store2 = state.store.dynamic_pointer_cast<LocalFSStore>();

    if (store2) {
        PathLocks lock;
        lockProfile(lock, profile);

        Path lockTokenCur = optimisticLockProfile(profile);
        if (lockToken != lockTokenCur) {
            printError(format("profile '%1%' changed while we were busy; restarting") % profile);
            return false;
        }

        debug(format("switching to new user environment"));
        Path generation = createGeneration(ref<LocalFSStore>(store2), profile, topLevelOut);
        switchLink(profile, generation);
    }

    return true;
}


}
