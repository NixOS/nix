#include "eval.hh"
#include "eval-settings.hh"
#include "hash.hh"
#include "primops.hh"
#include "print-options.hh"
#include "shared.hh"
#include "types.hh"
#include "util.hh"
#include "store-api.hh"
#include "derivations.hh"
#include "downstream-placeholder.hh"
#include "globals.hh"
#include "eval-inline.hh"
#include "filetransfer.hh"
#include "function-trace.hh"
#include "profiles.hh"
#include "print.hh"
#include "fs-input-accessor.hh"
#include "filtering-input-accessor.hh"
#include "memory-input-accessor.hh"
#include "signals.hh"
#include "gc-small-vector.hh"
#include "url.hh"
#include "fetch-to-store.hh"
#include "tarball.hh"
#include "flake/flakeref.hh"
#include "parser-tab.hh"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <cstring>
#include <optional>
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <fstream>
#include <functional>
#include <iostream>

#include <sys/resource.h>
#include <nlohmann/json.hpp>
#include <boost/container/small_vector.hpp>

#if HAVE_BOEHMGC

#define GC_INCLUDE_NEW

#include <gc/gc.h>
#include <gc/gc_cpp.h>

#include <boost/coroutine2/coroutine.hpp>
#include <boost/coroutine2/protected_fixedsize_stack.hpp>
#include <boost/context/stack_context.hpp>

#endif

using json = nlohmann::json;

namespace nix {

static char * allocString(size_t size)
{
    char * t;
#if HAVE_BOEHMGC
    t = (char *) GC_MALLOC_ATOMIC(size);
#else
    t = (char *) malloc(size);
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
// This function handles makeImmutableString(std::string_view()) by returning
// the empty string.
static const char * makeImmutableString(std::string_view s)
{
    const size_t size = s.size();
    if (size == 0)
        return "";
    auto t = allocString(size + 1);
    memcpy(t, s.data(), size);
    t[size] = '\0';
    return t;
}


RootValue allocRootValue(Value * v)
{
#if HAVE_BOEHMGC
    return std::allocate_shared<Value *>(traceable_allocator<Value *>(), v);
#else
    return std::make_shared<Value *>(v);
#endif
}

// Pretty print types for assertion errors
std::ostream & operator << (std::ostream & os, const ValueType t) {
    os << showType(t);
    return os;
}

std::string printValue(EvalState & state, Value & v)
{
    std::ostringstream out;
    v.print(state, out);
    return out.str();
}

void Value::print(EvalState & state, std::ostream & str, PrintOptions options)
{
    printValue(state, str, *this, options);
}

const Value * getPrimOp(const Value &v) {
    const Value * primOp = &v;
    while (primOp->isPrimOpApp()) {
        primOp = primOp->primOpApp.left;
    }
    assert(primOp->isPrimOp());
    return primOp;
}

std::string_view showType(ValueType type, bool withArticle)
{
    #define WA(a, w) withArticle ? a " " w : w
    switch (type) {
        case nInt: return WA("an", "integer");
        case nBool: return WA("a", "Boolean");
        case nString: return WA("a", "string");
        case nPath: return WA("a", "path");
        case nNull: return "null";
        case nAttrs: return WA("a", "set");
        case nList: return WA("a", "list");
        case nFunction: return WA("a", "function");
        case nExternal: return WA("an", "external value");
        case nFloat: return WA("a", "float");
        case nThunk: return WA("a", "thunk");
    }
    abort();
}


std::string showType(const Value & v)
{
    // Allow selecting a subset of enum values
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wswitch-enum"
    switch (v.internalType) {
        case tString: return v.string.context ? "a string with context" : "a string";
        case tPrimOp:
            return fmt("the built-in function '%s'", std::string(v.primOp->name));
        case tPrimOpApp:
            return fmt("the partially applied built-in function '%s'", std::string(getPrimOp(v)->primOp->name));
        case tExternal: return v.external->showType();
        case tThunk: return v.isBlackhole() ? "a black hole" : "a thunk";
        case tApp: return "a function application";
    default:
        return std::string(showType(v.type()));
    }
    #pragma GCC diagnostic pop
}

PosIdx Value::determinePos(const PosIdx pos) const
{
    // Allow selecting a subset of enum values
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wswitch-enum"
    switch (internalType) {
        case tAttrs: return attrs->pos;
        case tLambda: return lambda.fun->pos;
        case tApp: return app.left->determinePos(pos);
        default: return pos;
    }
    #pragma GCC diagnostic pop
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
    if (name.symbol) {
        return name.symbol;
    } else {
        Value nameValue;
        name.expr->eval(state, env, nameValue);
        state.forceStringNoCtx(nameValue, name.expr->getPos(), "while evaluating an attribute name");
        return state.symbols.create(nameValue.string_view());
    }
}

#if HAVE_BOEHMGC
/* Disable GC while this object lives. Used by CoroutineContext.
 *
 * Boehm keeps a count of GC_disable() and GC_enable() calls,
 * and only enables GC when the count matches.
 */
class BoehmDisableGC {
public:
    BoehmDisableGC() {
        GC_disable();
    };
    ~BoehmDisableGC() {
        GC_enable();
    };
};
#endif

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


#if NIX_BOEHM_PATCH_VERSION != 1
    printTalkative("Unpatched BoehmGC, disabling GC inside coroutines");
    /* Used to disable GC when entering coroutines on macOS */
    create_coro_gc_hook = []() -> std::shared_ptr<void> {
        return std::make_shared<BoehmDisableGC>();
    };
#endif

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
        debug("setting initial heap size to %1% bytes", size);
        GC_expand_hp(size);
    }

#endif

    gcInitialised = true;
}

