#include "nix/expr/primops.hh"
#include "nix/expr/eval-inline.hh"
#include "nix/expr/eval-settings.hh"
#include "nix/store/derivation/v2/convert.hh"
#include "nix/store/downstream-placeholder.hh"
#include "nix/store/globals.hh"
#include "nix/store/store-api.hh"

namespace nix {

using namespace derivation::v2;

namespace {

/**
 * Turns strings with context into interpolated strings: each store path
 * or placeholder its context accounts for becomes a reference to the
 * input, or own output, it stands for.
 */
struct Interpolator
{
    EvalState & state;
    std::string_view drvName;
    PathOf staticPathOf;

    /**
     * Keys are the strings that stand for references in the evaluator's
     * strings: own-output placeholders, input placeholders, and the
     * store paths of inputs.
     */
    std::map<std::string, Reference<SingleDerivedPath>> keys;

    std::set<SingleDerivedPath> inputs;

    void addOutput(const OutputName & name)
    {
        keys.emplace(hashPlaceholder(name), Reference<SingleDerivedPath>{name});
    }

    void addContext(const NixStringContext & context)
    {
        for (auto & c : context)
            std::visit(
                overloaded{
                    [&](const NixStringContextElem::Opaque & o) {
                        state.ensureLazyPathCopied(o.path);
                        SingleDerivedPath p = SingleDerivedPath::Opaque{.path = o.path};
                        inputs.insert(p);
                        keys.emplace(state.store->printStorePath(o.path), Reference<SingleDerivedPath>{p});
                    },
                    [&](const NixStringContextElem::Built & b) {
                        SingleDerivedPath::Built built{.drvPath = b.drvPath, .output = b.output};
                        SingleDerivedPath p = built;
                        inputs.insert(p);
                        keys.emplace(
                            DownstreamPlaceholder::fromSingleDerivedPathBuilt(built).render(),
                            Reference<SingleDerivedPath>{p});
                        if (auto path = staticPathOf(built))
                            keys.emplace(state.store->printStorePath(*path), Reference<SingleDerivedPath>{p});
                    },
                    [&](const NixStringContextElem::DrvDeep & d) {
                        /* The whole closure of the derivation, outputs
                           included, is made an input, as for version 1
                           derivations; the derivation's own path is
                           referred to as the constant input it is. */
                        StorePathSet refs;
                        state.store->computeFSClosure(d.drvPath, refs);
                        for (auto & j : refs) {
                            inputs.insert(SingleDerivedPath::Opaque{.path = j});
                            if (j.isDerivation())
                                for (auto & outputName : state.store->readDerivation(j).outputNames())
                                    inputs.insert(
                                        SingleDerivedPath::Built{
                                            .drvPath = makeConstantStorePathRef(j),
                                            .output = outputName,
                                        });
                        }
                        keys.emplace(
                            state.store->printStorePath(d.drvPath),
                            Reference<SingleDerivedPath>{SingleDerivedPath{SingleDerivedPath::Opaque{.path = d.drvPath}}});
                    },
                },
                c.raw);
    }

    Interpolated<SingleDerivedPath> interpolate(std::string_view s) const
    {
        Interpolated<SingleDerivedPath> res;
        while (!s.empty()) {
            size_t best = std::string_view::npos;
            const std::pair<const std::string, Reference<SingleDerivedPath>> * which = nullptr;
            for (auto & entry : keys) {
                auto i = s.find(entry.first);
                if (i < best || (i == best && which && entry.first.size() > which->first.size())) {
                    best = i;
                    which = &entry;
                }
            }
            if (!which) {
                res.append(std::string{s});
                break;
            }
            res.append(std::string{s.substr(0, best)});
            res.append(which->second);
            s.remove_prefix(best + which->first.size());
        }
        return res;
    }

    /**
     * Evaluate a string, and interpolate it.
     */
    Interpolated<SingleDerivedPath> string(Value & v, std::string_view what)
    {
        NixStringContext context;
        auto s = state.coerceToString(noPos, v, context, what, false, true).toOwned();
        addContext(context);
        return interpolate(s);
    }

