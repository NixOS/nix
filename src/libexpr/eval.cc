#include "eval.hh"
#include "hash.hh"
#include "util.hh"
#include "store-api.hh"
#include "derivations.hh"
#include "globals.hh"
#include "eval-inline.hh"
#include "download.hh"
#include "json.hh"
#include "function-trace.hh"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <iostream>
#include <fstream>

#include <sys/resource.h>

#if HAVE_BOEHMGC

#include <gc/gc.h>
#include <gc/gc_cpp.h>

#endif

namespace nix {


static char * dupString(const char * s)
{
    char * t;
#if HAVE_BOEHMGC
    t = GC_STRDUP(s);
#else
    t = strdup(s);
#endif
    if (!t) throw std::bad_alloc();
    return t;
}


static void printValue(std::ostream & str, std::set<const Value *> & active, const Value & v)
{
    checkInterrupt();

    if (active.find(&v) != active.end()) {
        str << "<CYCLE>";
        return;
    }
    active.insert(&v);

    switch (v.type) {
    case tInt:
        str << v.integer;
        break;
    case tBool:
        str << (v.boolean ? "true" : "false");
        break;
    case tString:
        str << "\"";
        for (const char * i = v.string.s; *i; i++)
            if (*i == '\"' || *i == '\\') str << "\\" << *i;
            else if (*i == '\n') str << "\\n";
            else if (*i == '\r') str << "\\r";
            else if (*i == '\t') str << "\\t";
            else str << *i;
        str << "\"";
        break;
    case tPath:
        str << v.path; // !!! escaping?
        break;
    case tNull:
        str << "null";
        break;
    case tAttrs: {
        str << "{ ";
        for (auto & i : v.attrs->lexicographicOrder()) {
            str << i->name << " = ";
            printValue(str, active, *i->value);
            str << "; ";
        }
        str << "}";
        break;
    }
    case tList1:
    case tList2:
    case tListN:
        str << "[ ";
        for (unsigned int n = 0; n < v.listSize(); ++n) {
            printValue(str, active, *v.listElems()[n]);
            str << " ";
        }
        str << "]";
        break;
    case tThunk:
    case tApp:
        str << "<CODE>";
        break;
    case tLambda:
        str << "<LAMBDA>";
        break;
    case tPrimOp:
        str << "<PRIMOP>";
        break;
    case tPrimOpApp:
        str << "<PRIMOP-APP>";
        break;
    case tExternal:
        str << *v.external;
        break;
    case tFloat:
        str << v.fpoint;
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


const Value *getPrimOp(const Value &v) {
    const Value * primOp = &v;
    while (primOp->type == tPrimOpApp) {
        primOp = primOp->primOpApp.left;
    }
    assert(primOp->type == tPrimOp);
    return primOp;
}


string showType(const Value & v)
{
    switch (v.type) {
        case tInt: return "an integer";
        case tBool: return "a boolean";
        case tString: return v.string.context ? "a string with context" : "a string";
        case tPath: return "a path";
        case tNull: return "null";
        case tAttrs: return "a set";
        case tList1: case tList2: case tListN: return "a list";
        case tThunk: return "a thunk";
        case tApp: return "a function application";
        case tLambda: return "a function";
        case tBlackhole: return "a black hole";
        case tPrimOp:
            return fmt("the built-in function '%s'", string(v.primOp->name));
        case tPrimOpApp:
            return fmt("the partially applied built-in function '%s'", string(getPrimOp(v)->primOp->name));
        case tExternal: return v.external->showType();
        case tFloat: return "a float";
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
    if (name.symbol.set()) {
        return name.symbol;
    } else {
        Value nameValue;
        name.expr->eval(state, env, nameValue);
        state.forceStringNoCtx(nameValue);
        return state.symbols.create(nameValue.string.s);
    }
}


static bool gcInitialised = false;

void initGC()
{
    if (gcInitialised) return;

#if HAVE_BOEHMGC
    /* Initialise the Boehm garbage collector. */

    /* Don't look for interior pointers. This reduces the odds of
       misdetection a bit. */
    GC_set_all_interior_pointers(0);

    /* We don't have any roots in data segments, so don't scan from
       there. */
    GC_set_no_dls(1);

    GC_INIT();

    GC_set_oom_fn(oomHandler);

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
static Strings parseNixPath(const string & s)
{
    Strings res;

    auto p = s.begin();

    while (p != s.end()) {
        auto start = p;
        auto start2 = p;

        while (p != s.end() && *p != ':') {
            if (*p == '=') start2 = p + 1;
            ++p;
        }

        if (p == s.end()) {
            if (p != start) res.push_back(std::string(start, p));
            break;
        }

        if (*p == ':') {
            if (isUri(std::string(start2, s.end()))) {
                ++p;
                while (p != s.end() && *p != ':') ++p;
            }
            res.push_back(std::string(start, p));
            if (p == s.end()) break;
        }

        ++p;
    }

    return res;
}


EvalState::EvalState(const Strings & _searchPath, ref<Store> store)
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
    , sToString(symbols.create("__toString"))
    , sRight(symbols.create("right"))
    , sWrong(symbols.create("wrong"))
    , sStructuredAttrs(symbols.create("__structuredAttrs"))
    , sBuilder(symbols.create("builder"))
    , sArgs(symbols.create("args"))
    , sOutputHash(symbols.create("outputHash"))
    , sOutputHashAlgo(symbols.create("outputHashAlgo"))
    , sOutputHashMode(symbols.create("outputHashMode"))
    , repair(NoRepair)
    , store(store)
    , baseEnv(allocEnv(128))
    , staticBaseEnv(false, 0)
{
    countCalls = getEnv("NIX_COUNT_CALLS", "0") != "0";

    assert(gcInitialised);

    static_assert(sizeof(Env) <= 16, "environment must be <= 16 bytes");

    /* Initialise the Nix expression search path. */
    if (!evalSettings.pureEval) {
        Strings paths = parseNixPath(getEnv("NIX_PATH", ""));
        for (auto & i : _searchPath) addToSearchPath(i);
        for (auto & i : paths) addToSearchPath(i);
    }
    addToSearchPath("nix=" + canonPath(settings.nixDataDir + "/nix/corepkgs", true));

    if (evalSettings.restrictEval || evalSettings.pureEval) {
        allowedPaths = PathSet();

        for (auto & i : searchPath) {
            auto r = resolveSearchPathElem(i);
            if (!r.first) continue;

            auto path = r.second;

            if (store->isInStore(r.second)) {
                PathSet closure;
                store->computeFSClosure(store->toStorePath(r.second), closure);
                for (auto & path : closure)
                    allowedPaths->insert(path);
            } else
                allowedPaths->insert(r.second);
        }
    }

    clearValue(vEmptySet);
    vEmptySet.type = tAttrs;
    vEmptySet.attrs = allocBindings(0);

    createBaseEnv();
}


EvalState::~EvalState()
{
}


Path EvalState::checkSourcePath(const Path & path_)
{
    if (!allowedPaths) return path_;

    auto i = resolvedPaths.find(path_);
    if (i != resolvedPaths.end())
        return i->second;

    bool found = false;

    /* First canonicalize the path without symlinks, so we make sure an
     * attacker can't append ../../... to a path that would be in allowedPaths
     * and thus leak symlink targets.
     */
    Path abspath = canonPath(path_);

    for (auto & i : *allowedPaths) {
        if (isDirOrInDir(abspath, i)) {
            found = true;
            break;
        }
    }

    if (!found)
        throw RestrictedPathError("access to path '%1%' is forbidden in restricted mode", abspath);

    /* Resolve symlinks. */
    debug(format("checking access to '%s'") % abspath);
    Path path = canonPath(abspath, true);

    for (auto & i : *allowedPaths) {
        if (isDirOrInDir(path, i)) {
            resolvedPaths[path_] = path;
            return path;
        }
    }

    throw RestrictedPathError("access to path '%1%' is forbidden in restricted mode", path);
}


void EvalState::checkURI(const std::string & uri)
{
    if (!evalSettings.restrictEval) return;

    /* 'uri' should be equal to a prefix, or in a subdirectory of a
       prefix. Thus, the prefix https://github.co does not permit
       access to https://github.com. Note: this allows 'http://' and
       'https://' as prefixes for any http/https URI. */
    for (auto & prefix : evalSettings.allowedUris.get())
        if (uri == prefix ||
            (uri.size() > prefix.size()
            && prefix.size() > 0
            && hasPrefix(uri, prefix)
            && (prefix[prefix.size() - 1] == '/' || uri[prefix.size()] == '/')))
            return;

    /* If the URI is a path, then check it against allowedPaths as
       well. */
    if (hasPrefix(uri, "/")) {
        checkSourcePath(uri);
        return;
    }

    if (hasPrefix(uri, "file://")) {
        checkSourcePath(std::string(uri, 7));
        return;
    }

    throw RestrictedPathError("access to URI '%s' is forbidden in restricted mode", uri);
}


Path EvalState::toRealPath(const Path & path, const PathSet & context)
{
    // FIXME: check whether 'path' is in 'context'.
    return
        !context.empty() && store->isInStore(path)
        ? store->toRealPath(path)
        : path;
};


Value * EvalState::addConstant(const string & name, Value & v)
{
    Value * v2 = allocValue();
    *v2 = v;
    staticBaseEnv.vars[symbols.create(name)] = baseEnvDispl;
    baseEnv.values[baseEnvDispl++] = v2;
    string name2 = string(name, 0, 2) == "__" ? string(name, 2) : name;
    baseEnv.values[0]->attrs->push_back(Attr(symbols.create(name2), v2));
    return v2;
}


Value * EvalState::addPrimOp(const string & name,
    size_t arity, PrimOpFun primOp)
{
    if (arity == 0) {
        Value v;
        primOp(*this, noPos, nullptr, v);
        return addConstant(name, v);
    }
    Value * v = allocValue();
    string name2 = string(name, 0, 2) == "__" ? string(name, 2) : name;
    Symbol sym = symbols.create(name2);
    v->type = tPrimOp;
    v->primOp = new PrimOp(primOp, arity, sym);
    staticBaseEnv.vars[symbols.create(name)] = baseEnvDispl;
    baseEnv.values[baseEnvDispl++] = v;
    baseEnv.values[0]->attrs->push_back(Attr(sym, v));
    return v;
}


Value & EvalState::getBuiltin(const string & name)
{
    return *baseEnv.values[0]->attrs->find(symbols.create(name))->value;
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

LocalNoInlineNoReturn(void throwAssertionError(const char * s, const string & s1, const Pos & pos))
{
    throw AssertionError(format(s) % s1 % pos);
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


void mkString(Value & v, const char * s)
{
    mkStringNoCopy(v, dupString(s));
}


Value & mkString(Value & v, const string & s, const PathSet & context)
{
    mkString(v, s.c_str());
    if (!context.empty()) {
        size_t n = 0;
        v.string.context = (const char * *)
            allocBytes((context.size() + 1) * sizeof(char *));
        for (auto & i : context)
            v.string.context[n++] = dupString(i.c_str());
        v.string.context[n] = 0;
    }
    return v;
}


void mkPath(Value & v, const char * s)
{
    mkPathNoCopy(v, dupString(s));
}


inline Value * EvalState::lookupVar(Env * env, const ExprVar & var, bool noEval)
{
    for (size_t l = var.level; l; --l, env = env->up) ;

    if (!var.fromWith) return env->values[var.displ];

    while (1) {
        if (env->type == Env::HasWithExpr) {
            if (noEval) return 0;
            Value * v = allocValue();
            evalAttrs(*env->up, (Expr *) env->values[0], *v);
            env->values[0] = v;
            env->type = Env::HasWithAttrs;
        }
        Bindings::iterator j = env->values[0]->attrs->find(var.name);
        if (j != env->values[0]->attrs->end()) {
            if (countCalls && j->pos) attrSelects[*j->pos]++;
            return j->value;
        }
        if (!env->prevWith)
            throwUndefinedVarError("undefined variable '%1%' at %2%", var.name, var.pos);
        for (size_t l = env->prevWith; l; --l, env = env->up) ;
    }
}


std::atomic<uint64_t> nrValuesFreed{0};

void finalizeValue(void * obj, void * data)
{
    nrValuesFreed++;
}

Value * EvalState::allocValue()
{
    nrValues++;
    auto v = (Value *) allocBytes(sizeof(Value));
    //GC_register_finalizer_no_order(v, finalizeValue, nullptr, nullptr, nullptr);
    return v;
}


Env & EvalState::allocEnv(size_t size)
{
    if (size > std::numeric_limits<decltype(Env::size)>::max())
        throw Error("environment size %d is too big", size);

    nrEnvs++;
    nrValuesInEnvs += size;
    Env * env = (Env *) allocBytes(sizeof(Env) + size * sizeof(Value *));
    env->size = (decltype(Env::size)) size;
    env->type = Env::Plain;

    /* We assume that env->values has been cleared by the allocator; maybeThunk() and lookupVar fromWith expect this. */

    return *env;
}


void EvalState::mkList(Value & v, size_t size)
{
    clearValue(v);
    if (size == 1)
        v.type = tList1;
    else if (size == 2)
        v.type = tList2;
    else {
        v.type = tListN;
        v.bigList.size = size;
        v.bigList.elems = size ? (Value * *) allocBytes(size * sizeof(Value *)) : 0;
    }
    nrListElems += size;
}


unsigned long nrThunks = 0;

static inline void mkThunk(Value & v, Env & env, Expr * expr)
{
    v.type = tThunk;
    v.thunk.env = &env;
    v.thunk.expr = expr;
    nrThunks++;
}


void EvalState::mkThunk_(Value & v, Expr * expr)
{
    mkThunk(v, baseEnv, expr);
}


void EvalState::mkPos(Value & v, Pos * pos)
{
    if (pos && pos->file.set()) {
        mkAttrs(v, 3);
        mkString(*allocAttr(v, sFile), pos->file);
        mkInt(*allocAttr(v, sLine), pos->line);
        mkInt(*allocAttr(v, sColumn), pos->column);
        v.attrs->sort();
    } else
        mkNull(v);
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

Value * ExprFloat::maybeThunk(EvalState & state, Env & env)
{
    nrAvoided++;
    return &v;
}

Value * ExprPath::maybeThunk(EvalState & state, Env & env)
{
    nrAvoided++;
    return &v;
}


void EvalState::evalFile(const Path & path_, Value & v)
{
    auto path = checkSourcePath(path_);

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

    printTalkative("evaluating file '%1%'", path2);
    Expr * e = nullptr;

    auto j = fileParseCache.find(path2);
    if (j != fileParseCache.end())
        e = j->second;

    if (!e)
        e = parseExprFromFile(checkSourcePath(path2));

    fileParseCache[path2] = e;

    try {
        eval(e, v);
    } catch (Error & e) {
        addErrorPrefix(e, "while evaluating the file '%1%':\n", path2);
        throw;
    }

    fileEvalCache[path2] = v;
    if (path != path2) fileEvalCache[path] = v;
}


void EvalState::resetFileCache()
{
    fileEvalCache.clear();
    fileParseCache.clear();
}


void EvalState::eval(Expr * e, Value & v)
{
    e->eval(*this, baseEnv, v);
}


inline bool EvalState::evalBool(Env & env, Expr * e)
{
    Value v;
    e->eval(*this, env, v);
    if (v.type != tBool)
        throwTypeError("value is %1% while a Boolean was expected", v);
    return v.boolean;
}


inline bool EvalState::evalBool(Env & env, Expr * e, const Pos & pos)
{
    Value v;
    e->eval(*this, env, v);
    if (v.type != tBool)
        throwTypeError("value is %1% while a Boolean was expected, at %2%", v, pos);
    return v.boolean;
}


inline void EvalState::evalAttrs(Env & env, Expr * e, Value & v)
{
    e->eval(*this, env, v);
    if (v.type != tAttrs)
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


void ExprFloat::eval(EvalState & state, Env & env, Value & v)
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
        size_t displ = 0;
        for (auto & i : attrs) {
            Value * vAttr;
            if (hasOverrides && !i.second.inherited) {
                vAttr = state.allocValue();
                mkThunk(*vAttr, env2, i.second.e);
            } else
                vAttr = i.second.e->maybeThunk(state, i.second.inherited ? env : env2);
            env2.values[displ++] = vAttr;
            v.attrs->push_back(Attr(i.first, vAttr, &i.second.pos));
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
            Value * vOverrides = (*v.attrs)[overrides->second.displ].value;
            state.forceAttrs(*vOverrides);
            Bindings * newBnds = state.allocBindings(v.attrs->capacity() + vOverrides->attrs->size());
            for (auto & i : *v.attrs)
                newBnds->push_back(i);
            for (auto & i : *vOverrides->attrs) {
                AttrDefs::iterator j = attrs.find(i.name);
                if (j != attrs.end()) {
                    (*newBnds)[j->second.displ] = i;
                    env2.values[j->second.displ] = i.value;
                } else
                    newBnds->push_back(i);
            }
            newBnds->sort();
            v.attrs = newBnds;
        }
    }

    else
        for (auto & i : attrs)
            v.attrs->push_back(Attr(i.first, i.second.e->maybeThunk(state, env), &i.second.pos));

    /* Dynamic attrs apply *after* rec and __overrides. */
    for (auto & i : dynamicAttrs) {
        Value nameVal;
        i.nameExpr->eval(state, *dynamicEnv, nameVal);
        state.forceValue(nameVal, i.pos);
        if (nameVal.type == tNull)
            continue;
        state.forceStringNoCtx(nameVal);
        Symbol nameSym = state.symbols.create(nameVal.string.s);
        Bindings::iterator j = v.attrs->find(nameSym);
        if (j != v.attrs->end())
            throwEvalError("dynamic attribute '%1%' at %2% already defined at %3%", nameSym, i.pos, *j->pos);

        i.valueExpr->setName(nameSym);
        /* Keep sorted order so find can catch duplicates */
        v.attrs->push_back(Attr(nameSym, i.valueExpr->maybeThunk(state, *dynamicEnv), &i.pos));
        v.attrs->sort(); // FIXME: inefficient
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
    size_t displ = 0;
    for (auto & i : attrs->attrs)
        env2.values[displ++] = i.second.e->maybeThunk(state, i.second.inherited ? env : env2);

    body->eval(state, env2, v);
}


void ExprList::eval(EvalState & state, Env & env, Value & v)
{
    state.mkList(v, elems.size());
    for (size_t n = 0; n < elems.size(); ++n)
        v.listElems()[n] = elems[n]->maybeThunk(state, env);
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
                if (vAttrs->type != tAttrs ||
                    (j = vAttrs->attrs->find(name)) == vAttrs->attrs->end())
                {
                    def->eval(state, env, v);
                    return;
                }
            } else {
                state.forceAttrs(*vAttrs, pos);
                if ((j = vAttrs->attrs->find(name)) == vAttrs->attrs->end())
                    throwEvalError("attribute '%1%' missing, at %2%", name, pos);
            }
            vAttrs = j->value;
            pos2 = j->pos;
            if (state.countCalls && pos2) state.attrSelects[*pos2]++;
        }

        state.forceValue(*vAttrs, ( pos2 != NULL ? *pos2 : this->pos ) );

    } catch (Error & e) {
        if (pos2 && pos2->file != state.sDerivationNix)
            addErrorPrefix(e, "while evaluating the attribute '%1%' at %2%:\n",
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
        if (vAttrs->type != tAttrs ||
            (j = vAttrs->attrs->find(name)) == vAttrs->attrs->end())
        {
            mkBool(v, false);
            return;
        } else {
            vAttrs = j->value;
        }
    }

    mkBool(v, true);
}


void ExprLambda::eval(EvalState & state, Env & env, Value & v)
{
    v.type = tLambda;
    v.lambda.env = &env;
    v.lambda.fun = this;
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
    size_t argsDone = 0;
    Value * primOp = &fun;
    while (primOp->type == tPrimOpApp) {
        argsDone++;
        primOp = primOp->primOpApp.left;
    }
    assert(primOp->type == tPrimOp);
    auto arity = primOp->primOp->arity;
    auto argsLeft = arity - argsDone;

    if (argsLeft == 1) {
        /* We have all the arguments, so call the primop. */

        /* Put all the arguments in an array. */
        Value * vArgs[arity];
        auto n = arity - 1;
        vArgs[n--] = &arg;
        for (Value * arg = &fun; arg->type == tPrimOpApp; arg = arg->primOpApp.left)
            vArgs[n--] = arg->primOpApp.right;

        /* And call the primop. */
        nrPrimOpCalls++;
        if (countCalls) primOpCalls[primOp->primOp->name]++;
        primOp->primOp->fun(*this, pos, vArgs, v);
    } else {
        Value * fun2 = allocValue();
        *fun2 = fun;
        v.type = tPrimOpApp;
        v.primOpApp.left = fun2;
        v.primOpApp.right = &arg;
    }
}

void EvalState::callFunction(Value & fun, Value & arg, Value & v, const Pos & pos)
{
    auto trace = evalSettings.traceFunctionCalls ? std::make_unique<FunctionCallTrace>(pos) : nullptr;

    forceValue(fun, pos);

    if (fun.type == tPrimOp || fun.type == tPrimOpApp) {
        callPrimOp(fun, arg, v, pos);
        return;
    }

    if (fun.type == tAttrs) {
      auto found = fun.attrs->find(sFunctor);
      if (found != fun.attrs->end()) {
        /* fun may be allocated on the stack of the calling function,
         * but for functors we may keep a reference, so heap-allocate
         * a copy and use that instead.
         */
        auto & fun2 = *allocValue();
        fun2 = fun;
        /* !!! Should we use the attr pos here? */
        Value v2;
        callFunction(*found->value, fun2, v2, pos);
        return callFunction(v2, arg, v, pos);
      }
    }

    if (fun.type != tLambda)
        throwTypeError("attempt to call something which is not a function but %1%, at %2%", fun, pos);

    ExprLambda & lambda(*fun.lambda.fun);

    auto size =
        (lambda.arg.empty() ? 0 : 1) +
        (lambda.matchAttrs ? lambda.formals->formals.size() : 0);
    Env & env2(allocEnv(size));
    env2.up = fun.lambda.env;

    size_t displ = 0;

    if (!lambda.matchAttrs)
        env2.values[displ++] = &arg;

    else {
        forceAttrs(arg, pos);

        if (!lambda.arg.empty())
            env2.values[displ++] = &arg;

        /* For each formal argument, get the actual argument.  If
           there is no matching actual argument but the formal
           argument has a default, use the default. */
        size_t attrsUsed = 0;
        for (auto & i : lambda.formals->formals) {
            Bindings::iterator j = arg.attrs->find(i.name);
            if (j == arg.attrs->end()) {
                if (!i.def) throwTypeError("%1% called without required argument '%2%', at %3%",
                    lambda, i.name, pos);
                env2.values[displ++] = i.def->maybeThunk(*this, env2);
            } else {
                attrsUsed++;
                env2.values[displ++] = j->value;
            }
        }

        /* Check that each actual argument is listed as a formal
           argument (unless the attribute match specifies a `...'). */
        if (!lambda.formals->ellipsis && attrsUsed != arg.attrs->size()) {
            /* Nope, so show the first unexpected argument to the
               user. */
            for (auto & i : *arg.attrs)
                if (lambda.formals->argNames.find(i.name) == lambda.formals->argNames.end())
                    throwTypeError("%1% called with unexpected argument '%2%', at %3%", lambda, i.name, pos);
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
        fun.lambda.fun->body->eval(*this, env2, v);
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

    if (fun.type == tAttrs) {
        auto found = fun.attrs->find(sFunctor);
        if (found != fun.attrs->end()) {
            Value * v = allocValue();
            callFunction(*found->value, fun, *v, noPos);
            forceValue(*v);
            return autoCallFunction(args, *v, res);
        }
    }

    if (fun.type != tLambda || !fun.lambda.fun->matchAttrs) {
        res = fun;
        return;
    }

    Value * actualArgs = allocValue();
    mkAttrs(*actualArgs, fun.lambda.fun->formals->formals.size());

    for (auto & i : fun.lambda.fun->formals->formals) {
        Bindings::iterator j = args.find(i.name);
        if (j != args.end())
            actualArgs->attrs->push_back(*j);
        else if (!i.def)
            throwTypeError("cannot auto-call a function that has an argument without a default value ('%1%')", i.name);
    }

    actualArgs->attrs->sort();

    callFunction(fun, *actualArgs, res, noPos);
}


void ExprWith::eval(EvalState & state, Env & env, Value & v)
{
    Env & env2(state.allocEnv(1));
    env2.up = &env;
    env2.prevWith = prevWith;
    env2.type = Env::HasWithExpr;
    env2.values[0] = (Value *) attrs;

    body->eval(state, env2, v);
}


void ExprIf::eval(EvalState & state, Env & env, Value & v)
{
    (state.evalBool(env, cond) ? then : else_)->eval(state, env, v);
}


void ExprAssert::eval(EvalState & state, Env & env, Value & v)
{
    if (!state.evalBool(env, cond, pos)) {
        std::ostringstream out;
        cond->show(out);
        throwAssertionError("assertion %1% failed at %2%", out.str(), pos);
    }
    body->eval(state, env, v);
}


void ExprOpNot::eval(EvalState & state, Env & env, Value & v)
{
    mkBool(v, !state.evalBool(env, e));
}


void ExprOpEq::eval(EvalState & state, Env & env, Value & v)
{
    Value v1; e1->eval(state, env, v1);
    Value v2; e2->eval(state, env, v2);
    mkBool(v, state.eqValues(v1, v2));
}


void ExprOpNEq::eval(EvalState & state, Env & env, Value & v)
{
    Value v1; e1->eval(state, env, v1);
    Value v2; e2->eval(state, env, v2);
    mkBool(v, !state.eqValues(v1, v2));
}


void ExprOpAnd::eval(EvalState & state, Env & env, Value & v)
{
    mkBool(v, state.evalBool(env, e1, pos) && state.evalBool(env, e2, pos));
}


void ExprOpOr::eval(EvalState & state, Env & env, Value & v)
{
    mkBool(v, state.evalBool(env, e1, pos) || state.evalBool(env, e2, pos));
}


void ExprOpImpl::eval(EvalState & state, Env & env, Value & v)
{
    mkBool(v, !state.evalBool(env, e1, pos) || state.evalBool(env, e2, pos));
}


void ExprOpUpdate::eval(EvalState & state, Env & env, Value & v)
{
    Value v1, v2;
    state.evalAttrs(env, e1, v1);
    state.evalAttrs(env, e2, v2);

    state.nrOpUpdates++;

    if (v1.attrs->size() == 0) { v = v2; return; }
    if (v2.attrs->size() == 0) { v = v1; return; }

    state.mkAttrs(v, v1.attrs->size() + v2.attrs->size());

    /* Merge the sets, preferring values from the second set.  Make
       sure to keep the resulting vector in sorted order. */
    Bindings::iterator i = v1.attrs->begin();
    Bindings::iterator j = v2.attrs->begin();

    while (i != v1.attrs->end() && j != v2.attrs->end()) {
        if (i->name == j->name) {
            v.attrs->push_back(*j);
            ++i; ++j;
        }
        else if (i->name < j->name)
            v.attrs->push_back(*i++);
        else
            v.attrs->push_back(*j++);
    }

    while (i != v1.attrs->end()) v.attrs->push_back(*i++);
    while (j != v2.attrs->end()) v.attrs->push_back(*j++);

    state.nrOpUpdateValuesCopied += v.attrs->size();
}


void ExprOpConcatLists::eval(EvalState & state, Env & env, Value & v)
{
    Value v1; e1->eval(state, env, v1);
    Value v2; e2->eval(state, env, v2);
    Value * lists[2] = { &v1, &v2 };
    state.concatLists(v, 2, lists, pos);
}


void EvalState::concatLists(Value & v, size_t nrLists, Value * * lists, const Pos & pos)
{
    nrListConcats++;

    Value * nonEmpty = 0;
    size_t len = 0;
    for (size_t n = 0; n < nrLists; ++n) {
        forceList(*lists[n], pos);
        auto l = lists[n]->listSize();
        len += l;
        if (l) nonEmpty = lists[n];
    }

    if (nonEmpty && len == nonEmpty->listSize()) {
        v = *nonEmpty;
        return;
    }

    mkList(v, len);
    auto out = v.listElems();
    for (size_t n = 0, pos = 0; n < nrLists; ++n) {
        auto l = lists[n]->listSize();
        if (l)
            memcpy(out + pos, lists[n]->listElems(), l * sizeof(Value *));
        pos += l;
    }
}


void ExprConcatStrings::eval(EvalState & state, Env & env, Value & v)
{
    PathSet context;
    std::ostringstream s;
    NixInt n = 0;
    NixFloat nf = 0;

    bool first = !forceString;
    ValueType firstType = tString;

    for (auto & i : *es) {
        Value vTmp;
        i->eval(state, env, vTmp);

        /* If the first element is a path, then the result will also
           be a path, we don't copy anything (yet - that's done later,
           since paths are copied when they are used in a derivation),
           and none of the strings are allowed to have contexts. */
        if (first) {
            firstType = vTmp.type;
            first = false;
        }

        if (firstType == tInt) {
            if (vTmp.type == tInt) {
                n += vTmp.integer;
            } else if (vTmp.type == tFloat) {
                // Upgrade the type from int to float;
                firstType = tFloat;
                nf = n;
                nf += vTmp.fpoint;
            } else
                throwEvalError("cannot add %1% to an integer, at %2%", showType(vTmp), pos);
        } else if (firstType == tFloat) {
            if (vTmp.type == tInt) {
                nf += vTmp.integer;
            } else if (vTmp.type == tFloat) {
                nf += vTmp.fpoint;
            } else
                throwEvalError("cannot add %1% to a float, at %2%", showType(vTmp), pos);
        } else
            s << state.coerceToString(pos, vTmp, context, false, firstType == tString);
    }

    if (firstType == tInt)
        mkInt(v, n);
    else if (firstType == tFloat)
        mkFloat(v, nf);
    else if (firstType == tPath) {
        if (!context.empty())
            throwEvalError("a string that refers to a store path cannot be appended to a path, at %1%", pos);
        auto path = canonPath(s.str());
        mkPath(v, path.c_str());
    } else
        mkString(v, s.str(), context);
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

        if (v.type == tAttrs) {
            for (auto & i : *v.attrs)
                try {
                    recurse(*i.value);
                } catch (Error & e) {
                    addErrorPrefix(e, "while evaluating the attribute '%1%' at %2%:\n", i.name, *i.pos);
                    throw;
                }
        }

        else if (v.isList()) {
            for (size_t n = 0; n < v.listSize(); ++n)
                recurse(*v.listElems()[n]);
        }
    };

    recurse(v);
}


NixInt EvalState::forceInt(Value & v, const Pos & pos)
{
    forceValue(v, pos);
    if (v.type != tInt)
        throwTypeError("value is %1% while an integer was expected, at %2%", v, pos);
    return v.integer;
}


NixFloat EvalState::forceFloat(Value & v, const Pos & pos)
{
    forceValue(v, pos);
    if (v.type == tInt)
        return v.integer;
    else if (v.type != tFloat)
        throwTypeError("value is %1% while a float was expected, at %2%", v, pos);
    return v.fpoint;
}


bool EvalState::forceBool(Value & v, const Pos & pos)
{
    forceValue(v);
    if (v.type != tBool)
        throwTypeError("value is %1% while a Boolean was expected, at %2%", v, pos);
    return v.boolean;
}


bool EvalState::isFunctor(Value & fun)
{
    return fun.type == tAttrs && fun.attrs->find(sFunctor) != fun.attrs->end();
}


void EvalState::forceFunction(Value & v, const Pos & pos)
{
    forceValue(v);
    if (v.type != tLambda && v.type != tPrimOp && v.type != tPrimOpApp && !isFunctor(v))
        throwTypeError("value is %1% while a function was expected, at %2%", v, pos);
}


string EvalState::forceString(Value & v, const Pos & pos)
{
    forceValue(v, pos);
    if (v.type != tString) {
        if (pos)
            throwTypeError("value is %1% while a string was expected, at %2%", v, pos);
        else
            throwTypeError("value is %1% while a string was expected", v);
    }
    return string(v.string.s);
}


void copyContext(const Value & v, PathSet & context)
{
    if (v.string.context)
        for (const char * * p = v.string.context; *p; ++p)
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
    if (v.string.context) {
        if (pos)
            throwEvalError("the string '%1%' is not allowed to refer to a store path (such as '%2%'), at %3%",
                v.string.s, v.string.context[0], pos);
        else
            throwEvalError("the string '%1%' is not allowed to refer to a store path (such as '%2%')",
                v.string.s, v.string.context[0]);
    }
    return s;
}


bool EvalState::isDerivation(Value & v)
{
    if (v.type != tAttrs) return false;
    Bindings::iterator i = v.attrs->find(sType);
    if (i == v.attrs->end()) return false;
    forceValue(*i->value);
    if (i->value->type != tString) return false;
    return strcmp(i->value->string.s, "derivation") == 0;
}


std::optional<string> EvalState::tryAttrsToString(const Pos & pos, Value & v,
    PathSet & context, bool coerceMore, bool copyToStore)
{
    auto i = v.attrs->find(sToString);
    if (i != v.attrs->end()) {
        Value v1;
        callFunction(*i->value, v, v1, pos);
        return coerceToString(pos, v1, context, coerceMore, copyToStore);
    }

    return {};
}

string EvalState::coerceToString(const Pos & pos, Value & v, PathSet & context,
    bool coerceMore, bool copyToStore)
{
    forceValue(v);

    string s;

    if (v.type == tString) {
        copyContext(v, context);
        return v.string.s;
    }

    if (v.type == tPath) {
        Path path(canonPath(v.path));
        return copyToStore ? copyPathToStore(context, path) : path;
    }

    if (v.type == tAttrs) {
        auto maybeString = tryAttrsToString(pos, v, context, coerceMore, copyToStore);
        if (maybeString) {
            return *maybeString;
        }
        auto i = v.attrs->find(sOutPath);
        if (i == v.attrs->end()) throwTypeError("cannot coerce a set to a string, at %1%", pos);
        return coerceToString(pos, *i->value, context, coerceMore, copyToStore);
    }

    if (v.type == tExternal)
        return v.external->coerceToString(pos, context, coerceMore, copyToStore);

    if (coerceMore) {

        /* Note that `false' is represented as an empty string for
           shell scripting convenience, just like `null'. */
        if (v.type == tBool && v.boolean) return "1";
        if (v.type == tBool && !v.boolean) return "";
        if (v.type == tInt) return std::to_string(v.integer);
        if (v.type == tFloat) return std::to_string(v.fpoint);
        if (v.type == tNull) return "";

        if (v.isList()) {
            string result;
            for (size_t n = 0; n < v.listSize(); ++n) {
                result += coerceToString(pos, *v.listElems()[n],
                    context, coerceMore, copyToStore);
                if (n < v.listSize() - 1
                    /* !!! not quite correct */
                    && (!v.listElems()[n]->isList() || v.listElems()[n]->listSize() != 0))
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
        throwEvalError("file names are not allowed to end in '%1%'", drvExtension);

    Path dstPath;
    if (srcToStore[path] != "")
        dstPath = srcToStore[path];
    else {
        dstPath = settings.readOnlyMode
            ? store->computeStorePathForPath(baseNameOf(path), checkSourcePath(path)).first
            : store->addToStore(baseNameOf(path), checkSourcePath(path), true, htSHA256, defaultPathFilter, repair);
        srcToStore[path] = dstPath;
        printMsg(lvlChatty, format("copied source '%1%' -> '%2%'")
            % path % dstPath);
    }

    context.insert(dstPath);
    return dstPath;
}


Path EvalState::coerceToPath(const Pos & pos, Value & v, PathSet & context)
{
    string path = coerceToString(pos, v, context, false, false);
    if (path == "" || path[0] != '/')
        throwEvalError("string '%1%' doesn't represent an absolute path, at %2%", path, pos);
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

    // Special case type-compatibility between float and int
    if (v1.type == tInt && v2.type == tFloat)
        return v1.integer == v2.fpoint;
    if (v1.type == tFloat && v2.type == tInt)
        return v1.fpoint == v2.integer;

    // All other types are not compatible with each other.
    if (v1.type != v2.type) return false;

    switch (v1.type) {

        case tInt:
            return v1.integer == v2.integer;

        case tBool:
            return v1.boolean == v2.boolean;

        case tString:
            return strcmp(v1.string.s, v2.string.s) == 0;

        case tPath:
            return strcmp(v1.path, v2.path) == 0;

        case tNull:
            return true;

        case tList1:
        case tList2:
        case tListN:
            if (v1.listSize() != v2.listSize()) return false;
            for (size_t n = 0; n < v1.listSize(); ++n)
                if (!eqValues(*v1.listElems()[n], *v2.listElems()[n])) return false;
            return true;

        case tAttrs: {
            /* If both sets denote a derivation (type = "derivation"),
               then compare their outPaths. */
            if (isDerivation(v1) && isDerivation(v2)) {
                Bindings::iterator i = v1.attrs->find(sOutPath);
                Bindings::iterator j = v2.attrs->find(sOutPath);
                if (i != v1.attrs->end() && j != v2.attrs->end())
                    return eqValues(*i->value, *j->value);
            }

            if (v1.attrs->size() != v2.attrs->size()) return false;

            /* Otherwise, compare the attributes one by one. */
            Bindings::iterator i, j;
            for (i = v1.attrs->begin(), j = v2.attrs->begin(); i != v1.attrs->end(); ++i, ++j)
                if (i->name != j->name || !eqValues(*i->value, *j->value))
                    return false;

            return true;
        }

        /* Functions are incomparable. */
        case tLambda:
        case tPrimOp:
        case tPrimOpApp:
            return false;

        case tExternal:
            return *v1.external == *v2.external;

        case tFloat:
            return v1.fpoint == v2.fpoint;

        default:
            throwEvalError("cannot compare %1% with %2%", showType(v1), showType(v2));
    }
}

void EvalState::printStats()
{
    bool showStats = getEnv("NIX_SHOW_STATS", "0") != "0";

    struct rusage buf;
    getrusage(RUSAGE_SELF, &buf);
    float cpuTime = buf.ru_utime.tv_sec + ((float) buf.ru_utime.tv_usec / 1000000);

    uint64_t bEnvs = nrEnvs * sizeof(Env) + nrValuesInEnvs * sizeof(Value *);
    uint64_t bLists = nrListElems * sizeof(Value *);
    uint64_t bValues = nrValues * sizeof(Value);
    uint64_t bAttrsets = nrAttrsets * sizeof(Bindings) + nrAttrsInAttrsets * sizeof(Attr);

#if HAVE_BOEHMGC
    GC_word heapSize, totalBytes;
    GC_get_heap_usage_safe(&heapSize, 0, 0, 0, &totalBytes);
#endif
    if (showStats) {
        auto outPath = getEnv("NIX_SHOW_STATS_PATH","-");
        std::fstream fs;
        if (outPath != "-")
            fs.open(outPath, std::fstream::out);
        JSONObject topObj(outPath == "-" ? std::cerr : fs, true);
        topObj.attr("cpuTime",cpuTime);
        {
            auto envs = topObj.object("envs");
            envs.attr("number", nrEnvs);
            envs.attr("elements", nrValuesInEnvs);
            envs.attr("bytes", bEnvs);
        }
        {
            auto lists = topObj.object("list");
            lists.attr("elements", nrListElems);
            lists.attr("bytes", bLists);
            lists.attr("concats", nrListConcats);
        }
        {
            auto values = topObj.object("values");
            values.attr("number", nrValues);
            values.attr("bytes", bValues);
        }
        {
            auto syms = topObj.object("symbols");
            syms.attr("number", symbols.size());
            syms.attr("bytes", symbols.totalSize());
        }
        {
            auto sets = topObj.object("sets");
            sets.attr("number", nrAttrsets);
            sets.attr("bytes", bAttrsets);
            sets.attr("elements", nrAttrsInAttrsets);
        }
        {
            auto sizes = topObj.object("sizes");
            sizes.attr("Env", sizeof(Env));
            sizes.attr("Value", sizeof(Value));
            sizes.attr("Bindings", sizeof(Bindings));
            sizes.attr("Attr", sizeof(Attr));
        }
        topObj.attr("nrOpUpdates", nrOpUpdates);
        topObj.attr("nrOpUpdateValuesCopied", nrOpUpdateValuesCopied);
        topObj.attr("nrThunks", nrThunks);
        topObj.attr("nrAvoided", nrAvoided);
        topObj.attr("nrLookups", nrLookups);
        topObj.attr("nrPrimOpCalls", nrPrimOpCalls);
        topObj.attr("nrFunctionCalls", nrFunctionCalls);
#if HAVE_BOEHMGC
        {
            auto gc = topObj.object("gc");
            gc.attr("heapSize", heapSize);
            gc.attr("totalBytes", totalBytes);
        }
#endif

        if (countCalls) {
            {
                auto obj = topObj.object("primops");
                for (auto & i : primOpCalls)
                    obj.attr(i.first, i.second);
            }
            {
                auto list = topObj.list("functions");
                for (auto & i : functionCalls) {
                    auto obj = list.object();
                    if (i.first->name.set())
                        obj.attr("name", (const string &) i.first->name);
                    else
                        obj.attr("name", nullptr);
                    if (i.first->pos) {
                        obj.attr("file", (const string &) i.first->pos.file);
                        obj.attr("line", i.first->pos.line);
                        obj.attr("column", i.first->pos.column);
                    }
                    obj.attr("count", i.second);
                }
            }
            {
                auto list = topObj.list("attributes");
                for (auto & i : attrSelects) {
                    auto obj = list.object();
                    if (i.first) {
                        obj.attr("file", (const string &) i.first.file);
                        obj.attr("line", i.first.line);
                        obj.attr("column", i.first.column);
                    }
                    obj.attr("count", i.second);
                }
            }
        }

        if (getEnv("NIX_SHOW_SYMBOLS", "0") != "0") {
            auto list = topObj.list("symbols");
            symbols.dump([&](const std::string & s) { list.elem(s); });
        }
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

        switch (v.type) {
        case tString:
            sz += doString(v.string.s);
            if (v.string.context)
                for (const char * * p = v.string.context; *p; ++p)
                    sz += doString(*p);
            break;
        case tPath:
            sz += doString(v.path);
            break;
        case tAttrs:
            if (seen.find(v.attrs) == seen.end()) {
                seen.insert(v.attrs);
                sz += sizeof(Bindings) + sizeof(Attr) * v.attrs->capacity();
                for (auto & i : *v.attrs)
                    sz += doValue(*i.value);
            }
            break;
        case tList1:
        case tList2:
        case tListN:
            if (seen.find(v.listElems()) == seen.end()) {
                seen.insert(v.listElems());
                sz += v.listSize() * sizeof(Value *);
                for (size_t n = 0; n < v.listSize(); ++n)
                    sz += doValue(*v.listElems()[n]);
            }
            break;
        case tThunk:
            sz += doEnv(*v.thunk.env);
            break;
        case tApp:
            sz += doValue(*v.app.left);
            sz += doValue(*v.app.right);
            break;
        case tLambda:
            sz += doEnv(*v.lambda.env);
            break;
        case tPrimOpApp:
            sz += doValue(*v.primOpApp.left);
            sz += doValue(*v.primOpApp.right);
            break;
        case tExternal:
            if (seen.find(v.external) != seen.end()) break;
            seen.insert(v.external);
            sz += v.external->valueSize(seen);
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

        if (env.type != Env::HasWithExpr)
            for (size_t i = 0; i < env.size; ++i)
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


EvalSettings evalSettings;

static GlobalConfig::Register r1(&evalSettings);


}
