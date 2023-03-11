#include "eval.hh"
#include "hash.hh"
#include "types.hh"
#include "util.hh"
#include "store-api.hh"
#include "derivations.hh"
#include "globals.hh"
#include "eval-inline.hh"
#include "filetransfer.hh"
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

#define GC_INCLUDE_NEW

#include <gc/gc.h>
#include <gc/gc_cpp.h>

#include <boost/coroutine2/coroutine.hpp>
#include <boost/coroutine2/protected_fixedsize_stack.hpp>
#include <boost/context/stack_context.hpp>

#endif

namespace nix {


static char * allocString(size_t size)
{
    char * t;
#if HAVE_BOEHMGC
    t = (char *) GC_MALLOC_ATOMIC(size);
#else
    t = malloc(size);
#endif
    if (!t) throw std::bad_alloc();
    return t;
}


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


// When there's no need to write to the string, we can optimize away empty
// string allocations.
// This function handles makeImmutableStringWithLen(null, 0) by returning the
// empty string.
static const char * makeImmutableStringWithLen(const char * s, size_t size)
{
    char * t;
    if (size == 0)
        return "";
#if HAVE_BOEHMGC
    t = GC_STRNDUP(s, size);
#else
    t = strndup(s, size);
#endif
    if (!t) throw std::bad_alloc();
    return t;
}

static inline const char * makeImmutableString(std::string_view s) {
    return makeImmutableStringWithLen(s.data(), s.size());
}


RootValue allocRootValue(Value * v)
{
#if HAVE_BOEHMGC
    return std::allocate_shared<Value *>(traceable_allocator<Value *>(), v);
#else
    return std::make_shared<Value *>(v);
#endif
}


void Value::print(std::ostream & str, std::set<const void *> * seen) const
{
    checkInterrupt();

    switch (internalType) {
    case tInt:
        str << integer;
        break;
    case tBool:
        str << (boolean ? "true" : "false");
        break;
    case tString:
        str << "\"";
        for (const char * i = string.s; *i; i++)
            if (*i == '\"' || *i == '\\') str << "\\" << *i;
            else if (*i == '\n') str << "\\n";
            else if (*i == '\r') str << "\\r";
            else if (*i == '\t') str << "\\t";
            else if (*i == '$' && *(i+1) == '{') str << "\\" << *i;
            else str << *i;
        str << "\"";
        break;
    case tPath:
        str << path; // !!! escaping?
        break;
    case tNull:
        str << "null";
        break;
    case tAttrs: {
        if (seen && !attrs->empty() && !seen->insert(attrs).second)
            str << "«repeated»";
        else {
            str << "{ ";
            for (auto & i : attrs->lexicographicOrder()) {
                str << i->name << " = ";
                i->value->print(str, seen);
                str << "; ";
            }
            str << "}";
        }
        break;
    }
    case tList1:
    case tList2:
    case tListN:
        if (seen && listSize() && !seen->insert(listElems()).second)
            str << "«repeated»";
        else {
            str << "[ ";
            for (auto v2 : listItems()) {
                v2->print(str, seen);
                str << " ";
            }
            str << "]";
        }
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
        str << *external;
        break;
    case tFloat:
        str << fpoint;
        break;
    default:
        abort();
    }
}


void Value::print(std::ostream & str, bool showRepeated) const
{
    std::set<const void *> seen;
    print(str, showRepeated ? nullptr : &seen);
}


std::ostream & operator << (std::ostream & str, const Value & v)
{
    v.print(str, false);
    return str;
}


const Value * getPrimOp(const Value &v) {
    const Value * primOp = &v;
    while (primOp->isPrimOpApp()) {
        primOp = primOp->primOpApp.left;
    }
    assert(primOp->isPrimOp());
    return primOp;
}

std::string_view showType(ValueType type)
{
    switch (type) {
        case nInt: return "an integer";
        case nBool: return "a Boolean";
        case nString: return "a string";
        case nPath: return "a path";
        case nNull: return "null";
        case nAttrs: return "a set";
        case nList: return "a list";
        case nFunction: return "a function";
        case nExternal: return "an external value";
        case nFloat: return "a float";
        case nThunk: return "a thunk";
    }
    abort();
}


std::string showType(const Value & v)
{
    switch (v.internalType) {
        case tString: return v.string.context ? "a string with context" : "a string";
        case tPrimOp:
            return fmt("the built-in function '%s'", std::string(v.primOp->name));
        case tPrimOpApp:
            return fmt("the partially applied built-in function '%s'", std::string(getPrimOp(v)->primOp->name));
        case tExternal: return v.external->showType();
        case tThunk: return "a thunk";
        case tApp: return "a function application";
        case tBlackhole: return "a black hole";
    default:
        return std::string(showType(v.type()));
    }
}

Pos Value::determinePos(const Pos & pos) const
{
    switch (internalType) {
        case tAttrs: return *attrs->pos;
        case tLambda: return lambda.fun->pos;
        case tApp: return app.left->determinePos(pos);
        default: return pos;
    }
}

bool Value::isTrivial() const
{
    return
        internalType != tApp
        && internalType != tPrimOpApp
        && (internalType != tThunk
            || (dynamic_cast<ExprAttrs *>(thunk.expr)
                && ((ExprAttrs *) thunk.expr)->dynamicAttrs.empty())
            || dynamic_cast<ExprLambda *>(thunk.expr)
            || dynamic_cast<ExprList *>(thunk.expr));
}


#if HAVE_BOEHMGC
/* Called when the Boehm GC runs out of memory. */
static void * oomHandler(size_t requested)
{
    /* Convert this to a proper C++ exception. */
    throw std::bad_alloc();
}

class BoehmGCStackAllocator : public StackAllocator {
    boost::coroutines2::protected_fixedsize_stack stack {
        // We allocate 8 MB, the default max stack size on NixOS.
        // A smaller stack might be quicker to allocate but reduces the stack
        // depth available for source filter expressions etc.
        std::max(boost::context::stack_traits::default_size(), static_cast<std::size_t>(8 * 1024 * 1024))
    };

    // This is specific to boost::coroutines2::protected_fixedsize_stack.
    // The stack protection page is included in sctx.size, so we have to
    // subtract one page size from the stack size.
    std::size_t pfss_usable_stack_size(boost::context::stack_context &sctx) {
        return sctx.size - boost::context::stack_traits::page_size();
    }

  public:
    boost::context::stack_context allocate() override {
        auto sctx = stack.allocate();

        // Stacks generally start at a high address and grow to lower addresses.
        // Architectures that do the opposite are rare; in fact so rare that
        // boost_routine does not implement it.
        // So we subtract the stack size.
        GC_add_roots(static_cast<char *>(sctx.sp) - pfss_usable_stack_size(sctx), sctx.sp);
        return sctx;
    }

