#include "nix/expr/get-drvs.hh"
#include "nix/expr/eval-inline.hh"
#include "nix/store/derived-path.hh"
#include "nix/store/derivations.hh"
#include "nix/store/store-api.hh"
#include "nix/store/path-with-outputs.hh"

#include <cstring>
#include <regex>

namespace nix {

PackageInfo::PackageInfo(EvalState & state, std::string attrPath, const Bindings * attrs)
    : state(&state)
    , attrs(attrs)
    , attrPath(std::move(attrPath))
{
}

PackageInfo::PackageInfo(EvalState & state, ref<Store> store, const std::string & drvPathWithOutputs)
    : state(&state)
    , attrs(nullptr)
    , attrPath("")
{
    auto [drvStorePath, selectedOutputs] = parsePathWithOutputs(*store, drvPathWithOutputs);

    this->drvPath = {{Path{SingleDerivedPath::Opaque{drvStorePath}, drvStorePath}}};

    auto drv = store->derivationFromPath(drvStorePath);

    name = drvStorePath.name();

    if (selectedOutputs.size() > 1)
        throw Error("building more than one derivation output is not supported, in '%s'", drvPathWithOutputs);

    outputName = selectedOutputs.empty() ? getOr(drv.env, "outputName", "out") : *selectedOutputs.begin();

    auto i = drv.outputs.find(outputName);
    if (i == drv.outputs.end())
        throw Error("derivation '%s' does not have output '%s'", store->printStorePath(drvStorePath), outputName);
    auto & [outputName_, output] = *i;

    auto outStorePath = output.path(*store, drv.name, outputName_);
    if (outStorePath) {
        outPath = Path{SingleDerivedPath::Opaque{*outStorePath}, *outStorePath};
    } else {
        // CA derivation with unknown output path
        outPath = Path{
            SingleDerivedPath::Built{makeConstantStorePathRef(drvStorePath), outputName_},
            std::nullopt};
    }
}

std::string PackageInfo::queryName() const
{
    if (name == "" && attrs) {
        auto i = attrs->get(state->s.name);
        if (!i)
            state->error<TypeError>("derivation name missing").debugThrow();
        name = state->forceStringNoCtx(*i->value, noPos, "while evaluating the 'name' attribute of a derivation");
    }
    return name;
}

std::string PackageInfo::querySystem() const
{
    if (system == "" && attrs) {
        auto i = attrs->get(state->s.system);
        system =
            !i ? "unknown"
               : state->forceStringNoCtx(*i->value, i->pos, "while evaluating the 'system' attribute of a derivation");
    }
    return system;
}

std::optional<PackageInfo::Path> PackageInfo::queryDrvPathFlexible() const
{
    if (!drvPath && attrs) {
        auto i = attrs->get(state->s.drvPath);
        if (i) {
            Value v;
            v.mkAttrs(const_cast<Bindings *>(attrs));
            std::optional<SingleDerivedPath> outPathDerivedPath;
            try {
                // Validate derivation structure
                outPathDerivedPath = state->coerceToSingleDerivedPath(
                    noPos, v, "while evaluating the derivation");
            } catch (Error & e) {
                auto info = e.info();
                info.msg = HintFmt("in a future version of Nix this will be an error: %s", Uncolored(info.msg.str()));
                logWarning(info);
            }
            if (outPathDerivedPath) {
                // Validation passed. Now get drvPath.
                auto drvPathDerivedPath = state->coerceToSingleDerivedPath(
                    i->pos, *i->value, "while evaluating the 'drvPath' attribute of a derivation");
                if (auto * opaque = std::get_if<SingleDerivedPath::Opaque>(&drvPathDerivedPath.raw())) {
                    opaque->path.requireDerivation();
                    drvPath = {{Path{std::move(drvPathDerivedPath), opaque->path}}};
                } else {
                    // Dynamic derivation - drvPath is itself a derivation output
                    drvPath = {{Path{std::move(drvPathDerivedPath), std::nullopt}}};
                }
            } else {
                // Fall back to old behavior
                NixStringContext context;
                auto found = state->coerceToStorePath(
                    i->pos, *i->value, context, "while evaluating the 'drvPath' attribute of a derivation");
                try {
                    found.requireDerivation();
                } catch (Error & e) {
                    e.addTrace(state->positions[i->pos], "while evaluating the 'drvPath' attribute of a derivation");
                    throw;
                }
                drvPath = {{Path{SingleDerivedPath::Opaque{found}, std::move(found)}}};
            }
        } else
            drvPath = {std::nullopt};
    }
    return drvPath.value_or(std::nullopt);
}

std::optional<StorePath> PackageInfo::queryDrvPath() const
{
    if (auto path = queryDrvPathFlexible())
        return path->storePath;
    return std::nullopt;
}

StorePath PackageInfo::requireDrvPath() const
{
    if (auto drvPath = queryDrvPath())
        return *drvPath;
    throw Error("derivation does not contain a 'drvPath' attribute");
}

PackageInfo::Path PackageInfo::queryOutPathFlexible() const
{
    if (!outPath && attrs) {
        Value v;
        v.mkAttrs(const_cast<Bindings *>(attrs));
        std::optional<SingleDerivedPath> derivedPath;
        try {
            derivedPath = state->coerceToSingleDerivedPath(
                noPos, v, "while evaluating the derivation");
        } catch (Error & e) {
            auto info = e.info();
            info.msg = HintFmt("in a future version of Nix this will be an error: %s", Uncolored(info.msg.str()));
            logWarning(info);
        }
        if (derivedPath) {
            if (auto * opaque = std::get_if<SingleDerivedPath::Opaque>(&derivedPath->raw())) {
                outPath = Path{std::move(*derivedPath), opaque->path};
            } else {
                // Built path - no concrete output path available (placeholder)
                outPath = Path{std::move(*derivedPath), std::nullopt};
            }
        } else {
            // Fall back to old behavior
            auto i = attrs->get(state->s.outPath);
            NixStringContext context;
            if (i) {
                auto path = state->coerceToStorePath(
                    i->pos, *i->value, context, "while evaluating the output path of a derivation");
                outPath = Path{SingleDerivedPath::Opaque{path}, std::move(path)};
            }
        }
    }
    if (!outPath)
        throw Error("derivation does not have an 'outPath' attribute");
    return *outPath;
}

StorePath PackageInfo::queryOutPath() const
{
    auto path = queryOutPathFlexible();
    if (!path.storePath)
        throw UnimplementedError("CA derivations are not yet supported");
    return *path.storePath;
}

PackageInfo::Outputs PackageInfo::queryOutputs(bool withPaths, bool onlyOutputsToInstall)
{
    if (outputs.empty()) {
        /* Get the ‘outputs’ list. */
        const Attr * i;
        if (attrs && (i = attrs->get(state->s.outputs))) {
            state->forceList(*i->value, i->pos, "while evaluating the 'outputs' attribute of a derivation");

            /* For each output... */
            for (auto elem : i->value->listView()) {
                std::string output(
                    state->forceStringNoCtx(*elem, i->pos, "while evaluating the name of an output of a derivation"));

                if (withPaths) {
                    /* Evaluate the corresponding set. */
                    auto out = attrs->get(state->symbols.create(output));
                    if (!out)
                        continue; // FIXME: throw error?
                    state->forceAttrs(*out->value, i->pos, "while evaluating an output of a derivation");

                    /* And evaluate its ‘outPath’ attribute. */
                    auto outPath = out->value->attrs()->get(state->s.outPath);
                    if (!outPath)
                        continue; // FIXME: throw error?
                    NixStringContext context;
                    outputs.emplace(
                        output,
                        state->coerceToStorePath(
                            outPath->pos, *outPath->value, context, "while evaluating an output path of a derivation"));
                } else
                    outputs.emplace(output, std::nullopt);
            }
        } else
            outputs.emplace("out", withPaths ? std::optional{queryOutPath()} : std::nullopt);
    }

    if (!onlyOutputsToInstall || !attrs)
        return outputs;

    const Attr * i;
    if (attrs && (i = attrs->get(state->s.outputSpecified))
        && state->forceBool(*i->value, i->pos, "while evaluating the 'outputSpecified' attribute of a derivation")) {
        Outputs result;
        auto out = outputs.find(queryOutputName());
        if (out == outputs.end())
            throw Error("derivation does not have output '%s'", queryOutputName());
        result.insert(*out);
        return result;
    }

    else {
        /* Check for `meta.outputsToInstall` and return `outputs` reduced to that. */
        const Value * outTI = queryMeta("outputsToInstall");
        if (!outTI)
            return outputs;
        auto errMsg = Error("this derivation has bad 'meta.outputsToInstall'");
        /* ^ this shows during `nix-env -i` right under the bad derivation */
        if (!outTI->isList())
            throw errMsg;
        Outputs result;
        for (auto elem : outTI->listView()) {
            if (elem->type() != nString)
                throw errMsg;
            auto out = outputs.find(elem->string_view());
            if (out == outputs.end())
                throw errMsg;
            result.insert(*out);
        }
        return result;
    }
}

std::string PackageInfo::queryOutputName() const
{
    if (outputName == "" && attrs) {
        auto i = attrs->get(state->s.outputName);
        outputName =
            i ? state->forceStringNoCtx(*i->value, noPos, "while evaluating the output name of a derivation") : "";
    }
    return outputName;
}

const Bindings * PackageInfo::getMeta()
{
    if (meta)
        return meta;
    if (!attrs)
        return 0;
    auto a = attrs->get(state->s.meta);
    if (!a)
        return 0;
    state->forceAttrs(*a->value, a->pos, "while evaluating the 'meta' attribute of a derivation");
    meta = a->value->attrs();
    return meta;
}

StringSet PackageInfo::queryMetaNames()
{
    StringSet res;
    if (!getMeta())
        return res;
    for (auto & i : *meta)
        res.emplace(state->symbols[i.name]);
    return res;
}

bool PackageInfo::checkMeta(Value & v)
{
    auto _level = state->addCallDepth(v.determinePos(noPos));

    state->forceValue(v, v.determinePos(noPos));
    if (v.type() == nList) {
        for (auto elem : v.listView())
            if (!checkMeta(*elem))
                return false;
        return true;
    } else if (v.type() == nAttrs) {
        if (v.attrs()->get(state->s.outPath))
            return false;
        for (auto & i : *v.attrs())
            if (!checkMeta(*i.value))
                return false;
        return true;
    } else
        return v.type() == nInt || v.type() == nBool || v.type() == nString || v.type() == nFloat;
}

Value * PackageInfo::queryMeta(const std::string & name)
{
    if (!getMeta())
        return 0;
    auto a = meta->get(state->symbols.create(name));
    if (!a || !checkMeta(*a->value))
        return 0;
    return a->value;
}

std::string PackageInfo::queryMetaString(const std::string & name)
{
    Value * v = queryMeta(name);
    if (!v || v->type() != nString)
        return "";
    return std::string{v->string_view()};
}

NixInt PackageInfo::queryMetaInt(const std::string & name, NixInt def)
{
    Value * v = queryMeta(name);
    if (!v)
        return def;
    if (v->type() == nInt)
        return v->integer();
    if (v->type() == nString) {
        /* Backwards compatibility with before we had support for
           integer meta fields. */
        if (auto n = string2Int<NixInt::Inner>(v->string_view()))
            return NixInt{*n};
    }
    return def;
}

NixFloat PackageInfo::queryMetaFloat(const std::string & name, NixFloat def)
{
    Value * v = queryMeta(name);
    if (!v)
        return def;
    if (v->type() == nFloat)
        return v->fpoint();
    if (v->type() == nString) {
        /* Backwards compatibility with before we had support for
           float meta fields. */
        if (auto n = string2Float<NixFloat>(v->string_view()))
            return *n;
    }
    return def;
}

bool PackageInfo::queryMetaBool(const std::string & name, bool def)
{
    Value * v = queryMeta(name);
    if (!v)
        return def;
    if (v->type() == nBool)
        return v->boolean();
    if (v->type() == nString) {
        /* Backwards compatibility with before we had support for
           Boolean meta fields. */
        if (v->string_view() == "true")
            return true;
        if (v->string_view() == "false")
            return false;
    }
    return def;
}

void PackageInfo::setMeta(const std::string & name, Value * v)
{
    getMeta();
    auto attrs = state->buildBindings(1 + (meta ? meta->size() : 0));
    auto sym = state->symbols.create(name);
    if (meta)
        for (auto i : *meta)
            if (i.name != sym)
                attrs.insert(i);
    if (v)
        attrs.insert(sym, v);
    meta = attrs.finish();
}

/* Cache for already considered attrsets. */
typedef std::set<const Bindings *> Done;

/* Evaluate value `v'.  If it evaluates to a set of type `derivation',
   then put information about it in `drvs' (unless it's already in `done').
   The result boolean indicates whether it makes sense
   for the caller to recursively search for derivations in `v'. */
static bool getDerivation(
    EvalState & state,
    Value & v,
    const std::string & attrPath,
    PackageInfos & drvs,
    Done & done,
    bool ignoreAssertionFailures)
{
    try {
        state.forceValue(v, v.determinePos(noPos));
        if (!state.isDerivation(v))
            return true;

        /* Remove spurious duplicates (e.g., a set like `rec { x =
           derivation {...}; y = x;}'. */
        if (!done.insert(v.attrs()).second)
            return false;

        PackageInfo drv(state, attrPath, v.attrs());

        drv.queryName();

        drvs.push_back(drv);

        return false;

    } catch (AssertionError & e) {
        if (ignoreAssertionFailures)
            return false;
        throw;
    }
}

std::optional<PackageInfo> getDerivation(EvalState & state, Value & v, bool ignoreAssertionFailures)
{
    Done done;
    PackageInfos drvs;
    getDerivation(state, v, "", drvs, done, ignoreAssertionFailures);
    if (drvs.size() != 1)
        return {};
    return std::move(drvs.front());
}

static std::string addToPath(const std::string & s1, std::string_view s2)
{
    return s1.empty() ? std::string(s2) : s1 + "." + s2;
}

static bool isAttrPathComponent(std::string_view symbol)
{
    if (symbol.empty())
        return false;

    /* [A-Za-z_] */
    unsigned char first = symbol[0];
    if (!((first >= 'A' && first <= 'Z') || (first >= 'a' && first <= 'z') || first == '_'))
        return false;

    /* [A-Za-z0-9-_+]* */
    for (unsigned char c : symbol.substr(1)) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'
            || c == '+')
            continue;
        return false;
    }

    return true;
}

