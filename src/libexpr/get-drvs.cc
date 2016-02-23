#include "get-drvs.hh"
#include "util.hh"
#include "eval-inline.hh"

#include <cstring>


namespace nix {


string DrvInfo::queryDrvPath()
{
    if (drvPath == "" && attrs) {
        Bindings::iterator i = attrs->find(state->sDrvPath);
        PathSet context;
        drvPath = i != attrs->end() ? state->coerceToPath(*i->pos, *i->value, context) : "";
    }
    return drvPath;
}


string DrvInfo::queryOutPath()
{
    if (outPath == "" && attrs) {
        Bindings::iterator i = attrs->find(state->sOutPath);
        PathSet context;
        outPath = i != attrs->end() ? state->coerceToPath(*i->pos, *i->value, context) : "";
    }
    return outPath;
}


DrvInfo::Outputs DrvInfo::queryOutputs(bool onlyOutputsToInstall)
{
    if (outputs.empty()) {
        /* Get the ‘outputs’ list. */
        Bindings::iterator i;
        if (attrs && (i = attrs->find(state->sOutputs)) != attrs->end()) {
            state->forceList(*i->value, *i->pos);

            /* For each output... */
            for (unsigned int j = 0; j < i->value->listSize(); ++j) {
                /* Evaluate the corresponding set. */
                string name = state->forceStringNoCtx(*i->value->listElems()[j], *i->pos);
                Bindings::iterator out = attrs->find(state->symbols.create(name));
                if (out == attrs->end()) continue; // FIXME: throw error?
                state->forceAttrs(*out->value);

                /* And evaluate its ‘outPath’ attribute. */
                Bindings::iterator outPath = out->value->attrs->find(state->sOutPath);
                if (outPath == out->value->attrs->end()) continue; // FIXME: throw error?
                PathSet context;
                outputs[name] = state->coerceToPath(*outPath->pos, *outPath->value, context);
            }
        } else
            outputs["out"] = queryOutPath();
    }
    if (!onlyOutputsToInstall || !attrs)
        return outputs;

    /* Check for `meta.outputsToInstall` and return `outputs` reduced to that. */
    const Value * outTI = queryMeta("outputsToInstall");
    if (!outTI) return outputs;
    const auto errMsg = Error("this derivation has bad ‘meta.outputsToInstall’");
        /* ^ this shows during `nix-env -i` right under the bad derivation */
    if (!outTI->isList()) throw errMsg;
    Outputs result;
    for (auto i = outTI->listElems(); i != outTI->listElems() + outTI->listSize(); ++i) {
        if ((*i)->type != tString) throw errMsg;
        auto out = outputs.find((*i)->string.s);
        if (out == outputs.end()) throw errMsg;
        result.insert(*out);
    }
    return result;
}


string DrvInfo::queryOutputName()
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
    state->forceValue(v);
    if (v.isList()) {
        for (unsigned int n = 0; n < v.listSize(); ++n)
            if (!checkMeta(*v.listElems()[n])) return false;
        return true;
    }
    else if (v.type == tAttrs) {
        Bindings::iterator i = v.attrs->find(state->sOutPath);
        if (i != v.attrs->end()) return false;
        for (auto & i : *v.attrs)
            if (!checkMeta(*i.value)) return false;
        return true;
    }
    else return v.type == tInt || v.type == tBool || v.type == tString;
}


Value * DrvInfo::queryMeta(const string & name)
{
    if (!getMeta()) return 0;
    Bindings::iterator a = meta->find(state->symbols.create(name));
    if (a == meta->end() || !checkMeta(*a->value)) return 0;
    return a->value;
}


string DrvInfo::queryMetaString(const string & name)
{
    Value * v = queryMeta(name);
    if (!v || v->type != tString) return "";
    return v->string.s;
}


int DrvInfo::queryMetaInt(const string & name, int def)
{
    Value * v = queryMeta(name);
    if (!v) return def;
    if (v->type == tInt) return v->integer;
    if (v->type == tString) {
        /* Backwards compatibility with before we had support for
           integer meta fields. */
        int n;
        if (string2Int(v->string.s, n)) return n;
    }
    return def;
}


bool DrvInfo::queryMetaBool(const string & name, bool def)
{
    Value * v = queryMeta(name);
    if (!v) return def;
    if (v->type == tBool) return v->boolean;
    if (v->type == tString) {
        /* Backwards compatibility with before we had support for
           Boolean meta fields. */
        if (strcmp(v->string.s, "true") == 0) return true;
        if (strcmp(v->string.s, "false") == 0) return false;
    }
    return def;
}


void DrvInfo::setMeta(const string & name, Value * v)
{
    getMeta();
    Bindings * old = meta;
    meta = state->allocBindings(1 + (old ? old->size() : 0));
    Symbol sym = state->symbols.create(name);
    if (old)
        for (auto i : *old)
            if (i.name != sym)
                meta->push_back(i);
    if (v) meta->push_back(Attr(sym, v));
    meta->sort();
}


