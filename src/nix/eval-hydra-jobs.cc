#include "command.hh"
#include "eval.hh"
#include "eval-inline.hh"
#include "derivations.hh"
#include "common-args.hh"
#include "json.hh"
#include "get-drvs.hh"

using namespace nix;

static std::string queryMetaStrings(EvalState & state, DrvInfo & drv, const string & name, const string & subAttribute)
{
    Strings res;
    std::function<void(Value & v)> rec;

    rec = [&](Value & v) {
        state.forceValue(v);
        if (v.type == tString)
            res.push_back(v.getString());
        else if (v.isList())
            for (unsigned int n = 0; n < v.listSize(); ++n)
                rec(*v.listElems()[n]);
        else if (v.type == tAttrs) {
            auto a = v.attrs->find(state.symbols.create(subAttribute));
            if (a != v.attrs->end())
                res.push_back(state.forceString(*a->value));
        }
    };

    Value * v = drv.queryMeta(name);
    if (v) rec(*v);

    return concatStringsSep(", ", res);
}

struct CmdEvalHydraJobs : MixJSON, MixDryRun, InstallableCommand
{
    std::optional<Path> gcRootsDir;

    CmdEvalHydraJobs()
    {
        mkFlag()
            .longName("gc-roots-dir")
            .description("garbage collector roots directory")
            .labels({"path"})
            .dest(&gcRootsDir);
    }

    std::string description() override
    {
        return "evaluate a Hydra jobset";
    }

    Examples examples() override
    {
        return {
            Example{
                "Evaluate Nixpkgs' release-combined jobset:",
                "nix eval-hydra-jobs -f '<nixpkgs/nixos/release-combined.nix>' '' --json"
            },
        };
    }

    void run(ref<Store> store) override
    {
        auto state = getEvalState();

        if (!gcRootsDir) warn("'--gc-roots-dir' not specified");

        if (dryRun) settings.readOnlyMode = true;

        /* Prevent access to paths outside of the Nix search path and
           to the environment. */
        evalSettings.restrictEval = true;

        auto v = installable->toValue(*state);

        auto jsonObj = json ? std::make_unique<JSONObject>(std::cout, true) : nullptr;

        std::function<void(Value & vIn, const string & attrPath)> findJobs;

        findJobs = [&](Value & vIn, const string & attrPath)
        {
            try {
                Activity act(*logger, lvlInfo, actUnknown, fmt("evaluating '%s'", attrPath));

                checkInterrupt();

                auto v = state->allocValue();
                state->autoCallFunction(getAutoArgs(*state), vIn, v);

                if (v->type == tAttrs) {
                    auto drv = getDerivation(*state, v, false);

                    if (drv) {

                        DrvInfo::Outputs outputs = drv->queryOutputs();

                        if (drv->querySystem() == "unknown")
                            throw EvalError("derivation must have a 'system' attribute");

                        auto drvPath = drv->queryDrvPath();

                        if (jsonObj) {
                            auto res = jsonObj->object(attrPath);
                            res.attr("nixName", drv->queryName());
                            res.attr("system", drv->querySystem());
                            res.attr("drvPath", drvPath);
                            res.attr("description", drv->queryMetaString("description"));
                            res.attr("license", queryMetaStrings(*state, *drv, "license", "shortName"));
                            res.attr("homepage", drv->queryMetaString("homepage"));
                            res.attr("maintainers", queryMetaStrings(*state, *drv, "maintainers", "email"));
                            res.attr("schedulingPriority", drv->queryMetaInt("schedulingPriority", 100));
                            res.attr("timeout", drv->queryMetaInt("timeout", 36000));
                            res.attr("maxSilent", drv->queryMetaInt("maxSilent", 7200));
                            res.attr("isChannel", drv->queryMetaBool("isHydraChannel", false));

                            /* If this is an aggregate, then get its constituents. */
                            auto a = v->attrs->get(state->symbols.create("_hydraAggregate"));
                            if (a && state->forceBool(*a->value, *a->pos)) {
                                auto a = v->attrs->get(state->symbols.create("constituents"));
                                if (!a)
                                    throw EvalError("derivation must have a ‘constituents’ attribute");
                                PathSet context;
                                state->coerceToString(*a->pos, *a->value, context, true, false);
                                PathSet drvs;
                                for (auto & i : context)
                                    if (i.at(0) == '!') {
                                        size_t index = i.find("!", 1);
                                        drvs.insert(string(i, index + 1));
                                    }
                                res.attr("constituents", concatStringsSep(" ", drvs));
                            }

                            /* Register the derivation as a GC root.  !!! This
                               registers roots for jobs that we may have already
                               done. */
                            auto localStore = state->store.dynamic_pointer_cast<LocalFSStore>();
                            if (gcRootsDir && localStore) {
                                Path root = *gcRootsDir + "/" + std::string(baseNameOf(drvPath));
                                if (!pathExists(root))
                                    localStore->addPermRoot(localStore->parseStorePath(drvPath), root, false);
                            }

                            auto res2 = res.object("outputs");
                            for (auto & j : outputs)
                                res2.attr(j.first, j.second);
                        } else
                            std::cout << fmt("%d: %d\n", attrPath, drvPath);

                    }

                    else {
                        if (!state->isDerivation(v)) {
                            for (auto & i : v->attrs->lexicographicOrder()) {
                                std::string name(i->name);

                                /* Skip jobs with dots in the name. */
                                if (name.find('.') != std::string::npos) {
                                    printError("skipping job with illegal name '%s'", name);
                                    continue;
                                }

                                findJobs(*i->value, (attrPath.empty() ? "" : attrPath + ".") + name);
                            }
                        }
                    }
                }

                else if (v->type == tNull) {
                    // allow null values, meaning 'do nothing'
                }

                else
                    throw TypeError("unsupported value: %s", *v);

            } catch (EvalError & e) {
                if (jsonObj)
                    jsonObj->object(attrPath).attr("error", filterANSIEscapes(e.msg(), true));
                else
                    printError("in job '%s': %s", attrPath, e.what());
            }
        };

        findJobs(v, "");
    }
};

static auto r1 = registerCommand<CmdEvalHydraJobs>("eval-hydra-jobs");
