#include "util.hh"
#include "get-drvs.hh"
#include "derivations.hh"
#include "store-api.hh"
#include "globals.hh"
#include "shared.hh"
#include "eval.hh"
#include "profiles.hh"


namespace nix {


DrvInfos queryInstalled(EvalState & state, const Path & userEnv)
{
    DrvInfos elems;
    Path manifestFile = userEnv + "/manifest.nix";
    if (pathExists(manifestFile)) {
        Value v;
        state.evalFile(manifestFile, v);
        Bindings bindings;
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
    foreach (DrvInfos::const_iterator, i, elems)
        if (i->queryDrvPath(state) != "")
            drvsToBuild.insert(i->queryDrvPath(state));

    debug(format("building user environment dependencies"));
    store->buildPaths(drvsToBuild, state.repair);

    /* Construct the whole top level derivation. */
    PathSet references;
    Value manifest;
    state.mkList(manifest, elems.size());
    unsigned int n = 0;
    foreach (DrvInfos::iterator, i, elems) {
        /* Create a pseudo-derivation containing the name, system,
           output paths, and optionally the derivation path, as well
           as the meta attributes. */
        Path drvPath = keepDerivations ? i->queryDrvPath(state) : "";

        Value & v(*state.allocValue());
        manifest.list.elems[n++] = &v;
        state.mkAttrs(v, 16);

        mkString(*state.allocAttr(v, state.sType), "derivation");
        mkString(*state.allocAttr(v, state.sName), i->name);
        if (!i->system.empty())
            mkString(*state.allocAttr(v, state.sSystem), i->system);
        mkString(*state.allocAttr(v, state.sOutPath), i->queryOutPath(state));
        if (drvPath != "")
            mkString(*state.allocAttr(v, state.sDrvPath), i->queryDrvPath(state));

        // Copy each output.
        DrvInfo::Outputs outputs = i->queryOutputs(state);
        Value & vOutputs = *state.allocAttr(v, state.sOutputs);
        state.mkList(vOutputs, outputs.size());
        unsigned int m = 0;
        foreach (DrvInfo::Outputs::iterator, j, outputs) {
            mkString(*(vOutputs.list.elems[m++] = state.allocValue()), j->first);
            Value & vOutputs = *state.allocAttr(v, state.symbols.create(j->first));
            state.mkAttrs(vOutputs, 2);
            mkString(*state.allocAttr(vOutputs, state.sOutPath), j->second);

            /* This is only necessary when installing store paths, e.g.,
               `nix-env -i /nix/store/abcd...-foo'. */
            store->addTempRoot(j->second);
            store->ensurePath(j->second);

            references.insert(j->second);
        }

        // Copy the meta attributes.
        Value & vMeta = *state.allocAttr(v, state.sMeta);
        state.mkAttrs(vMeta, 16);

        MetaInfo meta = i->queryMetaInfo(state);

        foreach (MetaInfo::const_iterator, j, meta) {
            Value & v2(*state.allocAttr(vMeta, state.symbols.create(j->first)));
            switch (j->second.type) {
                case MetaValue::tpInt: mkInt(v2, j->second.intValue); break;
                case MetaValue::tpString: mkString(v2, j->second.stringValue); break;
                case MetaValue::tpStrings: {
                    state.mkList(v2, j->second.stringValues.size());
                    unsigned int m = 0;
                    foreach (Strings::const_iterator, k, j->second.stringValues) {
                        v2.list.elems[m] = state.allocValue();
                        mkString(*v2.list.elems[m++], *k);
                    }
                    break;
                }
                default: abort();
            }
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
    DrvInfo topLevelDrv;
    if (!getDerivation(state, topLevel, topLevelDrv, false))
        abort();

    /* Realise the resulting store expression. */
    debug("building user environment");
    store->buildPaths(singleton<PathSet>(topLevelDrv.queryDrvPath(state)), state.repair);

    /* Switch the current user environment to the output path. */
    PathLocks lock;
    lockProfile(lock, profile);

    Path lockTokenCur = optimisticLockProfile(profile);
    if (lockToken != lockTokenCur) {
        printMsg(lvlError, format("profile `%1%' changed while we were busy; restarting") % profile);
        return false;
    }

    debug(format("switching to new user environment"));
    Path generation = createGeneration(profile, topLevelDrv.queryOutPath(state));
    switchLink(profile, generation);

    return true;
}


}
