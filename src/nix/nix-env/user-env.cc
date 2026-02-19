#include "user-env.hh"
#include "nix/store/derivations.hh"
#include "nix/store/store-api.hh"
#include "nix/store/path-with-outputs.hh"
#include "nix/store/local-fs-store.hh"
#include "nix/store/globals.hh"
#include "nix/main/shared.hh"
#include "nix/expr/eval.hh"
#include "nix/expr/eval-inline.hh"
#include "nix/store/profiles.hh"
#include "nix/expr/print-ambiguous.hh"
#include "nix/expr/static-string-data.hh"

#include <limits>
#include <sstream>

namespace nix {

PackageInfos queryInstalled(EvalState & state, const std::filesystem::path & userEnv)
{
    PackageInfos elems;
    if (pathExists(userEnv / "manifest.json"))
        throw Error("profile %s is incompatible with 'nix-env'; please use 'nix profile' instead", PathFmt(userEnv));
    auto manifestFile = userEnv / "manifest.nix";
    if (pathExists(manifestFile)) {
        Value v;
        state.evalFile(state.rootPath(CanonPath(std::filesystem::weakly_canonical(manifestFile).string())), v);
        Bindings & bindings = Bindings::emptyBindings;
        getDerivations(state, v, "", bindings, elems, false);
    }
    return elems;
}

bool createUserEnv(
    EvalState & state,
    PackageInfos & elems,
    const std::filesystem::path & profile,
    bool keepDerivations,
    const std::string & lockToken)
{
    /* Build the components in the user environment, if they don't
       exist already. */
    std::vector<StorePathWithOutputs> drvsToBuild;
    for (auto & i : elems)
        if (auto drvPath = i.queryDrvPath())
            drvsToBuild.push_back({*drvPath});

    debug("building user environment dependencies");
    state.store->buildPaths(toDerivedPaths(drvsToBuild), state.repair ? bmRepair : bmNormal);

    /* Construct the whole top level derivation. */
    StorePathSet references;
    auto list = state.buildList(elems.size());
    for (const auto & [n, i] : enumerate(elems)) {
        /* Create a pseudo-derivation containing the name, system,
           output paths, and optionally the derivation path, as well
           as the meta attributes. */
        std::optional<StorePath> drvPath = keepDerivations ? i.queryDrvPath() : std::nullopt;
        PackageInfo::Outputs outputs = i.queryOutputs(true, true);
        StringSet metaNames = i.queryMetaNames();

        auto attrs = state.buildBindings(7 + outputs.size());

        attrs.alloc(state.s.type).mkStringNoCopy("derivation"_sds);
        attrs.alloc(state.s.name).mkString(i.queryName(), state.mem);
        auto system = i.querySystem();
        if (!system.empty())
            attrs.alloc(state.s.system).mkString(system, state.mem);
        attrs.alloc(state.s.outPath).mkString(state.store->printStorePath(i.queryOutPath()), state.mem);
        if (drvPath)
            attrs.alloc(state.s.drvPath).mkString(state.store->printStorePath(*drvPath), state.mem);

        // Copy each output meant for installation.
        auto outputsList = state.buildList(outputs.size());
        for (const auto & [m, j] : enumerate(outputs)) {
            (outputsList[m] = state.allocValue())->mkString(j.first, state.mem);
            auto outputAttrs = state.buildBindings(2);
            outputAttrs.alloc(state.s.outPath).mkString(state.store->printStorePath(*j.second), state.mem);
            attrs.alloc(j.first).mkAttrs(outputAttrs);

            /* This is only necessary when installing store paths, e.g.,
               `nix-env -i /nix/store/abcd...-foo'. */
            state.store->addTempRoot(*j.second);
            state.store->ensurePath(*j.second);

            references.insert(*j.second);
        }
        attrs.alloc(state.s.outputs).mkList(outputsList);

        // Copy the meta attributes.
        auto meta = state.buildBindings(metaNames.size());
        for (auto & j : metaNames) {
            Value * v = i.queryMeta(j);
            if (!v)
                continue;
            meta.insert(state.symbols.create(j), v);
        }

        attrs.alloc(state.s.meta).mkAttrs(meta);

        (list[n] = state.allocValue())->mkAttrs(attrs);

        if (drvPath)
            references.insert(*drvPath);
    }

    Value manifest;
    manifest.mkList(list);

    /* Also write a copy of the list of user environment elements to
       the store; we need it for future modifications of the
       environment. */
    auto manifestFile = ({
        std::ostringstream str;
        printAmbiguous(state, manifest, str, nullptr);
        StringSource source{str.view()};
        state.store->addToStoreFromDump(
            source,
            "env-manifest.nix",
            FileSerialisationMethod::Flat,
            ContentAddressMethod::Raw::Text,
            HashAlgorithm::SHA256,
            references);
    });

    /* Get the environment builder expression. */
    Value envBuilder;
    state.eval(
        state.parseExprFromString(
#include "buildenv.nix.gen.hh"
            , state.rootPath(CanonPath::root)),
        envBuilder);

    /* Construct a Nix expression that calls the user environment
       builder with the manifest as argument. */
    auto attrs = state.buildBindings(3);
    state.mkStorePathString(manifestFile, attrs.alloc("manifest"));
    attrs.insert(state.symbols.create("derivations"), &manifest);
    Value args;
    args.mkAttrs(attrs);

    Value topLevel;
    topLevel.mkApp(&envBuilder, &args);

    /* Evaluate it. */
    debug("evaluating user environment builder");
    state.forceValue(topLevel, topLevel.determinePos(noPos));
    NixStringContext context;
    auto & aDrvPath(*topLevel.attrs()->get(state.s.drvPath));
    auto topLevelDrv = state.coerceToStorePath(aDrvPath.pos, *aDrvPath.value, context, "");
    topLevelDrv.requireDerivation();
    auto & aOutPath(*topLevel.attrs()->get(state.s.outPath));
    auto topLevelOut = state.coerceToStorePath(aOutPath.pos, *aOutPath.value, context, "");

    /* Realise the resulting store expression. */
    debug("building user environment");
    std::vector<StorePathWithOutputs> topLevelDrvs;
    topLevelDrvs.push_back({topLevelDrv});
    state.store->buildPaths(toDerivedPaths(topLevelDrvs), state.repair ? bmRepair : bmNormal);

    /* Switch the current user environment to the output path. */
    auto store2 = state.store.dynamic_pointer_cast<LocalFSStore>();

    if (store2) {
        PathLocks lock;
        lockProfile(lock, profile);

        std::filesystem::path lockTokenCur = optimisticLockProfile(profile);
        if (lockToken != lockTokenCur) {
            printInfo("profile %s changed while we were busy; restarting", PathFmt(profile));
            return false;
        }

        debug("switching to new user environment");
        std::filesystem::path generation = createGeneration(*store2, profile, topLevelOut);
        switchLink(profile, generation);
    }

    return true;
}

} // namespace nix
