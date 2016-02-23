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
    PathSet drvsToBuild;
    for (auto & i : elems)
        if (i.queryDrvPath() != "")
            drvsToBuild.insert(i.queryDrvPath());

    debug(format("building user environment dependencies"));
    store->buildPaths(drvsToBuild, state.repair ? bmRepair : bmNormal);

    /* Construct the whole top level derivation. */
    PathSet references;
    Value manifest;
    state.mkList(manifest, elems.size());
    unsigned int n = 0;
    for (auto & i : elems) {
        /* Create a pseudo-derivation containing the name, system,
           output paths, and optionally the derivation path, as well
           as the meta attributes. */
        Path drvPath = keepDerivations ? i.queryDrvPath() : "";

        Value & v(*state.allocValue());
        manifest.listElems()[n++] = &v;
        state.mkAttrs(v, 16);

        mkString(*state.allocAttr(v, state.sType), "derivation");
        mkString(*state.allocAttr(v, state.sName), i.name);
        if (!i.system.empty())
            mkString(*state.allocAttr(v, state.sSystem), i.system);
        mkString(*state.allocAttr(v, state.sOutPath), i.queryOutPath());
        if (drvPath != "")
            mkString(*state.allocAttr(v, state.sDrvPath), i.queryDrvPath());

        // Copy each output meant for installation.
        DrvInfo::Outputs outputs = i.queryOutputs(true);
        Value & vOutputs = *state.allocAttr(v, state.sOutputs);
        state.mkList(vOutputs, outputs.size());
        unsigned int m = 0;
        for (auto & j : outputs) {
            mkString(*(vOutputs.listElems()[m++] = state.allocValue()), j.first);
            Value & vOutputs = *state.allocAttr(v, state.symbols.create(j.first));
            state.mkAttrs(vOutputs, 2);
            mkString(*state.allocAttr(vOutputs, state.sOutPath), j.second);

            /* This is only necessary when installing store paths, e.g.,
               `nix-env -i /nix/store/abcd...-foo'. */
            store->addTempRoot(j.second);
            store->ensurePath(j.second);

            references.insert(j.second);
        }

        // Copy the meta attributes.
        Value & vMeta = *state.allocAttr(v, state.sMeta);
        state.mkAttrs(vMeta, 16);
        StringSet metaNames = i.queryMetaNames();
        for (auto & j : metaNames) {
            Value * v = i.queryMeta(j);
            if (!v) continue;
            vMeta.attrs->push_back(Attr(state.symbols.create(j), v));
        }
        vMeta.attrs->sort();
        v.attrs->sort();

        if (drvPath != "") references.insert(drvPath);
    }

    /* Also write a copy of the list of user environment elements to
       the store; we need it for future modifications of the
       environment. */
    Path manifestFile = store->addTextToStore("env-manifest.nix",
        (format("%1%") % manifest).str(), references);

    /* Get the environment builder expression. */
    Value envBuilder;
    state.evalFile(state.findFile("nix/buildenv.nix"), envBuilder);

    /* Construct a Nix expression that calls the user environment
       builder with the manifest as argument. */
    Value args, topLevel;
    state.mkAttrs(args, 3);
    mkString(*state.allocAttr(args, state.symbols.create("manifest")),
        manifestFile, singleton<PathSet>(manifestFile));
    args.attrs->push_back(Attr(state.symbols.create("derivations"), &manifest));
    args.attrs->sort();
    mkApp(topLevel, envBuilder, args);

    /* Evaluate it. */
    debug("evaluating user environment builder");
    state.forceValue(topLevel);
    PathSet context;
    Attr & aDrvPath(*topLevel.attrs->find(state.sDrvPath));
    Path topLevelDrv = state.coerceToPath(aDrvPath.pos ? *(aDrvPath.pos) : noPos, *(aDrvPath.value), context);
    Attr & aOutPath(*topLevel.attrs->find(state.sOutPath));
    Path topLevelOut = state.coerceToPath(aOutPath.pos ? *(aOutPath.pos) : noPos, *(aOutPath.value), context);

    /* Realise the resulting store expression. */
    debug("building user environment");
    store->buildPaths(singleton<PathSet>(topLevelDrv), state.repair ? bmRepair : bmNormal);

    /* Switch the current user environment to the output path. */
    PathLocks lock;
    lockProfile(lock, profile);

    Path lockTokenCur = optimisticLockProfile(profile);
    if (lockToken != lockTokenCur) {
        printMsg(lvlError, format("profile ‘%1%’ changed while we were busy; restarting") % profile);
        return false;
    }

    debug(format("switching to new user environment"));
    Path generation = createGeneration(profile, topLevelOut);
    switchLink(profile, generation);

    return true;
}


}