    void deallocate(boost::context::stack_context sctx) override {
        GC_remove_roots(static_cast<char *>(sctx.sp) - pfss_usable_stack_size(sctx), sctx.sp);
        stack.deallocate(sctx);
    }

};

static BoehmGCStackAllocator boehmGCStackAllocator;

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

    StackAllocator::defaultAllocator = &boehmGCStackAllocator;

    /* Set the initial heap size to something fairly big (25% of
       physical RAM, up to a maximum of 384 MiB) so that in most cases
       we don't need to garbage collect at all.  (Collection has a
       fairly significant overhead.)  The heap size can be overridden
       through libgc's GC_INITIAL_HEAP_SIZE environment variable.  We
       should probably also provide a nix.conf setting for this.  Note
       that GC_expand_hp() causes a lot of virtual, but not physical
       (resident) memory to be allocated.  This might be a problem on
       systems that don't overcommit. */
    if (!getEnv("GC_INITIAL_HEAP_SIZE")) {
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
static Strings parseNixPath(const std::string & s)
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


EvalState::EvalState(
    const Strings & _searchPath,
    ref<Store> store,
    std::shared_ptr<Store> buildStore)
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
    , sContentAddressed(symbols.create("__contentAddressed"))
    , sImpure(symbols.create("__impure"))
    , sOutputHash(symbols.create("outputHash"))
    , sOutputHashAlgo(symbols.create("outputHashAlgo"))
    , sOutputHashMode(symbols.create("outputHashMode"))
    , sRecurseForDerivations(symbols.create("recurseForDerivations"))
    , sDescription(symbols.create("description"))
    , sSelf(symbols.create("self"))
    , sEpsilon(symbols.create(""))
    , sStartSet(symbols.create("startSet"))
    , sOperator(symbols.create("operator"))
    , sKey(symbols.create("key"))
    , sPath(symbols.create("path"))
    , sPrefix(symbols.create("prefix"))
    , repair(NoRepair)
    , emptyBindings(0)
    , store(store)
    , buildStore(buildStore ? buildStore : store)
    , regexCache(makeRegexCache())
#if HAVE_BOEHMGC
    , valueAllocCache(std::allocate_shared<void *>(traceable_allocator<void *>(), nullptr))
    , env1AllocCache(std::allocate_shared<void *>(traceable_allocator<void *>(), nullptr))
#else
    , valueAllocCache(std::make_shared<void *>(nullptr))
    , env1AllocCache(std::make_shared<void *>(nullptr))
#endif
    , baseEnv(allocEnv(128))
    , staticBaseEnv(false, 0)
{
    countCalls = getEnv("NIX_COUNT_CALLS").value_or("0") != "0";

    assert(gcInitialised);

    static_assert(sizeof(Env) <= 16, "environment must be <= 16 bytes");

    /* Initialise the Nix expression search path. */
    if (!evalSettings.pureEval) {
        for (auto & i : _searchPath) addToSearchPath(i);
        for (auto & i : evalSettings.nixPath.get()) addToSearchPath(i);
    }

    if (evalSettings.restrictEval || evalSettings.pureEval) {
        allowedPaths = PathSet();

        for (auto & i : searchPath) {
            auto r = resolveSearchPathElem(i);
            if (!r.first) continue;

            auto path = r.second;

            if (store->isInStore(r.second)) {
                try {
                    StorePathSet closure;
                    store->computeFSClosure(store->toStorePath(r.second).first, closure);
                    for (auto & path : closure)
                        allowPath(path);
                } catch (InvalidPath &) {
                    allowPath(r.second);
                }
            } else
                allowPath(r.second);
        }
    }

    createBaseEnv();
}


EvalState::~EvalState()
{
}


void EvalState::allowPath(const Path & path)
{
    if (allowedPaths)
        allowedPaths->insert(path);
}

void EvalState::allowPath(const StorePath & storePath)
{
    if (allowedPaths)
        allowedPaths->insert(store->toRealPath(storePath));
}

void EvalState::allowAndSetStorePathString(const StorePath &storePath, Value & v)
{
    allowPath(storePath);

    auto path = store->printStorePath(storePath);
    v.mkString(path, PathSet({path}));
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

    if (hasPrefix(abspath, corepkgsPrefix)) return abspath;

    for (auto & i : *allowedPaths) {
        if (isDirOrInDir(abspath, i)) {
            found = true;
            break;
        }
    }

    if (!found) {
        auto modeInformation = evalSettings.pureEval
            ? "in pure eval mode (use '--impure' to override)"
            : "in restricted mode";
        throw RestrictedPathError("access to absolute path '%1%' is forbidden %2%", abspath, modeInformation);
    }

    /* Resolve symlinks. */
    debug(format("checking access to '%s'") % abspath);
    Path path = canonPath(abspath, true);

    for (auto & i : *allowedPaths) {
        if (isDirOrInDir(path, i)) {
            resolvedPaths[path_] = path;
            return path;
        }
    }

    throw RestrictedPathError("access to canonical path '%1%' is forbidden in restricted mode", path);
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
}


Value * EvalState::addConstant(const std::string & name, Value & v)
{
    Value * v2 = allocValue();
    *v2 = v;
    addConstant(name, v2);
    return v2;
}


void EvalState::addConstant(const std::string & name, Value * v)
{
    staticBaseEnv.vars.emplace_back(symbols.create(name), baseEnvDispl);
    baseEnv.values[baseEnvDispl++] = v;
    auto name2 = name.substr(0, 2) == "__" ? name.substr(2) : name;
    baseEnv.values[0]->attrs->push_back(Attr(symbols.create(name2), v));
}


Value * EvalState::addPrimOp(const std::string & name,
    size_t arity, PrimOpFun primOp)
{
    auto name2 = name.substr(0, 2) == "__" ? name.substr(2) : name;
    Symbol sym = symbols.create(name2);

    /* Hack to make constants lazy: turn them into a application of
       the primop to a dummy value. */
    if (arity == 0) {
        auto vPrimOp = allocValue();
        vPrimOp->mkPrimOp(new PrimOp { .fun = primOp, .arity = 1, .name = sym });
        Value v;
        v.mkApp(vPrimOp, vPrimOp);
        return addConstant(name, v);
    }

    Value * v = allocValue();
    v->mkPrimOp(new PrimOp { .fun = primOp, .arity = arity, .name = sym });
    staticBaseEnv.vars.emplace_back(symbols.create(name), baseEnvDispl);
    baseEnv.values[baseEnvDispl++] = v;
    baseEnv.values[0]->attrs->push_back(Attr(sym, v));
    return v;
}


Value * EvalState::addPrimOp(PrimOp && primOp)
{
    /* Hack to make constants lazy: turn them into a application of
       the primop to a dummy value. */
    if (primOp.arity == 0) {
        primOp.arity = 1;
        auto vPrimOp = allocValue();
        vPrimOp->mkPrimOp(new PrimOp(std::move(primOp)));
        Value v;
        v.mkApp(vPrimOp, vPrimOp);
        return addConstant(primOp.name, v);
    }

    Symbol envName = primOp.name;
    if (hasPrefix(primOp.name, "__"))
        primOp.name = symbols.create(std::string(primOp.name, 2));

    Value * v = allocValue();
    v->mkPrimOp(new PrimOp(std::move(primOp)));
    staticBaseEnv.vars.emplace_back(envName, baseEnvDispl);
    baseEnv.values[baseEnvDispl++] = v;
    baseEnv.values[0]->attrs->push_back(Attr(primOp.name, v));
    return v;
}


Value & EvalState::getBuiltin(const std::string & name)
{
    return *baseEnv.values[0]->attrs->find(symbols.create(name))->value;
}


std::optional<EvalState::Doc> EvalState::getDoc(Value & v)
{
    if (v.isPrimOp()) {
        auto v2 = &v;
        if (v2->primOp->doc)
            return Doc {
                .pos = noPos,
                .name = v2->primOp->name,
                .arity = v2->primOp->arity,
                .args = v2->primOp->args,
                .doc = v2->primOp->doc,
            };
    }
    return {};
}


/* Every "format" object (even temporary) takes up a few hundred bytes
   of stack space, which is a real killer in the recursive
   evaluator.  So here are some helper functions for throwing
   exceptions. */

LocalNoInlineNoReturn(void throwEvalError(const char * s, const std::string & s2))
{
    throw EvalError(s, s2);
}

LocalNoInlineNoReturn(void throwEvalError(const Pos & pos, const Suggestions & suggestions, const char * s, const std::string & s2))
{
    throw EvalError(ErrorInfo {
        .msg = hintfmt(s, s2),
        .errPos = pos,
        .suggestions = suggestions,
    });
}

LocalNoInlineNoReturn(void throwEvalError(const Pos & pos, const char * s, const std::string & s2))
{
    throw EvalError(ErrorInfo {
        .msg = hintfmt(s, s2),
        .errPos = pos
    });
}

LocalNoInlineNoReturn(void throwEvalError(const char * s, const std::string & s2, const std::string & s3))
{
    throw EvalError(s, s2, s3);
}

LocalNoInlineNoReturn(void throwEvalError(const Pos & pos, const char * s, const std::string & s2, const std::string & s3))
{
    throw EvalError({
        .msg = hintfmt(s, s2, s3),
        .errPos = pos
    });
}

LocalNoInlineNoReturn(void throwEvalError(const Pos & p1, const char * s, const Symbol & sym, const Pos & p2))
{
    // p1 is where the error occurred; p2 is a position mentioned in the message.
    throw EvalError({
        .msg = hintfmt(s, sym, p2),
        .errPos = p1
    });
}

LocalNoInlineNoReturn(void throwTypeError(const Pos & pos, const char * s))
{
    throw TypeError({
        .msg = hintfmt(s),
        .errPos = pos
    });
}

LocalNoInlineNoReturn(void throwTypeError(const Pos & pos, const char * s, const ExprLambda & fun, const Symbol & s2))
{
    throw TypeError({
        .msg = hintfmt(s, fun.showNamePos(), s2),
        .errPos = pos
    });
}

LocalNoInlineNoReturn(void throwTypeError(const Pos & pos, const Suggestions & suggestions, const char * s, const ExprLambda & fun, const Symbol & s2))
{
    throw TypeError(ErrorInfo {
        .msg = hintfmt(s, fun.showNamePos(), s2),
        .errPos = pos,
        .suggestions = suggestions,
    });
}


LocalNoInlineNoReturn(void throwTypeError(const char * s, const Value & v))
{
    throw TypeError(s, showType(v));
}

LocalNoInlineNoReturn(void throwAssertionError(const Pos & pos, const char * s, const std::string & s1))
{
    throw AssertionError({
        .msg = hintfmt(s, s1),
        .errPos = pos
    });
}

LocalNoInlineNoReturn(void throwUndefinedVarError(const Pos & pos, const char * s, const std::string & s1))
{
    throw UndefinedVarError({
        .msg = hintfmt(s, s1),
        .errPos = pos
    });
}

LocalNoInlineNoReturn(void throwMissingArgumentError(const Pos & pos, const char * s, const std::string & s1))
{
    throw MissingArgumentError({
        .msg = hintfmt(s, s1),
        .errPos = pos
    });
}

LocalNoInline(void addErrorTrace(Error & e, const char * s, const std::string & s2))
{
    e.addTrace(std::nullopt, s, s2);
}

LocalNoInline(void addErrorTrace(Error & e, const Pos & pos, const char * s, const std::string & s2))
{
    e.addTrace(pos, s, s2);
}


void Value::mkString(std::string_view s)
{
    mkString(makeImmutableString(s));
}


static void copyContextToValue(Value & v, const PathSet & context)
{
    if (!context.empty()) {
        size_t n = 0;
        v.string.context = (const char * *)
            allocBytes((context.size() + 1) * sizeof(char *));
        for (auto & i : context)
            v.string.context[n++] = dupString(i.c_str());
        v.string.context[n] = 0;
    }
}

void Value::mkString(std::string_view s, const PathSet & context)
{
    mkString(s);
    copyContextToValue(*this, context);
}

void Value::mkStringMove(const char * s, const PathSet & context)
{
    mkString(s);
    copyContextToValue(*this, context);
}


void Value::mkPath(std::string_view s)
{
    mkPath(makeImmutableString(s));
}


inline Value * EvalState::lookupVar(Env * env, const ExprVar & var, bool noEval)
{
    for (auto l = var.level; l; --l, env = env->up) ;

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
            if (countCalls) attrSelects[*j->pos]++;
            return j->value;
        }
        if (!env->prevWith)
            throwUndefinedVarError(var.pos, "undefined variable '%1%'", var.name);
        for (size_t l = env->prevWith; l; --l, env = env->up) ;
    }
}


