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


DrvInfo::Outputs DrvInfo::queryOutputs()
{
    if (outputs.empty()) {
        /* Get the ‘outputs’ list. */
        Bindings::iterator i;
        if (attrs && (i = attrs->find(state->sOutputs)) != attrs->end()) {
            state->forceList(*i->value, *i->pos);
            Value::asList list(i->value);

            /* For each output... */
            for (unsigned int j = 0; j < list.length(); ++j) {
                /* Evaluate the corresponding set. */
                string name = state->forceStringNoCtx(*list[j], *i->pos);
                Bindings::iterator out = attrs->find(state->symbols.create(name));
                if (out == attrs->end()) continue; // FIXME: throw error?
                state->forceAttrs(*out->value);

                /* And evaluate its ‘outPath’ attribute. */
                Bindings::iterator outPath = out->value->asAttrs()->find(state->sOutPath);
                if (outPath == out->value->asAttrs()->end()) continue; // FIXME: throw error?
                PathSet context;
                outputs[name] = state->coerceToPath(*outPath->pos, *outPath->value, context);
            }
        } else
            outputs["out"] = queryOutPath();
    }
    return outputs;
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
    meta = a->value->asAttrs();
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
        Value::asList list(v);
        for (unsigned int n = 0; n < list.length(); ++n)
            if (!checkMeta(*list[n])) return false;
        return true;
    }
    else if (v.type() == Value::tAttrs) {
        Bindings::iterator i = v.asAttrs()->find(state->sOutPath);
        if (i != v.asAttrs()->end()) return false;
        for (auto & i : *v.asAttrs())
            if (!checkMeta(*i.value)) return false;
        return true;
    }
    else return v.type() == Value::tInt || v.type() == Value::tBool || v.type() == Value::tString;
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
    if (!v || v->type() != Value::tString) return "";
    return v->asString();
}


int DrvInfo::queryMetaInt(const string & name, int def)
{
    Value * v = queryMeta(name);
    if (!v) return def;
    if (v->type() == Value::tInt) return v->asInt();
    if (v->type() == Value::tString) {
        /* Backwards compatibility with before we had support for
           integer meta fields. */
        int n;
        if (string2Int(v->asString(), n)) return n;
    }
    return def;
}


bool DrvInfo::queryMetaBool(const string & name, bool def)
{
    Value * v = queryMeta(name);
    if (!v) return def;
    if (v->type() == Value::tBool) return v->asBool();
    if (v->type() == Value::tString) {
        /* Backwards compatibility with before we had support for
           Boolean meta fields. */
        if (strcmp(v->asString(), "true") == 0) return true;
        if (strcmp(v->asString(), "false") == 0) return false;
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
   then put information about it in `drvs' (unless it's already in
   `doneExprs').  The result boolean indicates whether it makes sense
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
        if (done.find(v.asAttrs()) != done.end()) return false;
        done.insert(v.asAttrs());

        Bindings::iterator i = v.asAttrs()->find(state.sName);
        /* !!! We really would like to have a decent back trace here. */
        if (i == v.asAttrs()->end()) throw TypeError("derivation name missing");

        Bindings::iterator i2 = v.asAttrs()->find(state.sSystem);

        DrvInfo drv(state, state.forceStringNoCtx(*i->value), attrPath,
            i2 == v.asAttrs()->end() ? "unknown" : state.forceStringNoCtx(*i2->value, *i2->pos),
            v.asAttrs());

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

    else if (v.type() == Value::tAttrs) {

        /* !!! undocumented hackery to support combining channels in
           nix-env.cc. */
        bool combineChannels = v.asAttrs()->find(state.symbols.create("_combineChannels")) != v.asAttrs()->end();

        /* Consider the attributes in sorted order to get more
           deterministic behaviour in nix-env operations (e.g. when
           there are names clashes between derivations, the derivation
           bound to the attribute with the "lower" name should take
           precedence). */
        typedef std::map<string, Symbol> SortedSymbols;
        SortedSymbols attrs;
        for (auto & i : *v.asAttrs())
            attrs.insert(std::pair<string, Symbol>(i.name, i.name));

        for (auto & i : attrs) {
            startNest(nest, lvlDebug, format("evaluating attribute ‘%1%’") % i.first);
            string pathPrefix2 = addToPath(pathPrefix, i.first);
            Value & v2(*v.asAttrs()->find(i.second)->value);
            if (combineChannels)
                getDerivations(state, v2, pathPrefix2, autoArgs, drvs, done, ignoreAssertionFailures);
            else if (getDerivation(state, v2, pathPrefix2, drvs, done, ignoreAssertionFailures)) {
                /* If the value of this attribute is itself a set,
                   should we recurse into it?  => Only if it has a
                   `recurseForDerivations = true' attribute. */
                if (v2.type() == Value::tAttrs) {
                    Bindings::iterator j = v2.asAttrs()->find(state.symbols.create("recurseForDerivations"));
                    if (j != v2.asAttrs()->end() && state.forceBool(*j->value))
                        getDerivations(state, v2, pathPrefix2, autoArgs, drvs, done, ignoreAssertionFailures);
                }
            }
        }
    }

    else if (v.isList()) {
        Value::asList list(v);
        for (unsigned int n = 0; n < list.length(); ++n) {
            startNest(nest, lvlDebug,
                format("evaluating list element"));
            string pathPrefix2 = addToPath(pathPrefix, (format("%1%") % n).str());
            if (getDerivation(state, *list[n], pathPrefix2, drvs, done, ignoreAssertionFailures))
                getDerivations(state, *list[n], pathPrefix2, autoArgs, drvs, done, ignoreAssertionFailures);
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