EvalState::EvalState(
    const SearchPath & _searchPath,
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
    , sOutputSpecified(symbols.create("outputSpecified"))
    , exprSymbols{
        .sub = symbols.create("__sub"),
        .lessThan = symbols.create("__lessThan"),
        .mul = symbols.create("__mul"),
        .div = symbols.create("__div"),
        .or_ = symbols.create("or"),
        .findFile = symbols.create("__findFile"),
        .nixPath = symbols.create("__nixPath"),
        .body = symbols.create("body"),
    }
    , repair(NoRepair)
    , emptyBindings(0)
    , rootFS(
        evalSettings.restrictEval || evalSettings.pureEval
        ? ref<InputAccessor>(AllowListInputAccessor::create(makeFSInputAccessor(), {},
            [](const CanonPath & path) -> RestrictedPathError {
                auto modeInformation = evalSettings.pureEval
                    ? "in pure evaluation mode (use '--impure' to override)"
                    : "in restricted mode";
                throw RestrictedPathError("access to absolute path '%1%' is forbidden %2%", path, modeInformation);
            }))
        : makeFSInputAccessor())
    , corepkgsFS(makeMemoryInputAccessor())
    , internalFS(makeMemoryInputAccessor())
    , derivationInternal{corepkgsFS->addFile(
        CanonPath("derivation-internal.nix"),
        #include "primops/derivation.nix.gen.hh"
    )}
    , callFlakeInternal{internalFS->addFile(
        CanonPath("call-flake.nix"),
        #include "flake/call-flake.nix.gen.hh"
    )}
    , store(store)
    , buildStore(buildStore ? buildStore : store)
    , debugRepl(nullptr)
    , debugStop(false)
    , trylevel(0)
    , regexCache(makeRegexCache())
#if HAVE_BOEHMGC
    , valueAllocCache(std::allocate_shared<void *>(traceable_allocator<void *>(), nullptr))
    , env1AllocCache(std::allocate_shared<void *>(traceable_allocator<void *>(), nullptr))
#endif
    , baseEnv(allocEnv(128))
    , staticBaseEnv{std::make_shared<StaticEnv>(nullptr, nullptr)}
{
    corepkgsFS->setPathDisplay("<nix", ">");
    internalFS->setPathDisplay("«nix-internal»", "");

    countCalls = getEnv("NIX_COUNT_CALLS").value_or("0") != "0";

    assert(gcInitialised);

    static_assert(sizeof(Env) <= 16, "environment must be <= 16 bytes");

    vEmptyList.mkList(buildList(0));
    vNull.mkNull();
    vTrue.mkBool(true);
    vFalse.mkBool(false);
    vStringRegular.mkString("regular");
    vStringDirectory.mkString("directory");
    vStringSymlink.mkString("symlink");
    vStringUnknown.mkString("unknown");

    /* Initialise the Nix expression search path. */
    if (!evalSettings.pureEval) {
        for (auto & i : _searchPath.elements)
            searchPath.elements.emplace_back(SearchPath::Elem {i});
        for (auto & i : evalSettings.nixPath.get())
            searchPath.elements.emplace_back(SearchPath::Elem::parse(i));
    }

    /* Allow access to all paths in the search path. */
    if (rootFS.dynamic_pointer_cast<AllowListInputAccessor>())
        for (auto & i : searchPath.elements)
            resolveSearchPathPath(i.path, true);

    corepkgsFS->addFile(
        CanonPath("fetchurl.nix"),
        #include "fetchurl.nix.gen.hh"
    );

    createBaseEnv();
}


EvalState::~EvalState()
{
}


void EvalState::allowPath(const Path & path)
{
    if (auto rootFS2 = rootFS.dynamic_pointer_cast<AllowListInputAccessor>())
        rootFS2->allowPrefix(CanonPath(path));
}

void EvalState::allowPath(const StorePath & storePath)
{
    if (auto rootFS2 = rootFS.dynamic_pointer_cast<AllowListInputAccessor>())
        rootFS2->allowPrefix(CanonPath(store->toRealPath(storePath)));
}

void EvalState::allowAndSetStorePathString(const StorePath & storePath, Value & v)
{
    allowPath(storePath);

    mkStorePathString(storePath, v);
}

inline static bool isJustSchemePrefix(std::string_view prefix)
{
    return
        !prefix.empty()
        && prefix[prefix.size() - 1] == ':'
        && isValidSchemeName(prefix.substr(0, prefix.size() - 1));
}

bool isAllowedURI(std::string_view uri, const Strings & allowedUris)
{
    /* 'uri' should be equal to a prefix, or in a subdirectory of a
       prefix. Thus, the prefix https://github.co does not permit
       access to https://github.com. */
    for (auto & prefix : allowedUris) {
        if (uri == prefix
            // Allow access to subdirectories of the prefix.
            || (uri.size() > prefix.size()
                && prefix.size() > 0
                && hasPrefix(uri, prefix)
                && (
                    // Allow access to subdirectories of the prefix.
                    prefix[prefix.size() - 1] == '/'
                    || uri[prefix.size()] == '/'

                    // Allow access to whole schemes
                    || isJustSchemePrefix(prefix)
                    )
                ))
            return true;
    }

    return false;
}

void EvalState::checkURI(const std::string & uri)
{
    if (!evalSettings.restrictEval) return;

    if (isAllowedURI(uri, evalSettings.allowedUris.get())) return;

    /* If the URI is a path, then check it against allowedPaths as
       well. */
    if (hasPrefix(uri, "/")) {
        if (auto rootFS2 = rootFS.dynamic_pointer_cast<AllowListInputAccessor>())
            rootFS2->checkAccess(CanonPath(uri));
        return;
    }

    if (hasPrefix(uri, "file://")) {
        if (auto rootFS2 = rootFS.dynamic_pointer_cast<AllowListInputAccessor>())
            rootFS2->checkAccess(CanonPath(uri.substr(7)));
        return;
    }

    throw RestrictedPathError("access to URI '%s' is forbidden in restricted mode", uri);
}


Path EvalState::toRealPath(const Path & path, const NixStringContext & context)
{
    // FIXME: check whether 'path' is in 'context'.
    return
        !context.empty() && store->isInStore(path)
        ? store->toRealPath(path)
        : path;
}


Value * EvalState::addConstant(const std::string & name, Value & v, Constant info)
{
    Value * v2 = allocValue();
    *v2 = v;
    addConstant(name, v2, info);
    return v2;
}


void EvalState::addConstant(const std::string & name, Value * v, Constant info)
{
    auto name2 = name.substr(0, 2) == "__" ? name.substr(2) : name;

    constantInfos.push_back({name2, info});

    if (!(evalSettings.pureEval && info.impureOnly)) {
        /* Check the type, if possible.

           We might know the type of a thunk in advance, so be allowed
           to just write it down in that case. */
        if (auto gotType = v->type(true); gotType != nThunk)
            assert(info.type == gotType);

        /* Install value the base environment. */
        staticBaseEnv->vars.emplace_back(symbols.create(name), baseEnvDispl);
        baseEnv.values[baseEnvDispl++] = v;
        baseEnv.values[0]->attrs->push_back(Attr(symbols.create(name2), v));
    }
}


void PrimOp::check()
{
    if (arity > maxPrimOpArity) {
        throw Error("primop arity must not exceed %1%", maxPrimOpArity);
    }
}


std::ostream & operator<<(std::ostream & output, PrimOp & primOp)
{
    output << "primop " << primOp.name;
    return output;
}


PrimOp * Value::primOpAppPrimOp() const
{
    Value * left = primOpApp.left;
    while (left && !left->isPrimOp()) {
        left = left->primOpApp.left;
    }

    if (!left)
        return nullptr;
    return left->primOp;
}


void Value::mkPrimOp(PrimOp * p)
{
    p->check();
    clearValue();
    internalType = tPrimOp;
    primOp = p;
}


Value * EvalState::addPrimOp(PrimOp && primOp)
{
    /* Hack to make constants lazy: turn them into a application of
       the primop to a dummy value. */
    if (primOp.arity == 0) {
        primOp.arity = 1;
        auto vPrimOp = allocValue();
        vPrimOp->mkPrimOp(new PrimOp(primOp));
        Value v;
        v.mkApp(vPrimOp, vPrimOp);
        return addConstant(primOp.name, v, {
            .type = nThunk, // FIXME
            .doc = primOp.doc,
        });
    }

    auto envName = symbols.create(primOp.name);
    if (hasPrefix(primOp.name, "__"))
        primOp.name = primOp.name.substr(2);

    Value * v = allocValue();
    v->mkPrimOp(new PrimOp(primOp));
    staticBaseEnv->vars.emplace_back(envName, baseEnvDispl);
    baseEnv.values[baseEnvDispl++] = v;
    baseEnv.values[0]->attrs->push_back(Attr(symbols.create(primOp.name), v));
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
        if (auto * doc = v2->primOp->doc)
            return Doc {
                .pos = {},
                .name = v2->primOp->name,
                .arity = v2->primOp->arity,
                .args = v2->primOp->args,
                .doc = doc,
            };
    }
    return {};
}


// just for the current level of StaticEnv, not the whole chain.
void printStaticEnvBindings(const SymbolTable & st, const StaticEnv & se)
{
    std::cout << ANSI_MAGENTA;
    for (auto & i : se.vars)
        std::cout << st[i.first] << " ";
    std::cout << ANSI_NORMAL;
    std::cout << std::endl;
}

// just for the current level of Env, not the whole chain.
void printWithBindings(const SymbolTable & st, const Env & env)
{
    if (!env.values[0]->isThunk()) {
        std::cout << "with: ";
        std::cout << ANSI_MAGENTA;
        Bindings::iterator j = env.values[0]->attrs->begin();
        while (j != env.values[0]->attrs->end()) {
            std::cout << st[j->name] << " ";
            ++j;
        }
        std::cout << ANSI_NORMAL;
        std::cout << std::endl;
    }
}

void printEnvBindings(const SymbolTable & st, const StaticEnv & se, const Env & env, int lvl)
{
    std::cout << "Env level " << lvl << std::endl;

    if (se.up && env.up) {
        std::cout << "static: ";
        printStaticEnvBindings(st, se);
        if (se.isWith)
            printWithBindings(st, env);
        std::cout << std::endl;
        printEnvBindings(st, *se.up, *env.up, ++lvl);
    } else {
        std::cout << ANSI_MAGENTA;
        // for the top level, don't print the double underscore ones;
        // they are in builtins.
        for (auto & i : se.vars)
            if (!hasPrefix(st[i.first], "__"))
                std::cout << st[i.first] << " ";
        std::cout << ANSI_NORMAL;
        std::cout << std::endl;
        if (se.isWith)
            printWithBindings(st, env);  // probably nothing there for the top level.
        std::cout << std::endl;

    }
}

void printEnvBindings(const EvalState &es, const Expr & expr, const Env & env)
{
    // just print the names for now
    auto se = es.getStaticEnv(expr);
    if (se)
        printEnvBindings(es.symbols, *se, env, 0);
}

void mapStaticEnvBindings(const SymbolTable & st, const StaticEnv & se, const Env & env, ValMap & vm)
{
    // add bindings for the next level up first, so that the bindings for this level
    // override the higher levels.
    // The top level bindings (builtins) are skipped since they are added for us by initEnv()
    if (env.up && se.up) {
        mapStaticEnvBindings(st, *se.up, *env.up, vm);

        if (se.isWith && !env.values[0]->isThunk()) {
            // add 'with' bindings.
            Bindings::iterator j = env.values[0]->attrs->begin();
            while (j != env.values[0]->attrs->end()) {
                vm[st[j->name]] = j->value;
                ++j;
            }
        } else {
            // iterate through staticenv bindings and add them.
            for (auto & i : se.vars)
                vm[st[i.first]] = env.values[i.second];
        }
    }
}

std::unique_ptr<ValMap> mapStaticEnvBindings(const SymbolTable & st, const StaticEnv & se, const Env & env)
{
    auto vm = std::make_unique<ValMap>();
    mapStaticEnvBindings(st, se, env, *vm);
    return vm;
}

/**
 * Sets `inDebugger` to true on construction and false on destruction.
 */
class DebuggerGuard {
    bool & inDebugger;
public:
    DebuggerGuard(bool & inDebugger) : inDebugger(inDebugger) {
        inDebugger = true;
    }
    ~DebuggerGuard() {
        inDebugger = false;
    }
};