void EvalState::mkList(Value & v, size_t size)
{
    v.mkList(size);
    if (size > 2)
        v.bigList.elems = (Value * *) allocBytes(size * sizeof(Value *));
    nrListElems += size;
}


unsigned long nrThunks = 0;

static inline void mkThunk(Value & v, Env & env, Expr * expr)
{
    v.mkThunk(&env, expr);
    nrThunks++;
}


void EvalState::mkThunk_(Value & v, Expr * expr)
{
    mkThunk(v, baseEnv, expr);
}


void EvalState::mkPos(Value & v, ptr<Pos> pos)
{
    if (pos->file.set()) {
        auto attrs = buildBindings(3);
        attrs.alloc(sFile).mkString(pos->file);
        attrs.alloc(sLine).mkInt(pos->line);
        attrs.alloc(sColumn).mkInt(pos->column);
        v.mkAttrs(attrs);
    } else
        v.mkNull();
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


Value * ExprVar::maybeThunk(EvalState & state, Env & env)
{
    Value * v = state.lookupVar(&env, *this, true);
    /* The value might not be initialised in the environment yet.
       In that case, ignore it. */
    if (v) { state.nrAvoided++; return v; }
    return Expr::maybeThunk(state, env);
}


Value * ExprString::maybeThunk(EvalState & state, Env & env)
{
    state.nrAvoided++;
    return &v;
}

Value * ExprInt::maybeThunk(EvalState & state, Env & env)
{
    state.nrAvoided++;
    return &v;
}

Value * ExprFloat::maybeThunk(EvalState & state, Env & env)
{
    state.nrAvoided++;
    return &v;
}

Value * ExprPath::maybeThunk(EvalState & state, Env & env)
{
    state.nrAvoided++;
    return &v;
}


void EvalState::evalFile(const Path & path_, Value & v, bool mustBeTrivial)
{
    auto path = checkSourcePath(path_);

    FileEvalCache::iterator i;
    if ((i = fileEvalCache.find(path)) != fileEvalCache.end()) {
        v = i->second;
        return;
    }

    Path resolvedPath = resolveExprPath(path);
    if ((i = fileEvalCache.find(resolvedPath)) != fileEvalCache.end()) {
        v = i->second;
        return;
    }

    printTalkative("evaluating file '%1%'", resolvedPath);
    Expr * e = nullptr;

    auto j = fileParseCache.find(resolvedPath);
    if (j != fileParseCache.end())
        e = j->second;

    if (!e)
        e = parseExprFromFile(checkSourcePath(resolvedPath));

    cacheFile(path, resolvedPath, e, v, mustBeTrivial);
}


void EvalState::resetFileCache()
{
    fileEvalCache.clear();
    fileParseCache.clear();
}


void EvalState::cacheFile(
    const Path & path,
    const Path & resolvedPath,
    Expr * e,
    Value & v,
    bool mustBeTrivial)
{
    fileParseCache[resolvedPath] = e;

    try {
        // Enforce that 'flake.nix' is a direct attrset, not a
        // computation.
        if (mustBeTrivial &&
            !(dynamic_cast<ExprAttrs *>(e)))
            throw EvalError("file '%s' must be an attribute set", path);
        eval(e, v);
    } catch (Error & e) {
        addErrorTrace(e, "while evaluating the file '%1%':", resolvedPath);
        throw;
    }

    fileEvalCache[resolvedPath] = v;
    if (path != resolvedPath) fileEvalCache[path] = v;
}


void EvalState::eval(Expr * e, Value & v)
{
    e->eval(*this, baseEnv, v);
}


inline bool EvalState::evalBool(Env & env, Expr * e)
{
    Value v;
    e->eval(*this, env, v);
    if (v.type() != nBool)
        throwTypeError("value is %1% while a Boolean was expected", v);
    return v.boolean;
}


inline bool EvalState::evalBool(Env & env, Expr * e, const Pos & pos)
{
    Value v;
    e->eval(*this, env, v);
    if (v.type() != nBool)
        throwTypeError(pos, "value is %1% while a Boolean was expected", v);
    return v.boolean;
}


inline void EvalState::evalAttrs(Env & env, Expr * e, Value & v)
{
    e->eval(*this, env, v);
    if (v.type() != nAttrs)
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
    v.mkAttrs(state.buildBindings(attrs.size() + dynamicAttrs.size()).finish());
    auto dynamicEnv = &env;

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
        Displacement displ = 0;
        for (auto & i : attrs) {
            Value * vAttr;
            if (hasOverrides && !i.second.inherited) {
                vAttr = state.allocValue();
                mkThunk(*vAttr, env2, i.second.e);
            } else
                vAttr = i.second.e->maybeThunk(state, i.second.inherited ? env : env2);
            env2.values[displ++] = vAttr;
            v.attrs->push_back(Attr(i.first, vAttr, ptr(&i.second.pos)));
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
            state.forceAttrs(*vOverrides, [&]() { return vOverrides->determinePos(noPos); });
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
            v.attrs->push_back(Attr(i.first, i.second.e->maybeThunk(state, env), ptr(&i.second.pos)));

    /* Dynamic attrs apply *after* rec and __overrides. */
    for (auto & i : dynamicAttrs) {
        Value nameVal;
        i.nameExpr->eval(state, *dynamicEnv, nameVal);
        state.forceValue(nameVal, i.pos);
        if (nameVal.type() == nNull)
            continue;
        state.forceStringNoCtx(nameVal);
        Symbol nameSym = state.symbols.create(nameVal.string.s);
        Bindings::iterator j = v.attrs->find(nameSym);
        if (j != v.attrs->end())
            throwEvalError(i.pos, "dynamic attribute '%1%' already defined at %2%", nameSym, *j->pos);

        i.valueExpr->setName(nameSym);
        /* Keep sorted order so find can catch duplicates */
        v.attrs->push_back(Attr(nameSym, i.valueExpr->maybeThunk(state, *dynamicEnv), ptr(&i.pos)));
        v.attrs->sort(); // FIXME: inefficient
    }

    v.attrs->pos = ptr(&pos);
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
    Displacement displ = 0;
    for (auto & i : attrs->attrs)
        env2.values[displ++] = i.second.e->maybeThunk(state, i.second.inherited ? env : env2);

    body->eval(state, env2, v);
}


void ExprList::eval(EvalState & state, Env & env, Value & v)
{
    state.mkList(v, elems.size());
    for (auto [n, v2] : enumerate(v.listItems()))
        const_cast<Value * &>(v2) = elems[n]->maybeThunk(state, env);
}


void ExprVar::eval(EvalState & state, Env & env, Value & v)
{
    Value * v2 = state.lookupVar(&env, *this, false);
    state.forceValue(*v2, pos);
    v = *v2;
}


static std::string showAttrPath(EvalState & state, Env & env, const AttrPath & attrPath)
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


void ExprSelect::eval(EvalState & state, Env & env, Value & v)
{
    Value vTmp;
    ptr<Pos> pos2(&noPos);
    Value * vAttrs = &vTmp;

    e->eval(state, env, vTmp);

    try {

        for (auto & i : attrPath) {
            state.nrLookups++;
            Bindings::iterator j;
            Symbol name = getName(i, state, env);
            if (def) {
                state.forceValue(*vAttrs, pos);
                if (vAttrs->type() != nAttrs ||
                    (j = vAttrs->attrs->find(name)) == vAttrs->attrs->end())
                {
                    def->eval(state, env, v);
                    return;
                }
            } else {
                state.forceAttrs(*vAttrs, pos);
                if ((j = vAttrs->attrs->find(name)) == vAttrs->attrs->end()) {
                    std::set<std::string> allAttrNames;
                    for (auto & attr : *vAttrs->attrs)
                        allAttrNames.insert(attr.name);
                    throwEvalError(
                        pos,
                        Suggestions::bestMatches(allAttrNames, name),
                        "attribute '%1%' missing", name);
                }
            }
            vAttrs = j->value;
            pos2 = j->pos;
            if (state.countCalls) state.attrSelects[*pos2]++;
        }

        state.forceValue(*vAttrs, (*pos2 != noPos ? *pos2 : this->pos ) );

    } catch (Error & e) {
        if (*pos2 != noPos && pos2->file != state.sDerivationNix)
            addErrorTrace(e, *pos2, "while evaluating the attribute '%1%'",
                showAttrPath(state, env, attrPath));
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
        state.forceValue(*vAttrs, noPos);
        Bindings::iterator j;
        Symbol name = getName(i, state, env);
        if (vAttrs->type() != nAttrs ||
            (j = vAttrs->attrs->find(name)) == vAttrs->attrs->end())
        {
            v.mkBool(false);
            return;
        } else {
            vAttrs = j->value;
        }
    }

    v.mkBool(true);
}


void ExprLambda::eval(EvalState & state, Env & env, Value & v)
{
    v.mkLambda(&env, this);
}


void EvalState::callFunction(Value & fun, size_t nrArgs, Value * * args, Value & vRes, const Pos & pos)
{
    auto trace = evalSettings.traceFunctionCalls ? std::make_unique<FunctionCallTrace>(pos) : nullptr;

    forceValue(fun, pos);

    Value vCur(fun);

    auto makeAppChain = [&]()
    {
        vRes = vCur;
        for (size_t i = 0; i < nrArgs; ++i) {
            auto fun2 = allocValue();
            *fun2 = vRes;
            vRes.mkPrimOpApp(fun2, args[i]);
        }
    };

    Attr * functor;

    while (nrArgs > 0) {

        if (vCur.isLambda()) {

            ExprLambda & lambda(*vCur.lambda.fun);

            auto size =
                (lambda.arg.empty() ? 0 : 1) +
                (lambda.hasFormals() ? lambda.formals->formals.size() : 0);
            Env & env2(allocEnv(size));
            env2.up = vCur.lambda.env;

            Displacement displ = 0;

            if (!lambda.hasFormals())
                env2.values[displ++] = args[0];

            else {
                forceAttrs(*args[0], pos);

                if (!lambda.arg.empty())
                    env2.values[displ++] = args[0];

                /* For each formal argument, get the actual argument.  If
                   there is no matching actual argument but the formal
                   argument has a default, use the default. */
                size_t attrsUsed = 0;
                for (auto & i : lambda.formals->formals) {
                    auto j = args[0]->attrs->get(i.name);
                    if (!j) {
                        if (!i.def) throwTypeError(pos, "%1% called without required argument '%2%'",
                            lambda, i.name);
                        env2.values[displ++] = i.def->maybeThunk(*this, env2);
                    } else {
                        attrsUsed++;
                        env2.values[displ++] = j->value;
                    }
                }

                /* Check that each actual argument is listed as a formal
                   argument (unless the attribute match specifies a `...'). */
                if (!lambda.formals->ellipsis && attrsUsed != args[0]->attrs->size()) {
                    /* Nope, so show the first unexpected argument to the
                       user. */
                    for (auto & i : *args[0]->attrs)
                        if (!lambda.formals->has(i.name)) {
                            std::set<std::string> formalNames;
                            for (auto & formal : lambda.formals->formals)
                                formalNames.insert(formal.name);
                            throwTypeError(
                                pos,
                                Suggestions::bestMatches(formalNames, i.name),
                                "%1% called with unexpected argument '%2%'",
                                lambda,
                                i.name);
                        }
                    abort(); // can't happen
                }
            }

            nrFunctionCalls++;
            if (countCalls) incrFunctionCall(&lambda);

            /* Evaluate the body. */
            try {
                lambda.body->eval(*this, env2, vCur);
            } catch (Error & e) {
                if (loggerSettings.showTrace.get()) {
                    addErrorTrace(e, lambda.pos, "while evaluating %s",
                        (lambda.name.set()
                            ? "'" + (const std::string &) lambda.name + "'"
                            : "anonymous lambda"));
                    addErrorTrace(e, pos, "from call site%s", "");
                }
                throw;
            }

            nrArgs--;
            args += 1;
        }

        else if (vCur.isPrimOp()) {

            size_t argsLeft = vCur.primOp->arity;

            if (nrArgs < argsLeft) {
                /* We don't have enough arguments, so create a tPrimOpApp chain. */
                makeAppChain();
                return;
            } else {
                /* We have all the arguments, so call the primop. */
                nrPrimOpCalls++;
                if (countCalls) primOpCalls[vCur.primOp->name]++;
                vCur.primOp->fun(*this, pos, args, vCur);

                nrArgs -= argsLeft;
                args += argsLeft;
            }
        }

        else if (vCur.isPrimOpApp()) {
            /* Figure out the number of arguments still needed. */
            size_t argsDone = 0;
            Value * primOp = &vCur;
            while (primOp->isPrimOpApp()) {
                argsDone++;
                primOp = primOp->primOpApp.left;
            }
            assert(primOp->isPrimOp());
            auto arity = primOp->primOp->arity;
            auto argsLeft = arity - argsDone;

            if (nrArgs < argsLeft) {
                /* We still don't have enough arguments, so extend the tPrimOpApp chain. */
                makeAppChain();
                return;
            } else {
                /* We have all the arguments, so call the primop with
                   the previous and new arguments. */

                Value * vArgs[arity];
                auto n = argsDone;
                for (Value * arg = &vCur; arg->isPrimOpApp(); arg = arg->primOpApp.left)
                    vArgs[--n] = arg->primOpApp.right;

                for (size_t i = 0; i < argsLeft; ++i)
                    vArgs[argsDone + i] = args[i];

                nrPrimOpCalls++;
                if (countCalls) primOpCalls[primOp->primOp->name]++;
                primOp->primOp->fun(*this, pos, vArgs, vCur);

                nrArgs -= argsLeft;
                args += argsLeft;
            }
        }

        else if (vCur.type() == nAttrs && (functor = vCur.attrs->get(sFunctor))) {
            /* 'vCur' may be allocated on the stack of the calling
               function, but for functors we may keep a reference, so
               heap-allocate a copy and use that instead. */
            Value * args2[] = {allocValue(), args[0]};
            *args2[0] = vCur;
            /* !!! Should we use the attr pos here? */
            callFunction(*functor->value, 2, args2, vCur, pos);
            nrArgs--;
            args++;
        }

        else
            throwTypeError(pos, "attempt to call something which is not a function but %1%", vCur);
    }

    vRes = vCur;
}


void ExprCall::eval(EvalState & state, Env & env, Value & v)
{
    Value vFun;
    fun->eval(state, env, vFun);

    Value * vArgs[args.size()];
    for (size_t i = 0; i < args.size(); ++i)
        vArgs[i] = args[i]->maybeThunk(state, env);

    state.callFunction(vFun, args.size(), vArgs, v, pos);
}


// Lifted out of callFunction() because it creates a temporary that
// prevents tail-call optimisation.
void EvalState::incrFunctionCall(ExprLambda * fun)
{
    functionCalls[fun]++;
}


void EvalState::autoCallFunction(Bindings & args, Value & fun, Value & res)
{
    auto pos = fun.determinePos(noPos);

    forceValue(fun, pos);

    if (fun.type() == nAttrs) {
        auto found = fun.attrs->find(sFunctor);
        if (found != fun.attrs->end()) {
            Value * v = allocValue();
            callFunction(*found->value, fun, *v, pos);
            forceValue(*v, pos);
            return autoCallFunction(args, *v, res);
        }
    }

    if (!fun.isLambda() || !fun.lambda.fun->hasFormals()) {
        res = fun;
        return;
    }

    auto attrs = buildBindings(std::max(static_cast<uint32_t>(fun.lambda.fun->formals->formals.size()), args.size()));

    if (fun.lambda.fun->formals->ellipsis) {
        // If the formals have an ellipsis (eg the function accepts extra args) pass
        // all available automatic arguments (which includes arguments specified on
        // the command line via --arg/--argstr)
        for (auto & v : args)
            attrs.insert(v);
    } else {
        // Otherwise, only pass the arguments that the function accepts
        for (auto & i : fun.lambda.fun->formals->formals) {
            Bindings::iterator j = args.find(i.name);
            if (j != args.end()) {
                attrs.insert(*j);
            } else if (!i.def) {
                throwMissingArgumentError(i.pos, R"(cannot evaluate a function that has an argument without a value ('%1%')

Nix attempted to evaluate a function as a top level expression; in
this case it must have its arguments supplied either by default
values, or passed explicitly with '--arg' or '--argstr'. See
https://nixos.org/manual/nix/stable/#ss-functions.)", i.name);

            }
        }
    }

    callFunction(fun, allocValue()->mkAttrs(attrs), res, noPos);
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
    (state.evalBool(env, cond, pos) ? then : else_)->eval(state, env, v);
}


void ExprAssert::eval(EvalState & state, Env & env, Value & v)
{
    if (!state.evalBool(env, cond, pos)) {
        std::ostringstream out;
        cond->show(out);
        throwAssertionError(pos, "assertion '%1%' failed", out.str());
    }
    body->eval(state, env, v);
}


void ExprOpNot::eval(EvalState & state, Env & env, Value & v)
{
    v.mkBool(!state.evalBool(env, e));
}


void ExprOpEq::eval(EvalState & state, Env & env, Value & v)
{
    Value v1; e1->eval(state, env, v1);
    Value v2; e2->eval(state, env, v2);
    v.mkBool(state.eqValues(v1, v2));
}


void ExprOpNEq::eval(EvalState & state, Env & env, Value & v)
{
    Value v1; e1->eval(state, env, v1);
    Value v2; e2->eval(state, env, v2);
    v.mkBool(!state.eqValues(v1, v2));
}


void ExprOpAnd::eval(EvalState & state, Env & env, Value & v)
{
    v.mkBool(state.evalBool(env, e1, pos) && state.evalBool(env, e2, pos));
}


void ExprOpOr::eval(EvalState & state, Env & env, Value & v)
{
    v.mkBool(state.evalBool(env, e1, pos) || state.evalBool(env, e2, pos));
}


void ExprOpImpl::eval(EvalState & state, Env & env, Value & v)
{
    v.mkBool(!state.evalBool(env, e1, pos) || state.evalBool(env, e2, pos));
}


void ExprOpUpdate::eval(EvalState & state, Env & env, Value & v)
{
    Value v1, v2;
    state.evalAttrs(env, e1, v1);
    state.evalAttrs(env, e2, v2);

    state.nrOpUpdates++;

    if (v1.attrs->size() == 0) { v = v2; return; }
    if (v2.attrs->size() == 0) { v = v1; return; }

    auto attrs = state.buildBindings(v1.attrs->size() + v2.attrs->size());

    /* Merge the sets, preferring values from the second set.  Make
       sure to keep the resulting vector in sorted order. */
    Bindings::iterator i = v1.attrs->begin();
    Bindings::iterator j = v2.attrs->begin();

    while (i != v1.attrs->end() && j != v2.attrs->end()) {
        if (i->name == j->name) {
            attrs.insert(*j);
            ++i; ++j;
        }
        else if (i->name < j->name)
            attrs.insert(*i++);
        else
            attrs.insert(*j++);
    }

    while (i != v1.attrs->end()) attrs.insert(*i++);
    while (j != v2.attrs->end()) attrs.insert(*j++);

    v.mkAttrs(attrs.alreadySorted());

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
    std::vector<BackedStringView> s;
    size_t sSize = 0;
    NixInt n = 0;
    NixFloat nf = 0;

    bool first = !forceString;
    ValueType firstType = nString;

    const auto str = [&] {
        std::string result;
        result.reserve(sSize);
        for (const auto & part : s) result += *part;
        return result;
    };
    /* c_str() is not str().c_str() because we want to create a string
       Value. allocating a GC'd string directly and moving it into a
       Value lets us avoid an allocation and copy. */
    const auto c_str = [&] {
        char * result = allocString(sSize + 1);
        char * tmp = result;
        for (const auto & part : s) {
            memcpy(tmp, part->data(), part->size());
            tmp += part->size();
        }
        *tmp = 0;
        return result;
    };

    Value values[es->size()];
    Value * vTmpP = values;

    for (auto & [i_pos, i] : *es) {
        Value & vTmp = *vTmpP++;
        i->eval(state, env, vTmp);

        /* If the first element is a path, then the result will also
           be a path, we don't copy anything (yet - that's done later,
           since paths are copied when they are used in a derivation),
           and none of the strings are allowed to have contexts. */
        if (first) {
            firstType = vTmp.type();
        }

        if (firstType == nInt) {
            if (vTmp.type() == nInt) {
                n += vTmp.integer;
            } else if (vTmp.type() == nFloat) {
                // Upgrade the type from int to float;
                firstType = nFloat;
                nf = n;
                nf += vTmp.fpoint;
            } else
                throwEvalError(i_pos, "cannot add %1% to an integer", showType(vTmp));
        } else if (firstType == nFloat) {
            if (vTmp.type() == nInt) {
                nf += vTmp.integer;
            } else if (vTmp.type() == nFloat) {
                nf += vTmp.fpoint;
            } else
                throwEvalError(i_pos, "cannot add %1% to a float", showType(vTmp));
        } else {
            if (s.empty()) s.reserve(es->size());
            /* skip canonization of first path, which would only be not
            canonized in the first place if it's coming from a ./${foo} type
            path */
            auto part = state.coerceToString(i_pos, vTmp, context, false, firstType == nString, !first);
            sSize += part->size();
            s.emplace_back(std::move(part));
        }

        first = false;
    }

    if (firstType == nInt)
        v.mkInt(n);
    else if (firstType == nFloat)
        v.mkFloat(nf);
    else if (firstType == nPath) {
        if (!context.empty())
            throwEvalError(pos, "a string that refers to a store path cannot be appended to a path");
        v.mkPath(canonPath(str()));
    } else
        v.mkStringMove(c_str(), context);
}


void ExprPos::eval(EvalState & state, Env & env, Value & v)
{
    state.mkPos(v, ptr(&pos));
}


void EvalState::forceValueDeep(Value & v)
{
    std::set<const Value *> seen;

    std::function<void(Value & v)> recurse;

    recurse = [&](Value & v) {
        if (!seen.insert(&v).second) return;

        forceValue(v, [&]() { return v.determinePos(noPos); });

        if (v.type() == nAttrs) {
            for (auto & i : *v.attrs)
                try {
                    recurse(*i.value);
                } catch (Error & e) {
                    addErrorTrace(e, *i.pos, "while evaluating the attribute '%1%'", i.name);
                    throw;
                }
        }

        else if (v.isList()) {
            for (auto v2 : v.listItems())
                recurse(*v2);
        }
    };

    recurse(v);
}


NixInt EvalState::forceInt(Value & v, const Pos & pos)
{
    forceValue(v, pos);
    if (v.type() != nInt)
        throwTypeError(pos, "value is %1% while an integer was expected", v);
    return v.integer;
}


NixFloat EvalState::forceFloat(Value & v, const Pos & pos)
{
    forceValue(v, pos);
    if (v.type() == nInt)
        return v.integer;
    else if (v.type() != nFloat)
        throwTypeError(pos, "value is %1% while a float was expected", v);
    return v.fpoint;
}


bool EvalState::forceBool(Value & v, const Pos & pos)
{
    forceValue(v, pos);
    if (v.type() != nBool)
        throwTypeError(pos, "value is %1% while a Boolean was expected", v);
    return v.boolean;
}


bool EvalState::isFunctor(Value & fun)
{
    return fun.type() == nAttrs && fun.attrs->find(sFunctor) != fun.attrs->end();
}


void EvalState::forceFunction(Value & v, const Pos & pos)
{
    forceValue(v, pos);
    if (v.type() != nFunction && !isFunctor(v))
        throwTypeError(pos, "value is %1% while a function was expected", v);
}


std::string_view EvalState::forceString(Value & v, const Pos & pos)
{
    forceValue(v, pos);
    if (v.type() != nString) {
        if (pos)
            throwTypeError(pos, "value is %1% while a string was expected", v);
        else
            throwTypeError("value is %1% while a string was expected", v);
    }
    return v.string.s;
}


/* Decode a context string ‘!<name>!<path>’ into a pair <path,
   name>. */
NixStringContextElem decodeContext(const Store & store, std::string_view s)
{
    if (s.at(0) == '!') {
        size_t index = s.find("!", 1);
        return {
            store.parseStorePath(s.substr(index + 1)),
            std::string(s.substr(1, index - 1)),
        };
    } else
        return {
            store.parseStorePath(
                s.at(0) == '/'
                ? s
                : s.substr(1)),
            "",
        };
}


void copyContext(const Value & v, PathSet & context)
{
    if (v.string.context)
        for (const char * * p = v.string.context; *p; ++p)
            context.insert(*p);
}


NixStringContext Value::getContext(const Store & store)
{
    NixStringContext res;
    assert(internalType == tString);
    if (string.context)
        for (const char * * p = string.context; *p; ++p)
            res.push_back(decodeContext(store, *p));
    return res;
}


std::string_view EvalState::forceString(Value & v, PathSet & context, const Pos & pos)
{
    auto s = forceString(v, pos);
    copyContext(v, context);
    return s;
}


std::string_view EvalState::forceStringNoCtx(Value & v, const Pos & pos)
{
    auto s = forceString(v, pos);
    if (v.string.context) {
        if (pos)
            throwEvalError(pos, "the string '%1%' is not allowed to refer to a store path (such as '%2%')",
                v.string.s, v.string.context[0]);
        else
            throwEvalError("the string '%1%' is not allowed to refer to a store path (such as '%2%')",
                v.string.s, v.string.context[0]);
    }
    return s;
}


bool EvalState::isDerivation(Value & v)
{
    if (v.type() != nAttrs) return false;
    Bindings::iterator i = v.attrs->find(sType);
    if (i == v.attrs->end()) return false;
    forceValue(*i->value, *i->pos);
    if (i->value->type() != nString) return false;
    return strcmp(i->value->string.s, "derivation") == 0;
}


std::optional<std::string> EvalState::tryAttrsToString(const Pos & pos, Value & v,
    PathSet & context, bool coerceMore, bool copyToStore)
{
    auto i = v.attrs->find(sToString);
    if (i != v.attrs->end()) {
        Value v1;
        callFunction(*i->value, v, v1, pos);
        return coerceToString(pos, v1, context, coerceMore, copyToStore).toOwned();
    }

    return {};
}

BackedStringView EvalState::coerceToString(const Pos & pos, Value & v, PathSet & context,
    bool coerceMore, bool copyToStore, bool canonicalizePath)
{
    forceValue(v, pos);

    if (v.type() == nString) {
        copyContext(v, context);
        return std::string_view(v.string.s);
    }

    if (v.type() == nPath) {
        BackedStringView path(PathView(v.path));
        if (canonicalizePath)
            path = canonPath(*path);
        if (copyToStore)
            path = copyPathToStore(context, std::move(path).toOwned());
        return path;
    }

    if (v.type() == nAttrs) {
        auto maybeString = tryAttrsToString(pos, v, context, coerceMore, copyToStore);
        if (maybeString)
            return std::move(*maybeString);
        auto i = v.attrs->find(sOutPath);
        if (i == v.attrs->end()) throwTypeError(pos, "cannot coerce a set to a string");
        return coerceToString(pos, *i->value, context, coerceMore, copyToStore);
    }

    if (v.type() == nExternal)
        return v.external->coerceToString(pos, context, coerceMore, copyToStore);

    if (coerceMore) {

        /* Note that `false' is represented as an empty string for
           shell scripting convenience, just like `null'. */
        if (v.type() == nBool && v.boolean) return "1";
        if (v.type() == nBool && !v.boolean) return "";
        if (v.type() == nInt) return std::to_string(v.integer);
        if (v.type() == nFloat) return std::to_string(v.fpoint);
        if (v.type() == nNull) return "";

        if (v.isList()) {
            std::string result;
            for (auto [n, v2] : enumerate(v.listItems())) {
                result += *coerceToString(pos, *v2, context, coerceMore, copyToStore);
                if (n < v.listSize() - 1
                    /* !!! not quite correct */
                    && (!v2->isList() || v2->listSize() != 0))
                    result += " ";
            }
            return std::move(result);
        }
    }

    throwTypeError(pos, "cannot coerce %1% to a string", v);
}


std::string EvalState::copyPathToStore(PathSet & context, const Path & path)
{
    if (nix::isDerivation(path))
        throwEvalError("file names are not allowed to end in '%1%'", drvExtension);

    Path dstPath;
    auto i = srcToStore.find(path);
    if (i != srcToStore.end())
        dstPath = store->printStorePath(i->second);
    else {
        auto p = settings.readOnlyMode
            ? store->computeStorePathForPath(std::string(baseNameOf(path)), checkSourcePath(path)).first
            : store->addToStore(std::string(baseNameOf(path)), checkSourcePath(path), FileIngestionMethod::Recursive, htSHA256, defaultPathFilter, repair);
        dstPath = store->printStorePath(p);
        allowPath(p);
        srcToStore.insert_or_assign(path, std::move(p));
        printMsg(lvlChatty, "copied source '%1%' -> '%2%'", path, dstPath);
    }

    context.insert(dstPath);
    return dstPath;
}


Path EvalState::coerceToPath(const Pos & pos, Value & v, PathSet & context)
{
    auto path = coerceToString(pos, v, context, false, false).toOwned();
    if (path == "" || path[0] != '/')
        throwEvalError(pos, "string '%1%' doesn't represent an absolute path", path);
    return path;
}


StorePath EvalState::coerceToStorePath(const Pos & pos, Value & v, PathSet & context)
{
    auto path = coerceToString(pos, v, context, false, false).toOwned();
    if (auto storePath = store->maybeParseStorePath(path))
        return *storePath;
    throw EvalError({
        .msg = hintfmt("path '%1%' is not in the Nix store", path),
        .errPos = pos
    });
}


bool EvalState::eqValues(Value & v1, Value & v2)
{
    forceValue(v1, noPos);
    forceValue(v2, noPos);

    /* !!! Hack to support some old broken code that relies on pointer
       equality tests between sets.  (Specifically, builderDefs calls
       uniqList on a list of sets.)  Will remove this eventually. */
    if (&v1 == &v2) return true;

    // Special case type-compatibility between float and int
    if (v1.type() == nInt && v2.type() == nFloat)
        return v1.integer == v2.fpoint;
    if (v1.type() == nFloat && v2.type() == nInt)
        return v1.fpoint == v2.integer;

    // All other types are not compatible with each other.
    if (v1.type() != v2.type()) return false;

    switch (v1.type()) {

        case nInt:
            return v1.integer == v2.integer;

        case nBool:
            return v1.boolean == v2.boolean;

        case nString:
            return strcmp(v1.string.s, v2.string.s) == 0;

        case nPath:
            return strcmp(v1.path, v2.path) == 0;

        case nNull:
            return true;

        case nList:
            if (v1.listSize() != v2.listSize()) return false;
            for (size_t n = 0; n < v1.listSize(); ++n)
                if (!eqValues(*v1.listElems()[n], *v2.listElems()[n])) return false;
            return true;

        case nAttrs: {
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
        case nFunction:
            return false;

        case nExternal:
            return *v1.external == *v2.external;

        case nFloat:
            return v1.fpoint == v2.fpoint;

        default:
            throwEvalError("cannot compare %1% with %2%", showType(v1), showType(v2));
    }
}

void EvalState::printStats()
{
    bool showStats = getEnv("NIX_SHOW_STATS").value_or("0") != "0";

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
        auto outPath = getEnv("NIX_SHOW_STATS_PATH").value_or("-");
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
                        obj.attr("name", (const std::string &) i.first->name);
                    else
                        obj.attr("name", nullptr);
                    if (i.first->pos) {
                        obj.attr("file", (const std::string &) i.first->pos.file);
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
                        obj.attr("file", (const std::string &) i.first.file);
                        obj.attr("line", i.first.line);
                        obj.attr("column", i.first.column);
                    }
                    obj.attr("count", i.second);
                }
            }
        }

        if (getEnv("NIX_SHOW_SYMBOLS").value_or("0") != "0") {
            auto list = topObj.list("symbols");
            symbols.dump([&](const std::string & s) { list.elem(s); });
        }
    }
}