    /**
     * Evaluate a string that must refer to exactly one thing: an own
     * output by name, or an input by its store path or placeholder.
     */
    Reference<SingleDerivedPath> reference(Value & v, std::string_view what, const std::set<OutputName> & outputs)
    {
        NixStringContext context;
        auto s = state.coerceToString(noPos, v, context, what, false, true).toOwned();
        if (context.empty()) {
            if (!outputs.contains(s))
                state.error<EvalError>("'%s' %s is neither an output name nor a store path", s, what).debugThrow();
            return OutputName{s};
        }
        addContext(context);
        auto i = keys.find(s);
        if (i == keys.end())
            state.error<EvalError>("'%s' %s does not refer to exactly one input", s, what).debugThrow();
        return i->second;
    }

    SingleDerivedPath input(Value & v, std::string_view what)
    {
        auto ref = reference(v, what, {});
        auto * p = std::get_if<SingleDerivedPath>(&ref);
        if (!p)
            state.error<EvalError>("%s must refer to an input", what).debugThrow();
        return *p;
    }
};

} // namespace

static StringSet forceStrings(EvalState & state, Value & v, std::string_view what)
{
    state.forceList(v, noPos, what);
    StringSet res;
    for (auto * elem : v.listView())
        res.insert(std::string{state.forceStringNoCtx(*elem, noPos, what)});
    return res;
}

static void prim_derivationStrictV2(EvalState & state, CallSite callSite, Value * const * args, Value & v)
{

    std::string_view errorCtx = "while evaluating the argument passed to builtins.derivationStrictV2";
    state.forceAttrs(*args[0], noPos, errorCtx);
    auto attrs = args[0]->attrs();

    auto get = [&](std::string_view name) -> Value * {
        auto * a = attrs->get(state.symbols.create(name));
        return a ? a->value : nullptr;
    };
    auto require = [&](std::string_view name) -> Value & {
        auto * a = get(name);
        if (!a)
            state.error<EvalError>("required attribute '%s' missing", name).debugThrow();
        return *a;
    };

    Full drv{
        .name = std::string{state.forceStringNoCtx(require("name"), noPos, "while evaluating the 'name' attribute")},
        .platform =
            std::string{state.forceStringNoCtx(require("system"), noPos, "while evaluating the 'system' attribute")},
    };

    try {
        Interpolator interp{
            .state = state,
            .drvName = drv.name,
            .staticPathOf = staticPathOf(*state.store),
        };

        /* The outputs first, so that the strings can refer to them. */
        auto & outputsV = require("outputs");
        state.forceAttrs(outputsV, noPos, "while evaluating the 'outputs' attribute");
        std::set<OutputName> outputNames;
        for (auto & a : *outputsV.attrs())
            outputNames.insert(std::string{state.symbols[a.name]});
        if (outputNames.empty())
            state.error<EvalError>("a derivation must have at least one output").debugThrow();
        for (auto & name : outputNames)
            interp.addOutput(name);

        drv.builder = interp.string(require("builder"), "while evaluating the 'builder' attribute");

        if (auto * argsV = get("args")) {
            state.forceList(*argsV, noPos, "while evaluating the 'args' attribute");
            for (auto * a : argsV->listView())
                drv.args.push_back(interp.string(*a, "while evaluating an element of the 'args' attribute"));
        }

        if (auto * envV = get("env")) {
            state.forceAttrs(*envV, noPos, "while evaluating the 'env' attribute");
            for (auto & a : *envV->attrs()) {
                std::string name{state.symbols[a.name]};
                state.forceValue(*a.value, noPos);
                EnvVar<SingleDerivedPath> var;
                if (a.value->type() == nAttrs) {
                    auto * valueA = a.value->attrs()->get(state.symbols.create("value"));
                    if (!valueA)
                        state.error<EvalError>("environment variable '%s' has no 'value'", name).debugThrow();
                    var.value = interp.string(*valueA->value, "while evaluating an environment variable");
                    if (auto * pf = a.value->attrs()->get(state.symbols.create("passAsFile")))
                        var.passAsFile = state.forceBool(*pf->value, noPos, "while evaluating 'passAsFile'");
                } else {
                    var.value = interp.string(*a.value, "while evaluating an environment variable");
                }
                drv.env.emplace(std::move(name), std::move(var));
            }
        }

        for (auto & a : *outputsV.attrs()) {
            std::string outputName{state.symbols[a.name]};
            state.forceAttrs(*a.value, noPos, "while evaluating an output");
            auto * o = a.value->attrs();
            auto field = [&](std::string_view f) -> Value * {
                auto * x = o->get(state.symbols.create(f));
                return x ? x->value : nullptr;
            };

            Output<SingleDerivedPath> output{.raw = InputAddressed{}};
            auto * method = field("method");
            auto * hashAlgo = field("hashAlgo");
            if (auto * hash = field("hash")) {
                if (!method)
                    state.error<EvalError>("output '%s' needs 'method' as well as 'hash'", outputName).debugThrow();
                if (outputName != "out" || outputsV.attrs()->size() != 1)
                    state
                        .error<EvalError>(
                            "a derivation with a fixed content-addressed output must have just the one output 'out'")
                        .debugThrow();
                auto m = ContentAddressMethod::parse(state.forceStringNoCtx(*method, noPos, "while evaluating 'method'"));
                std::optional<HashAlgorithm> h;
                if (hashAlgo)
                    h = parseHashAlgo(state.forceStringNoCtx(*hashAlgo, noPos, "while evaluating 'hashAlgo'"));
                output.raw = derivation::Output::CAFixed{
                    .ca =
                        {
                            .method = std::move(m),
                            .hash = Hash::parseAny(state.forceStringNoCtx(*hash, noPos, "while evaluating 'hash'"), h),
                        },
                };
            } else if (method || hashAlgo) {
                if (!method || !hashAlgo)
                    state.error<EvalError>("output '%s' needs both 'method' and 'hashAlgo'", outputName).debugThrow();
                auto m = ContentAddressMethod::parse(state.forceStringNoCtx(*method, noPos, "while evaluating 'method'"));
                auto h = parseHashAlgo(state.forceStringNoCtx(*hashAlgo, noPos, "while evaluating 'hashAlgo'"));
                auto * impure = field("impure");
                if (impure && state.forceBool(*impure, noPos, "while evaluating 'impure'")) {
                    experimentalFeatureSettings.require(Xp::ImpureDerivations);
                    output.raw = derivation::Output::Impure{.method = std::move(m), .hashAlgo = h};
                } else {
                    experimentalFeatureSettings.require(Xp::CaDerivations);
                    output.raw = derivation::Output::CAFloating{.method = std::move(m), .hashAlgo = h};
                }
            }

            if (auto * optsV = field("options")) {
                state.forceAttrs(*optsV, noPos, "while evaluating an output's 'options'");
                auto & c = output.options.checks;
                auto refs = [&](Value & l, std::string_view what) {
                    state.forceList(l, noPos, what);
                    std::set<Reference<SingleDerivedPath>> res;
                    for (auto * e : l.listView())
                        res.insert(interp.reference(*e, what, outputNames));
                    return res;
                };
                for (auto & opt : *optsV->attrs()) {
                    std::string_view name = state.symbols[opt.name];
                    auto & ov = *opt.value;
                    if (name == "allowedReferences")
                        c.allowedReferences = refs(ov, "in 'allowedReferences'");
                    else if (name == "allowedRequisites")
                        c.allowedRequisites = refs(ov, "in 'allowedRequisites'");
                    else if (name == "disallowedReferences")
                        c.disallowedReferences = refs(ov, "in 'disallowedReferences'");
                    else if (name == "disallowedRequisites")
                        c.disallowedRequisites = refs(ov, "in 'disallowedRequisites'");
                    else if (name == "ignoreSelfRefs")
                        c.ignoreSelfRefs = state.forceBool(ov, noPos, "while evaluating 'ignoreSelfRefs'");
                    else if (name == "maxSize")
                        c.maxSize = state.forceInt(ov, noPos, "while evaluating 'maxSize'").value;
                    else if (name == "maxClosureSize")
                        c.maxClosureSize = state.forceInt(ov, noPos, "while evaluating 'maxClosureSize'").value;
                    else if (name == "unsafeDiscardReferences")
                        output.options.unsafeDiscardReferences =
                            state.forceBool(ov, noPos, "while evaluating 'unsafeDiscardReferences'");
                    else
                        state.error<EvalError>("unknown output option '%s'", name).debugThrow();
                }
            }

            drv.outputs.emplace(std::move(outputName), std::move(output));
        }

        if (auto * optsV = get("options")) {
            state.forceAttrs(*optsV, noPos, "while evaluating the 'options' attribute");
            auto & o = drv.options;
            for (auto & opt : *optsV->attrs()) {
                std::string_view name = state.symbols[opt.name];
                auto & ov = *opt.value;
                auto what = fmt("while evaluating the option '%s'", name);
                if (name == "additionalSandboxProfile")
                    o.additionalSandboxProfile = std::string{state.forceStringNoCtx(ov, noPos, what)};
                else if (name == "allowLocalNetworking")
                    o.allowLocalNetworking = state.forceBool(ov, noPos, what);
                else if (name == "allowSubstitutes")
                    o.allowSubstitutes = state.forceBool(ov, noPos, what);
                else if (name == "exportReferencesGraph") {
                    state.forceAttrs(ov, noPos, what);
                    for (auto & e : *ov.attrs()) {
                        state.forceList(*e.value, noPos, what);
                        std::set<SingleDerivedPath> paths;
                        for (auto * p : e.value->listView())
                            paths.insert(interp.input(*p, "in 'exportReferencesGraph'"));
                        o.exportReferencesGraph.emplace(std::string{state.symbols[e.name]}, std::move(paths));
                    }
                } else if (name == "impureEnvVars")
                    o.impureEnvVars = forceStrings(state, ov, what);
                else if (name == "impureHostDeps")
                    o.impureHostDeps = forceStrings(state, ov, what);
                else if (name == "noChroot")
                    o.noChroot = state.forceBool(ov, noPos, what);
                else if (name == "preferLocalBuild")
                    o.preferLocalBuild = state.forceBool(ov, noPos, what);
                else if (name == "requiredSystemFeatures")
                    o.requiredSystemFeatures = forceStrings(state, ov, what);
                else
                    state.error<EvalError>("unknown option '%s'", name).debugThrow();
            }
        }

        drv.inputs = derivation::FullInputs::fromSet(interp.inputs);
    } catch (Error & e) {
        e.addTrace(nullptr, HintFmt("while evaluating derivation '%s'", drv.name), TracePrint::Always);
        throw;
    }

    auto drvPath = settings.readOnlyMode ? computeStorePath(*state.store, drv)
                                         : writeDerivation(*state.store, drv, state.repair);
    auto outputPaths = staticOutputPaths(*state.store, drv, staticPathOf(*state.store));

    auto result = state.buildBindings(1 + drv.outputs.size());
    result.alloc(state.s.drvPath)
        .mkString(
            state.store->printStorePath(drvPath),
            {
                NixStringContextElem::DrvDeep{.drvPath = drvPath},
            },
            state.mem);
    for (auto & [outputName, output] : drv.outputs) {
        std::optional<StorePath> path;
        if (outputPaths)
            if (auto i = outputPaths->find(outputName); i != outputPaths->end())
                path = i->second;
        state.mkOutputString(
            result.alloc(outputName),
            SingleDerivedPath::Built{
                .drvPath = makeConstantStorePathRef(drvPath),
                .output = outputName,
            },
            path);
    }
    v.mkAttrs(result);
}

static RegisterPrimOp primop_derivationStrictV2({
    .name = "__derivationStrictV2",
    .args = {"attrs"},
    .doc = R"(
      Create a [version 2 derivation](@docroot@/language/derivations/v2.md).
    )",
    .impl = prim_derivationStrictV2,
    /* FIXME: the feature combining `ca-derivations` and
       `dynamic-derivations`, once it has a name. */
    .experimentalFeature = Xp::CaDerivations,
});

} // namespace nix