static void getDerivations(
    EvalState & state,
    Value & vIn,
    const std::string & pathPrefix,
    Bindings & autoArgs,
    PackageInfos & drvs,
    Done & done,
    bool ignoreAssertionFailures)
{
    auto _level = state.addCallDepth(vIn.determinePos(noPos));

    Value v;
    state.autoCallFunction(autoArgs, vIn, v);

    /* Process the expression. */
    if (!getDerivation(state, v, pathPrefix, drvs, done, ignoreAssertionFailures))
        ;

    else if (v.type() == nAttrs) {

        /* !!! undocumented hackery to support combining channels in
           nix-env.cc. */
        bool combineChannels = v.attrs()->get(state.symbols.create("_combineChannels"));

        /* Consider the attributes in sorted order to get more
           deterministic behaviour in nix-env operations (e.g. when
           there are names clashes between derivations, the derivation
           bound to the attribute with the "lower" name should take
           precedence). */
        for (auto & i : v.attrs()->lexicographicOrder(state.symbols)) {
            std::string_view symbol{state.symbols[i->name]};
            try {
                debug("evaluating attribute '%1%'", symbol);
                if (!isAttrPathComponent(symbol))
                    continue;
                std::string pathPrefix2 = addToPath(pathPrefix, symbol);
                if (combineChannels)
                    getDerivations(state, *i->value, pathPrefix2, autoArgs, drvs, done, ignoreAssertionFailures);
                else if (getDerivation(state, *i->value, pathPrefix2, drvs, done, ignoreAssertionFailures)) {
                    /* If the value of this attribute is itself a set,
                    should we recurse into it?  => Only if it has a
                    `recurseForDerivations = true' attribute. */
                    if (i->value->type() == nAttrs) {
                        auto j = i->value->attrs()->get(state.s.recurseForDerivations);
                        if (j
                            && state.forceBool(
                                *j->value, j->pos, "while evaluating the attribute `recurseForDerivations`"))
                            getDerivations(
                                state, *i->value, pathPrefix2, autoArgs, drvs, done, ignoreAssertionFailures);
                    }
                }
            } catch (Error & e) {
                e.addTrace(state.positions[i->pos], "while evaluating the attribute '%s'", symbol);
                throw;
            }
        }
    }

    else if (v.type() == nList) {
        auto listView = v.listView();
        for (auto [n, elem] : enumerate(listView)) {
            std::string pathPrefix2 = addToPath(pathPrefix, fmt("%d", n));
            if (getDerivation(state, *elem, pathPrefix2, drvs, done, ignoreAssertionFailures))
                getDerivations(state, *elem, pathPrefix2, autoArgs, drvs, done, ignoreAssertionFailures);
        }
    }

    else
        state.error<TypeError>("expression does not evaluate to a derivation (or a set or list of those)").debugThrow();
}

void getDerivations(
    EvalState & state,
    Value & v,
    const std::string & pathPrefix,
    Bindings & autoArgs,
    PackageInfos & drvs,
    bool ignoreAssertionFailures)
{
    Done done;
    getDerivations(state, v, pathPrefix, autoArgs, drvs, done, ignoreAssertionFailures);
}

} // namespace nix
