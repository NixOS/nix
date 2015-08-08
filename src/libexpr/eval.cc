#include "eval.hh"
#include "hash.hh"
#include "util.hh"
#include "store-api.hh"
#include "derivations.hh"
#include "globals.hh"
#include "eval-inline.hh"

#include <algorithm>
#include <cstring>
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>

#if HAVE_BOEHMGC

#include <gc/gc.h>
#include <gc/gc_cpp.h>

#define NEW new (UseGC)

#else

#define NEW new

#endif


namespace nix {


static void * allocBytes(size_t n)
{
    void * p;
#if HAVE_BOEHMGC
    p = GC_malloc(n);
#else
    p = malloc(n);
#endif
    if (!p) throw std::bad_alloc();
    return p;
}


static void printValue(std::ostream & str, std::set<const Value *> & active, const Value & v)
{
    checkInterrupt();

    if (active.find(&v) != active.end()) {
        str << "<CYCLE>";
        return;
    }
    active.insert(&v);

    switch (v.type()) {
    case Value::tInt:
        str << v.asInt();
        break;
    case Value::tBool:
        str << (v.asBool() ? "true" : "false");
        break;
    case Value::tString:
        str << "\"";
        for (const char * i = v.asString(); *i; i++)
            if (*i == '\"' || *i == '\\') str << "\\" << *i;
            else if (*i == '\n') str << "\\n";
            else if (*i == '\r') str << "\\r";
            else if (*i == '\t') str << "\\t";
            else str << *i;
        str << "\"";
        break;
    case Value::tPath:
        str << v.asPath(); // !!! escaping?
        break;
    case Value::tNull:
        str << "null";
        break;
    case Value::tAttrs: {
        str << "{ ";
        typedef std::map<string, Value *> Sorted;
        Sorted sorted;
        for (auto & i : *v.asAttrs())
            sorted[i.name] = i.value;
        for (auto & i : sorted) {
            str << i.first << " = ";
            printValue(str, active, *i.second);
            str << "; ";
        }
        str << "}";
        break;
    }
    case Value::tList0:
    case Value::tList1:
    case Value::tList2:
    case Value::tListN: {
        Value::asList list(v);
        str << "[ ";
        for (unsigned int n = 0; n < list.length(); ++n) {
            printValue(str, active, *list[n]);
            str << " ";
        }
        str << "]";
        break;
    }
    case Value::tThunk:
    case Value::tApp:
        str << "<CODE>";
        break;
    case Value::tLambda:
        str << "<LAMBDA>";
        break;
    case Value::tPrimOp:
        str << "<PRIMOP>";
        break;
    case Value::tPrimOpApp:
        str << "<PRIMOP-APP>";
        break;
    case Value::tExternal:
        str << *v.asExternal();
        break;
    default:
        throw Error("invalid value");
    }

    active.erase(&v);
}


std::ostream & operator << (std::ostream & str, const Value & v)
{
    std::set<const Value *> active;
    printValue(str, active, v);
    return str;
}


string showType(const Value & v)
{
    switch (v.type()) {
        case Value::tInt: return "an integer";
        case Value::tBool: return "a boolean";
        case Value::tString: return "a string";
        case Value::tPath: return "a path";
        case Value::tNull: return "null";
        case Value::tAttrs: return "a set";
        case Value::tList0:
        case Value::tList1:
        case Value::tList2:
        case Value::tListN: return "a list";
        case Value::tThunk: return "a thunk";
        case Value::tApp: return "a function application";
        case Value::tLambda: return "a function";
        case Value::tBlackhole: return "a black hole";
        case Value::tPrimOp: return "a built-in function";
        case Value::tPrimOpApp: return "a partially applied built-in function";
        case Value::tExternal: return v.asExternal()->showType();
    }
    abort();
}


#if HAVE_BOEHMGC
/* Called when the Boehm GC runs out of memory. */
static void * oomHandler(size_t requested)
{
    /* Convert this to a proper C++ exception. */
    throw std::bad_alloc();
}
#endif


static Symbol getName(const AttrName & name, EvalState & state, Env & env)
{
    if (name.symbol.set())
        return name.symbol;

    Value nameValue;
    name.expr->eval(state, env, nameValue);
    state.forceStringNoCtx(nameValue);
    return state.symbols.create(nameValue.asString());
}


static bool gcInitialised = false;

void initGC()
{
    if (gcInitialised) return;

#if HAVE_BOEHMGC
    /* Initialise the Boehm garbage collector. */
    GC_set_all_interior_pointers(0);

    /* Values are using interrior pointers to pack the value type. */
    GC_register_displacement(1);
    GC_register_displacement(2);

    GC_INIT();

    GC_oom_fn = oomHandler;

    /* Set the initial heap size to something fairly big (25% of
       physical RAM, up to a maximum of 384 MiB) so that in most cases
       we don't need to garbage collect at all.  (Collection has a
       fairly significant overhead.)  The heap size can be overridden
       through libgc's GC_INITIAL_HEAP_SIZE environment variable.  We
       should probably also provide a nix.conf setting for this.  Note
       that GC_expand_hp() causes a lot of virtual, but not physical
       (resident) memory to be allocated.  This might be a problem on
       systems that don't overcommit. */
    if (!getenv("GC_INITIAL_HEAP_SIZE")) {
        size_t size = 32 * 1024 * 1024;
#if HAVE_SYSCONF && defined(_SC_PAGESIZE) && defined(_SC_PHYS_PAGES)
        size_t maxSize = 384 * 1024 * 1024;
        long pageSize = sysconf(_SC_PAGESIZE);
        long pages = sysconf(_SC_PHYS_PAGES);
        if (pageSize != -1)
            size = (pageSize * pages) / 4; // 25% of RAM
        if (size > maxSize) size = maxSize;
#endif
        debug(format("setting initial heap size to %1% bytes") % size);
        GC_expand_hp(size);
    }

#endif

    gcInitialised = true;
}


/* Very hacky way to parse $NIX_PATH, which is colon-separated, but
   can contain URLs (e.g. "nixpkgs=https://bla...:foo=https://"). */
static Strings parseNixPath(const string & in)
{
    string marker = "\001//";
    auto res = tokenizeString<Strings>(replaceStrings(in, "://", marker), ":");
    for (auto & s : res)
        s = replaceStrings(s, marker, "://");
    return res;
}


EvalState::EvalState(const Strings & _searchPath)
    : sWith(symbols.create("<with>"))
    , sOutPath(symbols.create("outPath"))
    , sDrvPath(symbols.create("drvPath"))
    , sType(symbols.create("type"))
    , sMeta(symbols.create("meta"))
    , sName(symbols.create("name"))
    , sValue(symbols.create("value"))
    , sSystem(symbols.create("system"))
    , sOverrides(symbols.create("__overrides"))
    , sOutputs(symbols.create("outputs"))
    , sOutputName(symbols.create("outputName"))
    , sIgnoreNulls(symbols.create("__ignoreNulls"))
    , sFile(symbols.create("file"))
    , sLine(symbols.create("line"))
    , sColumn(symbols.create("column"))
    , sFunctor(symbols.create("__functor"))
    , baseEnv(allocEnv(128))
    , staticBaseEnv(false, 0)
{
    countCalls = getEnv("NIX_COUNT_CALLS", "0") != "0";

    restricted = settings.get("restrict-eval", false);

    assert(gcInitialised);

    /* Initialise the Nix expression search path. */
    Strings paths = parseNixPath(getEnv("NIX_PATH", ""));
    for (auto & i : _searchPath) addToSearchPath(i, true);
    for (auto & i : paths) addToSearchPath(i);
    addToSearchPath("nix=" + settings.nixDataDir + "/nix/corepkgs");

    vEmptySet.setAttrs(allocBindings(0));

    createBaseEnv();
}


EvalState::~EvalState()
{
    fileEvalCache.clear();
}


Path EvalState::checkSourcePath(const Path & path_)
{
    if (!restricted) return path_;

    /* Resolve symlinks. */
    Path path = canonPath(path_, true);

    for (auto & i : searchPath)
        if (path == i.second || isInDir(path, i.second))
            return path;

    /* To support import-from-derivation, allow access to anything in
       the store. FIXME: only allow access to paths that have been
       constructed by this evaluation. */
    if (isInStore(path)) return path;

#if 0
    /* Hack to support the chroot dependencies of corepkgs (see
       corepkgs/config.nix.in). */
    if (path == settings.nixPrefix && isStorePath(settings.nixPrefix))
        return path;
#endif

    throw RestrictedPathError(format("access to path ‘%1%’ is forbidden in restricted mode") % path_);
}


void EvalState::addConstant(const string & name, Value & v)
{
    Value * v2 = allocValue();
    *v2 = v;
    staticBaseEnv.vars[symbols.create(name)] = baseEnvDispl;
    baseEnv.values[baseEnvDispl++] = v2;
    string name2 = string(name, 0, 2) == "__" ? string(name, 2) : name;
    baseEnv.values[0]->asAttrs()->push_back(Attr(symbols.create(name2), v2));
}


void EvalState::addPrimOp(const string & name,
    unsigned int arity, PrimOpFun primOp)
{
    Value * v = allocValue();
    string name2 = string(name, 0, 2) == "__" ? string(name, 2) : name;
    Symbol sym = symbols.create(name2);
    v->setPrimOp(NEW PrimOp(primOp, arity, sym));
    staticBaseEnv.vars[symbols.create(name)] = baseEnvDispl;
    baseEnv.values[baseEnvDispl++] = v;
    baseEnv.values[0]->asAttrs()->push_back(Attr(sym, v));
}


void EvalState::getBuiltin(const string & name, Value & v)
{
    v = *baseEnv.values[0]->asAttrs()->find(symbols.create(name))->value;
}


/* Every "format" object (even temporary) takes up a few hundred bytes
   of stack space, which is a real killer in the recursive
   evaluator.  So here are some helper functions for throwing
   exceptions. */

LocalNoInlineNoReturn(void throwEvalError(const char * s, const string & s2))
{
    throw EvalError(format(s) % s2);
}

LocalNoInlineNoReturn(void throwEvalError(const char * s, const string & s2, const Pos & pos))
{
    throw EvalError(format(s) % s2 % pos);
}

LocalNoInlineNoReturn(void throwEvalError(const char * s, const string & s2, const string & s3))
{
    throw EvalError(format(s) % s2 % s3);
}

LocalNoInlineNoReturn(void throwEvalError(const char * s, const string & s2, const string & s3, const Pos & pos))
{
    throw EvalError(format(s) % s2 % s3 % pos);
}

LocalNoInlineNoReturn(void throwEvalError(const char * s, const Symbol & sym, const Pos & p1, const Pos & p2))
{
    throw EvalError(format(s) % sym % p1 % p2);
}

LocalNoInlineNoReturn(void throwTypeError(const char * s, const Pos & pos))
{
    throw TypeError(format(s) % pos);
}

LocalNoInlineNoReturn(void throwTypeError(const char * s, const string & s1))
{
    throw TypeError(format(s) % s1);
}

LocalNoInlineNoReturn(void throwTypeError(const char * s, const ExprLambda & fun, const Symbol & s2, const Pos & pos))
{
    throw TypeError(format(s) % fun.showNamePos() % s2 % pos);
}

LocalNoInlineNoReturn(void throwAssertionError(const char * s, const Pos & pos))
{
    throw AssertionError(format(s) % pos);
}

LocalNoInlineNoReturn(void throwUndefinedVarError(const char * s, const string & s1, const Pos & pos))
{
    throw UndefinedVarError(format(s) % s1 % pos);
}

LocalNoInline(void addErrorPrefix(Error & e, const char * s, const string & s2))
{
    e.addPrefix(format(s) % s2);
}

LocalNoInline(void addErrorPrefix(Error & e, const char * s, const ExprLambda & fun, const Pos & pos))
{
    e.addPrefix(format(s) % fun.showNamePos() % pos);
}

LocalNoInline(void addErrorPrefix(Error & e, const char * s, const string & s2, const Pos & pos))
{
    e.addPrefix(format(s) % s2 % pos);
}


inline Value * EvalState::lookupVar(Env * env, const ExprVar & var, bool noEval)
{
    for (unsigned int l = var.level; l; --l, env = env->up) ;

    if (!var.fromWith) return env->values[var.displ];

    while (1) {
        if (!env->haveWithAttrs) {
            if (noEval) return 0;
            Value * v = allocValue();
            evalAttrs(*env->up, (Expr *) env->values[0], *v);
            env->values[0] = v;
            env->haveWithAttrs = true;
        }
        Bindings::iterator j = env->values[0]->asAttrs()->find(var.name);
        if (j != env->values[0]->asAttrs()->end()) {
            if (countCalls && j->pos) attrSelects[*j->pos]++;
            return j->value;
        }
        if (!env->prevWith)
            throwUndefinedVarError("undefined variable ‘%1%’ at %2%", var.name, var.pos);
        for (unsigned int l = env->prevWith; l; --l, env = env->up) ;
    }
}


Value * EvalState::allocValue()
{
    nrValues++;
    return (Value *) allocBytes(sizeof(Value));
}


Env & EvalState::allocEnv(unsigned int size)
{
    assert(size <= std::numeric_limits<decltype(Env::size)>::max());

    nrEnvs++;
    nrValuesInEnvs += size;
    Env * env = (Env *) allocBytes(sizeof(Env) + size * sizeof(Value *));
    env->size = size;

    /* Clear the values because maybeThunk() and lookupVar fromWith expect this. */
    for (unsigned i = 0; i < size; ++i)
        env->values[i] = 0;

    return *env;
}


void EvalState::mkList(Value & v, unsigned int size)
{
    if (size == 0)
        v.setList();
    if (size == 1)
        v.setList(nullptr);
    else if (size == 2)
        v.setList(nullptr, nullptr);
    else
        v.setList((Value * *) allocBytes(size * sizeof(Value *)), size);
    nrListElems += size;
}


unsigned long nrThunks = 0;

static inline void mkThunk(Value & v, Env & env, Expr * expr)
{
    v.setThunk(&env, expr);
    nrThunks++;
}


void EvalState::mkThunk_(Value & v, Expr * expr)
{
    mkThunk(v, baseEnv, expr);
}


void EvalState::mkPos(Value & v, Pos * pos)
{
    if (pos) {
        mkAttrs(v, 3);
        allocAttr(v, sFile)->setString(pos->file);
        allocAttr(v, sLine)->setInt(pos->line);
        allocAttr(v, sColumn)->setInt(pos->column);
        v.asAttrs()->sort();
    } else
        v.setNull();
}


/* Create a thunk for the delayed computation of the given expression
   in the given environment.  But if the expression is a variable,
   then look it up right away.  This significantly reduces the number
   of thunks allocated. */
Value * Expr::maybeThunk(EvalState & state, Env & env)
{
    Value * v = state.allocValue();
    mkThunk(*v, env, this);
    return v;
}


unsigned long nrAvoided = 0;

Value * ExprVar::maybeThunk(EvalState & state, Env & env)
{
    Value * v = state.lookupVar(&env, *this, true);
    /* The value might not be initialised in the environment yet.
       In that case, ignore it. */
    if (v) { nrAvoided++; return v; }
    return Expr::maybeThunk(state, env);
}


Value * ExprString::maybeThunk(EvalState & state, Env & env)
{
    nrAvoided++;
    return &v;
}

Value * ExprInt::maybeThunk(EvalState & state, Env & env)
{
    nrAvoided++;
    return &v;
}

Value * ExprPath::maybeThunk(EvalState & state, Env & env)
{
    nrAvoided++;
    return &v;
}


void EvalState::evalFile(const Path & path, Value & v)
{
    FileEvalCache::iterator i;
    if ((i = fileEvalCache.find(path)) != fileEvalCache.end()) {
        v = i->second;
        return;
    }

    Path path2 = resolveExprPath(path);
    if ((i = fileEvalCache.find(path2)) != fileEvalCache.end()) {
        v = i->second;
        return;
    }

    startNest(nest, lvlTalkative, format("evaluating file ‘%1%’") % path2);
    Expr * e = parseExprFromFile(checkSourcePath(path2));
    try {
        eval(e, v);
    } catch (Error & e) {
        addErrorPrefix(e, "while evaluating the file ‘%1%’:\n", path2);
        throw;
    }

    fileEvalCache[path2] = v;
    if (path != path2) fileEvalCache[path] = v;
}


void EvalState::resetFileCache()
{
    fileEvalCache.clear();
}


void EvalState::eval(Expr * e, Value & v)
{
    e->eval(*this, baseEnv, v);
}


inline bool EvalState::evalBool(Env & env, Expr * e)
{
    Value v;
    e->eval(*this, env, v);
    if (v.type() != Value::tBool)
        throwTypeError("value is %1% while a Boolean was expected", v);
    return v.asBool();
}


inline bool EvalState::evalBool(Env & env, Expr * e, const Pos & pos)
{
    Value v;
    e->eval(*this, env, v);
    if (v.type() != Value::tBool)
        throwTypeError("value is %1% while a Boolean was expected, at %2%", v, pos);
    return v.asBool();
}


inline void EvalState::evalAttrs(Env & env, Expr * e, Value & v)
{
    e->eval(*this, env, v);
    if (v.type() != Value::tAttrs)
        throwTypeError("value is %1% while a set was expected", v);
}


void Expr::eval(EvalState & state, Env & env, Value & v)
{
    abort();
}


void ExprInt::eval(EvalState & state, Env & env, Value & v)
{
    v = this->v;
}


void ExprString::eval(EvalState & state, Env & env, Value & v)
{
    v = this->v;
}


void ExprPath::eval(EvalState & state, Env & env, Value & v)
{
    v = this->v;
}


void ExprAttrs::eval(EvalState & state, Env & env, Value & v)
{
    state.mkAttrs(v, attrs.size() + dynamicAttrs.size());
    Env *dynamicEnv = &env;

    if (recursive) {
        /* Create a new environment that contains the attributes in
           this `rec'. */
        Env & env2(state.allocEnv(attrs.size()));
        env2.up = &env;
        dynamicEnv = &env2;

        AttrDefs::iterator overrides = attrs.find(state.sOverrides);
        bool hasOverrides = overrides != attrs.end();

        /* The recursive attributes are evaluated in the new
           environment, while the inherited attributes are evaluated
           in the original environment. */
        unsigned int displ = 0;
        for (auto & i : attrs) {
            Value * vAttr;
            if (hasOverrides && !i.second.inherited) {
                vAttr = state.allocValue();
                mkThunk(*vAttr, env2, i.second.e);
            } else
                vAttr = i.second.e->maybeThunk(state, i.second.inherited ? env : env2);
            env2.values[displ++] = vAttr;
            v.asAttrs()->push_back(Attr(i.first, vAttr, &i.second.pos));
        }

        /* If the rec contains an attribute called `__overrides', then
           evaluate it, and add the attributes in that set to the rec.
           This allows overriding of recursive attributes, which is
           otherwise not possible.  (You can use the // operator to
           replace an attribute, but other attributes in the rec will
           still reference the original value, because that value has
           been substituted into the bodies of the other attributes.
           Hence we need __overrides.) */
        if (hasOverrides) {
            Value * vOverrides = (*v.asAttrs())[overrides->second.displ].value;
            state.forceAttrs(*vOverrides);
            Bindings * newBnds = state.allocBindings(v.asAttrs()->size() + vOverrides->asAttrs()->size());
            for (auto & i : *v.asAttrs())
                newBnds->push_back(i);
            for (auto & i : *vOverrides->asAttrs()) {
                AttrDefs::iterator j = attrs.find(i.name);
                if (j != attrs.end()) {
                    (*newBnds)[j->second.displ] = i;
                    env2.values[j->second.displ] = i.value;
                } else
                    newBnds->push_back(i);
            }
            newBnds->sort();
            v.setAttrs(newBnds);
        }
    }

    else
        for (auto & i : attrs)
            v.asAttrs()->push_back(Attr(i.first, i.second.e->maybeThunk(state, env), &i.second.pos));

    /* Dynamic attrs apply *after* rec and __overrides. */
    for (auto & i : dynamicAttrs) {
        Value nameVal;
        i.nameExpr->eval(state, *dynamicEnv, nameVal);
        state.forceValue(nameVal, i.pos);
        if (nameVal.type() == Value::tNull)
            continue;
        state.forceStringNoCtx(nameVal);
        Symbol nameSym = state.symbols.create(nameVal.asString());
        Bindings::iterator j = v.asAttrs()->find(nameSym);
        if (j != v.asAttrs()->end())
            throwEvalError("dynamic attribute ‘%1%’ at %2% already defined at %3%", nameSym, i.pos, *j->pos);

        i.valueExpr->setName(nameSym);
        /* Keep sorted order so find can catch duplicates */
        v.asAttrs()->push_back(Attr(nameSym, i.valueExpr->maybeThunk(state, *dynamicEnv), &i.pos));
        v.asAttrs()->sort(); // FIXME: inefficient
    }
}


void ExprLet::eval(EvalState & state, Env & env, Value & v)
{
    /* Create a new environment that contains the attributes in this
       `let'. */
    Env & env2(state.allocEnv(attrs->attrs.size()));
    env2.up = &env;

    /* The recursive attributes are evaluated in the new environment,
       while the inherited attributes are evaluated in the original
       environment. */
    unsigned int displ = 0;
    for (auto & i : attrs->attrs)
        env2.values[displ++] = i.second.e->maybeThunk(state, i.second.inherited ? env : env2);

    body->eval(state, env2, v);
}


void ExprList::eval(EvalState & state, Env & env, Value & v)
{
    state.mkList(v, elems.size());
    Value::asMutableList list(v);
    for (unsigned int n = 0; n < elems.size(); ++n)
        list[n] = elems[n]->maybeThunk(state, env);
}


void ExprVar::eval(EvalState & state, Env & env, Value & v)
{
    Value * v2 = state.lookupVar(&env, *this, false);
    state.forceValue(*v2, pos);
    v = *v2;
}


static string showAttrPath(EvalState & state, Env & env, const AttrPath & attrPath)
{
    std::ostringstream out;
    bool first = true;
    for (auto & i : attrPath) {
        if (!first) out << '.'; else first = false;
        try {
            out << getName(i, state, env);
        } catch (Error & e) {
            assert(!i.symbol.set());
            out << "\"${" << *i.expr << "}\"";
        }
    }
    return out.str();
}


unsigned long nrLookups = 0;

void ExprSelect::eval(EvalState & state, Env & env, Value & v)
{
    Value vTmp;
    Pos * pos2 = 0;
    Value * vAttrs = &vTmp;

    e->eval(state, env, vTmp);

    try {

        for (auto & i : attrPath) {
            nrLookups++;
            Bindings::iterator j;
            Symbol name = getName(i, state, env);
            if (def) {
                state.forceValue(*vAttrs, pos);
                if (vAttrs->type() != Value::tAttrs ||
                    (j = vAttrs->asAttrs()->find(name)) == vAttrs->asAttrs()->end())
                {
                    def->eval(state, env, v);
                    return;
                }
            } else {
                state.forceAttrs(*vAttrs, pos);
                if ((j = vAttrs->asAttrs()->find(name)) == vAttrs->asAttrs()->end())
                    throwEvalError("attribute ‘%1%’ missing, at %2%", name, pos);
            }
            vAttrs = j->value;
            pos2 = j->pos;
            if (state.countCalls && pos2) state.attrSelects[*pos2]++;
        }

        state.forceValue(*vAttrs, ( pos2 != NULL ? *pos2 : this->pos ) );

    } catch (Error & e) {
        if (pos2 && pos2->file != state.sDerivationNix)
            addErrorPrefix(e, "while evaluating the attribute ‘%1%’ at %2%:\n",
                showAttrPath(state, env, attrPath), *pos2);
        throw;
    }

    v = *vAttrs;
}


void ExprOpHasAttr::eval(EvalState & state, Env & env, Value & v)
{
    Value vTmp;
    Value * vAttrs = &vTmp;

    e->eval(state, env, vTmp);

    for (auto & i : attrPath) {
        state.forceValue(*vAttrs);
        Bindings::iterator j;
        Symbol name = getName(i, state, env);
        if (vAttrs->type() != Value::tAttrs ||
            (j = vAttrs->asAttrs()->find(name)) == vAttrs->asAttrs()->end())
        {
            v.setBool(false);
            return;
        } else {
            vAttrs = j->value;
        }
    }

    v.setBool(true);
}


void ExprLambda::eval(EvalState & state, Env & env, Value & v)
{
    v.setLambda(&env, this);
}


void ExprApp::eval(EvalState & state, Env & env, Value & v)
{
    /* FIXME: vFun prevents GCC from doing tail call optimisation. */
    Value vFun;
    e1->eval(state, env, vFun);
    state.callFunction(vFun, *(e2->maybeThunk(state, env)), v, pos);
}


void EvalState::callPrimOp(Value & fun, Value & arg, Value & v, const Pos & pos)
{
    /* Figure out the number of arguments still needed. */
    unsigned int argsDone = 0;
    Value * primOp = &fun;
    while (primOp->type() == Value::tPrimOpApp) {
        argsDone++;
        primOp = primOp->asAppLeft();
    }
    assert(primOp->type() == Value::tPrimOp);
    unsigned int arity = primOp->asPrimOp()->arity;
    unsigned int argsLeft = arity - argsDone;

    if (argsLeft == 1) {
        /* We have all the arguments, so call the primop. */

        /* Put all the arguments in an array. */
        Value * vArgs[arity];
        unsigned int n = arity - 1;
        vArgs[n--] = &arg;
        for (Value * arg = &fun; arg->type() == Value::tPrimOpApp; arg = arg->asAppLeft())
            vArgs[n--] = arg->asAppRight();

        /* And call the primop. */
        nrPrimOpCalls++;
        if (countCalls) primOpCalls[primOp->asPrimOp()->name]++;
        primOp->asPrimOp()->fun(*this, pos, vArgs, v);
    } else {
        Value * fun2 = allocValue();
        *fun2 = fun;
        v.setPrimOpApp(fun2, &arg);
    }
}


void EvalState::callFunction(Value & fun, Value & arg, Value & v, const Pos & pos)
{
    if (fun.type() == Value::tPrimOp || fun.type() == Value::tPrimOpApp) {
        callPrimOp(fun, arg, v, pos);
        return;
    }

    if (fun.type() == Value::tAttrs) {
      auto found = fun.asAttrs()->find(sFunctor);
      if (found != fun.asAttrs()->end()) {
        forceValue(*found->value, pos);
        Value * v2 = allocValue();
        callFunction(*found->value, fun, *v2, pos);
        forceValue(*v2, pos);
        return callFunction(*v2, arg, v, pos);
      }
    }

    if (fun.type() != Value::tLambda)
        throwTypeError("attempt to call something which is not a function but %1%, at %2%", fun, pos);

    ExprLambda & lambda(*fun.asLambda());

    unsigned int size =
        (lambda.arg.empty() ? 0 : 1) +
        (lambda.matchAttrs ? lambda.formals->formals.size() : 0);
    Env & env2(allocEnv(size));
    env2.up = fun.asExprEnv();

    unsigned int displ = 0;

    if (!lambda.matchAttrs)
        env2.values[displ++] = &arg;

    else {
        forceAttrs(arg, pos);

        if (!lambda.arg.empty())
            env2.values[displ++] = &arg;

        /* For each formal argument, get the actual argument.  If
           there is no matching actual argument but the formal
           argument has a default, use the default. */
        unsigned int attrsUsed = 0;
        for (auto & i : lambda.formals->formals) {
            Bindings::iterator j = arg.asAttrs()->find(i.name);
            if (j == arg.asAttrs()->end()) {
                if (!i.def) throwTypeError("%1% called without required argument ‘%2%’, at %3%",
                    lambda, i.name, pos);
                env2.values[displ++] = i.def->maybeThunk(*this, env2);
            } else {
                attrsUsed++;
                env2.values[displ++] = j->value;
            }
        }

        /* Check that each actual argument is listed as a formal
           argument (unless the attribute match specifies a `...'). */
        if (!lambda.formals->ellipsis && attrsUsed != arg.asAttrs()->size()) {
            /* Nope, so show the first unexpected argument to the
               user. */
            for (auto & i : *arg.asAttrs())
                if (lambda.formals->argNames.find(i.name) == lambda.formals->argNames.end())
                    throwTypeError("%1% called with unexpected argument ‘%2%’, at %3%", lambda, i.name, pos);
            abort(); // can't happen
        }
    }

    nrFunctionCalls++;
    if (countCalls) incrFunctionCall(&lambda);

    /* Evaluate the body.  This is conditional on showTrace, because
       catching exceptions makes this function not tail-recursive. */
    if (settings.showTrace)
        try {
            lambda.body->eval(*this, env2, v);
        } catch (Error & e) {
            addErrorPrefix(e, "while evaluating %1%, called from %2%:\n", lambda, pos);
            throw;
        }
    else
        fun.asLambda()->body->eval(*this, env2, v);
}


// Lifted out of callFunction() because it creates a temporary that
// prevents tail-call optimisation.
void EvalState::incrFunctionCall(ExprLambda * fun)
{
    functionCalls[fun]++;
}


void EvalState::autoCallFunction(Bindings & args, Value & fun, Value & res)
{
    forceValue(fun);

    if (fun.type() != Value::tLambda || !fun.asLambda()->matchAttrs) {
        res = fun;
        return;
    }

    Value * actualArgs = allocValue();
    mkAttrs(*actualArgs, fun.asLambda()->formals->formals.size());

    for (auto & i : fun.asLambda()->formals->formals) {
        Bindings::iterator j = args.find(i.name);
        if (j != args.end())
            actualArgs->asAttrs()->push_back(*j);
        else if (!i.def)
            throwTypeError("cannot auto-call a function that has an argument without a default value (‘%1%’)", i.name);
    }

    actualArgs->asAttrs()->sort();

    callFunction(fun, *actualArgs, res, noPos);
}


void ExprWith::eval(EvalState & state, Env & env, Value & v)
{
    Env & env2(state.allocEnv(1));
    env2.up = &env;
    env2.prevWith = prevWith;
    env2.haveWithAttrs = false;
    env2.values[0] = (Value *) attrs;

    body->eval(state, env2, v);
}


void ExprIf::eval(EvalState & state, Env & env, Value & v)
{
    (state.evalBool(env, cond) ? then : else_)->eval(state, env, v);
}


void ExprAssert::eval(EvalState & state, Env & env, Value & v)
{
    if (!state.evalBool(env, cond, pos))
        throwAssertionError("assertion failed at %1%", pos);
    body->eval(state, env, v);
}


void ExprOpNot::eval(EvalState & state, Env & env, Value & v)
{
    v.setBool(!state.evalBool(env, e));
}


void ExprOpEq::eval(EvalState & state, Env & env, Value & v)
{
    Value v1; e1->eval(state, env, v1);
    Value v2; e2->eval(state, env, v2);
    v.setBool(state.eqValues(v1, v2));
}


void ExprOpNEq::eval(EvalState & state, Env & env, Value & v)
{
    Value v1; e1->eval(state, env, v1);
    Value v2; e2->eval(state, env, v2);
    v.setBool(!state.eqValues(v1, v2));
}


void ExprOpAnd::eval(EvalState & state, Env & env, Value & v)
{
    v.setBool(state.evalBool(env, e1, pos) && state.evalBool(env, e2, pos));
}


void ExprOpOr::eval(EvalState & state, Env & env, Value & v)
{
    v.setBool(state.evalBool(env, e1, pos) || state.evalBool(env, e2, pos));
}


void ExprOpImpl::eval(EvalState & state, Env & env, Value & v)
{
    v.setBool(!state.evalBool(env, e1, pos) || state.evalBool(env, e2, pos));
}


void ExprOpUpdate::eval(EvalState & state, Env & env, Value & v)
{
    Value v1, v2;
    state.evalAttrs(env, e1, v1);
    state.evalAttrs(env, e2, v2);

    state.nrOpUpdates++;

    if (v1.asAttrs()->size() == 0) { v = v2; return; }
    if (v2.asAttrs()->size() == 0) { v = v1; return; }

    state.mkAttrs(v, v1.asAttrs()->size() + v2.asAttrs()->size());

    /* Merge the sets, preferring values from the second set.  Make
       sure to keep the resulting vector in sorted order. */
    Bindings::iterator i = v1.asAttrs()->begin();
    Bindings::iterator j = v2.asAttrs()->begin();

    while (i != v1.asAttrs()->end() && j != v2.asAttrs()->end()) {
        if (i->name == j->name) {
            v.asAttrs()->push_back(*j);
            ++i; ++j;
        }
        else if (i->name < j->name)
            v.asAttrs()->push_back(*i++);
        else
            v.asAttrs()->push_back(*j++);
    }

    while (i != v1.asAttrs()->end()) v.asAttrs()->push_back(*i++);
    while (j != v2.asAttrs()->end()) v.asAttrs()->push_back(*j++);

    state.nrOpUpdateValuesCopied += v.asAttrs()->size();
}


void ExprOpConcatLists::eval(EvalState & state, Env & env, Value & v)
{
    Value v1; e1->eval(state, env, v1);
    Value v2; e2->eval(state, env, v2);
    Value * lists[2] = { &v1, &v2 };
    state.concatLists(v, 2, lists, pos);
}


void EvalState::concatLists(Value & v, unsigned int nrLists, Value * * lists, const Pos & pos)
{
    nrListConcats++;

    Value * nonEmpty = 0;
    unsigned int len = 0;
    for (unsigned int n = 0; n < nrLists; ++n) {
        forceList(*lists[n], pos);
        Value::asList ln(lists[n]);
        unsigned int l = ln.length();
        len += l;
        if (l) nonEmpty = lists[n];
    }

    if (nonEmpty) {
        Value::asList nonEmptyList(nonEmpty);
        if (len == nonEmptyList.length()) {
            v = *nonEmpty;
            return;
        }
    }

    mkList(v, len);
    Value::asMutableList outList(v);
    auto out = outList.begin();
    for (unsigned int n = 0, pos = 0; n < nrLists; ++n) {
        Value::asList ln(lists[n]);
        unsigned int l = ln.length();
        memcpy(out + pos, ln.begin(), l * sizeof(Value *));
        pos += l;
    }
}


void ExprConcatStrings::eval(EvalState & state, Env & env, Value & v)
{
    PathSet context;
    std::ostringstream s;
    NixInt n = 0;

    bool first = !forceString;
    Value::Type firstType = Value::tString;

    for (auto & i : *es) {
        Value vTmp;
        i->eval(state, env, vTmp);

        /* If the first element is a path, then the result will also
           be a path, we don't copy anything (yet - that's done later,
           since paths are copied when they are used in a derivation),
           and none of the strings are allowed to have contexts. */
        if (first) {
            firstType = vTmp.type();
            first = false;
        }

        if (firstType == Value::tInt) {
            if (vTmp.type() != Value::tInt)
                throwEvalError("cannot add %1% to an integer, at %2%", showType(vTmp), pos);
            n += vTmp.asInt();
        } else
            s << state.coerceToString(pos, vTmp, context, false, firstType == Value::tString);
    }

    if (firstType == Value::tInt)
        v.setInt(n);
    else if (firstType == Value::tPath) {
        if (!context.empty())
            throwEvalError("a string that refers to a store path cannot be appended to a path, at %1%", pos);
        auto path = canonPath(s.str());
        v.setPath(path.c_str());
    } else
        v.setString(s.str(), context);
}


void ExprPos::eval(EvalState & state, Env & env, Value & v)
{
    state.mkPos(v, &pos);
}


void EvalState::forceValueDeep(Value & v)
{
    std::set<const Value *> seen;

    std::function<void(Value & v)> recurse;

    recurse = [&](Value & v) {
        if (seen.find(&v) != seen.end()) return;
        seen.insert(&v);

        forceValue(v);

        if (v.type() == Value::tAttrs) {
            for (auto & i : *v.asAttrs())
                try {
                    recurse(*i.value);
                } catch (Error & e) {
                    addErrorPrefix(e, "while evaluating the attribute ‘%1%’ at %2%:\n", i.name, *i.pos);
                    throw;
                }
        }

        else if (v.isList()) {
            Value::asList list(v);
            for (unsigned int n = 0; n < list.length(); ++n)
                recurse(*list[n]);
        }
    };

    recurse(v);
}


NixInt EvalState::forceInt(Value & v, const Pos & pos)
{
    forceValue(v, pos);
    if (v.type() != Value::tInt)
        throwTypeError("value is %1% while an integer was expected, at %2%", v, pos);
    return v.asInt();
}


bool EvalState::forceBool(Value & v)
{
    forceValue(v);
    if (v.type() != Value::tBool)
        throwTypeError("value is %1% while a Boolean was expected", v);
    return v.asBool();
}


void EvalState::forceFunction(Value & v, const Pos & pos)
{
    forceValue(v);
    if (v.type() != Value::tLambda && v.type() != Value::tPrimOp && v.type() != Value::tPrimOpApp)
        throwTypeError("value is %1% while a function was expected, at %2%", v, pos);
}


string EvalState::forceString(Value & v, const Pos & pos)
{
    forceValue(v, pos);
    if (v.type() != Value::tString) {
        if (pos)
            throwTypeError("value is %1% while a string was expected, at %2%", v, pos);
        else
            throwTypeError("value is %1% while a string was expected", v);
    }
    return string(v.asString());
}


void copyContext(const Value & v, PathSet & context)
{
    if (v.asStringContext())
        for (const char * * p = v.asStringContext(); *p; ++p)
            context.insert(*p);
}


string EvalState::forceString(Value & v, PathSet & context, const Pos & pos)
{
    string s = forceString(v, pos);
    copyContext(v, context);
    return s;
}


string EvalState::forceStringNoCtx(Value & v, const Pos & pos)
{
    string s = forceString(v, pos);
    if (v.asStringContext()) {
        if (pos)
            throwEvalError("the string ‘%1%’ is not allowed to refer to a store path (such as ‘%2%’), at %3%",
                v.asString(), v.asStringContext()[0], pos);
        else
            throwEvalError("the string ‘%1%’ is not allowed to refer to a store path (such as ‘%2%’)",
                v.asString(), v.asStringContext()[0]);
    }
    return s;
}


bool EvalState::isDerivation(Value & v)
{
    if (v.type() != Value::tAttrs) return false;
    Bindings::iterator i = v.asAttrs()->find(sType);
    if (i == v.asAttrs()->end()) return false;
    forceValue(*i->value);
    if (i->value->type() != Value::tString) return false;
    return strcmp(i->value->asString(), "derivation") == 0;
}


string EvalState::coerceToString(const Pos & pos, Value & v, PathSet & context,
    bool coerceMore, bool copyToStore)
{
    forceValue(v);

    string s;

    if (v.type() == Value::tString) {
        copyContext(v, context);
        return v.asString();
    }

    if (v.type() == Value::tPath) {
        Path path(canonPath(v.asPath()));
        return copyToStore ? copyPathToStore(context, path) : path;
    }

    if (v.type() == Value::tAttrs) {
        Bindings::iterator i = v.asAttrs()->find(sOutPath);
        if (i == v.asAttrs()->end()) throwTypeError("cannot coerce a set to a string, at %1%", pos);
        return coerceToString(pos, *i->value, context, coerceMore, copyToStore);
    }

    if (v.type() == Value::tExternal)
        return v.asExternal()->coerceToString(pos, context, coerceMore, copyToStore);

    if (coerceMore) {

        /* Note that `false' is represented as an empty string for
           shell scripting convenience, just like `null'. */
        if (v.type() == Value::tBool && v.asBool()) return "1";
        if (v.type() == Value::tBool && !v.asBool()) return "";
        if (v.type() == Value::tInt) return int2String(v.asInt());
        if (v.type() == Value::tNull) return "";

        if (v.isList()) {
            string result;
            Value::asList vList(v);
            for (unsigned int n = 0; n < vList.length(); ++n) {
                result += coerceToString(pos, *vList[n],
                    context, coerceMore, copyToStore);
                if (n < vList.length() - 1
                    /* !!! not quite correct */
                    && (!vList[n]->isList() || vList[n]->type() != Value::tList0))
                    result += " ";
            }
            return result;
        }
    }

    throwTypeError("cannot coerce %1% to a string, at %2%", v, pos);
}


string EvalState::copyPathToStore(PathSet & context, const Path & path)
{
    if (nix::isDerivation(path))
        throwEvalError("file names are not allowed to end in ‘%1%’", drvExtension);

    Path dstPath;
    if (srcToStore[path] != "")
        dstPath = srcToStore[path];
    else {
        dstPath = settings.readOnlyMode
            ? computeStorePathForPath(checkSourcePath(path)).first
            : store->addToStore(baseNameOf(path), checkSourcePath(path), true, htSHA256, defaultPathFilter, repair);
        srcToStore[path] = dstPath;
        printMsg(lvlChatty, format("copied source ‘%1%’ -> ‘%2%’")
            % path % dstPath);
    }

    context.insert(dstPath);
    return dstPath;
}


Path EvalState::coerceToPath(const Pos & pos, Value & v, PathSet & context)
{
    string path = coerceToString(pos, v, context, false, false);
    if (path == "" || path[0] != '/')
        throwEvalError("string ‘%1%’ doesn't represent an absolute path, at %2%", path, pos);
    return path;
}


bool EvalState::eqValues(Value & v1, Value & v2)
{
    forceValue(v1);
    forceValue(v2);

    /* !!! Hack to support some old broken code that relies on pointer
       equality tests between sets.  (Specifically, builderDefs calls
       uniqList on a list of sets.)  Will remove this eventually. */
    if (&v1 == &v2) return true;

    if (v1.type() != v2.type()) return false;

    switch (v1.type()) {

        case Value::tInt:
            return v1.asInt() == v2.asInt();

        case Value::tBool:
            return v1.asBool() == v2.asBool();

        case Value::tString:
            return strcmp(v1.asString(), v2.asString()) == 0;

        case Value::tPath:
            return strcmp(v1.asPath(), v2.asPath()) == 0;

        case Value::tNull:
            return true;

        case Value::tList0:
        case Value::tList1:
        case Value::tList2:
        case Value::tListN: {
            Value::asList l1(v1);
            Value::asList l2(v2);
            if (l1.length() != l2.length())
                return false;
            for (unsigned int n = 0; n < l1.length(); ++n)
                if (!eqValues(*l1[n], *l2[n]))
                    return false;
            return true;
        }

        case Value::tAttrs: {
            /* If both sets denote a derivation (type = "derivation"),
               then compare their outPaths. */
            if (isDerivation(v1) && isDerivation(v2)) {
                Bindings::iterator i = v1.asAttrs()->find(sOutPath);
                Bindings::iterator j = v2.asAttrs()->find(sOutPath);
                if (i != v1.asAttrs()->end() && j != v2.asAttrs()->end())
                    return eqValues(*i->value, *j->value);
            }

            if (v1.asAttrs()->size() != v2.asAttrs()->size()) return false;

            /* Otherwise, compare the attributes one by one. */
            Bindings::iterator i, j;
            for (i = v1.asAttrs()->begin(), j = v2.asAttrs()->begin(); i != v1.asAttrs()->end(); ++i, ++j)
                if (i->name != j->name || !eqValues(*i->value, *j->value))
                    return false;

            return true;
        }

        /* Functions are incomparable. */
        case Value::tLambda:
        case Value::tPrimOp:
        case Value::tPrimOpApp:
            return false;

        case Value::tExternal:
            return *v1.asExternal() == *v2.asExternal();

        default:
            throwEvalError("cannot compare %1% with %2%", showType(v1), showType(v2));
    }
}


void EvalState::printStats()
{
    bool showStats = getEnv("NIX_SHOW_STATS", "0") != "0";
    Verbosity v = showStats ? lvlInfo : lvlDebug;
    printMsg(v, "evaluation statistics:");

    struct rusage buf;
    getrusage(RUSAGE_SELF, &buf);
    float cpuTime = buf.ru_utime.tv_sec + ((float) buf.ru_utime.tv_usec / 1000000);

    uint64_t bEnvs = nrEnvs * sizeof(Env) + nrValuesInEnvs * sizeof(Value *);
    uint64_t bLists = nrListElems * sizeof(Value *);
    uint64_t bValues = nrValues * sizeof(Value);
    uint64_t bAttrsets = nrAttrsets * sizeof(Bindings) + nrAttrsInAttrsets * sizeof(Attr);

    printMsg(v, format("  time elapsed: %1%") % cpuTime);
    printMsg(v, format("  size of a value: %1%") % sizeof(Value));
    printMsg(v, format("  size of an attr: %1%") % sizeof(Attr));
    printMsg(v, format("  environments allocated: %1% (%2% bytes)") % nrEnvs % bEnvs);
    printMsg(v, format("  list elements: %1% (%2% bytes)") % nrListElems % bLists);
    printMsg(v, format("  list concatenations: %1%") % nrListConcats);
    printMsg(v, format("  values allocated: %1% (%2% bytes)") % nrValues % bValues);
    printMsg(v, format("  sets allocated: %1% (%2% bytes)") % nrAttrsets % bAttrsets);
    printMsg(v, format("  right-biased unions: %1%") % nrOpUpdates);
    printMsg(v, format("  values copied in right-biased unions: %1%") % nrOpUpdateValuesCopied);
    printMsg(v, format("  symbols in symbol table: %1%") % symbols.size());
    printMsg(v, format("  size of symbol table: %1%") % symbols.totalSize());
    printMsg(v, format("  number of thunks: %1%") % nrThunks);
    printMsg(v, format("  number of thunks avoided: %1%") % nrAvoided);
    printMsg(v, format("  number of attr lookups: %1%") % nrLookups);
    printMsg(v, format("  number of primop calls: %1%") % nrPrimOpCalls);
    printMsg(v, format("  number of function calls: %1%") % nrFunctionCalls);
    printMsg(v, format("  total allocations: %1% bytes") % (bEnvs + bLists + bValues + bAttrsets));

#if HAVE_BOEHMGC
    GC_word heapSize, totalBytes;
    GC_get_heap_usage_safe(&heapSize, 0, 0, 0, &totalBytes);
    printMsg(v, format("  current Boehm heap size: %1% bytes") % heapSize);
    printMsg(v, format("  total Boehm heap allocations: %1% bytes") % totalBytes);
#endif

    if (countCalls) {
        v = lvlInfo;

        printMsg(v, format("calls to %1% primops:") % primOpCalls.size());
        typedef std::multimap<unsigned int, Symbol> PrimOpCalls_;
        PrimOpCalls_ primOpCalls_;
        for (auto & i : primOpCalls)
            primOpCalls_.insert(std::pair<unsigned int, Symbol>(i.second, i.first));
        for (auto i = primOpCalls_.rbegin(); i != primOpCalls_.rend(); ++i)
            printMsg(v, format("%1$10d %2%") % i->first % i->second);

        printMsg(v, format("calls to %1% functions:") % functionCalls.size());
        typedef std::multimap<unsigned int, ExprLambda *> FunctionCalls_;
        FunctionCalls_ functionCalls_;
        for (auto & i : functionCalls)
            functionCalls_.insert(std::pair<unsigned int, ExprLambda *>(i.second, i.first));
        for (auto i = functionCalls_.rbegin(); i != functionCalls_.rend(); ++i)
            printMsg(v, format("%1$10d %2%") % i->first % i->second->showNamePos());

        printMsg(v, format("evaluations of %1% attributes:") % attrSelects.size());
        typedef std::multimap<unsigned int, Pos> AttrSelects_;
        AttrSelects_ attrSelects_;
        for (auto & i : attrSelects)
            attrSelects_.insert(std::pair<unsigned int, Pos>(i.second, i.first));
        for (auto i = attrSelects_.rbegin(); i != attrSelects_.rend(); ++i)
            printMsg(v, format("%1$10d %2%") % i->first % i->second);

    }
}


size_t valueSize(Value & v)
{
    std::set<const void *> seen;

    auto doString = [&](const char * s) -> size_t {
        if (seen.find(s) != seen.end()) return 0;
        seen.insert(s);
        return strlen(s) + 1;
    };

    std::function<size_t(Value & v)> doValue;
    std::function<size_t(Env & v)> doEnv;

    doValue = [&](Value & v) -> size_t {
        if (seen.find(&v) != seen.end()) return 0;
        seen.insert(&v);

        size_t sz = sizeof(Value);

        switch (v.type()) {
        case Value::tString:
            sz += doString(v.asString());
            if (v.asStringContext())
                for (const char * * p = v.asStringContext(); *p; ++p)
                    sz += doString(*p);
            break;
        case Value::tPath:
            sz += doString(v.asPath());
            break;
        case Value::tAttrs:
            if (seen.find(v.asAttrs()) == seen.end()) {
                seen.insert(v.asAttrs());
                sz += sizeof(Bindings) + sizeof(Attr) * v.asAttrs()->capacity();
                for (auto & i : *v.asAttrs())
                    sz += doValue(*i.value);
            }
            break;
        case Value::tList0:
        case Value::tList1:
        case Value::tList2:
        case Value::tListN: {
            Value::asList list(v);
            if (seen.find(list.begin()) == seen.end()) {
                seen.insert(list.begin());
                sz += list.length() * sizeof(Value *);
                for (unsigned int n = 0; n < list.length(); ++n)
                    sz += doValue(*list[n]);
            }
            break;
        }
        case Value::tThunk:
            sz += doEnv(*v.asExprEnv());
            break;
        case Value::tApp:
        case Value::tPrimOpApp:
            sz += doValue(*v.asAppLeft());
            sz += doValue(*v.asAppRight());
            break;
        case Value::tLambda:
            sz += doEnv(*v.asExprEnv());
            break;
        case Value::tExternal:
            if (seen.find(v.asExternal()) != seen.end()) break;
            seen.insert(v.asExternal());
            sz += v.asExternal()->valueSize(seen);
            break;
        default:
            ;
        }

        return sz;
    };

    doEnv = [&](Env & env) -> size_t {
        if (seen.find(&env) != seen.end()) return 0;
        seen.insert(&env);

        size_t sz = sizeof(Env) + sizeof(Value *) * env.size;

        for (unsigned int i = 0; i < env.size; ++i)
            if (env.values[i])
                sz += doValue(*env.values[i]);

        if (env.up) sz += doEnv(*env.up);

        return sz;
    };

    return doValue(v);
}


string ExternalValueBase::coerceToString(const Pos & pos, PathSet & context, bool copyMore, bool copyToStore) const
{
    throw TypeError(format("cannot coerce %1% to a string, at %2%") %
        showType() % pos);
}


bool ExternalValueBase::operator==(const ExternalValueBase & b) const
{
    return false;
}


std::ostream & operator << (std::ostream & str, const ExternalValueBase & v) {
    return v.print(str);
}


}