std::string ExternalValueBase::coerceToString(const Pos & pos, PathSet & context, bool copyMore, bool copyToStore) const
{
    throw TypeError({
        .msg = hintfmt("cannot coerce %1% to a string", showType()),
        .errPos = pos
    });
}


bool ExternalValueBase::operator==(const ExternalValueBase & b) const
{
    return false;
}


std::ostream & operator << (std::ostream & str, const ExternalValueBase & v) {
    return v.print(str);
}


EvalSettings::EvalSettings()
{
    auto var = getEnv("NIX_PATH");
    if (var) nixPath = parseNixPath(*var);
}

Strings EvalSettings::getDefaultNixPath()
{
    Strings res;
    auto add = [&](const Path & p, const std::string & s = std::string()) {
        if (pathExists(p)) {
            if (s.empty()) {
                res.push_back(p);
            } else {
                res.push_back(s + "=" + p);
            }
        }
    };

    if (!evalSettings.restrictEval && !evalSettings.pureEval) {
        add(getHome() + "/.nix-defexpr/channels");
        add(settings.nixStateDir + "/profiles/per-user/root/channels/nixpkgs", "nixpkgs");
        add(settings.nixStateDir + "/profiles/per-user/root/channels");
    }

    return res;
}

EvalSettings evalSettings;

static GlobalConfig::Register rEvalSettings(&evalSettings);


}