/* Cache for already considered attrsets. */
typedef set<Bindings *> Done;


/* Evaluate value `v'.  If it evaluates to a set of type `derivation',
   then put information about it in `drvs' (unless it's already in `done').
   The result boolean indicates whether it makes sense
   for the caller to recursively search for derivations in `v'. */
static bool getDerivation(EvalState & state, Value & v,
    const string & attrPath, DrvInfos & drvs, Done & done,
    bool ignoreAssertionFailures)
{
    try {
        state.forceValue(v);
        if (!state.isDerivation(v)) return true;

        /* Remove spurious duplicates (e.g., a set like `rec { x =
           derivation {...}; y = x;}'. */
        if (done.find(v.attrs) != done.end()) return false;
        done.insert(v.attrs);

        Bindings::iterator i = v.attrs->find(state.sName);
        /* !!! We really would like to have a decent back trace here. */
        if (i == v.attrs->end()) throw TypeError("derivation name missing");

        Bindings::iterator i2 = v.attrs->find(state.sSystem);

        DrvInfo drv(state, state.forceStringNoCtx(*i->value), attrPath,
            i2 == v.attrs->end() ? "unknown" : state.forceStringNoCtx(*i2->value, *i2->pos),
            v.attrs);

        drvs.push_back(drv);
        return false;

    } catch (AssertionError & e) {
        if (ignoreAssertionFailures) return false;
        throw;
    }
}


bool getDerivation(EvalState & state, Value & v, DrvInfo & drv,
    bool ignoreAssertionFailures)
{
    Done done;
    DrvInfos drvs;
    getDerivation(state, v, "", drvs, done, ignoreAssertionFailures);
    if (drvs.size() != 1) return false;
    drv = drvs.front();
    return true;
}


static string addToPath(const string & s1, const string & s2)
{
    return s1.empty() ? s2 : s1 + "." + s2;
}


static void getDerivations(EvalState & state, Value & vIn,
    const string & pathPrefix, Bindings & autoArgs,
    DrvInfos & drvs, Done & done,
    bool ignoreAssertionFailures)
{
    Value v;
    state.autoCallFunction(autoArgs, vIn, v);

    /* Process the expression. */
    if (!getDerivation(state, v, pathPrefix, drvs, done, ignoreAssertionFailures)) ;

    else if (v.type == tAttrs) {

        /* !!! undocumented hackery to support combining channels in
           nix-env.cc. */
        bool combineChannels = v.attrs->find(state.symbols.create("_combineChannels")) != v.attrs->end();

        /* Consider the attributes in sorted order to get more
           deterministic behaviour in nix-env operations (e.g. when
           there are names clashes between derivations, the derivation
           bound to the attribute with the "lower" name should take
           precedence). */
        typedef std::map<string, Symbol> SortedSymbols;
        SortedSymbols attrs;
        for (auto & i : *v.attrs)
            attrs.insert(std::pair<string, Symbol>(i.name, i.name));

        for (auto & i : attrs) {
            startNest(nest, lvlDebug, format("evaluating attribute ‘%1%’") % i.first);
            string pathPrefix2 = addToPath(pathPrefix, i.first);
            Value & v2(*v.attrs->find(i.second)->value);
            if (combineChannels)
                getDerivations(state, v2, pathPrefix2, autoArgs, drvs, done, ignoreAssertionFailures);
            else if (getDerivation(state, v2, pathPrefix2, drvs, done, ignoreAssertionFailures)) {
                /* If the value of this attribute is itself a set,
                   should we recurse into it?  => Only if it has a
                   `recurseForDerivations = true' attribute. */
                if (v2.type == tAttrs) {
                    Bindings::iterator j = v2.attrs->find(state.symbols.create("recurseForDerivations"));
                    if (j != v2.attrs->end() && state.forceBool(*j->value))
                        getDerivations(state, v2, pathPrefix2, autoArgs, drvs, done, ignoreAssertionFailures);
                }
            }
        }
    }

    else if (v.isList()) {
        for (unsigned int n = 0; n < v.listSize(); ++n) {
            startNest(nest, lvlDebug,
                format("evaluating list element"));
            string pathPrefix2 = addToPath(pathPrefix, (format("%1%") % n).str());
            if (getDerivation(state, *v.listElems()[n], pathPrefix2, drvs, done, ignoreAssertionFailures))
                getDerivations(state, *v.listElems()[n], pathPrefix2, autoArgs, drvs, done, ignoreAssertionFailures);
        }
    }

    else throw TypeError("expression does not evaluate to a derivation (or a set or list of those)");
}


void getDerivations(EvalState & state, Value & v, const string & pathPrefix,
    Bindings & autoArgs, DrvInfos & drvs, bool ignoreAssertionFailures)
{
    Done done;
    getDerivations(state, v, pathPrefix, autoArgs, drvs, done, ignoreAssertionFailures);
}


}
