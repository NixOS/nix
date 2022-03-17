#include "get-drvs.hh"
#include "util.hh"
#include "eval-inline.hh"
#include "derivations.hh"
#include "store-api.hh"
#include "path-with-outputs.hh"

#include <cstring>
#include <regex>


namespace nix {


DrvInfo::DrvInfo(EvalState & state, std::string attrPath, Bindings * attrs)
    : state(&state), attrs(attrs), attrPath(std::move(attrPath))
{
}


DrvInfo::DrvInfo(EvalState & state, ref<Store> store, const std::string & drvPathWithOutputs)
    : state(&state), attrs(nullptr), attrPath("")
{
    auto [drvPath, selectedOutputs] = parsePathWithOutputs(*store, drvPathWithOutputs);

    this->drvPath = drvPath;

    auto drv = store->derivationFromPath(drvPath);

    name = drvPath.name();

    if (selectedOutputs.size() > 1)
        throw Error("building more than one derivation output is not supported, in '%s'", drvPathWithOutputs);

    outputName =
        selectedOutputs.empty()
        ? get(drv.env, "outputName").value_or("out")
        : *selectedOutputs.begin();

    auto i = drv.outputs.find(outputName);
    if (i == drv.outputs.end())
        throw Error("derivation '%s' does not have output '%s'", store->printStorePath(drvPath), outputName);
    auto & [outputName, output] = *i;

    outPath = {output.path(*store, drv.name, outputName)};
}


std::string DrvInfo::queryName() const
{
    if (name == "" && attrs) {
        auto i = attrs->find(state->sName);
        if (i == attrs->end()) throw TypeError("derivation name missing");
        name = state->forceStringNoCtx(*i->value);
    }
    return name;
}


std::string DrvInfo::querySystem() const
{
    if (system == "" && attrs) {
        auto i = attrs->find(state->sSystem);
        system = i == attrs->end() ? "unknown" : state->forceStringNoCtx(*i->value, *i->pos);
    }
    return system;
}


std::optional<StorePath> DrvInfo::queryDrvPath() const
{
    if (!drvPath && attrs) {
        Bindings::iterator i = attrs->find(state->sDrvPath);
        PathSet context;
        if (i == attrs->end())
            drvPath = {std::nullopt};
        else
            drvPath = {state->coerceToStorePath(*i->pos, *i->value, context)};
    }
    return drvPath.value_or(std::nullopt);
}


StorePath DrvInfo::requireDrvPath() const
{
    if (auto drvPath = queryDrvPath())
        return *drvPath;
    throw Error("derivation does not contain a 'drvPath' attribute");
}


StorePath DrvInfo::queryOutPath() const
{
    if (!outPath && attrs) {
        Bindings::iterator i = attrs->find(state->sOutPath);
        PathSet context;
        if (i != attrs->end())
            outPath = state->coerceToStorePath(*i->pos, *i->value, context);
    }
    if (!outPath)
        throw UnimplementedError("CA derivations are not yet supported");
    return *outPath;
}


DrvInfo::Outputs DrvInfo::queryOutputs(bool withPaths, bool onlyOutputsToInstall)
{
    if (outputs.empty()) {
        /* Get the ‘outputs’ list. */
        Bindings::iterator i;
        if (attrs && (i = attrs->find(state->sOutputs)) != attrs->end()) {
            state->forceList(*i->value, *i->pos);

            /* For each output... */
            for (auto elem : i->value->listItems()) {
                std::string output(state->forceStringNoCtx(*elem, *i->pos));

                if (withPaths) {
                    /* Evaluate the corresponding set. */
                    Bindings::iterator out = attrs->find(state->symbols.create(output));
                    if (out == attrs->end()) continue; // FIXME: throw error?
                    state->forceAttrs(*out->value, *i->pos);

                    /* And evaluate its ‘outPath’ attribute. */
                    Bindings::iterator outPath = out->value->attrs->find(state->sOutPath);
                    if (outPath == out->value->attrs->end()) continue; // FIXME: throw error?
                    PathSet context;
                    outputs.emplace(output, state->coerceToStorePath(*outPath->pos, *outPath->value, context));
                } else
                    outputs.emplace(output, std::nullopt);
            }
        } else
            outputs.emplace("out", withPaths ? std::optional{queryOutPath()} : std::nullopt);
    }
    if (!onlyOutputsToInstall || !attrs)
        return outputs;

    /* Check for `meta.outputsToInstall` and return `outputs` reduced to that. */
    const Value * outTI = queryMeta("outputsToInstall");
    if (!outTI) return outputs;
    const auto errMsg = Error("this derivation has bad 'meta.outputsToInstall'");
        /* ^ this shows during `nix-env -i` right under the bad derivation */
    if (!outTI->isList()) throw errMsg;
    Outputs result;
    for (auto elem : outTI->listItems()) {
        if (elem->type() != nString) throw errMsg;
        auto out = outputs.find(elem->string.s);
        if (out == outputs.end()) throw errMsg;
        result.insert(*out);
    }
    return result;
}


std::string DrvInfo::queryOutputName() const
{
    if (outputName == "" && attrs) {
        Bindings::iterator i = attrs->find(state->sOutputName);
        outputName = i != attrs->end() ? state->forceStringNoCtx(*i->value) : "";
    }
    return outputName;
}


Bindings * DrvInfo::getMeta()
{
    if (meta) return meta;
    if (!attrs) return 0;
    Bindings::iterator a = attrs->find(state->sMeta);
    if (a == attrs->end()) return 0;
    state->forceAttrs(*a->value, *a->pos);
    meta = a->value->attrs;
    return meta;
}


StringSet DrvInfo::queryMetaNames()
{
    StringSet res;
    if (!getMeta()) return res;
    for (auto & i : *meta)
        res.insert(i.name);
    return res;
}


bool DrvInfo::checkMeta(Value & v)
{
    state->forceValue(v, [&]() { return v.determinePos(noPos); });
    if (v.type() == nList) {
        for (auto elem : v.listItems())
            if (!checkMeta(*elem)) return false;
        return true;
    }
    else if (v.type() == nAttrs) {
        Bindings::iterator i = v.attrs->find(state->sOutPath);
        if (i != v.attrs->end()) return false;
        for (auto & i : *v.attrs)
            if (!checkMeta(*i.value)) return false;
        return true;
    }
    else return v.type() == nInt || v.type() == nBool || v.type() == nString ||
                v.type() == nFloat;
}


Value * DrvInfo::queryMeta(const std::string & name)
{
    if (!getMeta()) return 0;
    Bindings::iterator a = meta->find(state->symbols.create(name));
    if (a == meta->end() || !checkMeta(*a->value)) return 0;
    return a->value;
}


std::string DrvInfo::queryMetaString(const std::string & name)
{
    Value * v = queryMeta(name);
    if (!v || v->type() != nString) return "";
    return v->string.s;
}


NixInt DrvInfo::queryMetaInt(const std::string & name, NixInt def)
{
    Value * v = queryMeta(name);
    if (!v) return def;
    if (v->type() == nInt) return v->integer;
    if (v->type() == nString) {
        /* Backwards compatibility with before we had support for
           integer meta fields. */
        if (auto n = string2Int<NixInt>(v->string.s))
            return *n;
    }
    return def;
}

NixFloat DrvInfo::queryMetaFloat(const std::string & name, NixFloat def)
{
    Value * v = queryMeta(name);
    if (!v) return def;
    if (v->type() == nFloat) return v->fpoint;
    if (v->type() == nString) {
        /* Backwards compatibility with before we had support for
           float meta fields. */
        if (auto n = string2Float<NixFloat>(v->string.s))
            return *n;
    }
    return def;
}


bool DrvInfo::queryMetaBool(const std::string & name, bool def)
{
    Value * v = queryMeta(name);
    if (!v) return def;
    if (v->type() == nBool) return v->boolean;
    if (v->type() == nString) {
        /* Backwards compatibility with before we had support for
           Boolean meta fields. */
        if (strcmp(v->string.s, "true") == 0) return true;
        if (strcmp(v->string.s, "false") == 0) return false;
    }
    return def;
}


void DrvInfo::setMeta(const std::string & name, Value * v)
{
    getMeta();
    auto attrs = state->buildBindings(1 + (meta ? meta->size() : 0));
    Symbol sym = state->symbols.create(name);
    if (meta)
        for (auto i : *meta)
            if (i.name != sym)
                attrs.insert(i);
    if (v) attrs.insert(sym, v);
    meta = attrs.finish();
}


/* Cache for already considered attrsets. */
typedef std::set<Bindings *> Done;


/* Evaluate value `v'.  If it evaluates to a set of type `derivation',
   then put information about it in `drvs' (unless it's already in `done').
   The result boolean indicates whether it makes sense
   for the caller to recursively search for derivations in `v'. */
static bool getDerivation(EvalState & state, Value & v,
    const std::string & attrPath, DrvInfos & drvs, Done & done,
    bool ignoreAssertionFailures)
{
    try {
        state.forceValue(v, [&]() { return v.determinePos(noPos); });
        if (!state.isDerivation(v)) return true;

        /* Remove spurious duplicates (e.g., a set like `rec { x =
           derivation {...}; y = x;}'. */
        if (!done.insert(v.attrs).second) return false;

        DrvInfo drv(state, attrPath, v.attrs);

        drv.queryName();

        drvs.push_back(drv);

        return false;

    } catch (AssertionError & e) {
        if (ignoreAssertionFailures) return false;
        throw;
    }
}


std::optional<DrvInfo> getDerivation(EvalState & state, Value & v,
    bool ignoreAssertionFailures)
{
    Done done;
    DrvInfos drvs;
    getDerivation(state, v, "", drvs, done, ignoreAssertionFailures);
    if (drvs.size() != 1) return {};
    return std::move(drvs.front());
}


static std::string addToPath(const std::string & s1, const std::string & s2)
{
    return s1.empty() ? s2 : s1 + "." + s2;
}


static std::regex attrRegex("[A-Za-z_][A-Za-z0-9-_+]*");


static void getDerivations(EvalState & state, Value & vIn,
    const std::string & pathPrefix, Bindings & autoArgs,
    DrvInfos & drvs, Done & done,
    bool ignoreAssertionFailures)
{
    Value v;
    state.autoCallFunction(autoArgs, vIn, v);

    /* Process the expression. */
    if (!getDerivation(state, v, pathPrefix, drvs, done, ignoreAssertionFailures)) ;

    else if (v.type() == nAttrs) {

        /* !!! undocumented hackery to support combining channels in
           nix-env.cc. */
        bool combineChannels = v.attrs->find(state.symbols.create("_combineChannels")) != v.attrs->end();

        /* Consider the attributes in sorted order to get more
           deterministic behaviour in nix-env operations (e.g. when
           there are names clashes between derivations, the derivation
           bound to the attribute with the "lower" name should take
           precedence). */
        for (auto & i : v.attrs->lexicographicOrder()) {
            debug("evaluating attribute '%1%'", i->name);
            if (!std::regex_match(std::string(i->name), attrRegex))
                continue;
            std::string pathPrefix2 = addToPath(pathPrefix, i->name);
            if (combineChannels)
                getDerivations(state, *i->value, pathPrefix2, autoArgs, drvs, done, ignoreAssertionFailures);
            else if (getDerivation(state, *i->value, pathPrefix2, drvs, done, ignoreAssertionFailures)) {
                /* If the value of this attribute is itself a set,
                   should we recurse into it?  => Only if it has a
                   `recurseForDerivations = true' attribute. */
                if (i->value->type() == nAttrs) {
                    Bindings::iterator j = i->value->attrs->find(state.sRecurseForDerivations);
                    if (j != i->value->attrs->end() && state.forceBool(*j->value, *j->pos))
                        getDerivations(state, *i->value, pathPrefix2, autoArgs, drvs, done, ignoreAssertionFailures);
                }
            }
        }
    }

    else if (v.type() == nList) {
        for (auto [n, elem] : enumerate(v.listItems())) {
            std::string pathPrefix2 = addToPath(pathPrefix, fmt("%d", n));
            if (getDerivation(state, *elem, pathPrefix2, drvs, done, ignoreAssertionFailures))
                getDerivations(state, *elem, pathPrefix2, autoArgs, drvs, done, ignoreAssertionFailures);
        }
    }

    else throw TypeError("expression does not evaluate to a derivation (or a set or list of those)");
}


void getDerivations(EvalState & state, Value & v, const std::string & pathPrefix,
    Bindings & autoArgs, DrvInfos & drvs, bool ignoreAssertionFailures)
{
    Done done;
    getDerivations(state, v, pathPrefix, autoArgs, drvs, done, ignoreAssertionFailures);
}


}