void EvalState::runDebugRepl(const Error * error, const Env & env, const Expr & expr)
{
    // Make sure we have a debugger to run and we're not already in a debugger.
    if (!debugRepl || inDebugger)
        return;

    auto dts =
        error && expr.getPos()
        ? std::make_unique<DebugTraceStacker>(
            *this,
            DebugTrace {
                .pos = error->info().pos ? error->info().pos : positions[expr.getPos()],
                .expr = expr,
                .env = env,
                .hint = error->info().msg,
                .isError = true
            })
        : nullptr;

    if (error)
    {
        printError("%s\n", error->what());

        if (trylevel > 0 && error->info().level != lvlInfo)
            printError("This exception occurred in a 'tryEval' call. Use " ANSI_GREEN "--ignore-try" ANSI_NORMAL " to skip these.\n");
    }

    auto se = getStaticEnv(expr);
    if (se) {
        auto vm = mapStaticEnvBindings(symbols, *se.get(), env);
        DebuggerGuard _guard(inDebugger);
        auto exitStatus = (debugRepl)(ref<EvalState>(shared_from_this()), *vm);
        switch (exitStatus) {
            case ReplExitStatus::QuitAll:
                if (error)
                    throw *error;
                throw Exit(0);
            case ReplExitStatus::Continue:
                break;
            default:
                abort();
        }
    }
}

template<typename... Args>
void EvalState::addErrorTrace(Error & e, const Args & ... formatArgs) const
{
    e.addTrace(nullptr, HintFmt(formatArgs...));
}

template<typename... Args>
void EvalState::addErrorTrace(Error & e, const PosIdx pos, const Args & ... formatArgs) const
{
    e.addTrace(positions[pos], HintFmt(formatArgs...));
}

template<typename... Args>
static std::unique_ptr<DebugTraceStacker> makeDebugTraceStacker(
    EvalState & state,
    Expr & expr,
    Env & env,
    std::shared_ptr<Pos> && pos,
    const Args & ... formatArgs)
{
    return std::make_unique<DebugTraceStacker>(state,
        DebugTrace {
            .pos = std::move(pos),
            .expr = expr,
            .env = env,
            .hint = HintFmt(formatArgs...),
            .isError = false
        });
}

DebugTraceStacker::DebugTraceStacker(EvalState & evalState, DebugTrace t)
    : evalState(evalState)
    , trace(std::move(t))
{
    evalState.debugTraces.push_front(trace);
    if (evalState.debugStop && evalState.debugRepl)
        evalState.runDebugRepl(nullptr, trace.env, trace.expr);
}

void Value::mkString(std::string_view s)
{
    mkString(makeImmutableString(s));
}


static void copyContextToValue(Value & v, const NixStringContext & context)
{
    if (!context.empty()) {
        size_t n = 0;
        v.string.context = (const char * *)
            allocBytes((context.size() + 1) * sizeof(char *));
        for (auto & i : context)
            v.string.context[n++] = dupString(i.to_string().c_str());
        v.string.context[n] = 0;
    }
}

void Value::mkString(std::string_view s, const NixStringContext & context)
{
    mkString(s);
    copyContextToValue(*this, context);
}

void Value::mkStringMove(const char * s, const NixStringContext & context)
{
    mkString(s);
    copyContextToValue(*this, context);
}

void Value::mkPath(const SourcePath & path)
{
    mkPath(&*path.accessor, makeImmutableString(path.path.abs()));
}


inline Value * EvalState::lookupVar(Env * env, const ExprVar & var, bool noEval)
{
    for (auto l = var.level; l; --l, env = env->up) ;

    if (!var.fromWith) return env->values[var.displ];

    // This early exit defeats the `maybeThunk` optimization for variables from `with`,
    // The added complexity of handling this appears to be similarly in cost, or
    // the cases where applicable were insignificant in the first place.
    if (noEval) return nullptr;

    auto * fromWith = var.fromWith;
    while (1) {
        forceAttrs(*env->values[0], fromWith->pos, "while evaluating the first subexpression of a with expression");
        Bindings::iterator j = env->values[0]->attrs->find(var.name);
        if (j != env->values[0]->attrs->end()) {
            if (countCalls) attrSelects[j->pos]++;
            return j->value;
        }
        if (!fromWith->parentWith)
            error<UndefinedVarError>("undefined variable '%1%'", symbols[var.name]).atPos(var.pos).withFrame(*env, var).debugThrow();
        for (size_t l = fromWith->prevWith; l; --l, env = env->up) ;
        fromWith = fromWith->parentWith;
    }
}

ListBuilder::ListBuilder(EvalState & state, size_t size)
    : size(size)
    , elems(size <= 2 ? inlineElems : (Value * *) allocBytes(size * sizeof(Value *)))
{
    state.nrListElems += size;
}

