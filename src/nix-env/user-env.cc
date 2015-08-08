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


alignas(16) static const char sDerivation[] = "derivation";

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
    Value::asMutableList manifestList(manifest);
    unsigned int n = 0;
    for (auto & i : elems) {
        /* Create a pseudo-derivation containing the name, system,
           output paths, and optionally the derivation path, as well
           as the meta attributes. */
        Path drvPath = keepDerivations ? i.queryDrvPath() : "";

        Value & v(*state.allocValue());
        manifestList[n++] = &v;
        state.mkAttrs(v, 16);

        state.allocAttr(v, state.sType)->setStringNoCopy(sDerivation, nullptr);
        state.allocAttr(v, state.sName)->setString(i.name);
        if (!i.system.empty())
            state.allocAttr(v, state.sSystem)->setString(i.system);
        state.allocAttr(v, state.sOutPath)->setString(i.queryOutPath());
        if (drvPath != "")
            state.allocAttr(v, state.sDrvPath)->setString(i.queryDrvPath());

        // Copy each output.
        DrvInfo::Outputs outputs = i.queryOutputs();
        Value & vOutputs = *state.allocAttr(v, state.sOutputs);
        state.mkList(vOutputs, outputs.size());
        Value::asMutableList vOutputsList(vOutputs);
        unsigned int m = 0;
        for (auto & j : outputs) {
            (vOutputsList[m++] = state.allocValue())->setString(j.first);
            Value & vOutputs = *state.allocAttr(v, state.symbols.create(j.first));
            state.mkAttrs(vOutputs, 2);
            state.allocAttr(vOutputs, state.sOutPath)->setString(j.second);

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
            vMeta.asAttrs()->push_back(Attr(state.symbols.create(j), v));
        }
        vMeta.asAttrs()->sort();
        v.asAttrs()->sort();

        if (drvPath != "") references.insert(drvPath);
    }
    manifestList.synchronizeValueContent();

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
    state.allocAttr(args, state.symbols.create("manifest"))->setString(
        manifestFile, singleton<PathSet>(manifestFile));
    args.asAttrs()->push_back(Attr(state.symbols.create("derivations"), &manifest));
    args.asAttrs()->sort();
    topLevel.setApp(envBuilder, args);

    /* Evaluate it. */
    debug("evaluating user environment builder");
    state.forceValue(topLevel);
    PathSet context;
    Attr & aDrvPath(*topLevel.asAttrs()->find(state.sDrvPath));
    Path topLevelDrv = state.coerceToPath(aDrvPath.pos ? *(aDrvPath.pos) : noPos, *(aDrvPath.value), context);
    Attr & aOutPath(*topLevel.asAttrs()->find(state.sOutPath));
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