Value * EvalState::getBool(bool b) {
    return b ? &vTrue : &vFalse;
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


void EvalState::mkPos(Value & v, PosIdx p)
{
    auto origin = positions.originOf(p);
    if (auto path = std::get_if<SourcePath>(&origin)) {
        auto attrs = buildBindings(3);
        attrs.alloc(sFile).mkString(path->path.abs());
        makePositionThunks(*this, p, attrs.alloc(sLine), attrs.alloc(sColumn));
        v.mkAttrs(attrs);
    } else
        v.mkNull();
}


void EvalState::mkStorePathString(const StorePath & p, Value & v)
{
    v.mkString(
        store->printStorePath(p),
        NixStringContext {
            NixStringContextElem::Opaque { .path = p },
        });
}


std::string EvalState::mkOutputStringRaw(
    const SingleDerivedPath::Built & b,
    std::optional<StorePath> optStaticOutputPath,
    const ExperimentalFeatureSettings & xpSettings)
{
    /* In practice, this is testing for the case of CA derivations, or
       dynamic derivations. */
    return optStaticOutputPath
        ? store->printStorePath(std::move(*optStaticOutputPath))
        /* Downstream we would substitute this for an actual path once
           we build the floating CA derivation */
        : DownstreamPlaceholder::fromSingleDerivedPathBuilt(b, xpSettings).render();
}


void EvalState::mkOutputString(
    Value & value,
    const SingleDerivedPath::Built & b,
    std::optional<StorePath> optStaticOutputPath,
    const ExperimentalFeatureSettings & xpSettings)
{
    value.mkString(
        mkOutputStringRaw(b, optStaticOutputPath, xpSettings),
        NixStringContext { b });
}


std::string EvalState::mkSingleDerivedPathStringRaw(
    const SingleDerivedPath & p)
{
    return std::visit(overloaded {
        [&](const SingleDerivedPath::Opaque & o) {
            return store->printStorePath(o.path);
        },
        [&](const SingleDerivedPath::Built & b) {
            auto optStaticOutputPath = std::visit(overloaded {
                [&](const SingleDerivedPath::Opaque & o) {
                    auto drv = store->readDerivation(o.path);
                    auto i = drv.outputs.find(b.output);
                    if (i == drv.outputs.end())
                        throw Error("derivation '%s' does not have output '%s'", b.drvPath->to_string(*store), b.output);
                    return i->second.path(*store, drv.name, b.output);
                },
                [&](const SingleDerivedPath::Built & o) -> std::optional<StorePath> {
                    return std::nullopt;
                },
            }, b.drvPath->raw());
            return mkOutputStringRaw(b, optStaticOutputPath);
        }
    }, p.raw());
}


void EvalState::mkSingleDerivedPathString(
    const SingleDerivedPath & p,
    Value & v)
{
    v.mkString(
        mkSingleDerivedPathStringRaw(p),
        NixStringContext {
            std::visit([](auto && v) -> NixStringContextElem { return v; }, p),
        });
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


void EvalState::evalFile(const SourcePath & path, Value & v, bool mustBeTrivial)
{
    FileEvalCache::iterator i;
    if ((i = fileEvalCache.find(path)) != fileEvalCache.end()) {
        v = i->second;
        return;
    }

    auto resolvedPath = resolveExprPath(path);
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
        e = parseExprFromFile(resolvedPath);

    fileParseCache[resolvedPath] = e;

    try {
        auto dts = debugRepl
            ? makeDebugTraceStacker(
                *this,
                *e,
                this->baseEnv,
                e->getPos() ? std::make_shared<Pos>(positions[e->getPos()]) : nullptr,
                "while evaluating the file '%1%':", resolvedPath.to_string())
            : nullptr;

        // Enforce that 'flake.nix' is a direct attrset, not a
        // computation.
        if (mustBeTrivial &&
            !(dynamic_cast<ExprAttrs *>(e)))
            error<EvalError>("file '%s' must be an attribute set", path).debugThrow();
        eval(e, v);
    } catch (Error & e) {
        addErrorTrace(e, "while evaluating the file '%1%':", resolvedPath.to_string());
        throw;
    }

    fileEvalCache[resolvedPath] = v;
    if (path != resolvedPath) fileEvalCache[path] = v;
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


inline bool EvalState::evalBool(Env & env, Expr * e, const PosIdx pos, std::string_view errorCtx)
{
    try {
        Value v;
        e->eval(*this, env, v);
        if (v.type() != nBool)
            error<TypeError>(
                 "expected a Boolean but found %1%: %2%",
                 showType(v),
                 ValuePrinter(*this, v, errorPrintOptions)
             ).atPos(pos).withFrame(env, *e).debugThrow();
        return v.boolean;
    } catch (Error & e) {
        e.addTrace(positions[pos], errorCtx);
        throw;
    }
}


inline void EvalState::evalAttrs(Env & env, Expr * e, Value & v, const PosIdx pos, std::string_view errorCtx)
{
    try {
        e->eval(*this, env, v);
        if (v.type() != nAttrs)
            error<TypeError>(
                "expected a set but found %1%: %2%",
                showType(v),
                ValuePrinter(*this, v, errorPrintOptions)
            ).withFrame(env, *e).debugThrow();
    } catch (Error & e) {
        e.addTrace(positions[pos], errorCtx);
        throw;
    }
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


Env * ExprAttrs::buildInheritFromEnv(EvalState & state, Env & up)
{
    Env & inheritEnv = state.allocEnv(inheritFromExprs->size());
    inheritEnv.up = &up;

    Displacement displ = 0;
    for (auto from : *inheritFromExprs)
        inheritEnv.values[displ++] = from->maybeThunk(state, up);

    return &inheritEnv;
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
        Env * inheritEnv = inheritFromExprs ? buildInheritFromEnv(state, env2) : nullptr;

        AttrDefs::iterator overrides = attrs.find(state.sOverrides);
        bool hasOverrides = overrides != attrs.end();

        /* The recursive attributes are evaluated in the new
           environment, while the inherited attributes are evaluated
           in the original environment. */
        Displacement displ = 0;
        for (auto & i : attrs) {
            Value * vAttr;
            if (hasOverrides && i.second.kind != AttrDef::Kind::Inherited) {
                vAttr = state.allocValue();
                mkThunk(*vAttr, *i.second.chooseByKind(&env2, &env, inheritEnv), i.second.e);
            } else
                vAttr = i.second.e->maybeThunk(state, *i.second.chooseByKind(&env2, &env, inheritEnv));
            env2.values[displ++] = vAttr;
            v.attrs->push_back(Attr(i.first, vAttr, i.second.pos));
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
            state.forceAttrs(*vOverrides, [&]() { return vOverrides->determinePos(noPos); }, "while evaluating the `__overrides` attribute");
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

    else {
        Env * inheritEnv = inheritFromExprs ? buildInheritFromEnv(state, env) : nullptr;
        for (auto & i : attrs) {
            v.attrs->push_back(Attr(
                    i.first,
                    i.second.e->maybeThunk(state, *i.second.chooseByKind(&env, &env, inheritEnv)),
                    i.second.pos));
        }
    }

    /* Dynamic attrs apply *after* rec and __overrides. */
    for (auto & i : dynamicAttrs) {
        Value nameVal;
        i.nameExpr->eval(state, *dynamicEnv, nameVal);
        state.forceValue(nameVal, i.pos);
        if (nameVal.type() == nNull)
            continue;
        state.forceStringNoCtx(nameVal, i.pos, "while evaluating the name of a dynamic attribute");
        auto nameSym = state.symbols.create(nameVal.string_view());
        Bindings::iterator j = v.attrs->find(nameSym);
        if (j != v.attrs->end())
            state.error<EvalError>("dynamic attribute '%1%' already defined at %2%", state.symbols[nameSym], state.positions[j->pos]).atPos(i.pos).withFrame(env, *this).debugThrow();

        i.valueExpr->setName(nameSym);
        /* Keep sorted order so find can catch duplicates */
        v.attrs->push_back(Attr(nameSym, i.valueExpr->maybeThunk(state, *dynamicEnv), i.pos));
        v.attrs->sort(); // FIXME: inefficient
    }

    v.attrs->pos = pos;
}


void ExprLet::eval(EvalState & state, Env & env, Value & v)
{
    /* Create a new environment that contains the attributes in this
       `let'. */
    Env & env2(state.allocEnv(attrs->attrs.size()));
    env2.up = &env;

    Env * inheritEnv = attrs->inheritFromExprs ? attrs->buildInheritFromEnv(state, env2) : nullptr;

    /* The recursive attributes are evaluated in the new environment,
       while the inherited attributes are evaluated in the original
       environment. */
    Displacement displ = 0;
    for (auto & i : attrs->attrs) {
        env2.values[displ++] = i.second.e->maybeThunk(
            state,
            *i.second.chooseByKind(&env2, &env, inheritEnv));
    }

    auto dts = state.debugRepl
        ? makeDebugTraceStacker(
            state,
            *this,
            env2,
            getPos()
                ? std::make_shared<Pos>(state.positions[getPos()])
                : nullptr,
            "while evaluating a '%1%' expression",
            "let"
        )
        : nullptr;

    body->eval(state, env2, v);
}


void ExprList::eval(EvalState & state, Env & env, Value & v)
{
    auto list = state.buildList(elems.size());
    for (const auto & [n, v2] : enumerate(list))
        v2 = elems[n]->maybeThunk(state, env);
    v.mkList(list);
}


Value * ExprList::maybeThunk(EvalState & state, Env & env)
{
    if (elems.empty()) {
        return &state.vEmptyList;
    }
    return Expr::maybeThunk(state, env);
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
            out << state.symbols[getName(i, state, env)];
        } catch (Error & e) {
            assert(!i.symbol);
            out << "\"${";
            i.expr->show(state.symbols, out);
            out << "}\"";
        }
    }
    return out.str();
}


void ExprSelect::eval(EvalState & state, Env & env, Value & v)
{
    Value vTmp;
    PosIdx pos2;
    Value * vAttrs = &vTmp;

    e->eval(state, env, vTmp);

    try {
        auto dts = state.debugRepl
            ? makeDebugTraceStacker(
                state,
                *this,
                env,
                state.positions[getPos()],
                "while evaluating the attribute '%1%'",
                showAttrPath(state, env, attrPath))
            : nullptr;

        for (auto & i : attrPath) {
            state.nrLookups++;
            Bindings::iterator j;
            auto name = getName(i, state, env);
            if (def) {
                state.forceValue(*vAttrs, pos);
                if (vAttrs->type() != nAttrs ||
                    (j = vAttrs->attrs->find(name)) == vAttrs->attrs->end())
                {
                    def->eval(state, env, v);
                    return;
                }
            } else {
                state.forceAttrs(*vAttrs, pos, "while selecting an attribute");
                if ((j = vAttrs->attrs->find(name)) == vAttrs->attrs->end()) {
                    std::set<std::string> allAttrNames;
                    for (auto & attr : *vAttrs->attrs)
                        allAttrNames.insert(state.symbols[attr.name]);
                    auto suggestions = Suggestions::bestMatches(allAttrNames, state.symbols[name]);
                    state.error<EvalError>("attribute '%1%' missing", state.symbols[name])
                        .atPos(pos).withSuggestions(suggestions).withFrame(env, *this).debugThrow();
                }
            }
            vAttrs = j->value;
            pos2 = j->pos;
            if (state.countCalls) state.attrSelects[pos2]++;
        }

        state.forceValue(*vAttrs, (pos2 ? pos2 : this->pos ) );

    } catch (Error & e) {
        if (pos2) {
            auto pos2r = state.positions[pos2];
            auto origin = std::get_if<SourcePath>(&pos2r.origin);
            if (!(origin && *origin == state.derivationInternal))
                state.addErrorTrace(e, pos2, "while evaluating the attribute '%1%'",
                    showAttrPath(state, env, attrPath));
        }
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
        state.forceValue(*vAttrs, getPos());
        Bindings::iterator j;
        auto name = getName(i, state, env);
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

namespace {
/** Increments a count on construction and decrements on destruction.
 */
class CallDepth {
  size_t & count;
public:
  CallDepth(size_t & count) : count(count) {
    ++count;
  }
  ~CallDepth() {
    --count;
  }
};
};

void EvalState::callFunction(Value & fun, size_t nrArgs, Value * * args, Value & vRes, const PosIdx pos)
{
    if (callDepth > evalSettings.maxCallDepth)
        error<EvalError>("stack overflow; max-call-depth exceeded").atPos(pos).debugThrow();
    CallDepth _level(callDepth);

    auto trace = evalSettings.traceFunctionCalls
        ? std::make_unique<FunctionCallTrace>(positions[pos])
        : nullptr;

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
                (!lambda.arg ? 0 : 1) +
                (lambda.hasFormals() ? lambda.formals->formals.size() : 0);
            Env & env2(allocEnv(size));
            env2.up = vCur.lambda.env;

            Displacement displ = 0;

            if (!lambda.hasFormals())
                env2.values[displ++] = args[0];
            else {
                try {
                    forceAttrs(*args[0], lambda.pos, "while evaluating the value passed for the lambda argument");
                } catch (Error & e) {
                    if (pos) e.addTrace(positions[pos], "from call site");
                    throw;
                }

                if (lambda.arg)
                    env2.values[displ++] = args[0];

                /* For each formal argument, get the actual argument.  If
                   there is no matching actual argument but the formal
                   argument has a default, use the default. */
                size_t attrsUsed = 0;
                for (auto & i : lambda.formals->formals) {
                    auto j = args[0]->attrs->get(i.name);
                    if (!j) {
                        if (!i.def) {
                            error<TypeError>("function '%1%' called without required argument '%2%'",
                                             (lambda.name ? std::string(symbols[lambda.name]) : "anonymous lambda"),
                                             symbols[i.name])
                                    .atPos(lambda.pos)
                                    .withTrace(pos, "from call site")
                                    .withFrame(*fun.lambda.env, lambda)
                                    .debugThrow();
                        }
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
                                formalNames.insert(symbols[formal.name]);
                            auto suggestions = Suggestions::bestMatches(formalNames, symbols[i.name]);
                            error<TypeError>("function '%1%' called with unexpected argument '%2%'",
                                             (lambda.name ? std::string(symbols[lambda.name]) : "anonymous lambda"),
                                             symbols[i.name])
                                .atPos(lambda.pos)
                                .withTrace(pos, "from call site")
                                .withSuggestions(suggestions)
                                .withFrame(*fun.lambda.env, lambda)
                                .debugThrow();
                        }
                    abort(); // can't happen
                }
            }

            nrFunctionCalls++;
            if (countCalls) incrFunctionCall(&lambda);

            /* Evaluate the body. */
            try {
                auto dts = debugRepl
                    ? makeDebugTraceStacker(
                        *this, *lambda.body, env2, positions[lambda.pos],
                        "while calling %s",
                        lambda.name
                        ? concatStrings("'", symbols[lambda.name], "'")
                        : "anonymous lambda")
                    : nullptr;

                lambda.body->eval(*this, env2, vCur);
            } catch (Error & e) {
                if (loggerSettings.showTrace.get()) {
                    addErrorTrace(
                        e,
                        lambda.pos,
                        "while calling %s",
                        lambda.name
                        ? concatStrings("'", symbols[lambda.name], "'")
                        : "anonymous lambda");
                    if (pos) addErrorTrace(e, pos, "from call site");
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
                auto * fn = vCur.primOp;

                nrPrimOpCalls++;
                if (countCalls) primOpCalls[fn->name]++;

                try {
                    fn->fun(*this, vCur.determinePos(noPos), args, vCur);
                } catch (Error & e) {
                    if (fn->addTrace)
                        addErrorTrace(e, pos, "while calling the '%1%' builtin", fn->name);
                    throw;
                }

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

                Value * vArgs[maxPrimOpArity];
                auto n = argsDone;
                for (Value * arg = &vCur; arg->isPrimOpApp(); arg = arg->primOpApp.left)
                    vArgs[--n] = arg->primOpApp.right;

                for (size_t i = 0; i < argsLeft; ++i)
                    vArgs[argsDone + i] = args[i];

                auto fn = primOp->primOp;
                nrPrimOpCalls++;
                if (countCalls) primOpCalls[fn->name]++;

                try {
                    // TODO:
                    // 1. Unify this and above code. Heavily redundant.
                    // 2. Create a fake env (arg1, arg2, etc.) and a fake expr (arg1: arg2: etc: builtins.name arg1 arg2 etc)
                    //    so the debugger allows to inspect the wrong parameters passed to the builtin.
                    fn->fun(*this, vCur.determinePos(noPos), vArgs, vCur);
                } catch (Error & e) {
                    if (fn->addTrace)
                        addErrorTrace(e, pos, "while calling the '%1%' builtin", fn->name);
                    throw;
                }

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
            try {
                callFunction(*functor->value, 2, args2, vCur, functor->pos);
            } catch (Error & e) {
                e.addTrace(positions[pos], "while calling a functor (an attribute set with a '__functor' attribute)");
                throw;
            }
            nrArgs--;
            args++;
        }

        else
            error<TypeError>(
                    "attempt to call something which is not a function but %1%: %2%",
                    showType(vCur),
                    ValuePrinter(*this, vCur, errorPrintOptions))
                .atPos(pos)
                .debugThrow();
    }

    vRes = vCur;
}


void ExprCall::eval(EvalState & state, Env & env, Value & v)
{
    auto dts = state.debugRepl
        ? makeDebugTraceStacker(
            state,
            *this,
            env,
            getPos()
                ? std::make_shared<Pos>(state.positions[getPos()])
                : nullptr,
            "while calling a function"
        )
        : nullptr;

    Value vFun;
    fun->eval(state, env, vFun);

    // Empirical arity of Nixpkgs lambdas by regex e.g. ([a-zA-Z]+:(\s|(/\*.*\/)|(#.*\n))*){5}
    // 2: over 4000
    // 3: about 300
    // 4: about 60
    // 5: under 10
    // This excluded attrset lambdas (`{...}:`). Contributions of mixed lambdas appears insignificant at ~150 total.
    SmallValueVector<4> vArgs(args.size());
    for (size_t i = 0; i < args.size(); ++i)
        vArgs[i] = args[i]->maybeThunk(state, env);

    state.callFunction(vFun, args.size(), vArgs.data(), v, pos);
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
                error<MissingArgumentError>(R"(cannot evaluate a function that has an argument without a value ('%1%')
Nix attempted to evaluate a function as a top level expression; in
this case it must have its arguments supplied either by default
values, or passed explicitly with '--arg' or '--argstr'. See
https://nixos.org/manual/nix/stable/language/constructs.html#functions.)", symbols[i.name])
                    .atPos(i.pos).withFrame(*fun.lambda.env, *fun.lambda.fun).debugThrow();
            }
        }
    }

    callFunction(fun, allocValue()->mkAttrs(attrs), res, pos);
}


void ExprWith::eval(EvalState & state, Env & env, Value & v)
{
    Env & env2(state.allocEnv(1));
    env2.up = &env;
    env2.values[0] = attrs->maybeThunk(state, env);

    body->eval(state, env2, v);
}


void ExprIf::eval(EvalState & state, Env & env, Value & v)
{
    // We cheat in the parser, and pass the position of the condition as the position of the if itself.
    (state.evalBool(env, cond, pos, "while evaluating a branch condition") ? then : else_)->eval(state, env, v);
}


void ExprAssert::eval(EvalState & state, Env & env, Value & v)
{
    if (!state.evalBool(env, cond, pos, "in the condition of the assert statement")) {
        std::ostringstream out;
        cond->show(state.symbols, out);
        state.error<AssertionError>("assertion '%1%' failed", out.str()).atPos(pos).withFrame(env, *this).debugThrow();
    }
    body->eval(state, env, v);
}


void ExprOpNot::eval(EvalState & state, Env & env, Value & v)
{
    v.mkBool(!state.evalBool(env, e, getPos(), "in the argument of the not operator")); // XXX: FIXME: !
}


void ExprOpEq::eval(EvalState & state, Env & env, Value & v)
{
    Value v1; e1->eval(state, env, v1);
    Value v2; e2->eval(state, env, v2);
    v.mkBool(state.eqValues(v1, v2, pos, "while testing two values for equality"));
}


void ExprOpNEq::eval(EvalState & state, Env & env, Value & v)
{
    Value v1; e1->eval(state, env, v1);
    Value v2; e2->eval(state, env, v2);
    v.mkBool(!state.eqValues(v1, v2, pos, "while testing two values for inequality"));
}


void ExprOpAnd::eval(EvalState & state, Env & env, Value & v)
{
    v.mkBool(state.evalBool(env, e1, pos, "in the left operand of the AND (&&) operator") && state.evalBool(env, e2, pos, "in the right operand of the AND (&&) operator"));
}


void ExprOpOr::eval(EvalState & state, Env & env, Value & v)
{
    v.mkBool(state.evalBool(env, e1, pos, "in the left operand of the OR (||) operator") || state.evalBool(env, e2, pos, "in the right operand of the OR (||) operator"));
}


void ExprOpImpl::eval(EvalState & state, Env & env, Value & v)
{
    v.mkBool(!state.evalBool(env, e1, pos, "in the left operand of the IMPL (->) operator") || state.evalBool(env, e2, pos, "in the right operand of the IMPL (->) operator"));
}


void ExprOpUpdate::eval(EvalState & state, Env & env, Value & v)
{
    Value v1, v2;
    state.evalAttrs(env, e1, v1, pos, "in the left operand of the update (//) operator");
    state.evalAttrs(env, e2, v2, pos, "in the right operand of the update (//) operator");

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
    state.concatLists(v, 2, lists, pos, "while evaluating one of the elements to concatenate");
}


void EvalState::concatLists(Value & v, size_t nrLists, Value * const * lists, const PosIdx pos, std::string_view errorCtx)
{
    nrListConcats++;

    Value * nonEmpty = 0;
    size_t len = 0;
    for (size_t n = 0; n < nrLists; ++n) {
        forceList(*lists[n], pos, errorCtx);
        auto l = lists[n]->listSize();
        len += l;
        if (l) nonEmpty = lists[n];
    }

    if (nonEmpty && len == nonEmpty->listSize()) {
        v = *nonEmpty;
        return;
    }

    auto list = buildList(len);
    auto out = list.elems;
    for (size_t n = 0, pos = 0; n < nrLists; ++n) {
        auto l = lists[n]->listSize();
        if (l)
            memcpy(out + pos, lists[n]->listElems(), l * sizeof(Value *));
        pos += l;
    }
    v.mkList(list);
}


void ExprConcatStrings::eval(EvalState & state, Env & env, Value & v)
{
    NixStringContext context;
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

    // List of returned strings. References to these Values must NOT be persisted.
    SmallTemporaryValueVector<conservativeStackReservation> values(es->size());
    Value * vTmpP = values.data();

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
                state.error<EvalError>("cannot add %1% to an integer", showType(vTmp)).atPos(i_pos).withFrame(env, *this).debugThrow();
        } else if (firstType == nFloat) {
            if (vTmp.type() == nInt) {
                nf += vTmp.integer;
            } else if (vTmp.type() == nFloat) {
                nf += vTmp.fpoint;
            } else
                state.error<EvalError>("cannot add %1% to a float", showType(vTmp)).atPos(i_pos).withFrame(env, *this).debugThrow();
        } else {
            if (s.empty()) s.reserve(es->size());
            /* skip canonization of first path, which would only be not
            canonized in the first place if it's coming from a ./${foo} type
            path */
            auto part = state.coerceToString(i_pos, vTmp, context,
                                             "while evaluating a path segment",
                                             false, firstType == nString, !first);
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
            state.error<EvalError>("a string that refers to a store path cannot be appended to a path").atPos(pos).withFrame(env, *this).debugThrow();
        v.mkPath(state.rootPath(CanonPath(canonPath(str()))));
    } else
        v.mkStringMove(c_str(), context);
}


void ExprPos::eval(EvalState & state, Env & env, Value & v)
{
    state.mkPos(v, pos);
}


void ExprBlackHole::eval(EvalState & state, Env & env, Value & v)
{
    state.error<InfiniteRecursionError>("infinite recursion encountered")
        .atPos(v.determinePos(noPos))
        .debugThrow();
}

// always force this to be separate, otherwise forceValue may inline it and take
// a massive perf hit
[[gnu::noinline]]
void EvalState::tryFixupBlackHolePos(Value & v, PosIdx pos)
{
    if (!v.isBlackhole())
        return;
    auto e = std::current_exception();
    try {
        std::rethrow_exception(e);
    } catch (InfiniteRecursionError & e) {
        e.atPos(positions[pos]);
    } catch (...) {
    }
}


void EvalState::forceValueDeep(Value & v)
{
    std::set<const Value *> seen;

    std::function<void(Value & v)> recurse;

    recurse = [&](Value & v) {
        if (!seen.insert(&v).second) return;

        forceValue(v, v.determinePos(noPos));

        if (v.type() == nAttrs) {
            for (auto & i : *v.attrs)
                try {
                    // If the value is a thunk, we're evaling. Otherwise no trace necessary.
                    auto dts = debugRepl && i.value->isThunk()
                        ? makeDebugTraceStacker(*this, *i.value->thunk.expr, *i.value->thunk.env, positions[i.pos],
                            "while evaluating the attribute '%1%'", symbols[i.name])
                        : nullptr;

                    recurse(*i.value);
                } catch (Error & e) {
                    addErrorTrace(e, i.pos, "while evaluating the attribute '%1%'", symbols[i.name]);
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


NixInt EvalState::forceInt(Value & v, const PosIdx pos, std::string_view errorCtx)
{
    try {
        forceValue(v, pos);
        if (v.type() != nInt)
            error<TypeError>(
                "expected an integer but found %1%: %2%",
                showType(v),
                ValuePrinter(*this, v, errorPrintOptions)
            ).atPos(pos).debugThrow();
        return v.integer;
    } catch (Error & e) {
        e.addTrace(positions[pos], errorCtx);
        throw;
    }

    return v.integer;
}


NixFloat EvalState::forceFloat(Value & v, const PosIdx pos, std::string_view errorCtx)
{
    try {
        forceValue(v, pos);
        if (v.type() == nInt)
            return v.integer;
        else if (v.type() != nFloat)
            error<TypeError>(
                "expected a float but found %1%: %2%",
                showType(v),
                ValuePrinter(*this, v, errorPrintOptions)
            ).atPos(pos).debugThrow();
        return v.fpoint;
    } catch (Error & e) {
        e.addTrace(positions[pos], errorCtx);
        throw;
    }
}


bool EvalState::forceBool(Value & v, const PosIdx pos, std::string_view errorCtx)
{
    try {
        forceValue(v, pos);
        if (v.type() != nBool)
            error<TypeError>(
                "expected a Boolean but found %1%: %2%",
                showType(v),
                ValuePrinter(*this, v, errorPrintOptions)
            ).atPos(pos).debugThrow();
        return v.boolean;
    } catch (Error & e) {
        e.addTrace(positions[pos], errorCtx);
        throw;
    }

    return v.boolean;
}


bool EvalState::isFunctor(Value & fun)
{
    return fun.type() == nAttrs && fun.attrs->find(sFunctor) != fun.attrs->end();
}


void EvalState::forceFunction(Value & v, const PosIdx pos, std::string_view errorCtx)
{
    try {
        forceValue(v, pos);
        if (v.type() != nFunction && !isFunctor(v))
            error<TypeError>(
                "expected a function but found %1%: %2%",
                showType(v),
                ValuePrinter(*this, v, errorPrintOptions)
            ).atPos(pos).debugThrow();
    } catch (Error & e) {
        e.addTrace(positions[pos], errorCtx);
        throw;
    }
}


std::string_view EvalState::forceString(Value & v, const PosIdx pos, std::string_view errorCtx)
{
    try {
        forceValue(v, pos);
        if (v.type() != nString)
            error<TypeError>(
                "expected a string but found %1%: %2%",
                showType(v),
                ValuePrinter(*this, v, errorPrintOptions)
            ).atPos(pos).debugThrow();
        return v.string_view();
    } catch (Error & e) {
        e.addTrace(positions[pos], errorCtx);
        throw;
    }
}


void copyContext(const Value & v, NixStringContext & context)
{
    if (v.string.context)
        for (const char * * p = v.string.context; *p; ++p)
            context.insert(NixStringContextElem::parse(*p));
}


std::string_view EvalState::forceString(Value & v, NixStringContext & context, const PosIdx pos, std::string_view errorCtx)
{
    auto s = forceString(v, pos, errorCtx);
    copyContext(v, context);
    return s;
}


std::string_view EvalState::forceStringNoCtx(Value & v, const PosIdx pos, std::string_view errorCtx)
{
    auto s = forceString(v, pos, errorCtx);
    if (v.context()) {
        error<EvalError>("the string '%1%' is not allowed to refer to a store path (such as '%2%')", v.string_view(), v.context()[0]).withTrace(pos, errorCtx).debugThrow();
    }
    return s;
}


bool EvalState::isDerivation(Value & v)
{
    if (v.type() != nAttrs) return false;
    Bindings::iterator i = v.attrs->find(sType);
    if (i == v.attrs->end()) return false;
    forceValue(*i->value, i->pos);
    if (i->value->type() != nString) return false;
    return i->value->string_view().compare("derivation") == 0;
}


std::optional<std::string> EvalState::tryAttrsToString(const PosIdx pos, Value & v,
    NixStringContext & context, bool coerceMore, bool copyToStore)
{
    auto i = v.attrs->find(sToString);
    if (i != v.attrs->end()) {
        Value v1;
        callFunction(*i->value, v, v1, pos);
        return coerceToString(pos, v1, context,
                "while evaluating the result of the `__toString` attribute",
                coerceMore, copyToStore).toOwned();
    }

    return {};
}

BackedStringView EvalState::coerceToString(
    const PosIdx pos,
    Value & v,
    NixStringContext & context,
    std::string_view errorCtx,
    bool coerceMore,
    bool copyToStore,
    bool canonicalizePath)
{
    forceValue(v, pos);

    if (v.type() == nString) {
        copyContext(v, context);
        return v.string_view();
    }

    if (v.type() == nPath) {
        return
            !canonicalizePath && !copyToStore
            ? // FIXME: hack to preserve path literals that end in a
              // slash, as in /foo/${x}.
              v._path.path
            : copyToStore
            ? store->printStorePath(copyPathToStore(context, v.path()))
            : std::string(v.path().path.abs());
    }

    if (v.type() == nAttrs) {
        auto maybeString = tryAttrsToString(pos, v, context, coerceMore, copyToStore);
        if (maybeString)
            return std::move(*maybeString);
        auto i = v.attrs->find(sOutPath);
        if (i == v.attrs->end()) {
            error<TypeError>(
                "cannot coerce %1% to a string: %2%",
                showType(v),
                ValuePrinter(*this, v, errorPrintOptions)
            )
                .withTrace(pos, errorCtx)
                .debugThrow();
        }
        return coerceToString(pos, *i->value, context, errorCtx,
                              coerceMore, copyToStore, canonicalizePath);
    }

    if (v.type() == nExternal) {
        try {
            return v.external->coerceToString(*this, pos, context, coerceMore, copyToStore);
        } catch (Error & e) {
            e.addTrace(nullptr, errorCtx);
            throw;
        }
    }

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
                try {
                    result += *coerceToString(pos, *v2, context,
                            "while evaluating one element of the list",
                            coerceMore, copyToStore, canonicalizePath);
                } catch (Error & e) {
                    e.addTrace(positions[pos], errorCtx);
                    throw;
                }
                if (n < v.listSize() - 1
                    /* !!! not quite correct */
                    && (!v2->isList() || v2->listSize() != 0))
                    result += " ";
            }
            return result;
        }
    }

    error<TypeError>("cannot coerce %1% to a string: %2%",
        showType(v),
        ValuePrinter(*this, v, errorPrintOptions)
    )
        .withTrace(pos, errorCtx)
        .debugThrow();
}


StorePath EvalState::copyPathToStore(NixStringContext & context, const SourcePath & path)
{
    if (nix::isDerivation(path.path.abs()))
        error<EvalError>("file names are not allowed to end in '%1%'", drvExtension).debugThrow();

    auto i = srcToStore.find(path);

    auto dstPath = i != srcToStore.end()
        ? i->second
        : [&]() {
            auto dstPath = fetchToStore(
                *store,
                path.resolveSymlinks(),
                settings.readOnlyMode ? FetchMode::DryRun : FetchMode::Copy,
                path.baseName(),
                FileIngestionMethod::Recursive,
                nullptr,
                repair);
            allowPath(dstPath);
            srcToStore.insert_or_assign(path, dstPath);
            printMsg(lvlChatty, "copied source '%1%' -> '%2%'", path, store->printStorePath(dstPath));
            return dstPath;
        }();

    context.insert(NixStringContextElem::Opaque {
        .path = dstPath
    });
    return dstPath;
}


SourcePath EvalState::coerceToPath(const PosIdx pos, Value & v, NixStringContext & context, std::string_view errorCtx)
{
    try {
        forceValue(v, pos);
    } catch (Error & e) {
        e.addTrace(positions[pos], errorCtx);
        throw;
    }

    /* Handle path values directly, without coercing to a string. */
    if (v.type() == nPath)
        return v.path();

    /* Similarly, handle __toString where the result may be a path
       value. */
    if (v.type() == nAttrs) {
        auto i = v.attrs->find(sToString);
        if (i != v.attrs->end()) {
            Value v1;
            callFunction(*i->value, v, v1, pos);
            return coerceToPath(pos, v1, context, errorCtx);
        }
    }

    /* Any other value should be coercable to a string, interpreted
       relative to the root filesystem. */
    auto path = coerceToString(pos, v, context, errorCtx, false, false, true).toOwned();
    if (path == "" || path[0] != '/')
        error<EvalError>("string '%1%' doesn't represent an absolute path", path).withTrace(pos, errorCtx).debugThrow();
    return rootPath(CanonPath(path));
}


StorePath EvalState::coerceToStorePath(const PosIdx pos, Value & v, NixStringContext & context, std::string_view errorCtx)
{
    auto path = coerceToString(pos, v, context, errorCtx, false, false, true).toOwned();
    if (auto storePath = store->maybeParseStorePath(path))
        return *storePath;
    error<EvalError>("path '%1%' is not in the Nix store", path).withTrace(pos, errorCtx).debugThrow();
}


std::pair<SingleDerivedPath, std::string_view> EvalState::coerceToSingleDerivedPathUnchecked(const PosIdx pos, Value & v, std::string_view errorCtx)
{
    NixStringContext context;
    auto s = forceString(v, context, pos, errorCtx);
    auto csize = context.size();
    if (csize != 1)
        error<EvalError>(
            "string '%s' has %d entries in its context. It should only have exactly one entry",
            s, csize)
            .withTrace(pos, errorCtx).debugThrow();
    auto derivedPath = std::visit(overloaded {
        [&](NixStringContextElem::Opaque && o) -> SingleDerivedPath {
            return std::move(o);
        },
        [&](NixStringContextElem::DrvDeep &&) -> SingleDerivedPath {
            error<EvalError>(
                "string '%s' has a context which refers to a complete source and binary closure. This is not supported at this time",
                s).withTrace(pos, errorCtx).debugThrow();
        },
        [&](NixStringContextElem::Built && b) -> SingleDerivedPath {
            return std::move(b);
        },
    }, ((NixStringContextElem &&) *context.begin()).raw);
    return {
        std::move(derivedPath),
        std::move(s),
    };
}


SingleDerivedPath EvalState::coerceToSingleDerivedPath(const PosIdx pos, Value & v, std::string_view errorCtx)
{
    auto [derivedPath, s_] = coerceToSingleDerivedPathUnchecked(pos, v, errorCtx);
    auto s = s_;
    auto sExpected = mkSingleDerivedPathStringRaw(derivedPath);
    if (s != sExpected) {
        /* `std::visit` is used here just to provide a more precise
           error message. */
        std::visit(overloaded {
            [&](const SingleDerivedPath::Opaque & o) {
                error<EvalError>(
                    "path string '%s' has context with the different path '%s'",
                    s, sExpected)
                    .withTrace(pos, errorCtx).debugThrow();
            },
            [&](const SingleDerivedPath::Built & b) {
                error<EvalError>(
                    "string '%s' has context with the output '%s' from derivation '%s', but the string is not the right placeholder for this derivation output. It should be '%s'",
                    s, b.output, b.drvPath->to_string(*store), sExpected)
                    .withTrace(pos, errorCtx).debugThrow();
            }
        }, derivedPath.raw());
    }
    return derivedPath;
}


bool EvalState::eqValues(Value & v1, Value & v2, const PosIdx pos, std::string_view errorCtx)
{
    forceValue(v1, pos);
    forceValue(v2, pos);

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
            return strcmp(v1.c_str(), v2.c_str()) == 0;

        case nPath:
            return
                // FIXME: compare accessors by their fingerprint.
                v1._path.accessor == v2._path.accessor
                && strcmp(v1._path.path, v2._path.path) == 0;

        case nNull:
            return true;

        case nList:
            if (v1.listSize() != v2.listSize()) return false;
            for (size_t n = 0; n < v1.listSize(); ++n)
                if (!eqValues(*v1.listElems()[n], *v2.listElems()[n], pos, errorCtx)) return false;
            return true;

        case nAttrs: {
            /* If both sets denote a derivation (type = "derivation"),
               then compare their outPaths. */
            if (isDerivation(v1) && isDerivation(v2)) {
                Bindings::iterator i = v1.attrs->find(sOutPath);
                Bindings::iterator j = v2.attrs->find(sOutPath);
                if (i != v1.attrs->end() && j != v2.attrs->end())
                    return eqValues(*i->value, *j->value, pos, errorCtx);
            }

            if (v1.attrs->size() != v2.attrs->size()) return false;

            /* Otherwise, compare the attributes one by one. */
            Bindings::iterator i, j;
            for (i = v1.attrs->begin(), j = v2.attrs->begin(); i != v1.attrs->end(); ++i, ++j)
                if (i->name != j->name || !eqValues(*i->value, *j->value, pos, errorCtx))
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

        case nThunk: // Must not be left by forceValue
        default:
            error<EvalError>("cannot compare %1% with %2%", showType(v1), showType(v2)).withTrace(pos, errorCtx).debugThrow();
    }
}

bool EvalState::fullGC() {
#if HAVE_BOEHMGC
    GC_gcollect();
    // Check that it ran. We might replace this with a version that uses more
    // of the boehm API to get this reliably, at a maintenance cost.
    // We use a 1K margin because technically this has a race condtion, but we
    // probably won't encounter it in practice, because the CLI isn't concurrent
    // like that.
    return GC_get_bytes_since_gc() < 1024;
#else
    return false;
#endif
}

void EvalState::maybePrintStats()
{
    bool showStats = getEnv("NIX_SHOW_STATS").value_or("0") != "0";

    if (showStats) {
        // Make the final heap size more deterministic.
#if HAVE_BOEHMGC
        if (!fullGC()) {
            warn("failed to perform a full GC before reporting stats");
        }
#endif
        printStatistics();
    }
}

void EvalState::printStatistics()
{
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

    auto outPath = getEnv("NIX_SHOW_STATS_PATH").value_or("-");
    std::fstream fs;
    if (outPath != "-")
        fs.open(outPath, std::fstream::out);
    json topObj = json::object();
    topObj["cpuTime"] = cpuTime;
    topObj["envs"] = {
        {"number", nrEnvs},
        {"elements", nrValuesInEnvs},
        {"bytes", bEnvs},
    };
    topObj["nrExprs"] = Expr::nrExprs;
    topObj["list"] = {
        {"elements", nrListElems},
        {"bytes", bLists},
        {"concats", nrListConcats},
    };
    topObj["values"] = {
        {"number", nrValues},
        {"bytes", bValues},
    };
    topObj["symbols"] = {
        {"number", symbols.size()},
        {"bytes", symbols.totalSize()},
    };
    topObj["sets"] = {
        {"number", nrAttrsets},
        {"bytes", bAttrsets},
        {"elements", nrAttrsInAttrsets},
    };
    topObj["sizes"] = {
        {"Env", sizeof(Env)},
        {"Value", sizeof(Value)},
        {"Bindings", sizeof(Bindings)},
        {"Attr", sizeof(Attr)},
    };
    topObj["nrOpUpdates"] = nrOpUpdates;
    topObj["nrOpUpdateValuesCopied"] = nrOpUpdateValuesCopied;
    topObj["nrThunks"] = nrThunks;
    topObj["nrAvoided"] = nrAvoided;
    topObj["nrLookups"] = nrLookups;
    topObj["nrPrimOpCalls"] = nrPrimOpCalls;
    topObj["nrFunctionCalls"] = nrFunctionCalls;
#if HAVE_BOEHMGC
    topObj["gc"] = {
        {"heapSize", heapSize},
        {"totalBytes", totalBytes},
    };
#endif

    if (countCalls) {
        topObj["primops"] = primOpCalls;
        {
            auto& list = topObj["functions"];
            list = json::array();
            for (auto & [fun, count] : functionCalls) {
                json obj = json::object();
                if (fun->name)
                    obj["name"] = (std::string_view) symbols[fun->name];
                else
                    obj["name"] = nullptr;
                if (auto pos = positions[fun->pos]) {
                    if (auto path = std::get_if<SourcePath>(&pos.origin))
                        obj["file"] = path->to_string();
                    obj["line"] = pos.line;
                    obj["column"] = pos.column;
                }
                obj["count"] = count;
                list.push_back(obj);
            }
        }
        {
            auto list = topObj["attributes"];
            list = json::array();
            for (auto & i : attrSelects) {
                json obj = json::object();
                if (auto pos = positions[i.first]) {
                    if (auto path = std::get_if<SourcePath>(&pos.origin))
                        obj["file"] = path->to_string();
                    obj["line"] = pos.line;
                    obj["column"] = pos.column;
                }
                obj["count"] = i.second;
                list.push_back(obj);
            }
        }
    }

    if (getEnv("NIX_SHOW_SYMBOLS").value_or("0") != "0") {
        // XXX: overrides earlier assignment
        topObj["symbols"] = json::array();
        auto &list = topObj["symbols"];
        symbols.dump([&](const std::string & s) { list.emplace_back(s); });
    }
    if (outPath == "-") {
        std::cerr << topObj.dump(2) << std::endl;
    } else {
        fs << topObj.dump(2) << std::endl;
    }
}


SourcePath resolveExprPath(SourcePath path)
{
    unsigned int followCount = 0, maxFollow = 1024;

    /* If `path' is a symlink, follow it.  This is so that relative
       path references work. */
    while (!path.path.isRoot()) {
        // Basic cycle/depth limit to avoid infinite loops.
        if (++followCount >= maxFollow)
            throw Error("too many symbolic links encountered while traversing the path '%s'", path);
        auto p = path.parent().resolveSymlinks() / path.baseName();
        if (p.lstat().type != InputAccessor::tSymlink) break;
        path = {path.accessor, CanonPath(p.readLink(), path.path.parent().value_or(CanonPath::root))};
    }

    /* If `path' refers to a directory, append `/default.nix'. */
    if (path.resolveSymlinks().lstat().type == InputAccessor::tDirectory)
        return path / "default.nix";

    return path;
}


Expr * EvalState::parseExprFromFile(const SourcePath & path)
{
    return parseExprFromFile(path, staticBaseEnv);
}


Expr * EvalState::parseExprFromFile(const SourcePath & path, std::shared_ptr<StaticEnv> & staticEnv)
{
    auto buffer = path.resolveSymlinks().readFile();
    // readFile hopefully have left some extra space for terminators
    buffer.append("\0\0", 2);
    return parse(buffer.data(), buffer.size(), Pos::Origin(path), path.parent(), staticEnv);
}


Expr * EvalState::parseExprFromString(std::string s_, const SourcePath & basePath, std::shared_ptr<StaticEnv> & staticEnv)
{
    // NOTE this method (and parseStdin) must take care to *fully copy* their input
    // into their respective Pos::Origin until the parser stops overwriting its input
    // data.
    auto s = make_ref<std::string>(s_);
    s_.append("\0\0", 2);
    return parse(s_.data(), s_.size(), Pos::String{.source = s}, basePath, staticEnv);
}


Expr * EvalState::parseExprFromString(std::string s, const SourcePath & basePath)
{
    return parseExprFromString(std::move(s), basePath, staticBaseEnv);
}


Expr * EvalState::parseStdin()
{
    // NOTE this method (and parseExprFromString) must take care to *fully copy* their
    // input into their respective Pos::Origin until the parser stops overwriting its
    // input data.
    //Activity act(*logger, lvlTalkative, "parsing standard input");
    auto buffer = drainFD(0);
    // drainFD should have left some extra space for terminators
    buffer.append("\0\0", 2);
    auto s = make_ref<std::string>(buffer);
    return parse(buffer.data(), buffer.size(), Pos::Stdin{.source = s}, rootPath("."), staticBaseEnv);
}


SourcePath EvalState::findFile(const std::string_view path)
{
    return findFile(searchPath, path);
}


SourcePath EvalState::findFile(const SearchPath & searchPath, const std::string_view path, const PosIdx pos)
{
    for (auto & i : searchPath.elements) {
        auto suffixOpt = i.prefix.suffixIfPotentialMatch(path);

        if (!suffixOpt) continue;
        auto suffix = *suffixOpt;

        auto rOpt = resolveSearchPathPath(i.path);
        if (!rOpt) continue;
        auto r = *rOpt;

        Path res = suffix == "" ? r : concatStrings(r, "/", suffix);
        if (pathExists(res)) return rootPath(CanonPath(canonPath(res)));
    }

    if (hasPrefix(path, "nix/"))
        return {corepkgsFS, CanonPath(path.substr(3))};

    error<ThrownError>(
        evalSettings.pureEval
            ? "cannot look up '<%s>' in pure evaluation mode (use '--impure' to override)"
            : "file '%s' was not found in the Nix search path (add it using $NIX_PATH or -I)",
        path
    ).atPos(pos).debugThrow();
}


std::optional<std::string> EvalState::resolveSearchPathPath(const SearchPath::Path & value0, bool initAccessControl)
{
    auto & value = value0.s;
    auto i = searchPathResolved.find(value);
    if (i != searchPathResolved.end()) return i->second;

    std::optional<std::string> res;

    if (EvalSettings::isPseudoUrl(value)) {
        try {
            auto accessor = fetchers::downloadTarball(
                EvalSettings::resolvePseudoUrl(value)).accessor;
            auto storePath = fetchToStore(*store, SourcePath(accessor), FetchMode::Copy);
            res = { store->toRealPath(storePath) };
        } catch (Error & e) {
            logWarning({
                .msg = HintFmt("Nix search path entry '%1%' cannot be downloaded, ignoring", value)
            });
        }
    }

    else if (hasPrefix(value, "flake:")) {
        experimentalFeatureSettings.require(Xp::Flakes);
        auto flakeRef = parseFlakeRef(value.substr(6), {}, true, false);
        debug("fetching flake search path element '%s''", value);
        auto storePath = flakeRef.resolve(store).fetchTree(store).first;
        res = { store->toRealPath(storePath) };
    }

    else {
        auto path = absPath(value);

        /* Allow access to paths in the search path. */
        if (initAccessControl) {
            allowPath(path);
            if (store->isInStore(path)) {
                try {
                    StorePathSet closure;
                    store->computeFSClosure(store->toStorePath(path).first, closure);
                    for (auto & p : closure)
                        allowPath(p);
                } catch (InvalidPath &) { }
            }
        }

        if (pathExists(path))
            res = { path };
        else {
            logWarning({
                .msg = HintFmt("Nix search path entry '%1%' does not exist, ignoring", value)
            });
            res = std::nullopt;
        }
    }

    if (res)
        debug("resolved search path element '%s' to '%s'", value, *res);
    else
        debug("failed to resolve search path element '%s'", value);

    searchPathResolved.emplace(value, res);
    return res;
}


Expr * EvalState::parse(
    char * text,
    size_t length,
    Pos::Origin origin,
    const SourcePath & basePath,
    std::shared_ptr<StaticEnv> & staticEnv)
{
    auto result = parseExprFromBuf(text, length, origin, basePath, symbols, positions, rootFS, exprSymbols);

    result->bindVars(*this, staticEnv);

    return result;
}


std::string ExternalValueBase::coerceToString(EvalState & state, const PosIdx & pos, NixStringContext & context, bool copyMore, bool copyToStore) const
{
    state.error<TypeError>(
        "cannot coerce %1% to a string: %2%", showType(), *this
    ).atPos(pos).debugThrow();
}


bool ExternalValueBase::operator==(const ExternalValueBase & b) const
{
    return false;
}


std::ostream & operator << (std::ostream & str, const ExternalValueBase & v) {
    return v.print(str);
}


}
