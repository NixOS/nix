#include "nix/store/derivations.hh"
#include "nix/store/downstream-placeholder.hh"
#include "nix/expr/eval-inline.hh"
#include "nix/expr/eval.hh"
#include "nix/expr/eval-settings.hh"
#include "nix/expr/gc-small-vector.hh"
#include "nix/expr/json-to-value.hh"
#include "nix/expr/static-string-data.hh"
#include "nix/store/globals.hh"
#include "nix/store/names.hh"
#include "nix/store/path-references.hh"
#include "nix/store/store-api.hh"
#include "nix/util/util.hh"
#include "nix/util/processes.hh"
#include "nix/expr/value-to-json.hh"
#include "nix/expr/value-to-xml.hh"
#include "nix/expr/primops.hh"
#include "nix/fetchers/fetch-to-store.hh"
#include "nix/util/sort.hh"

#include <boost/container/small_vector.hpp>
#include <boost/unordered/concurrent_flat_map.hpp>
#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>
#include <nlohmann/json.hpp>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <sstream>
#include <regex>
#include <utility>
#include <variant>

#ifndef _WIN32
#  include <dlfcn.h>
#endif

#include <cmath>

namespace nix {

RegisterPrimOp::PrimOps & RegisterPrimOp::primOps()
{
    static RegisterPrimOp::PrimOps primOps;
    return primOps;
}

/*************************************************************
 * Miscellaneous
 *************************************************************/

static inline Value * mkString(EvalState & state, const std::csub_match & match)
{
    Value * v = state.allocValue();
    v->mkString({match.first, match.second}, state.mem);
    return v;
}

std::string EvalState::realiseString(Value & s, StorePathSet * storePathsOutMaybe, bool isIFD, const PosIdx pos)
{
    nix::NixStringContext stringContext;
    auto rawStr = coerceToString(pos, s, stringContext, "while realising a string").toOwned();
    auto rewrites = realiseContext(stringContext, storePathsOutMaybe, isIFD);

    return nix::rewriteStrings(rawStr, rewrites);
}

StringMap EvalState::realiseContext(const NixStringContext & context, StorePathSet * maybePathsOut, bool isIFD)
{
    std::vector<DerivedPath::Built> drvs;
    StringMap res;

    for (auto & c : context) {
        auto ensureValid = [&](const StorePath & p) {
            if (!store->isValidPath(p))
                error<InvalidPathError>(store->printStorePath(p)).debugThrow();
        };
        std::visit(
            overloaded{
                [&](const NixStringContextElem::Built & b) {
                    drvs.push_back(
                        DerivedPath::Built{
                            .drvPath = b.drvPath,
                            .outputs = OutputsSpec::Names{b.output},
                        });
                    ensureValid(b.drvPath->getBaseStorePath());
                },
                [&](const NixStringContextElem::Opaque & o) {
                    ensureValid(o.path);
                    if (maybePathsOut)
                        maybePathsOut->emplace(o.path);
                },
                [&](const NixStringContextElem::DrvDeep & d) {
                    /* Treat same as Opaque */
                    ensureValid(d.drvPath);
                    if (maybePathsOut)
                        maybePathsOut->emplace(d.drvPath);
                },
            },
            c.raw);
    }

    if (drvs.empty())
        return {};

    if (isIFD) {
        if (!settings.enableImportFromDerivation)
            error<IFDError>(
                "cannot build '%1%' during evaluation because the option 'allow-import-from-derivation' is disabled",
                drvs.begin()->to_string(*store))
                .debugThrow();

        if (settings.traceImportFromDerivation)
            warn("built '%1%' during evaluation due to an import from derivation", drvs.begin()->to_string(*store));
    }

    /* Build/substitute the context. */
    std::vector<DerivedPath> buildReqs;
    buildReqs.reserve(drvs.size());
    for (auto & d : drvs)
        buildReqs.emplace_back(DerivedPath{d});
    buildStore->buildPaths(buildReqs, bmNormal, store);

    StorePathSet outputsToCopyAndAllow;

    for (auto & drv : drvs) {
        auto outputs = resolveDerivedPath(*buildStore, drv, &*store);
        for (auto & [outputName, outputPath] : outputs) {
            outputsToCopyAndAllow.insert(outputPath);
            if (maybePathsOut)
                maybePathsOut->emplace(outputPath);

            /* Get all the output paths corresponding to the placeholders we had */
            if (experimentalFeatureSettings.isEnabled(Xp::CaDerivations)) {
                res.insert_or_assign(
                    DownstreamPlaceholder::fromSingleDerivedPathBuilt(
                        SingleDerivedPath::Built{
                            .drvPath = drv.drvPath,
                            .output = outputName,
                        })
                        .render(),
                    buildStore->printStorePath(outputPath));
            }
        }
    }

    if (store != buildStore)
        copyClosure(*buildStore, *store, outputsToCopyAndAllow);

    if (isIFD) {
        /* Allow access to the output closures of this derivation. */
        for (auto & outputPath : outputsToCopyAndAllow)
            allowClosure(outputPath);
    }

    return res;
}

static SourcePath realisePath(
    EvalState & state,
    const PosIdx pos,
    Value & v,
    std::optional<SymlinkResolution> resolveSymlinks = SymlinkResolution::Full)
{
    NixStringContext context;

    auto path = state.coerceToPath(noPos, v, context, "while realising the context of a path");

    try {
        if (!context.empty() && path.accessor == state.rootFS) {
            auto rewrites = state.realiseContext(context);
            path = {path.accessor, CanonPath(rewriteStrings(path.path.abs(), rewrites))};
        }
        return resolveSymlinks ? path.resolveSymlinks(*resolveSymlinks) : path;
    } catch (Error & e) {
        e.addTrace(state.positions[pos], "while realising the context of path '%s'", path);
        throw;
    }
}

/**
 * Add and attribute to the given attribute map from the output name to
 * the output path, or a placeholder.
 *
 * Where possible the path is used, but for floating CA derivations we
 * may not know it. For sake of determinism we always assume we don't
 * and instead put in a place holder. In either case, however, the
 * string context will contain the drv path and output name, so
 * downstream derivations will have the proper dependency, and in
 * addition, before building, the placeholder will be rewritten to be
 * the actual path.
 *
 * The 'drv' and 'drvPath' outputs must correspond.
 */
static void mkOutputString(
    EvalState & state,
    BindingsBuilder & attrs,
    const StorePath & drvPath,
    const std::pair<std::string, DerivationOutput> & o)
{
    state.mkOutputString(
        attrs.alloc(o.first),
        SingleDerivedPath::Built{
            .drvPath = makeConstantStorePathRef(drvPath),
            .output = o.first,
        },
        o.second.path(*state.store, Derivation::nameFromPath(drvPath), o.first));
}

/**
 * `import` will parse a derivation when it imports a `.drv` file from the store.
 *
 * @param state The evaluation state.
 * @param pos The position of the `import` call.
 * @param path The path to the `.drv` to import.
 * @param storePath The path to the `.drv` to import.
 * @param v Return value
 */
void derivationToValue(
    EvalState & state, const PosIdx pos, const SourcePath & path, const StorePath & storePath, Value & v)
{
    auto path2 = path.path.abs();
    Derivation drv = state.store->readDerivation(storePath);
    auto attrs = state.buildBindings(3 + drv.outputs.size());
    attrs.alloc(state.s.drvPath)
        .mkString(
            path2,
            {
                NixStringContextElem::DrvDeep{.drvPath = storePath},
            },
            state.mem);
    attrs.alloc(state.s.name).mkString(drv.env["name"], state.mem);

    auto list = state.buildList(drv.outputs.size());
    for (const auto & [i, o] : enumerate(drv.outputs)) {
        mkOutputString(state, attrs, storePath, o);
        (list[i] = state.allocValue())->mkString(o.first, state.mem);
    }
    attrs.alloc(state.s.outputs).mkList(list);

    auto w = state.allocValue();
    w->mkAttrs(attrs);

    if (!state.vImportedDrvToDerivation) {
        state.vImportedDrvToDerivation = allocRootValue(state.allocValue());
        state.eval(
            state.parseExprFromString(
#include "imported-drv-to-derivation.nix.gen.hh"
                , state.rootPath(CanonPath::root)),
            **state.vImportedDrvToDerivation);
    }

    state.forceFunction(
        **state.vImportedDrvToDerivation, pos, "while evaluating imported-drv-to-derivation.nix.gen.hh");
    v.mkApp(*state.vImportedDrvToDerivation, w);
    state.forceAttrs(v, pos, "while calling imported-drv-to-derivation.nix.gen.hh");
}

/**
 * Import a Nix file with an alternate base scope, as `builtins.scopedImport` does.
 *
 * @param state The evaluation state.
 * @param pos The position of the import call.
 * @param path The path to the file to import.
 * @param vScope The base scope to use for the import.
 * @param v Return value
 */
static void scopedImport(EvalState & state, const PosIdx pos, SourcePath & path, Value * vScope, Value & v)
{
    state.forceAttrs(*vScope, pos, "while evaluating the first argument passed to builtins.scopedImport");

    Env * env = &state.mem.allocEnv(vScope->attrs()->size());
    env->up = &state.baseEnv;

    auto staticEnv = std::make_shared<StaticEnv>(nullptr, state.staticBaseEnv, vScope->attrs()->size());

    unsigned int displ = 0;
    for (auto & attr : *vScope->attrs()) {
        staticEnv->vars.emplace_back(attr.name, displ);
        env->values[displ++] = attr.value;
    }

    // No need to call staticEnv.sort(), because
    // args[0]->attrs is already sorted.

    printTalkative("evaluating file '%1%'", path);
    Expr * e = state.parseExprFromFile(resolveExprPath(path), staticEnv);

    e->eval(state, *env, v);
}

/* Load and evaluate an expression from path specified by the
   argument. */
static void import(EvalState & state, const PosIdx pos, Value & vPath, Value * vScope, Value & v)
{
    auto path = realisePath(state, pos, vPath, std::nullopt);
    auto path2 = path.path.abs();

    // FIXME
    auto isValidDerivationInStore = [&]() -> std::optional<StorePath> {
        if (!state.store->isStorePath(path2))
            return std::nullopt;
        auto storePath = state.store->parseStorePath(path2);
        if (!(state.store->isValidPath(storePath) && isDerivation(path2)))
            return std::nullopt;
        return storePath;
    };

    if (auto storePath = isValidDerivationInStore()) {
        derivationToValue(state, pos, path, *storePath, v);
    } else if (vScope) {
        scopedImport(state, pos, path, vScope, v);
    } else {
        state.evalFile(path, v);
    }
}

static RegisterPrimOp primop_scopedImport(
    {.name = "scopedImport",
     .args = {"scope", "path"},
     .doc = R"(
      Load, parse, and return the Nix expression in the file *path*, with the attributes from *scope* available as variables in the lexical scope of the imported file.

      This function is similar to [`import`](#builtins-import), but allows you to provide additional variables that will be available in the scope of the imported expression.
      The *scope* argument must be an attribute set; each attribute becomes a variable available in the imported file.
      Built-in functions and values remain accessible unless shadowed by *scope* attributes.

      > **Note**
      >
      > Variables from *scope* shadow built-ins with the same name, allowing you to override built-ins for the imported expression.

      > **Note**
      >
      > Unlike [`import`](#builtins-import), `scopedImport` does not memoize evaluation results.
      > While the parsing result may be reused, each call produces a distinct value.
      > This is observable through performance and side effects such as [`builtins.trace`](#builtins-trace).

      The *path* argument must meet the same criteria as an [interpolated expression](@docroot@/language/string-interpolation.md#interpolated-expression).

      If *path* is a directory, the file `default.nix` in that directory is used if it exists.

      > **Example**
      >
      > Create a file `greet.nix`:
      >
      > ```nix
      > # greet.nix
      > "${greeting}, ${name}!"
      > ```
      >
      > Import it with additional variables in scope:
      >
      > ```nix
      > scopedImport { greeting = "Hello"; name = "World"; } ./greet.nix
      > ```
      >
      >     "Hello, World!"

      Evaluation aborts if the file doesn't exist or contains an invalid Nix expression.
    )",
     .fun = [](EvalState & state, const PosIdx pos, Value ** args, Value & v) {
         import(state, pos, *args[1], args[0], v);
     }});

static RegisterPrimOp primop_import(
    {.name = "import",
     .args = {"path"},
     // TODO turn "normal path values" into link below
     .doc = R"(
      Load, parse, and return the Nix expression in the file *path*.

      > **Note**
      >
      > Unlike some languages, `import` is a regular function in Nix.

      The *path* argument must meet the same criteria as an [interpolated expression](@docroot@/language/string-interpolation.md#interpolated-expression).

      If *path* is a directory, the file `default.nix` in that directory is used if it exists.

      > **Example**
      >
      > ```console
      > $ echo 123 > default.nix
      > ```
      >
      > Import `default.nix` from the current directory.
      >
      > ```nix
      > import ./.
      > ```
      >
      >     123

      Evaluation aborts if the file doesn’t exist or contains an invalid Nix expression.

      A Nix expression loaded by `import` must not contain any *free variables*, that is, identifiers that are not defined in the Nix expression itself and are not built-in.
      Therefore, it cannot refer to variables that are in scope at the call site.

      > **Example**
      >
      > If you have a calling expression
      >
      > ```nix
      > rec {
      >   x = 123;
      >   y = import ./foo.nix;
      > }
      > ```
      >
      >  then the following `foo.nix` throws an error:
      >
      >  ```nix
      >  # foo.nix
      >  x + 456
      >  ```
      >
      >  since `x` is not in scope in `foo.nix`.
      > If you want `x` to be available in `foo.nix`, pass it as a function argument:
      >
      >  ```nix
      >  rec {
      >    x = 123;
      >    y = import ./foo.nix x;
      >  }
      >  ```
      >
      >  and
      >
      >  ```nix
      >  # foo.nix
      >  x: x + 456
      >  ```
      >
      >  The function argument doesn’t have to be called `x` in `foo.nix`; any name would work.
    )",
     .fun = [](EvalState & state, const PosIdx pos, Value ** args, Value & v) {
         import(state, pos, *args[0], nullptr, v);
     }});

#ifndef _WIN32 // TODO implement via DLL loading on Windows

/* Want reasonable symbol names, so extern C */
/* !!! Should we pass the Pos or the file name too? */
extern "C" typedef void (*ValueInitializer)(EvalState & state, Value & v);

/* Load a ValueInitializer from a DSO and return whatever it initializes */
void prim_importNative(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    auto path = realisePath(state, pos, *args[0]);

    std::string sym(
        state.forceStringNoCtx(*args[1], pos, "while evaluating the second argument passed to builtins.importNative"));

    void * handle = dlopen(path.path.c_str(), RTLD_LAZY | RTLD_LOCAL);
    if (!handle)
        state.error<EvalError>("could not open '%1%': %2%", path, dlerror()).debugThrow();

    dlerror();
    ValueInitializer func = (ValueInitializer) dlsym(handle, sym.c_str());
    if (!func) {
        char * message = dlerror();
        if (message)
            state.error<EvalError>("could not load symbol '%1%' from '%2%': %3%", sym, path, message).debugThrow();
        else
            state
                .error<EvalError>(
                    "symbol '%1%' from '%2%' resolved to NULL when a function pointer was expected", sym, path)
                .debugThrow();
    }

    (func)(state, v);

    /* We don't dlclose because v may be a primop referencing a function in the shared object file */
}

/* Execute a program and parse its output */
void prim_exec(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    state.forceList(*args[0], pos, "while evaluating the first argument passed to builtins.exec");
    auto elems = args[0]->listView();
    auto count = args[0]->listSize();
    if (count == 0)
        state.error<EvalError>("at least one argument to 'exec' required").atPos(pos).debugThrow();
    NixStringContext context;
    auto program = state
                       .coerceToString(
                           pos,
                           *elems[0],
                           context,
                           "while evaluating the first element of the argument passed to builtins.exec",
                           false,
                           false)
                       .toOwned();
    Strings commandArgs;
    for (size_t i = 1; i < count; ++i) {
        commandArgs.push_back(state
                                  .coerceToString(
                                      pos,
                                      *elems[i],
                                      context,
                                      "while evaluating an element of the argument passed to builtins.exec",
                                      false,
                                      false)
                                  .toOwned());
    }
    try {
        auto _ = state.realiseContext(context); // FIXME: Handle CA derivations
    } catch (InvalidPathError & e) {
        state.error<EvalError>("cannot execute '%1%', since path '%2%' is not valid", program, e.path)
            .atPos(pos)
            .debugThrow();
    }

    auto output = runProgram(program, true, commandArgs);
    Expr * parsed;
    try {
        parsed = state.parseExprFromString(std::move(output), state.rootPath(CanonPath::root));
    } catch (Error & e) {
        e.addTrace(state.positions[pos], "while parsing the output from '%1%'", program);
        throw;
    }
    try {
        state.eval(parsed, v);
    } catch (Error & e) {
        e.addTrace(state.positions[pos], "while evaluating the output from '%1%'", program);
        throw;
    }
}

#endif

/* Return a string representing the type of the expression. */
static void prim_typeOf(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    state.forceValue(*args[0], pos);
    switch (args[0]->type()) {
    case nInt:
        v.mkStringNoCopy("int"_sds);
        break;
    case nBool:
        v.mkStringNoCopy("bool"_sds);
        break;
    case nString:
        v.mkStringNoCopy("string"_sds);
        break;
    case nPath:
        v.mkStringNoCopy("path"_sds);
        break;
    case nNull:
        v.mkStringNoCopy("null"_sds);
        break;
    case nAttrs:
        v.mkStringNoCopy("set"_sds);
        break;
    case nList:
        v.mkStringNoCopy("list"_sds);
        break;
    case nFunction:
        v.mkStringNoCopy("lambda"_sds);
        break;
    case nExternal:
        v.mkString(args[0]->external()->typeOf(), state.mem);
        break;
    case nFloat:
        v.mkStringNoCopy("float"_sds);
        break;
    case nThunk:
        unreachable();
    }
}

static RegisterPrimOp primop_typeOf({
    .name = "__typeOf",
    .args = {"e"},
    .doc = R"(
      Return a string representing the type of the value *e*, namely
      `"int"`, `"bool"`, `"string"`, `"path"`, `"null"`, `"set"`,
      `"list"`, `"lambda"` or `"float"`.
    )",
    .fun = prim_typeOf,
});

/* Determine whether the argument is the null value. */
static void prim_isNull(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    state.forceValue(*args[0], pos);
    v.mkBool(args[0]->type() == nNull);
}

static RegisterPrimOp primop_isNull({
    .name = "isNull",
    .args = {"e"},
    .doc = R"(
      Return `true` if *e* evaluates to `null`, and `false` otherwise.

      This is equivalent to `e == null`.
    )",
    .fun = prim_isNull,
});

/* Determine whether the argument is a function. */
static void prim_isFunction(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    state.forceValue(*args[0], pos);
    v.mkBool(args[0]->type() == nFunction);
}

static RegisterPrimOp primop_isFunction({
    .name = "__isFunction",
    .args = {"e"},
    .doc = R"(
      Return `true` if *e* evaluates to a function, and `false` otherwise.
    )",
    .fun = prim_isFunction,
});

/* Determine whether the argument is an integer. */
static void prim_isInt(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    state.forceValue(*args[0], pos);
    v.mkBool(args[0]->type() == nInt);
}

static RegisterPrimOp primop_isInt({
    .name = "__isInt",
    .args = {"e"},
    .doc = R"(
      Return `true` if *e* evaluates to an integer, and `false` otherwise.
    )",
    .fun = prim_isInt,
});

/* Determine whether the argument is a float. */
static void prim_isFloat(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    state.forceValue(*args[0], pos);
    v.mkBool(args[0]->type() == nFloat);
}

static RegisterPrimOp primop_isFloat({
    .name = "__isFloat",
    .args = {"e"},
    .doc = R"(
      Return `true` if *e* evaluates to a float, and `false` otherwise.
    )",
    .fun = prim_isFloat,
});

/* Determine whether the argument is a string. */
static void prim_isString(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    state.forceValue(*args[0], pos);
    v.mkBool(args[0]->type() == nString);
}

static RegisterPrimOp primop_isString({
    .name = "__isString",
    .args = {"e"},
    .doc = R"(
      Return `true` if *e* evaluates to a string, and `false` otherwise.
    )",
    .fun = prim_isString,
});

/* Determine whether the argument is a Boolean. */
static void prim_isBool(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    state.forceValue(*args[0], pos);
    v.mkBool(args[0]->type() == nBool);
}

static RegisterPrimOp primop_isBool({
    .name = "__isBool",
    .args = {"e"},
    .doc = R"(
      Return `true` if *e* evaluates to a bool, and `false` otherwise.
    )",
    .fun = prim_isBool,
});

/* Determine whether the argument is a path. */
static void prim_isPath(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    state.forceValue(*args[0], pos);
    v.mkBool(args[0]->type() == nPath);
}

static RegisterPrimOp primop_isPath({
    .name = "__isPath",
    .args = {"e"},
    .doc = R"(
      Return `true` if *e* evaluates to a path, and `false` otherwise.
    )",
    .fun = prim_isPath,
});

template<typename Callable>
static inline void withExceptionContext(Trace trace, Callable && func)
{
    try {
        func();
    } catch (Error & e) {
        e.pushTrace(trace);
        throw;
    }
}

struct CompareValues
{
    EvalState & state;
    const PosIdx pos;
    const std::string_view errorCtx;

    CompareValues(EvalState & state, const PosIdx pos, const std::string_view && errorCtx)
        : state(state)
        , pos(pos)
        , errorCtx(errorCtx) {};

    bool operator()(Value * v1, Value * v2) const
    {
        return (*this)(v1, v2, errorCtx);
    }

    bool operator()(Value * v1, Value * v2, std::string_view errorCtx) const
    {
        try {
            if (v1->type() == nFloat && v2->type() == nInt)
                return v1->fpoint() < v2->integer().value;
            if (v1->type() == nInt && v2->type() == nFloat)
                return v1->integer().value < v2->fpoint();
            if (v1->type() != v2->type())
                state
                    .error<EvalError>(
                        "cannot compare %s with %s; values are %s and %s",
                        showType(*v1),
                        showType(*v2),
                        ValuePrinter(state, *v1, errorPrintOptions),
                        ValuePrinter(state, *v2, errorPrintOptions))
                    .debugThrow();
// Allow selecting a subset of enum values
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch-enum"
            switch (v1->type()) {
            case nInt:
                return v1->integer() < v2->integer();
            case nFloat:
                return v1->fpoint() < v2->fpoint();
            case nString:
                return v1->string_view() < v2->string_view();
            case nPath:
                // Note: we don't take the accessor into account
                // since it's not obvious how to compare them in a
                // reproducible way.
                return v1->pathStrView() < v2->pathStrView();
            case nList:
                // Lexicographic comparison
                for (size_t i = 0;; i++) {
                    if (i == v2->listSize()) {
                        return false;
                    } else if (i == v1->listSize()) {
                        return true;
                    } else if (!state.eqValues(*v1->listView()[i], *v2->listView()[i], pos, errorCtx)) {
                        return (*this)(v1->listView()[i], v2->listView()[i], "while comparing two list elements");
                    }
                }
            default:
                state
                    .error<EvalError>(
                        "cannot compare %s with %s; values of that type are incomparable (values are %s and %s)",
                        showType(*v1),
                        showType(*v2),
                        ValuePrinter(state, *v1, errorPrintOptions),
                        ValuePrinter(state, *v2, errorPrintOptions))
                    .debugThrow();
#pragma GCC diagnostic pop
            }
        } catch (Error & e) {
            if (!errorCtx.empty())
                e.addTrace(nullptr, errorCtx);
            throw;
        }
    }
};

namespace {

struct GenericClosureKeyTracker
{
    EvalState & state;
    size_t reserveHint;

    CompareValues cmp;

    /* Used to provide error traces when a key can't be compared against the existing set of keys. */
    Value * diagnosticKey = nullptr;
    Value * diagnosticElem = nullptr;

    struct StringKeyHash
    {
        size_t operator()(const Value * v) const noexcept
        {
            return StringViewHash{}(v->string_view());
        }
    };

    struct StringKeyEq
    {
        bool operator()(const Value * a, const Value * b) const noexcept
        {
            return a->string_view() == b->string_view();
        }
    };

    struct PathKeyHash
    {
        size_t operator()(const Value * v) const noexcept
        {
            return StringViewHash{}(v->pathStrView());
        }
    };

    struct PathKeyEq
    {
        bool operator()(const Value * a, const Value * b) const noexcept
        {
            return a->pathStrView() == b->pathStrView();
        }
    };

    struct IntKeyHash
    {
        size_t operator()(const Value * v) const noexcept
        {
            return std::hash<decltype(v->integer().value)>{}(v->integer().value);
        }
    };

    struct IntKeyEq
    {
        bool operator()(const Value * a, const Value * b) const noexcept
        {
            return a->integer().value == b->integer().value;
        }
    };

    using KeyAlloc = traceable_allocator<Value *>;
    using StringKeySet = boost::unordered_flat_set<Value *, StringKeyHash, StringKeyEq, KeyAlloc>;
    using PathKeySet = boost::unordered_flat_set<Value *, PathKeyHash, PathKeyEq, KeyAlloc>;

    using KeyToElemAlloc = traceable_allocator<std::pair<Value * const, Value *>>;
    using IntKeyToElem = boost::unordered_flat_map<Value *, Value *, IntKeyHash, IntKeyEq, KeyToElemAlloc>;

    using FallbackKeyToElem = std::map<Value *, Value *, CompareValues>;

    using KeyTrackerState = std::variant<std::monostate, StringKeySet, PathKeySet, IntKeyToElem, FallbackKeyToElem>;
    KeyTrackerState keyToElem;

    GenericClosureKeyTracker(EvalState & state, size_t reserveHint)
        : state(state)
        , reserveHint(reserveHint)
        , cmp(state, noPos, "")
    {
    }

    bool insert(Value * key, Value * elem)
    {
        const auto keyType = key->type();

        switch (keyToElem.index()) {
        case 0: { // std::monostate
            diagnosticKey = key;
            diagnosticElem = elem;
            if (keyType == nString) {
                auto & keys = keyToElem.emplace<StringKeySet>();
                keys.reserve(reserveHint);
                return keys.emplace(key).second;
            }
            if (keyType == nPath) {
                auto & keys = keyToElem.emplace<PathKeySet>();
                keys.reserve(reserveHint);
                return keys.emplace(key).second;
            }
            if (keyType == nInt) {
                auto & keys = keyToElem.emplace<IntKeyToElem>();
                keys.reserve(reserveHint);
                return keys.emplace(key, elem).second;
            }
            auto & keys = keyToElem.emplace<FallbackKeyToElem>(cmp);
            return insertFallbackKey(keys, key, elem);
        }

        case 1: { // StringKeySet
            if (keyType == nString)
                return std::get<StringKeySet>(keyToElem).emplace(key).second;
            throwCompareError(key, elem);
        }

        case 2: { // PathKeySet
            if (keyType == nPath)
                return std::get<PathKeySet>(keyToElem).emplace(key).second;
            throwCompareError(key, elem);
        }

        case 3: { // IntKeyToElem
            auto & keys = std::get<IntKeyToElem>(keyToElem);
            if (keyType == nInt)
                return keys.emplace(key, elem).second;
            if (keyType == nFloat) {
                auto & fallback = promoteIntKeysToFallback();
                return insertFallbackKey(fallback, key, elem);
            }
            throwCompareError(key, elem);
        }

        case 4: { // FallbackKeyToElem
            return insertFallbackKey(std::get<FallbackKeyToElem>(keyToElem), key, elem);
        }
        }

        unreachable();
    }

private:
    void addCompareTraces(Error & err, Value * elem, Value * otherElem)
    {
        // Traces are printed in reverse order; pre-swap them.
        err.addTrace(nullptr, "with element %s", ValuePrinter(state, *otherElem, errorPrintOptions));
        err.addTrace(nullptr, "while comparing element %s", ValuePrinter(state, *elem, errorPrintOptions));
    }

    [[noreturn]] void throwCompareError(Value * key, Value * elem)
    {
        try {
            cmp(key, diagnosticKey);
        } catch (Error & err) {
            addCompareTraces(err, elem, diagnosticElem);
            throw;
        }
        unreachable();
    }

    FallbackKeyToElem & promoteIntKeysToFallback()
    {
        auto intKeys = std::move(std::get<IntKeyToElem>(keyToElem));
        auto & fallback = keyToElem.emplace<FallbackKeyToElem>(cmp);

        for (auto & [existingKey, existingElem] : intKeys)
            fallback.insert({existingKey, existingElem});
        return fallback;
    }

    bool insertFallbackKey(FallbackKeyToElem & fallback, Value * key, Value * elem)
    {
        try {
            auto [it, inserted] = fallback.insert({key, elem});
            return inserted;
        } catch (Error & err) {
            // Try to find which element we're comparing against
            Value * otherElem = nullptr;
            for (auto & [otherKey, otherKeyElem] : fallback) {
                try {
                    cmp(key, otherKey);
                } catch (Error &) {
                    // Found the element we're comparing against
                    otherElem = otherKeyElem;
                    break;
                }
            }
            if (otherElem) {
                addCompareTraces(err, elem, otherElem);
            } else {
                // Couldn't find the specific element, just show current
                err.addTrace(
                    nullptr, "while checking key of element %s", ValuePrinter(state, *elem, errorPrintOptions));
            }
            throw;
        }
    }
};

} // namespace

static void prim_genericClosure(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    state.forceAttrs(*args[0], noPos, "while evaluating the first argument passed to builtins.genericClosure");

    /* Get the start set. */
    auto startSet = state.getAttr(
        state.s.startSet, args[0]->attrs(), "in the attrset passed as argument to builtins.genericClosure");

    state.forceList(
        *startSet->value,
        noPos,
        "while evaluating the 'startSet' attribute passed as argument to builtins.genericClosure");

    const auto startSetSize = startSet->value->listSize();
    if (startSetSize == 0) {
        v = *startSet->value;
        return;
    }

    ValueVector workQueue;
    workQueue.reserve(startSetSize);
    for (auto elem : startSet->value->listView())
        workQueue.push_back(elem);

    /* Get the operator. */
    auto op = state.getAttr(
        state.s.operator_, args[0]->attrs(), "in the attrset passed as argument to builtins.genericClosure");
    state.forceFunction(
        *op->value, noPos, "while evaluating the 'operator' attribute passed as argument to builtins.genericClosure");

    /* Construct the closure by applying the operator to elements of
       `workQueue', adding the result to `workQueue', continuing until
       no new elements are found. */
    ValueVector res;
    res.reserve(startSetSize);

    GenericClosureKeyTracker keys(state, startSetSize);

    for (size_t head = 0; head < workQueue.size(); ++head) {
        Value * e = std::exchange(workQueue[head], nullptr);

        try {
            state.forceAttrs(*e, noPos, "");
        } catch (Error & err) {
            err.addTrace(nullptr, "in genericClosure element %s", ValuePrinter(state, *e, errorPrintOptions));
            throw;
        }

        const Attr * key;
        try {
            key = state.getAttr(state.s.key, e->attrs(), "");
        } catch (Error & err) {
            err.addTrace(nullptr, "in genericClosure element %s", ValuePrinter(state, *e, errorPrintOptions));
            throw;
        }
        state.forceValue(*key->value, noPos);

        if (!keys.insert(key->value, e))
            continue;
        res.push_back(e);

        /* Call the `operator' function with `e' as argument. */
        Value newElements;
        try {
            state.callFunction(*op->value, {&e, 1}, newElements, noPos);
            state.forceList(
                newElements,
                noPos,
                "while evaluating the return value of the `operator` passed to builtins.genericClosure");

            /* Add the values returned by the operator to the work set. */
            for (auto elem : newElements.listView()) {
                state.forceValue(*elem, noPos); // "while evaluating one one of the elements returned by the `operator`
                                                // passed to builtins.genericClosure");
                workQueue.push_back(elem);
            }
        } catch (Error & err) {
            err.addTrace(
                nullptr,
                "while calling %s on genericClosure element %s",
                state.symbols[state.s.operator_],
                ValuePrinter(state, *e, errorPrintOptions));
            throw;
        }
    }

    /* Create the result list. */
    auto list = state.buildList(res.size());
    for (const auto & [n, i] : enumerate(res))
        list[n] = i;
    v.mkList(list);
}

static RegisterPrimOp primop_genericClosure(
    PrimOp{
        .name = "__genericClosure",
        .args = {"attrset"},
        .arity = 1,
        .doc = R"(
      `builtins.genericClosure` iteratively computes the transitive closure over an arbitrary relation defined by a function.

      It takes *attrset* with two attributes named `startSet` and `operator`, and returns a list of attribute sets:

      - `startSet`:
        The initial list of attribute sets.

      - `operator`:
        A function that takes an attribute set and returns a list of attribute sets.
        It defines how each item in the current set is processed and expanded into more items.

      Each attribute set in the list `startSet` and the list returned by `operator` must have an attribute `key`, which must support equality comparison.
      The value of `key` can be one of the following types:

      - [Int](@docroot@/language/types.md#type-int)
      - [Float](@docroot@/language/types.md#type-float)
      - [Boolean](@docroot@/language/types.md#type-bool)
      - [String](@docroot@/language/types.md#type-string)
      - [Path](@docroot@/language/types.md#type-path)
      - [List](@docroot@/language/types.md#type-list)

      The result is produced by calling the `operator` on each `item` that has not been called yet, including newly added items, until no new items are added.
      Items are compared by their `key` attribute.

      Common usages are:

      - Generating unique collections of items, such as dependency graphs.
      - Traversing through structures that may contain cycles or loops.
      - Processing data structures with complex internal relationships.

      > **Example**
      >
      > ```nix
      > builtins.genericClosure {
      >   startSet = [ {key = 5;} ];
      >   operator = item: [{
      >     key = if (item.key / 2 ) * 2 == item.key
      >          then item.key / 2
      >          else 3 * item.key + 1;
      >   }];
      > }
      > ```
      >
      > evaluates to
      >
      > ```nix
      > [ { key = 5; } { key = 16; } { key = 8; } { key = 4; } { key = 2; } { key = 1; } ]
      > ```
      )",
        .fun = prim_genericClosure,
    });

static RegisterPrimOp primop_break(
    {.name = "break",
     .args = {"v"},
     .doc = R"(
      In debug mode (enabled using `--debugger`), pause Nix expression evaluation and enter the REPL.
      Otherwise, return the argument `v`.
    )",
     .fun = [](EvalState & state, const PosIdx pos, Value ** args, Value & v) {
         if (state.canDebug()) {
             auto error = Error(
                 ErrorInfo{
                     .level = lvlInfo,
                     .msg = HintFmt("breakpoint reached"),
                     .pos = state.positions[pos],
                 });

             state.runDebugRepl(&error);
         }

         // Return the value we were passed.
         v = *args[0];
     }});

static RegisterPrimOp primop_abort(
    {.name = "abort",
     .args = {"s"},
     .doc = R"(
      Abort Nix expression evaluation and print the error message *s*.
    )",
     .fun = [](EvalState & state, const PosIdx pos, Value ** args, Value & v) {
         NixStringContext context;
         auto s =
             state.coerceToString(pos, *args[0], context, "while evaluating the error message passed to builtins.abort")
                 .toOwned();
         state.error<Abort>("evaluation aborted with the following error message: '%1%'", s)
             .setIsFromExpr()
             .debugThrow();
     }});

static RegisterPrimOp primop_throw(
    {.name = "throw",
     .args = {"s"},
     .doc = R"(
      Throw an error message *s*. This usually aborts Nix expression
      evaluation, but in `nix-env -qa` and other commands that try to
      evaluate a set of derivations to get information about those
      derivations, a derivation that throws an error is silently skipped
      (which is not the case for `abort`).
    )",
     .fun = [](EvalState & state, const PosIdx pos, Value ** args, Value & v) {
         NixStringContext context;
         auto s =
             state.coerceToString(pos, *args[0], context, "while evaluating the error message passed to builtin.throw")
                 .toOwned();
         state.error<ThrownError>(s).setIsFromExpr().debugThrow();
     }});

static void prim_addErrorContext(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    try {
        state.forceValue(*args[1], pos);
        v = *args[1];
    } catch (Error & e) {
        NixStringContext context;
        auto message = state
                           .coerceToString(
                               pos,
                               *args[0],
                               context,
                               "while evaluating the error message passed to builtins.addErrorContext",
                               false,
                               false)
                           .toOwned();
        e.addTrace(nullptr, HintFmt(message), TracePrint::Always);
        throw;
    }
}

static RegisterPrimOp primop_addErrorContext(
    PrimOp{
        .name = "__addErrorContext",
        .arity = 2,
        // The normal trace item is redundant
        .addTrace = false,
        .fun = prim_addErrorContext,
    });

static void prim_ceil(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    auto value = state.forceFloat(
        *args[0], args[0]->determinePos(pos), "while evaluating the first argument passed to builtins.ceil");
    auto ceilValue = ceil(value);
    bool isInt = args[0]->type() == nInt;
    constexpr NixFloat int_min = std::numeric_limits<NixInt::Inner>::min(); // power of 2, so that no rounding occurs
    if (ceilValue >= int_min && ceilValue < -int_min) {
        v.mkInt(ceilValue);
    } else if (isInt) {
        // a NixInt, e.g. INT64_MAX, can be rounded to -int_min due to the cast to NixFloat
        state
            .error<EvalError>(
                "Due to a bug (see https://github.com/NixOS/nix/issues/12899) the NixInt argument %1% caused undefined behavior in previous Nix versions.\n\tFuture Nix versions might implement the correct behavior.",
                args[0]->integer().value)
            .atPos(pos)
            .debugThrow();
    } else {
        state.error<EvalError>("NixFloat argument %1% is not in the range of NixInt", args[0]->fpoint())
            .atPos(pos)
            .debugThrow();
    }
    // `forceFloat` casts NixInt to NixFloat, but instead NixInt args shall be returned unmodified
    if (isInt) {
        auto arg = args[0]->integer();
        auto res = v.integer();
        if (arg != res) {
            state
                .error<EvalError>(
                    "Due to a bug (see https://github.com/NixOS/nix/issues/12899) a loss of precision occurred in previous Nix versions because the NixInt argument %1% was rounded to %2%.\n\tFuture Nix versions might implement the correct behavior.",
                    arg,
                    res)
                .atPos(pos)
                .debugThrow();
        }
    }
}

static RegisterPrimOp primop_ceil({
    .name = "__ceil",
    .args = {"number"},
    .doc = R"(
        Rounds and converts *number* to the next higher NixInt value if possible, i.e. `ceil *number* >= *number*` and
        `ceil *number* - *number* < 1`.

        An evaluation error is thrown, if there exists no such NixInt value `ceil *number*`.
        Due to bugs in previous Nix versions an evaluation error might be thrown, if the datatype of *number* is
        a NixInt and if `*number* < -9007199254740992` or `*number* > 9007199254740992`.

        If the datatype of *number* is neither a NixInt (signed 64-bit integer) nor a NixFloat
        (IEEE-754 double-precision floating-point number), an evaluation error is thrown.
    )",
    .fun = prim_ceil,
});

static void prim_floor(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    auto value = state.forceFloat(
        *args[0], args[0]->determinePos(pos), "while evaluating the first argument passed to builtins.floor");
    auto floorValue = floor(value);
    bool isInt = args[0]->type() == nInt;
    constexpr NixFloat int_min = std::numeric_limits<NixInt::Inner>::min(); // power of 2, so that no rounding occurs
    if (floorValue >= int_min && floorValue < -int_min) {
        v.mkInt(floorValue);
    } else if (isInt) {
        // a NixInt, e.g. INT64_MAX, can be rounded to -int_min due to the cast to NixFloat
        state
            .error<EvalError>(
                "Due to a bug (see https://github.com/NixOS/nix/issues/12899) the NixInt argument %1% caused undefined behavior in previous Nix versions.\n\tFuture Nix versions might implement the correct behavior.",
                args[0]->integer().value)
            .atPos(pos)
            .debugThrow();
    } else {
        state.error<EvalError>("NixFloat argument %1% is not in the range of NixInt", args[0]->fpoint())
            .atPos(pos)
            .debugThrow();
    }
    // `forceFloat` casts NixInt to NixFloat, but instead NixInt args shall be returned unmodified
    if (isInt) {
        auto arg = args[0]->integer();
        auto res = v.integer();
        if (arg != res) {
            state
                .error<EvalError>(
                    "Due to a bug (see https://github.com/NixOS/nix/issues/12899) a loss of precision occurred in previous Nix versions because the NixInt argument %1% was rounded to %2%.\n\tFuture Nix versions might implement the correct behavior.",
                    arg,
                    res)
                .atPos(pos)
                .debugThrow();
        }
    }
}

static RegisterPrimOp primop_floor({
    .name = "__floor",
    .args = {"number"},
    .doc = R"(
        Rounds and converts *number* to the next lower NixInt value if possible, i.e. `floor *number* <= *number*` and
        `*number* - floor *number* < 1`.

        An evaluation error is thrown, if there exists no such NixInt value `floor *number*`.
        Due to bugs in previous Nix versions an evaluation error might be thrown, if the datatype of *number* is
        a NixInt and if `*number* < -9007199254740992` or `*number* > 9007199254740992`.

        If the datatype of *number* is neither a NixInt (signed 64-bit integer) nor a NixFloat
        (IEEE-754 double-precision floating-point number), an evaluation error will be thrown.
    )",
    .fun = prim_floor,
});

/* Try evaluating the argument. Success => {success=true; value=something;},
 * else => {success=false; value=false;} */
static void prim_tryEval(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    auto attrs = state.buildBindings(2);

    /* increment state.trylevel, and decrement it when this function returns. */
    MaintainCount trylevel(state.trylevel);

    ReplExitStatus (*savedDebugRepl)(ref<EvalState> es, const ValMap & extraEnv) = nullptr;
    if (state.debugRepl && state.settings.ignoreExceptionsDuringTry) {
        /* to prevent starting the repl from exceptions within a tryEval, null it. */
        savedDebugRepl = state.debugRepl;
        state.debugRepl = nullptr;
    }

    try {
        state.forceValue(*args[0], pos);
        attrs.insert(state.s.value, args[0]);
        attrs.insert(state.symbols.create("success"), &Value::vTrue);
    } catch (AssertionError & e) {
        // `value = false;` is unfortunate but removing it is a breaking change.
        attrs.insert(state.s.value, &Value::vFalse);
        attrs.insert(state.symbols.create("success"), &Value::vFalse);
    }

    // restore the debugRepl pointer if we saved it earlier.
    if (savedDebugRepl)
        state.debugRepl = savedDebugRepl;

    v.mkAttrs(attrs);
}

static RegisterPrimOp primop_tryEval({
    .name = "__tryEval",
    .args = {"e"},
    .doc = R"(
      Try to shallowly evaluate *e*. Return a set containing the
      attributes `success` (`true` if *e* evaluated successfully,
      `false` if an error was thrown) and `value`, equalling *e* if
      successful and `false` otherwise. `tryEval` only prevents
      errors created by `throw` or `assert` from being thrown.
      Errors `tryEval` doesn't catch are, for example, those created
      by `abort` and type errors generated by builtins. Also note that
      this doesn't evaluate *e* deeply, so `let e = { x = throw ""; };
      in (builtins.tryEval e).success` is `true`. Using
      `builtins.deepSeq` one can get the expected result:
      `let e = { x = throw ""; }; in
      (builtins.tryEval (builtins.deepSeq e e)).success` is
      `false`.

      `tryEval` intentionally does not return the error message, because that risks bringing non-determinism into the evaluation result, and it would become very difficult to improve error reporting without breaking existing expressions.
      Instead, use [`builtins.addErrorContext`](@docroot@/language/builtins.md#builtins-addErrorContext) to add context to the error message, and use a Nix unit testing tool for testing.
    )",
    .fun = prim_tryEval,
});

/* Return an environment variable.  Use with care. */
static void prim_getEnv(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    std::string name(
        state.forceStringNoCtx(*args[0], pos, "while evaluating the first argument passed to builtins.getEnv"));
    v.mkString(state.settings.restrictEval || state.settings.pureEval ? "" : getEnv(name).value_or(""), state.mem);
}

static RegisterPrimOp primop_getEnv({
    .name = "__getEnv",
    .args = {"s"},
    .doc = R"(
      `getEnv` returns the value of the environment variable *s*, or an
      empty string if the variable doesn’t exist. This function should be
      used with care, as it can introduce all sorts of nasty environment
      dependencies in your Nix expression.

      `getEnv` is used in Nix Packages to locate the file
      `~/.nixpkgs/config.nix`, which contains user-local settings for Nix
      Packages. (That is, it does a `getEnv "HOME"` to locate the user’s
      home directory.)
    )",
    .fun = prim_getEnv,
});

/* Evaluate the first argument, then return the second argument. */
static void prim_seq(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    state.forceValue(*args[0], pos);
    state.forceValue(*args[1], pos);
    v = *args[1];
}

static RegisterPrimOp primop_seq({
    .name = "__seq",
    .args = {"e1", "e2"},
    .doc = R"(
      Evaluate *e1*, then evaluate and return *e2*. This ensures that a
      computation is strict in the value of *e1*.
    )",
    .fun = prim_seq,
});

/* Evaluate the first argument deeply (i.e. recursing into lists and
   attrsets), then return the second argument. */
static void prim_deepSeq(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    state.forceValueDeep(*args[0]);
    state.forceValue(*args[1], pos);
    v = *args[1];
}

static RegisterPrimOp primop_deepSeq({
    .name = "__deepSeq",
    .args = {"e1", "e2"},
    .doc = R"(
      This is like `seq e1 e2`, except that *e1* is evaluated *deeply*:
      if it’s a list or set, its elements or attributes are also
      evaluated recursively.
    )",
    .fun = prim_deepSeq,
});

/* Evaluate the first expression and print it on standard error.  Then
   return the second expression.  Useful for debugging. */
static void prim_trace(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    state.forceValue(*args[0], pos);
    if (args[0]->type() == nString)
        printError("trace: %1%", args[0]->string_view());
    else
        printError("trace: %1%", ValuePrinter(state, *args[0]));
    if (state.settings.builtinsTraceDebugger) {
        state.runDebugRepl(nullptr);
    }
    state.forceValue(*args[1], pos);
    v = *args[1];
}

static RegisterPrimOp primop_trace({
    .name = "__trace",
    .args = {"e1", "e2"},
    .doc = R"(
      Evaluate *e1* and print its abstract syntax representation on
      standard error. Then return *e2*. This function is useful for
      debugging.

      If the
      [`debugger-on-trace`](@docroot@/command-ref/conf-file.md#conf-debugger-on-trace)
      option is set to `true` and the `--debugger` flag is given, the
      interactive debugger is started when `trace` is called (like
      [`break`](@docroot@/language/builtins.md#builtins-break)).
    )",
    .fun = prim_trace,
});

static void prim_warn(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    // We only accept a string argument for now. The use case for pretty printing a value is covered by `trace`.
    // By rejecting non-strings we allow future versions to add more features without breaking existing code.
    auto msgStr =
        state.forceString(*args[0], pos, "while evaluating the first argument; the message passed to builtins.warn");

    {
        BaseError msg(std::string{msgStr});
        msg.atPos(state.positions[pos]);
        auto info = msg.info();
        info.level = lvlWarn;
        info.isFromExpr = true;
        logWarning(info);
    }

    if (state.settings.builtinsAbortOnWarn) {
        // Not an EvalError or subclass, which would cause the error to be stored in the eval cache.
        state.error<EvalBaseError>("aborting to reveal stack trace of warning, as abort-on-warn is set")
            .setIsFromExpr()
            .debugThrow();
    }
    if (state.settings.builtinsTraceDebugger || state.settings.builtinsDebuggerOnWarn) {
        state.runDebugRepl(nullptr);
    }
    state.forceValue(*args[1], pos);
    v = *args[1];
}

static RegisterPrimOp primop_warn({
    .name = "__warn",
    .args = {"e1", "e2"},
    .doc = R"(
      Evaluate *e1*, which must be a string, and print it on standard error as a warning.
      Then return *e2*.
      This function is useful for non-critical situations where attention is advisable.

      If the
      [`debugger-on-trace`](@docroot@/command-ref/conf-file.md#conf-debugger-on-trace)
      or [`debugger-on-warn`](@docroot@/command-ref/conf-file.md#conf-debugger-on-warn)
      option is set to `true` and the `--debugger` flag is given, the
      interactive debugger will be started when `warn` is called (like
      [`break`](@docroot@/language/builtins.md#builtins-break)).

      If the
      [`abort-on-warn`](@docroot@/command-ref/conf-file.md#conf-abort-on-warn)
      option is set, the evaluation is aborted after the warning is printed.
      This is useful to reveal the stack trace of the warning, when the context is non-interactive and a debugger can not be launched.
    )",
    .fun = prim_warn,
});

/* Takes two arguments and evaluates to the second one. Used as the
 * builtins.traceVerbose implementation when --trace-verbose is not enabled
 */
static void prim_second(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    state.forceValue(*args[1], pos);
    v = *args[1];
}

/*************************************************************
 * Derivations
 *************************************************************/

static void derivationStrictInternal(EvalState & state, std::string_view name, const Bindings * attrs, Value & v);

/* Construct (as a unobservable side effect) a Nix derivation
   expression that performs the derivation described by the argument
   set.  Returns the original set extended with the following
   attributes: `outPath' containing the primary output path of the
   derivation; `drvPath' containing the path of the Nix expression;
   and `type' set to `derivation' to indicate that this is a
   derivation. */
static void prim_derivationStrict(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    state.forceAttrs(*args[0], pos, "while evaluating the argument passed to builtins.derivationStrict");

    auto attrs = args[0]->attrs();

    /* Figure out the name first (for stack backtraces). */
    auto nameAttr =
        state.getAttr(state.s.name, attrs, "in the attrset passed as argument to builtins.derivationStrict");

    std::string_view drvName;
    try {
        drvName = state.forceStringNoCtx(
            *nameAttr->value, pos, "while evaluating the `name` attribute passed to builtins.derivationStrict");
    } catch (Error & e) {
        e.addTrace(state.positions[nameAttr->pos], "while evaluating the derivation attribute 'name'");
        throw;
    }

    try {
        derivationStrictInternal(state, drvName, attrs, v);
    } catch (Error & e) {
        Pos pos = state.positions[nameAttr->pos];
        /*
         * Here we make two abuses of the error system
         *
         * 1. We print the location as a string to avoid a code snippet being
         * printed. While the location of the name attribute is a good hint, the
         * exact code there is irrelevant.
         *
         * 2. We mark this trace as a frame trace, meaning that we stop printing
         * less important traces from now on. In particular, this prevents the
         * display of the automatic "while calling builtins.derivationStrict"
         * trace, which is of little use for the public we target here.
         *
         * Please keep in mind that error reporting is done on a best-effort
         * basis in nix. There is no accurate location for a derivation, as it
         * often results from the composition of several functions
         * (derivationStrict, derivation, mkDerivation, mkPythonModule, etc.)
         */
        e.addTrace(
            nullptr,
            HintFmt(
                "while evaluating derivation '%s'\n"
                "  whose name attribute is located at %s",
                drvName,
                pos));
        throw;
    }
}

/**
 * Early validation for the derivation name, for better error message.
 * It is checked again when constructing store paths.
 *
 * @todo Check that the `.drv` suffix also fits.
 */
static void checkDerivationName(EvalState & state, std::string_view drvName)
{
    try {
        checkName(drvName);
    } catch (BadStorePathName & e) {
        // "Please pass a different name": Users may not be aware that they can
        //     pass a different one, in functions like `fetchurl` where the name
        //     is optional.
        // Note that Nixpkgs generally won't trigger this, because `mkDerivation`
        // sanitizes the name.
        state
            .error<EvalError>(
                "invalid derivation name: %s. Please pass a different '%s'.", Uncolored(e.message()), "name")
            .debugThrow();
    }
}

static void derivationStrictInternal(EvalState & state, std::string_view drvName, const Bindings * attrs, Value & v)
{
    checkDerivationName(state, drvName);

    /* Check whether attributes should be passed as a JSON file. */
    using nlohmann::json;
    std::optional<StructuredAttrs> jsonObject;
    auto pos = v.determinePos(noPos);
    auto attr = attrs->get(state.s.structuredAttrs);
    if (attr
        && state.forceBool(
            *attr->value,
            pos,
            "while evaluating the `__structuredAttrs` "
            "attribute passed to builtins.derivationStrict"))
        jsonObject = StructuredAttrs{};

    /* Check whether null attributes should be ignored. */
    bool ignoreNulls = false;
    attr = attrs->get(state.s.ignoreNulls);
    if (attr)
        ignoreNulls = state.forceBool(
            *attr->value,
            pos,
            "while evaluating the `__ignoreNulls` attribute "
            "passed to builtins.derivationStrict");

    /* Build the derivation expression by processing the attributes. */
    Derivation drv;
    drv.name = drvName;

    NixStringContext context;

    bool contentAddressed = false;
    bool isImpure = false;
    std::optional<std::string> outputHash;
    std::optional<HashAlgorithm> outputHashAlgo;
    std::optional<ContentAddressMethod> ingestionMethod;

    StringSet outputs;
    outputs.insert("out");

    for (auto & i : attrs->lexicographicOrder(state.symbols)) {
        if (i->name == state.s.ignoreNulls)
            continue;
        auto key = state.symbols[i->name];
        vomit("processing attribute '%1%'", key);

        auto handleHashMode = [&](const std::string_view s) {
            if (s == "recursive") {
                // back compat, new name is "nar"
                ingestionMethod = ContentAddressMethod::Raw::NixArchive;
            } else
                try {
                    ingestionMethod = ContentAddressMethod::parse(s);
                } catch (UsageError &) {
                    state.error<EvalError>("invalid value '%s' for 'outputHashMode' attribute", s)
                        .atPos(v)
                        .debugThrow();
                }
            if (ingestionMethod == ContentAddressMethod::Raw::Text)
                experimentalFeatureSettings.require(
                    Xp::DynamicDerivations, fmt("text-hashed derivation '%s', outputHashMode = \"text\"", drvName));
            if (ingestionMethod == ContentAddressMethod::Raw::Git)
                experimentalFeatureSettings.require(Xp::GitHashing);
        };

        auto handleOutputs = [&](const Strings & ss) {
            outputs.clear();
            for (auto & j : ss) {
                if (outputs.find(j) != outputs.end())
                    state.error<EvalError>("duplicate derivation output '%1%'", j).atPos(v).debugThrow();
                /* !!! Check whether j is a valid attribute
                   name. */
                /* Derivations cannot be named ‘drvPath’, because
                   we already have an attribute ‘drvPath’ in
                   the resulting set (see state.sDrvPath). */
                if (j == "drvPath")
                    state.error<EvalError>("invalid derivation output name 'drvPath'").atPos(v).debugThrow();
                outputs.insert(j);
            }
            if (outputs.empty())
                state.error<EvalError>("derivation cannot have an empty set of outputs").atPos(v).debugThrow();
        };

        try {
            // This try-catch block adds context for most errors.
            // Use this empty error context to signify that we defer to it.
            const std::string_view context_below("");

            if (ignoreNulls) {
                state.forceValue(*i->value, pos);
                if (i->value->type() == nNull)
                    continue;
            }

            switch (i->name.getId()) {
            case EvalState::s.contentAddressed.getId():
                if (state.forceBool(*i->value, pos, context_below)) {
                    contentAddressed = true;
                    experimentalFeatureSettings.require(Xp::CaDerivations);
                }
                break;
            case EvalState::s.impure.getId():
                if (state.forceBool(*i->value, pos, context_below)) {
                    isImpure = true;
                    experimentalFeatureSettings.require(Xp::ImpureDerivations);
                }
                break;
            /* The `args' attribute is special: it supplies the
               command-line arguments to the builder. */
            case EvalState::s.args.getId():
                state.forceList(*i->value, pos, context_below);
                for (auto elem : i->value->listView()) {
                    auto s = state
                                 .coerceToString(
                                     pos, *elem, context, "while evaluating an element of the argument list", true)
                                 .toOwned();
                    drv.args.push_back(s);
                }
                break;
            /* All other attributes are passed to the builder through
               the environment. */
            default:

                if (jsonObject) {

                    if (i->name == state.s.structuredAttrs)
                        continue;

                    jsonObject->structuredAttrs.emplace(key, printValueAsJSON(state, true, *i->value, pos, context));

                    switch (i->name.getId()) {
                    case EvalState::s.builder.getId():
                        drv.builder = state.forceString(*i->value, context, pos, context_below);
                        break;
                    case EvalState::s.system.getId():
                        drv.platform = state.forceStringNoCtx(*i->value, pos, context_below);
                        break;
                    case EvalState::s.outputHash.getId():
                        outputHash = state.forceStringNoCtx(*i->value, pos, context_below);
                        break;
                    case EvalState::s.outputHashAlgo.getId():
                        outputHashAlgo = parseHashAlgoOpt(state.forceStringNoCtx(*i->value, pos, context_below));
                        break;
                    case EvalState::s.outputHashMode.getId():
                        handleHashMode(state.forceStringNoCtx(*i->value, pos, context_below));
                        break;
                    case EvalState::s.outputs.getId(): {
                        /* Require 'outputs' to be a list of strings. */
                        state.forceList(*i->value, pos, context_below);
                        Strings ss;
                        for (auto elem : i->value->listView())
                            ss.emplace_back(state.forceStringNoCtx(*elem, pos, context_below));
                        handleOutputs(ss);
                        break;
                    }
                    default:
                        break;
                    }

                    switch (i->name.getId()) {
                    case EvalState::s.allowedReferences.getId():
                        warn(
                            "In a derivation named '%s', 'structuredAttrs' disables the effect of the derivation attribute 'allowedReferences'; use 'outputChecks.<output>.allowedReferences' instead",
                            drvName);
                        break;
                    case EvalState::s.allowedRequisites.getId():
                        warn(
                            "In a derivation named '%s', 'structuredAttrs' disables the effect of the derivation attribute 'allowedRequisites'; use 'outputChecks.<output>.allowedRequisites' instead",
                            drvName);
                        break;
                    case EvalState::s.disallowedReferences.getId():
                        warn(
                            "In a derivation named '%s', 'structuredAttrs' disables the effect of the derivation attribute 'disallowedReferences'; use 'outputChecks.<output>.disallowedReferences' instead",
                            drvName);
                        break;
                    case EvalState::s.disallowedRequisites.getId():
                        warn(
                            "In a derivation named '%s', 'structuredAttrs' disables the effect of the derivation attribute 'disallowedRequisites'; use 'outputChecks.<output>.disallowedRequisites' instead",
                            drvName);
                        break;
                    case EvalState::s.maxSize.getId():
                        warn(
                            "In a derivation named '%s', 'structuredAttrs' disables the effect of the derivation attribute 'maxSize'; use 'outputChecks.<output>.maxSize' instead",
                            drvName);
                        break;
                    case EvalState::s.maxClosureSize.getId():
                        warn(
                            "In a derivation named '%s', 'structuredAttrs' disables the effect of the derivation attribute 'maxClosureSize'; use 'outputChecks.<output>.maxClosureSize' instead",
                            drvName);
                        break;
                    default:
                        break;
                    }

                } else {
                    auto s = state.coerceToString(pos, *i->value, context, context_below, true).toOwned();
                    if (i->name == state.s.json) {
                        warn(
                            "In derivation '%s': setting structured attributes via '__json' is deprecated, and may be disallowed in future versions of Nix. Set '__structuredAttrs = true' instead.",
                            drvName);
                        drv.structuredAttrs = StructuredAttrs::parse(s);
                    } else {
                        drv.env.emplace(key, s);
                        switch (i->name.getId()) {
                        case EvalState::s.builder.getId():
                            drv.builder = std::move(s);
                            break;
                        case EvalState::s.system.getId():
                            drv.platform = std::move(s);
                            break;
                        case EvalState::s.outputHash.getId():
                            outputHash = std::move(s);
                            break;
                        case EvalState::s.outputHashAlgo.getId():
                            outputHashAlgo = parseHashAlgoOpt(s);
                            break;
                        case EvalState::s.outputHashMode.getId():
                            handleHashMode(s);
                            break;
                        case EvalState::s.outputs.getId():
                            handleOutputs(tokenizeString<Strings>(s));
                            break;
                        default:
                            break;
                        }
                    }
                }
                break;
            }

        } catch (Error & e) {
            e.addTrace(
                state.positions[i->pos], HintFmt("while evaluating attribute '%1%' of derivation '%2%'", key, drvName));
            throw;
        }
    }

    if (jsonObject) {
        /* The only other way `drv.structuredAttrs` can be set is when
           `jsonObject` is not set. */
        assert(!drv.structuredAttrs);
        drv.structuredAttrs = std::move(*jsonObject);
    }

    /* Everything in the context of the strings in the derivation
       attributes should be added as dependencies of the resulting
       derivation. */
    for (auto & c : context) {
        std::visit(
            overloaded{
                /* Since this allows the builder to gain access to every
                   path in the dependency graph of the derivation (including
                   all outputs), all paths in the graph must be added to
                   this derivation's list of inputs to ensure that they are
                   available when the builder runs. */
                [&](const NixStringContextElem::DrvDeep & d) {
                    /* !!! This doesn't work if readOnlyMode is set. */
                    StorePathSet refs;
                    state.store->computeFSClosure(d.drvPath, refs);
                    for (auto & j : refs) {
                        drv.inputSrcs.insert(j);
                        if (j.isDerivation()) {
                            drv.inputDrvs.map[j].value = state.store->readDerivation(j).outputNames();
                        }
                    }
                },
                [&](const NixStringContextElem::Built & b) {
                    drv.inputDrvs.ensureSlot(*b.drvPath).value.insert(b.output);
                },
                [&](const NixStringContextElem::Opaque & o) { drv.inputSrcs.insert(o.path); },
            },
            c.raw);
    }

    /* Do we have all required attributes? */
    if (drv.builder == "")
        state.error<EvalError>("required attribute 'builder' missing").atPos(v).debugThrow();

    if (drv.platform == "")
        state.error<EvalError>("required attribute 'system' missing").atPos(v).debugThrow();

    /* Check whether the derivation name is valid. */
    if (isDerivation(drvName)
        && !(
            ingestionMethod == ContentAddressMethod::Raw::Text && outputs.size() == 1 && *(outputs.begin()) == "out")) {
        state
            .error<EvalError>(
                "derivation names are allowed to end in '%s' only if they produce a single derivation file",
                drvExtension)
            .atPos(v)
            .debugThrow();
    }

    if (outputHash) {
        /* Handle fixed-output derivations.

           Ignore `__contentAddressed` because fixed output derivations are
           already content addressed. */
        if (outputs.size() != 1 || *(outputs.begin()) != "out")
            state.error<EvalError>("multiple outputs are not supported in fixed-output derivations")
                .atPos(v)
                .debugThrow();

        auto h = newHashAllowEmpty(*outputHash, outputHashAlgo);

        auto method = ingestionMethod.value_or(ContentAddressMethod::Raw::Flat);

        DerivationOutput::CAFixed dof{
            .ca =
                ContentAddress{
                    .method = std::move(method),
                    .hash = std::move(h),
                },
        };

        drv.env["out"] = state.store->printStorePath(dof.path(*state.store, drvName, "out"));
        drv.outputs.insert_or_assign("out", std::move(dof));
    }

    else if (contentAddressed || isImpure) {
        if (contentAddressed && isImpure)
            state.error<EvalError>("derivation cannot be both content-addressed and impure").atPos(v).debugThrow();

        auto ha = outputHashAlgo.value_or(HashAlgorithm::SHA256);
        auto method = ingestionMethod.value_or(ContentAddressMethod::Raw::NixArchive);

        for (auto & i : outputs) {
            drv.env[i] = hashPlaceholder(i);
            if (isImpure)
                drv.outputs.insert_or_assign(
                    i,
                    DerivationOutput::Impure{
                        .method = method,
                        .hashAlgo = ha,
                    });
            else
                drv.outputs.insert_or_assign(
                    i,
                    DerivationOutput::CAFloating{
                        .method = method,
                        .hashAlgo = ha,
                    });
        }
    }

    else {
        /* Compute a hash over the "masked" store derivation, which is
           the final one except that in the list of outputs, the
           output paths are empty strings, and the corresponding
           environment variables have an empty value.  This ensures
           that changes in the set of output names do get reflected in
           the hash. */
        for (auto & i : outputs) {
            drv.env[i] = "";
            drv.outputs.insert_or_assign(i, DerivationOutput::Deferred{});
        }

        drv.fillInOutputPaths(*state.store);
    }

    /* Write the resulting term into the Nix store directory. */
    auto drvPath = writeDerivation(*state.store, drv, state.repair);
    auto drvPathS = state.store->printStorePath(drvPath);

    printMsg(lvlChatty, "instantiated '%1%' -> '%2%'", drvName, drvPathS);

    /* Optimisation, but required in read-only mode! because in that
       case we don't actually write store derivations, so we can't
       read them later. */
    {
        auto h = hashDerivationModulo(*state.store, drv, false);
        drvHashes.insert_or_assign(drvPath, std::move(h));
    }

    auto result = state.buildBindings(1 + drv.outputs.size());
    result.alloc(state.s.drvPath)
        .mkString(
            drvPathS,
            {
                NixStringContextElem::DrvDeep{.drvPath = drvPath},
            },
            state.mem);
    for (auto & i : drv.outputs)
        mkOutputString(state, result, drvPath, i);

    v.mkAttrs(result);
}

static RegisterPrimOp primop_derivationStrict(
    PrimOp{
        .name = "derivationStrict",
        .arity = 1,
        .fun = prim_derivationStrict,
    });

/* Return a placeholder string for the specified output that will be
   substituted by the corresponding output path at build time. For
   example, 'placeholder "out"' returns the string
   /1rz4g4znpzjwh1xymhjpm42vipw92pr73vdgl6xs1hycac8kf2n9. At build
   time, any occurrence of this string in an derivation attribute will
   be replaced with the concrete path in the Nix store of the output
   ‘out’. */
static void prim_placeholder(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    v.mkString(
        hashPlaceholder(state.forceStringNoCtx(
            *args[0], pos, "while evaluating the first argument passed to builtins.placeholder")),
        state.mem);
}

static RegisterPrimOp primop_placeholder({
    .name = "placeholder",
    .args = {"output"},
    .doc = R"(
      Return an
      [output placeholder string](@docroot@/store/derivation/index.md#output-placeholder)
      for the specified *output* that will be substituted by the corresponding
      [output path](@docroot@/glossary.md#gloss-output-path)
      at build time.

      Typical outputs would be `"out"`, `"bin"` or `"dev"`.
    )",
    .fun = prim_placeholder,
});

/*************************************************************
 * Paths
 *************************************************************/

/* Convert the argument to a path and then to a string (confusing,
   eh?).  !!! obsolete? */
static void prim_toPath(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    NixStringContext context;
    auto path =
        state.coerceToPath(pos, *args[0], context, "while evaluating the first argument passed to builtins.toPath");
    v.mkString(path.path.abs(), context, state.mem);
}

static RegisterPrimOp primop_toPath({
    .name = "__toPath",
    .args = {"s"},
    .doc = R"(
      **DEPRECATED.** Use `/. + "/path"` to convert a string into an absolute
      path. For relative paths, use `./. + "/path"`.
    )",
    .fun = prim_toPath,
});

/* Allow a valid store path to be used in an expression.  This is
   useful in some generated expressions such as in nix-push, which
   generates a call to a function with an already existing store path
   as argument.  You don't want to use `toPath' here because it copies
   the path to the Nix store, which yields a copy like
   /nix/store/newhash-oldhash-oldname.  In the past, `toPath' had
   special case behaviour for store paths, but that created weird
   corner cases. */
static void prim_storePath(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    if (state.settings.pureEval)
        state.error<EvalError>("'%s' is not allowed in pure evaluation mode", "builtins.storePath")
            .atPos(pos)
            .debugThrow();

    NixStringContext context;
    auto path =
        state.coerceToPath(pos, *args[0], context, "while evaluating the first argument passed to 'builtins.storePath'")
            .path;
    /* Resolve symlinks in ‘path’, unless ‘path’ itself is a symlink
       directly in the store.  The latter condition is necessary so
       e.g. nix-push does the right thing. */
    if (!state.store->isStorePath(path.abs()))
        path = CanonPath(canonPath(path.abs(), true));
    if (!state.store->isInStore(path.abs()))
        state.error<EvalError>("path '%1%' is not in the Nix store", path).atPos(pos).debugThrow();
    auto path2 = state.store->toStorePath(path.abs()).first;
    if (!settings.readOnlyMode)
        state.store->ensurePath(path2);
    context.insert(NixStringContextElem::Opaque{.path = path2});
    v.mkString(path.abs(), context, state.mem);
}

static RegisterPrimOp primop_storePath({
    .name = "__storePath",
    .args = {"path"},
    .doc = R"(
      This function allows you to define a dependency on an already
      existing store path. For example, the derivation attribute `src
      = builtins.storePath /nix/store/f1d18v1y…-source` causes the
      derivation to depend on the specified path, which must exist or
      be substitutable. Note that this differs from a plain path
      (e.g. `src = /nix/store/f1d18v1y…-source`) in that the latter
      causes the path to be *copied* again to the Nix store, resulting
      in a new path (e.g. `/nix/store/ld01dnzc…-source-source`).

      Not available in [pure evaluation mode](@docroot@/command-ref/conf-file.md#conf-pure-eval).

      See also [`builtins.fetchClosure`](#builtins-fetchClosure).
    )",
    .fun = prim_storePath,
});

static void prim_pathExists(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    try {
        auto & arg = *args[0];

        /* SourcePath doesn't know about trailing slash. */
        state.forceValue(arg, pos);
        auto mustBeDir =
            arg.type() == nString && (arg.string_view().ends_with("/") || arg.string_view().ends_with("/."));

        auto symlinkResolution = mustBeDir ? SymlinkResolution::Full : SymlinkResolution::Ancestors;
        auto path = realisePath(state, pos, arg, symlinkResolution);

        auto st = path.maybeLstat();
        auto exists = st && (!mustBeDir || st->type == SourceAccessor::tDirectory);
        v.mkBool(exists);
    } catch (RestrictedPathError & e) {
        v.mkBool(false);
    }
}

static RegisterPrimOp primop_pathExists({
    .name = "__pathExists",
    .args = {"path"},
    .doc = R"(
      Return `true` if the path *path* exists at evaluation time, and
      `false` otherwise.
    )",
    .fun = prim_pathExists,
});

// Ideally, all trailing slashes should have been removed, but it's been like this for
// almost a decade as of writing. Changing it will affect reproducibility.
static std::string_view legacyBaseNameOf(std::string_view path)
{
    if (path.empty())
        return "";

    auto last = path.size() - 1;
    if (path[last] == '/' && last > 0)
        last -= 1;

    auto pos = path.rfind('/', last);
    if (pos == path.npos)
        pos = 0;
    else
        pos += 1;

    return path.substr(pos, last - pos + 1);
}

/* Return the base name of the given string, i.e., everything
   following the last slash. */
static void prim_baseNameOf(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    NixStringContext context;
    v.mkString(
        legacyBaseNameOf(*state.coerceToString(
            pos, *args[0], context, "while evaluating the first argument passed to builtins.baseNameOf", false, false)),
        context,
        state.mem);
}

static RegisterPrimOp primop_baseNameOf({
    .name = "baseNameOf",
    .args = {"x"},
    .doc = R"(
      Return the *base name* of either a [path value](@docroot@/language/types.md#type-path) *x* or a string *x*, depending on which type is passed, and according to the following rules.

      For a path value, the *base name* is considered to be the part of the path after the last directory separator, including any file extensions.
      This is the simple case, as path values don't have trailing slashes.

      When the argument is a string, a more involved logic applies. If the string ends with a `/`, only this one final slash is removed.

      After this, the *base name* is returned as previously described, assuming `/` as the directory separator. (Note that evaluation must be platform independent.)

      This is somewhat similar to the [GNU `basename`](https://www.gnu.org/software/coreutils/manual/html_node/basename-invocation.html) command, but GNU `basename` strips any number of trailing slashes.
    )",
    .fun = prim_baseNameOf,
});

/* Return the directory of the given path, i.e., everything before the
   last slash.  Return either a path or a string depending on the type
   of the argument. */
static void prim_dirOf(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    state.forceValue(*args[0], pos);
    if (args[0]->type() == nPath) {
        auto path = args[0]->path();
        v.mkPath(path.path.isRoot() ? path : path.parent(), state.mem);
    } else {
        NixStringContext context;
        auto path = state.coerceToString(
            pos, *args[0], context, "while evaluating the first argument passed to 'builtins.dirOf'", false, false);
        auto pos = path->rfind('/');
        if (pos == path->npos)
            v.mkStringMove("."_sds, context, state.mem);
        else if (pos == 0)
            v.mkStringMove("/"_sds, context, state.mem);
        else
            v.mkString(path->substr(0, pos), context, state.mem);
    }
}

static RegisterPrimOp primop_dirOf({
    .name = "dirOf",
    .args = {"s"},
    .doc = R"(
      Return the directory part of the string *s*, that is, everything
      before the final slash in the string. This is similar to the GNU
      `dirname` command.
    )",
    .fun = prim_dirOf,
});

/* Return the contents of a file as a string. */
static void prim_readFile(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    auto path = realisePath(state, pos, *args[0]);
    auto s = path.readFile();
    if (s.find((char) 0) != std::string::npos)
        state.error<EvalError>("the contents of the file '%1%' cannot be represented as a Nix string", path)
            .atPos(pos)
            .debugThrow();
    StorePathSet refs;
    if (state.store->isInStore(path.path.abs())) {
        try {
            refs = state.store->queryPathInfo(state.store->toStorePath(path.path.abs()).first)->references;
        } catch (Error &) { // FIXME: should be InvalidPathError
        }
        // Re-scan references to filter down to just the ones that actually occur in the file.
        auto refsSink = PathRefScanSink::fromPaths(refs);
        refsSink << s;
        refs = refsSink.getResultPaths();
    }
    NixStringContext context;
    for (auto && p : std::move(refs)) {
        context.insert(
            NixStringContextElem::Opaque{
                .path = std::move((StorePath &&) p),
            });
    }
    v.mkString(s, context, state.mem);
}

static RegisterPrimOp primop_readFile({
    .name = "__readFile",
    .args = {"path"},
    .doc = R"(
      Return the contents of the file *path* as a string.
    )",
    .fun = prim_readFile,
});

/* Find a file in the Nix search path. Used to implement <x> paths,
   which are desugared to 'findFile __nixPath "x"'. */
static void prim_findFile(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    state.forceList(*args[0], pos, "while evaluating the first argument passed to builtins.findFile");

    LookupPath lookupPath;

    for (auto v2 : args[0]->listView()) {
        state.forceAttrs(*v2, pos, "while evaluating an element of the list passed to builtins.findFile");

        std::string prefix;
        auto i = v2->attrs()->get(state.s.prefix);
        if (i)
            prefix = state.forceStringNoCtx(
                *i->value,
                pos,
                "while evaluating the `prefix` attribute of an element of the list passed to builtins.findFile");

        i = state.getAttr(state.s.path, v2->attrs(), "in an element of the __nixPath");

        NixStringContext context;
        auto path =
            state
                .coerceToString(
                    pos,
                    *i->value,
                    context,
                    "while evaluating the `path` attribute of an element of the list passed to builtins.findFile",
                    false,
                    false)
                .toOwned();

        try {
            auto rewrites = state.realiseContext(context);
            path = rewriteStrings(std::move(path), rewrites);
        } catch (InvalidPathError & e) {
            state.error<EvalError>("cannot find '%1%', since path '%2%' is not valid", path, e.path)
                .atPos(pos)
                .debugThrow();
        }

        lookupPath.elements.emplace_back(
            LookupPath::Elem{
                .prefix = LookupPath::Prefix{.s = std::move(prefix)},
                .path = LookupPath::Path{.s = std::move(path)},
            });
    }

    auto path =
        state.forceStringNoCtx(*args[1], pos, "while evaluating the second argument passed to builtins.findFile");

    v.mkPath(state.findFile(lookupPath, path, pos), state.mem);
}

static RegisterPrimOp primop_findFile(
    PrimOp{
        .name = "__findFile",
        .args = {"search-path", "lookup-path"},
        .doc = R"(
      Find *lookup-path* in *search-path*.

      [Lookup path](@docroot@/language/constructs/lookup-path.md) expressions are [desugared](https://en.wikipedia.org/wiki/Syntactic_sugar) using this and [`builtins.nixPath`](#builtins-nixPath):

      ```nix
      <nixpkgs>
      ```

      is equivalent to:

      ```nix
      builtins.findFile builtins.nixPath "nixpkgs"
      ```

      A search path is represented as a list of [attribute sets](./types.md#type-attrs) with two attributes:
      - `prefix` is a relative path.
      - `path` denotes a file system location

      Examples of search path attribute sets:

      - ```
        {
          prefix = "";
          path = "/nix/var/nix/profiles/per-user/root/channels";
        }
        ```
      - ```
        {
          prefix = "nixos-config";
          path = "/etc/nixos/configuration.nix";
        }
        ```
      - ```
        {
          prefix = "nixpkgs";
          path = "https://github.com/NixOS/nixpkgs/tarballs/master";
        }
        ```
      - ```
        {
          prefix = "nixpkgs";
          path = "channel:nixpkgs-unstable";
        }
        ```
      - ```
        {
          prefix = "flake-compat";
          path = "flake:github:edolstra/flake-compat";
        }
        ```

      The lookup algorithm checks each entry until a match is found, returning a [path value](@docroot@/language/types.md#type-path) of the match:

      - If a prefix of `lookup-path` matches `prefix`, then the remainder of *lookup-path* (the "suffix") is searched for within the directory denoted by `path`.
        The contents of `path` may need to be downloaded at this point to look inside.

      - If the suffix is found inside that directory, then the entry is a match.
        The combined absolute path of the directory (now downloaded if need be) and the suffix is returned.

      > **Example**
      >
      > A *search-path* value
      >
      > ```
      > [
      >   {
      >     prefix = "";
      >     path = "/home/eelco/Dev";
      >   }
      >   {
      >     prefix = "nixos-config";
      >     path = "/etc/nixos";
      >   }
      > ]
      > ```
      >
      > and a *lookup-path* value `"nixos-config"` causes Nix to try `/home/eelco/Dev/nixos-config` and `/etc/nixos` in that order and return the first path that exists.

      If `path` starts with `http://` or `https://`, it is interpreted as the URL of a tarball to be downloaded and unpacked to a temporary location.
      The tarball must consist of a single top-level directory.

      The URLs of the tarballs from the official `nixos.org` channels can be abbreviated as `channel:<channel-name>`.
      See [documentation on `nix-channel`](@docroot@/command-ref/nix-channel.md) for details about channels.

      > **Example**
      >
      > These two search path entries are equivalent:
      >
      > - ```
      >   {
      >     prefix = "nixpkgs";
      >     path = "channel:nixpkgs-unstable";
      >   }
      >   ```
      > - ```
      >   {
      >     prefix = "nixpkgs";
      >     path = "https://channels.nixos.org/nixos-unstable/nixexprs.tar.xz";
      >   }
      >   ```

      Search paths can also point to source trees using [flake URLs](@docroot@/command-ref/new-cli/nix3-flake.md#url-like-syntax).


      > **Example**
      >
      > The search path entry
      >
      > ```
      > {
      >   prefix = "nixpkgs";
      >   path = "flake:nixpkgs";
      > }
      > ```
      > specifies that the prefix `nixpkgs` shall refer to the source tree downloaded from the `nixpkgs` entry in the flake registry.
      >
      > Similarly
      >
      > ```
      > {
      >   prefix = "nixpkgs";
      >   path = "flake:github:nixos/nixpkgs/nixos-22.05";
      > }
      > ```
      >
      > makes `<nixpkgs>` refer to a particular branch of the `NixOS/nixpkgs` repository on GitHub.
    )",
        .fun = prim_findFile,
    });

/* Return the cryptographic hash of a file in base-16. */
static void prim_hashFile(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    auto algo =
        state.forceStringNoCtx(*args[0], pos, "while evaluating the first argument passed to builtins.hashFile");
    std::optional<HashAlgorithm> ha = parseHashAlgo(algo);
    if (!ha)
        state.error<EvalError>("unknown hash algorithm '%1%'", algo).atPos(pos).debugThrow();

    auto path = realisePath(state, pos, *args[1]);

    v.mkString(hashString(*ha, path.readFile()).to_string(HashFormat::Base16, false), state.mem);
}

static RegisterPrimOp primop_hashFile({
    .name = "__hashFile",
    .args = {"type", "p"},
    .doc = R"(
      Return a base-16 representation of the cryptographic hash of the
      file at path *p*. The hash algorithm specified by *type* must be one
      of `"md5"`, `"sha1"`, `"sha256"` or `"sha512"`.
    )",
    .fun = prim_hashFile,
});

static const Value & fileTypeToString(EvalState & state, SourceAccessor::Type type)
{
    struct Constants
    {
        Value regular;
        Value directory;
        Value symlink;
        Value unknown;
    };

    static const Constants stringValues = []() {
        Constants res;
        res.regular.mkStringNoCopy("regular"_sds);
        res.directory.mkStringNoCopy("directory"_sds);
        res.symlink.mkStringNoCopy("symlink"_sds);
        res.unknown.mkStringNoCopy("unknown"_sds);
        return res;
    }();

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch-enum"
    using enum SourceAccessor::Type;
    switch (type) {
    case tRegular:
        return stringValues.regular;
    case tDirectory:
        return stringValues.directory;
    case tSymlink:
        return stringValues.symlink;
    default:
        return stringValues.unknown;
    }
}

static void prim_readFileType(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    auto path = realisePath(state, pos, *args[0], std::nullopt);
    /* Retrieve the directory entry type and stringize it. */
    v = fileTypeToString(state, path.lstat().type);
}

static RegisterPrimOp primop_readFileType({
    .name = "__readFileType",
    .args = {"p"},
    .doc = R"(
      Determine the directory entry type of a filesystem node, being
      one of `"directory"`, `"regular"`, `"symlink"`, or `"unknown"`.
    )",
    .fun = prim_readFileType,
});

/* Read a directory (without . or ..) */
static void prim_readDir(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    auto path = realisePath(state, pos, *args[0]);

    // Retrieve directory entries for all nodes in a directory.
    // This is similar to `getFileType` but is optimized to reduce system calls
    // on many systems.
    auto entries = path.readDirectory();
    auto attrs = state.buildBindings(entries.size());

    // If we hit unknown directory entry types we may need to fallback to
    // using `getFileType` on some systems.
    // In order to reduce system calls we make each lookup lazy by using
    // `builtins.readFileType` application.
    Value * readFileType = nullptr;

    for (auto & [name, type] : entries) {
        if (!type) {
            auto & attr = attrs.alloc(name);
            // Some filesystems or operating systems may not be able to return
            // detailed node info quickly in this case we produce a thunk to
            // query the file type lazily.
            auto epath = state.allocValue();
            epath->mkPath(path / name, state.mem);
            if (!readFileType)
                readFileType = &state.getBuiltin("readFileType");
            attr.mkApp(readFileType, epath);
        } else {
            // This branch of the conditional is much more likely.
            // Here we just stringize the directory entry type.
            // N.B. const_cast here is ok, because these values will never be modified, since
            // only thunks are mutable - other types do not change once constructed.
            attrs.insert(state.symbols.create(name), const_cast<Value *>(&fileTypeToString(state, *type)));
        }
    }

    v.mkAttrs(attrs);
}

static RegisterPrimOp primop_readDir({
    .name = "__readDir",
    .args = {"path"},
    .doc = R"(
      Return the contents of the directory *path* as a set mapping
      directory entries to the corresponding file type. For instance, if
      directory `A` contains a regular file `B` and another directory
      `C`, then `builtins.readDir ./A` returns the set

      ```nix
      { B = "regular"; C = "directory"; }
      ```

      The possible values for the file type are `"regular"`,
      `"directory"`, `"symlink"` and `"unknown"`.
    )",
    .fun = prim_readDir,
});

/* Extend single element string context with another output. */
static void prim_outputOf(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    SingleDerivedPath drvPath =
        state.coerceToSingleDerivedPath(pos, *args[0], "while evaluating the first argument to builtins.outputOf");

    OutputNameView outputName =
        state.forceStringNoCtx(*args[1], pos, "while evaluating the second argument to builtins.outputOf");

    state.mkSingleDerivedPathString(
        SingleDerivedPath::Built{
            .drvPath = make_ref<SingleDerivedPath>(drvPath),
            .output = std::string{outputName},
        },
        v);
}

static RegisterPrimOp primop_outputOf({
    .name = "__outputOf",
    .args = {"derivation-reference", "output-name"},
    .doc = R"(
      Return the output path of a derivation, literally or using an
      [input placeholder string](@docroot@/store/derivation/index.md#input-placeholder)
      if needed.

      If the derivation has a statically-known output path (i.e. the derivation output is input-addressed, or fixed content-addressed), the output path is returned.
      But if the derivation is content-addressed or if the derivation is itself not-statically produced (i.e. is the output of another derivation), an input placeholder is returned instead.

      *`derivation reference`* must be a string that may contain a regular store path to a derivation, or may be an input placeholder reference.
      If the derivation is produced by a derivation, you must explicitly select `drv.outPath`.
      This primop can be chained arbitrarily deeply.
      For instance,

      ```nix
      builtins.outputOf
        (builtins.outputOf myDrv "out")
        "out"
      ```

      returns an input placeholder for the output of the output of `myDrv`.

      This primop corresponds to the `^` sigil for [deriving paths](@docroot@/glossary.md#gloss-deriving-path), e.g. as part of installable syntax on the command line.
    )",
    .fun = prim_outputOf,
    .experimentalFeature = Xp::DynamicDerivations,
});

/*************************************************************
 * Creating files
 *************************************************************/

/* Convert the argument (which can be any Nix expression) to an XML
   representation returned in a string.  Not all Nix expressions can
   be sensibly or completely represented (e.g., functions). */
static void prim_toXML(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    std::ostringstream out;
    NixStringContext context;
    printValueAsXML(state, true, false, *args[0], out, context, pos);
    v.mkString(out.view(), context, state.mem);
}

static RegisterPrimOp primop_toXML({
    .name = "__toXML",
    .args = {"e"},
    .doc = R"(
      Return a string containing an XML representation of *e*. The main
      application for `toXML` is to communicate information with the
      builder in a more structured format than plain environment
      variables.

      Here is an example where this is the case:

      ```nix
      { stdenv, fetchurl, libxslt, jira, uberwiki }:

      stdenv.mkDerivation (rec {
        name = "web-server";

        buildInputs = [ libxslt ];

        builder = builtins.toFile "builder.sh" "
          source $stdenv/setup
          mkdir $out
          echo "$servlets" | xsltproc ${stylesheet} - > $out/server-conf.xml ①
        ";

        stylesheet = builtins.toFile "stylesheet.xsl" ②
         "<?xml version='1.0' encoding='UTF-8'?>
          <xsl:stylesheet xmlns:xsl='http://www.w3.org/1999/XSL/Transform' version='1.0'>
            <xsl:template match='/'>
              <Configure>
                <xsl:for-each select='/expr/list/attrs'>
                  <Call name='addWebApplication'>
                    <Arg><xsl:value-of select=\"attr[@name = 'path']/string/@value\" /></Arg>
                    <Arg><xsl:value-of select=\"attr[@name = 'war']/path/@value\" /></Arg>
                  </Call>
                </xsl:for-each>
              </Configure>
            </xsl:template>
          </xsl:stylesheet>
        ";

        servlets = builtins.toXML [ ③
          { path = "/bugtracker"; war = jira + "/lib/atlassian-jira.war"; }
          { path = "/wiki"; war = uberwiki + "/uberwiki.war"; }
        ];
      })
      ```

      The builder is supposed to generate the configuration file for a
      [Jetty servlet container](http://jetty.mortbay.org/). A servlet
      container contains a number of servlets (`*.war` files) each
      exported under a specific URI prefix. So the servlet configuration
      is a list of sets containing the `path` and `war` of the servlet
      (①). This kind of information is difficult to communicate with the
      normal method of passing information through an environment
      variable, which just concatenates everything together into a
      string (which might just work in this case, but wouldn’t work if
      fields are optional or contain lists themselves). Instead the Nix
      expression is converted to an XML representation with `toXML`,
      which is unambiguous and can easily be processed with the
      appropriate tools. For instance, in the example an XSLT stylesheet
      (at point ②) is applied to it (at point ①) to generate the XML
      configuration file for the Jetty server. The XML representation
      produced at point ③ by `toXML` is as follows:

      ```xml
      <?xml version='1.0' encoding='utf-8'?>
      <expr>
        <list>
          <attrs>
            <attr name="path">
              <string value="/bugtracker" />
            </attr>
            <attr name="war">
              <path value="/nix/store/d1jh9pasa7k2...-jira/lib/atlassian-jira.war" />
            </attr>
          </attrs>
          <attrs>
            <attr name="path">
              <string value="/wiki" />
            </attr>
            <attr name="war">
              <path value="/nix/store/y6423b1yi4sx...-uberwiki/uberwiki.war" />
            </attr>
          </attrs>
        </list>
      </expr>
      ```

      Note that we used the `toFile` built-in to write the builder and
      the stylesheet “inline” in the Nix expression. The path of the
      stylesheet is spliced into the builder using the syntax `xsltproc
      ${stylesheet}`.
    )",
    .fun = prim_toXML,
});

/* Convert the argument (which can be any Nix expression) to a JSON
   string.  Not all Nix expressions can be sensibly or completely
   represented (e.g., functions). */
static void prim_toJSON(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    std::ostringstream out;
    NixStringContext context;
    printValueAsJSON(state, true, *args[0], pos, out, context);
    v.mkString(out.view(), context, state.mem);
}

static RegisterPrimOp primop_toJSON({
    .name = "__toJSON",
    .args = {"e"},
    .doc = R"(
      Return a string containing a JSON representation of *e*. Strings,
      integers, floats, booleans, nulls and lists are mapped to their JSON
      equivalents. Sets (except derivations) are represented as objects.
      Derivations are translated to a JSON string containing the
      derivation’s output path. Paths are copied to the store and
      represented as a JSON string of the resulting store path.
    )",
    .fun = prim_toJSON,
});

/* Parse a JSON string to a value. */
static void prim_fromJSON(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    auto s = state.forceStringNoCtx(*args[0], pos, "while evaluating the first argument passed to builtins.fromJSON");
    try {
        parseJSON(state, s, v);
    } catch (JSONParseError & e) {
        e.addTrace(state.positions[pos], "while decoding a JSON string");
        throw;
    }
}

static RegisterPrimOp primop_fromJSON({
    .name = "__fromJSON",
    .args = {"e"},
    .doc = R"(
      Convert a JSON string to a Nix value. For example,

      ```nix
      builtins.fromJSON ''{"x": [1, 2, 3], "y": null}''
      ```

      returns the value `{ x = [ 1 2 3 ]; y = null; }`.
    )",
    .fun = prim_fromJSON,
});

/* Store a string in the Nix store as a source file that can be used
   as an input by derivations. */
static void prim_toFile(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    NixStringContext context;
    auto name = state.forceStringNoCtx(*args[0], pos, "while evaluating the first argument passed to builtins.toFile");
    auto contents =
        state.forceString(*args[1], context, pos, "while evaluating the second argument passed to builtins.toFile");

    StorePathSet refs;

    for (auto c : context) {
        if (auto p = std::get_if<NixStringContextElem::Opaque>(&c.raw))
            refs.insert(p->path);
        else
            state
                .error<EvalError>(
                    "files created by %1% may not reference derivations, but %2% references %3%",
                    "builtins.toFile",
                    name,
                    c.to_string())
                .atPos(pos)
                .debugThrow();
    }

    auto storePath = settings.readOnlyMode ? state.store->makeFixedOutputPathFromCA(
                                                 name,
                                                 TextInfo{
                                                     .hash = hashString(HashAlgorithm::SHA256, contents),
                                                     .references = std::move(refs),
                                                 })
                                           : ({
                                                 StringSource s{contents};
                                                 state.store->addToStoreFromDump(
                                                     s,
                                                     name,
                                                     FileSerialisationMethod::Flat,
                                                     ContentAddressMethod::Raw::Text,
                                                     HashAlgorithm::SHA256,
                                                     refs,
                                                     state.repair);
                                             });

    /* Note: we don't need to add `context' to the context of the
       result, since `storePath' itself has references to the paths
       used in args[1]. */

    /* Add the output of this to the allowed paths. */
    state.allowAndSetStorePathString(storePath, v);
}

static RegisterPrimOp primop_toFile({
    .name = "__toFile",
    .args = {"name", "s"},
    .doc = R"(
      Store the string *s* in a file in the Nix store and return its
      path.  The file has suffix *name*. This file can be used as an
      input to derivations. One application is to write builders
      “inline”. For instance, the following Nix expression combines the
      Nix expression for GNU Hello and its build script into one file:

      ```nix
      { stdenv, fetchurl, perl }:

      stdenv.mkDerivation {
        name = "hello-2.1.1";

        builder = builtins.toFile "builder.sh" "
          source $stdenv/setup

          PATH=$perl/bin:$PATH

          tar xvfz $src
          cd hello-*
          ./configure --prefix=$out
          make
          make install
        ";

        src = fetchurl {
          url = "http://ftp.nluug.nl/pub/gnu/hello/hello-2.1.1.tar.gz";
          sha256 = "1md7jsfd8pa45z73bz1kszpp01yw6x5ljkjk2hx7wl800any6465";
        };
        inherit perl;
      }
      ```

      It is even possible for one file to refer to another, e.g.,

      ```nix
      builder = let
        configFile = builtins.toFile "foo.conf" "
          # This is some dummy configuration file.
          ...
        ";
      in builtins.toFile "builder.sh" "
        source $stdenv/setup
        ...
        cp ${configFile} $out/etc/foo.conf
      ";
      ```

      Note that `${configFile}` is a
      [string interpolation](@docroot@/language/types.md#type-string), so the result of the
      expression `configFile`
      (i.e., a path like `/nix/store/m7p7jfny445k...-foo.conf`) will be
      spliced into the resulting string.

      It is however *not* allowed to have files mutually referring to each
      other, like so:

      ```nix
      let
        foo = builtins.toFile "foo" "...${bar}...";
        bar = builtins.toFile "bar" "...${foo}...";
      in foo
      ```

      This is not allowed because it would cause a cyclic dependency in
      the computation of the cryptographic hashes for `foo` and `bar`.

      It is also not possible to reference the result of a derivation. If
      you are using Nixpkgs, the `writeTextFile` function is able to do
      that.
    )",
    .fun = prim_toFile,
});

bool EvalState::callPathFilter(Value * filterFun, const SourcePath & path, PosIdx pos)
{
    auto st = path.lstat();

    /* Call the filter function.  The first argument is the path, the
       second is a string indicating the type of the file. */
    Value arg1;
    arg1.mkString(path.path.abs(), mem);

    // assert that type is not "unknown"
    Value * args[]{&arg1, const_cast<Value *>(&fileTypeToString(*this, st.type))};
    Value res;
    callFunction(*filterFun, args, res, pos);

    return forceBool(res, pos, "while evaluating the return value of the path filter function");
}

static void addPath(
    EvalState & state,
    const PosIdx pos,
    std::string_view name,
    SourcePath path,
    Value * filterFun,
    ContentAddressMethod method,
    const std::optional<Hash> expectedHash,
    Value & v,
    const NixStringContext & context)
{
    try {
        StorePathSet refs;

        if (path.accessor == state.rootFS && state.store->isInStore(path.path.abs()) && !context.empty()) {
            // FIXME: handle CA derivation outputs (where path needs to
            // be rewritten to the actual output).
            auto rewrites = state.realiseContext(context);
            path = {path.accessor, CanonPath(rewriteStrings(path.path.abs(), rewrites))};
            auto [storePath, subPath] = state.store->toStorePath(path.path.abs());
            try {
                refs = state.store->queryPathInfo(storePath)->references;
            } catch (Error &) { // FIXME: should be InvalidPathError
            }
        }

        std::unique_ptr<PathFilter> filter;
        if (filterFun)
            filter = std::make_unique<PathFilter>([&](const Path & p) {
                auto p2 = CanonPath(p);
                return state.callPathFilter(filterFun, {path.accessor, p2}, pos);
            });

        std::optional<StorePath> expectedStorePath;
        if (expectedHash)
            expectedStorePath = state.store->makeFixedOutputPathFromCA(
                name, ContentAddressWithReferences::fromParts(method, *expectedHash, {refs}));

        if (!expectedHash || !state.store->isValidPath(*expectedStorePath)) {
            // FIXME: support refs in fetchToStore()?
            auto dstPath = refs.empty() ? fetchToStore(
                                              state.fetchSettings,
                                              *state.store,
                                              path.resolveSymlinks(),
                                              settings.readOnlyMode ? FetchMode::DryRun : FetchMode::Copy,
                                              name,
                                              method,
                                              filter.get(),
                                              state.repair)
                                        : state.store->addToStore(
                                              name,
                                              path.resolveSymlinks(),
                                              method,
                                              HashAlgorithm::SHA256,
                                              refs,
                                              filter ? *filter.get() : defaultPathFilter,
                                              state.repair);
            if (expectedHash && expectedStorePath != dstPath)
                state.error<EvalError>("store path mismatch in (possibly filtered) path added from '%s'", path)
                    .atPos(pos)
                    .debugThrow();
            state.allowAndSetStorePathString(dstPath, v);
        } else
            state.allowAndSetStorePathString(*expectedStorePath, v);
    } catch (Error & e) {
        e.addTrace(state.positions[pos], "while adding path '%s'", path);
        throw;
    }
}

static void prim_filterSource(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    NixStringContext context;
    auto path = state.coerceToPath(
        pos,
        *args[1],
        context,
        "while evaluating the second argument (the path to filter) passed to 'builtins.filterSource'");
    state.forceFunction(*args[0], pos, "while evaluating the first argument passed to builtins.filterSource");

    addPath(
        state, pos, path.baseName(), path, args[0], ContentAddressMethod::Raw::NixArchive, std::nullopt, v, context);
}

static RegisterPrimOp primop_filterSource({
    .name = "__filterSource",
    .args = {"e1", "e2"},
    .doc = R"(
      > **Warning**
      >
      > `filterSource` should not be used to filter store paths. Since
      > `filterSource` uses the name of the input directory while naming
      > the output directory, doing so produces a directory name in
      > the form of `<hash2>-<hash>-<name>`, where `<hash>-<name>` is
      > the name of the input directory. Since `<hash>` depends on the
      > unfiltered directory, the name of the output directory
      > indirectly depends on files that are filtered out by the
      > function. This triggers a rebuild even when a filtered out
      > file is changed. Use `builtins.path` instead, which allows
      > specifying the name of the output directory.

      This function allows you to copy sources into the Nix store while
      filtering certain files. For instance, suppose that you want to use
      the directory `source-dir` as an input to a Nix expression, e.g.

      ```nix
      stdenv.mkDerivation {
        ...
        src = ./source-dir;
      }
      ```

      However, if `source-dir` is a Subversion working copy, then all of
      those annoying `.svn` subdirectories are also copied to the
      store. Worse, the contents of those directories may change a lot,
      causing lots of spurious rebuilds. With `filterSource` you can
      filter out the `.svn` directories:

      ```nix
      src = builtins.filterSource
        (path: type: type != "directory" || baseNameOf path != ".svn")
        ./source-dir;
      ```

      Thus, the first argument *e1* must be a predicate function that is
      called for each regular file, directory or symlink in the source
      tree *e2*. If the function returns `true`, the file is copied to the
      Nix store, otherwise it is omitted. The function is called with two
      arguments. The first is the full path of the file. The second is a
      string that identifies the type of the file, which is either
      `"regular"`, `"directory"`, `"symlink"` or `"unknown"` (for other
      kinds of files such as device nodes or fifos — but note that those
      cannot be copied to the Nix store, so if the predicate returns
      `true` for them, the copy fails). If you exclude a directory,
      the entire corresponding subtree of *e2* is excluded.
    )",
    .fun = prim_filterSource,
});

static void prim_path(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    std::optional<SourcePath> path;
    std::string_view name;
    Value * filterFun = nullptr;
    auto method = ContentAddressMethod::Raw::NixArchive;
    std::optional<Hash> expectedHash;
    NixStringContext context;

    state.forceAttrs(*args[0], pos, "while evaluating the argument passed to 'builtins.path'");

    for (auto & attr : *args[0]->attrs()) {
        auto n = state.symbols[attr.name];
        if (n == "path")
            path.emplace(state.coerceToPath(
                attr.pos, *attr.value, context, "while evaluating the 'path' attribute passed to 'builtins.path'"));
        else if (attr.name == state.s.name)
            name = state.forceStringNoCtx(
                *attr.value, attr.pos, "while evaluating the `name` attribute passed to builtins.path");
        else if (n == "filter")
            state.forceFunction(
                *(filterFun = attr.value), attr.pos, "while evaluating the `filter` parameter passed to builtins.path");
        else if (n == "recursive")
            method = state.forceBool(
                         *attr.value, attr.pos, "while evaluating the `recursive` attribute passed to builtins.path")
                         ? ContentAddressMethod::Raw::NixArchive
                         : ContentAddressMethod::Raw::Flat;
        else if (n == "sha256")
            expectedHash = newHashAllowEmpty(
                state.forceStringNoCtx(
                    *attr.value, attr.pos, "while evaluating the `sha256` attribute passed to builtins.path"),
                HashAlgorithm::SHA256);
        else
            state.error<EvalError>("unsupported argument '%1%' to 'builtins.path'", state.symbols[attr.name])
                .atPos(attr.pos)
                .debugThrow();
    }
    if (!path)
        state.error<EvalError>("missing required 'path' attribute in the first argument to 'builtins.path'")
            .atPos(pos)
            .debugThrow();
    if (name.empty())
        name = path->baseName();

    addPath(state, pos, name, *path, filterFun, method, expectedHash, v, context);
}

static RegisterPrimOp primop_path({
    .name = "__path",
    .args = {"args"},
    .doc = R"(
      An enrichment of the built-in path type, based on the attributes
      present in *args*. All are optional except `path`:

        - path\
          The underlying path.

        - name\
          The name of the path when added to the store. This can used to
          reference paths that have nix-illegal characters in their names,
          like `@`.

        - filter\
          A function of the type expected by [`builtins.filterSource`](#builtins-filterSource),
          with the same semantics.

        - recursive\
          When `false`, when `path` is added to the store it is with a
          flat hash, rather than a hash of the NAR serialization of the
          file. Thus, `path` must refer to a regular file, not a
          directory. This allows similar behavior to `fetchurl`. Defaults
          to `true`.

        - sha256\
          When provided, this is the expected hash of the file at the
          path. Evaluation fails if the hash is incorrect, and
          providing a hash allows `builtins.path` to be used even when the
          `pure-eval` nix config option is on.
    )",
    .fun = prim_path,
});

/*************************************************************
 * Sets
 *************************************************************/

/* Return the names of the attributes in a set as a sorted list of
   strings. */
static void prim_attrNames(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    state.forceAttrs(*args[0], pos, "while evaluating the argument passed to builtins.attrNames");

    auto list = state.buildList(args[0]->attrs()->size());

    for (const auto & [n, i] : enumerate(*args[0]->attrs()))
        list[n] = Value::toPtr(state.symbols[i.name]);

    std::sort(list.begin(), list.end(), [](Value * v1, Value * v2) { return v1->string_view() < v2->string_view(); });

    v.mkList(list);
}

static RegisterPrimOp primop_attrNames({
    .name = "__attrNames",
    .args = {"set"},
    .doc = R"(
      Return the names of the attributes in the set *set* in an
      alphabetically sorted list. For instance, `builtins.attrNames { y
      = 1; x = "foo"; }` evaluates to `[ "x" "y" ]`.
    )",
    .fun = prim_attrNames,
});

/* Return the values of the attributes in a set as a list, in the same
   order as attrNames. */
static void prim_attrValues(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    state.forceAttrs(*args[0], pos, "while evaluating the argument passed to builtins.attrValues");

    auto list = state.buildList(args[0]->attrs()->size());

    for (const auto & [n, i] : enumerate(*args[0]->attrs()))
        list[n] = (Value *) &i;

    std::sort(list.begin(), list.end(), [&](Value * v1, Value * v2) {
        std::string_view s1 = state.symbols[((Attr *) v1)->name], s2 = state.symbols[((Attr *) v2)->name];
        return s1 < s2;
    });

    for (auto & v : list)
        v = ((Attr *) v)->value;

    v.mkList(list);
}

static RegisterPrimOp primop_attrValues({
    .name = "__attrValues",
    .args = {"set"},
    .doc = R"(
      Return the values of the attributes in the set *set* in the order
      corresponding to the sorted attribute names.
    )",
    .fun = prim_attrValues,
});

/* Dynamic version of the `.' operator. */
void prim_getAttr(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    auto attr = state.forceStringNoCtx(*args[0], pos, "while evaluating the first argument passed to builtins.getAttr");
    state.forceAttrs(*args[1], pos, "while evaluating the second argument passed to builtins.getAttr");
    auto i = state.getAttr(state.symbols.create(attr), args[1]->attrs(), "in the attribute set under consideration");
    // !!! add to stack trace?
    if (state.countCalls && i->pos)
        state.attrSelects[i->pos]++;
    state.forceValue(*i->value, pos);
    v = *i->value;
}

static RegisterPrimOp primop_getAttr({
    .name = "__getAttr",
    .args = {"s", "set"},
    .doc = R"(
      `getAttr` returns the attribute named *s* from *set*. Evaluation
      aborts if the attribute doesn’t exist. This is a dynamic version of
      the `.` operator, since *s* is an expression rather than an
      identifier.
    )",
    .fun = prim_getAttr,
});

/* Return position information of the specified attribute. */
static void prim_unsafeGetAttrPos(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    auto attr = state.forceStringNoCtx(
        *args[0], pos, "while evaluating the first argument passed to builtins.unsafeGetAttrPos");
    state.forceAttrs(*args[1], pos, "while evaluating the second argument passed to builtins.unsafeGetAttrPos");
    auto i = args[1]->attrs()->get(state.symbols.create(attr));
    if (!i)
        v.mkNull();
    else
        state.mkPos(v, i->pos);
}

static RegisterPrimOp primop_unsafeGetAttrPos(
    PrimOp{
        .name = "__unsafeGetAttrPos",
        .args = {"s", "set"},
        .arity = 2,
        .doc = R"(
      `unsafeGetAttrPos` returns the position of the attribute named *s*
      from *set*. This is used by Nixpkgs to provide location information
      in error messages.
    )",
        .fun = prim_unsafeGetAttrPos,
    });

// access to exact position information (ie, line and column numbers) is deferred
// due to the cost associated with calculating that information and how rarely
// it is used in practice. this is achieved by creating thunks to otherwise
// inaccessible primops that are not exposed as __op or under builtins to turn
// the internal PosIdx back into a line and column number, respectively. exposing
// these primops in any way would at best be not useful and at worst create wildly
// indeterministic eval results depending on parse order of files.
//
// in a simpler world this would instead be implemented as another kind of thunk,
// but each type of thunk has an associated runtime cost in the current evaluator.
// as with black holes this cost is too high to justify another thunk type to check
// for in the very hot path that is forceValue.
static struct LazyPosAccessors
{
    PrimOp primop_lineOfPos{.arity = 1, .fun = [](EvalState & state, PosIdx pos, Value ** args, Value & v) {
                                v.mkInt(state.positions[PosIdx(args[0]->integer().value)].line);
                            }};
    PrimOp primop_columnOfPos{.arity = 1, .fun = [](EvalState & state, PosIdx pos, Value ** args, Value & v) {
                                  v.mkInt(state.positions[PosIdx(args[0]->integer().value)].column);
                              }};

    Value lineOfPos, columnOfPos;

    LazyPosAccessors()
    {
        lineOfPos.mkPrimOp(&primop_lineOfPos);
        columnOfPos.mkPrimOp(&primop_columnOfPos);
    }

    void operator()(EvalState & state, const PosIdx pos, Value & line, Value & column)
    {
        Value * posV = state.allocValue();
        posV->mkInt(pos.id);
        line.mkApp(&lineOfPos, posV);
        column.mkApp(&columnOfPos, posV);
    }
} makeLazyPosAccessors;

void makePositionThunks(EvalState & state, const PosIdx pos, Value & line, Value & column)
{
    makeLazyPosAccessors(state, pos, line, column);
}

/* Dynamic version of the `?' operator. */
static void prim_hasAttr(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    auto attr = state.forceStringNoCtx(*args[0], pos, "while evaluating the first argument passed to builtins.hasAttr");
    state.forceAttrs(*args[1], pos, "while evaluating the second argument passed to builtins.hasAttr");
    v.mkBool(args[1]->attrs()->get(state.symbols.create(attr)));
}

static RegisterPrimOp primop_hasAttr({
    .name = "__hasAttr",
    .args = {"s", "set"},
    .doc = R"(
      `hasAttr` returns `true` if *set* has an attribute named *s*, and
      `false` otherwise. This is a dynamic version of the `?` operator,
      since *s* is an expression rather than an identifier.
    )",
    .fun = prim_hasAttr,
});

/* Determine whether the argument is a set. */
static void prim_isAttrs(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    state.forceValue(*args[0], pos);
    v.mkBool(args[0]->type() == nAttrs);
}

static RegisterPrimOp primop_isAttrs({
    .name = "__isAttrs",
    .args = {"e"},
    .doc = R"(
      Return `true` if *e* evaluates to a set, and `false` otherwise.
    )",
    .fun = prim_isAttrs,
});

static void prim_removeAttrs(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    state.forceAttrs(*args[0], pos, "while evaluating the first argument passed to builtins.removeAttrs");
    state.forceList(*args[1], pos, "while evaluating the second argument passed to builtins.removeAttrs");

    /* Get the attribute names to be removed.
       We keep them as Attrs instead of Symbols so std::set_difference
       can be used to remove them from attrs[0]. */
    // 64: large enough to fit the attributes of a derivation
    boost::container::small_vector<Attr, 64> names;
    names.reserve(args[1]->listSize());
    for (auto elem : args[1]->listView()) {
        state.forceStringNoCtx(
            *elem, pos, "while evaluating the values of the second argument passed to builtins.removeAttrs");
        names.emplace_back(state.symbols.create(elem->string_view()), nullptr);
    }
    std::sort(names.begin(), names.end());

    /* Copy all attributes not in that set.  Note that we don't need
       to sort v.attrs because it's a subset of an already sorted
       vector. */
    auto attrs = state.buildBindings(args[0]->attrs()->size());
    std::set_difference(
        args[0]->attrs()->begin(), args[0]->attrs()->end(), names.begin(), names.end(), std::back_inserter(attrs));
    v.mkAttrs(attrs.alreadySorted());
}

static RegisterPrimOp primop_removeAttrs({
    .name = "removeAttrs",
    .args = {"set", "list"},
    .doc = R"(
      Remove the attributes listed in *list* from *set*. The attributes
      don’t have to exist in *set*. For instance,

      ```nix
      removeAttrs { x = 1; y = 2; z = 3; } [ "a" "x" "z" ]
      ```

      evaluates to `{ y = 2; }`.
    )",
    .fun = prim_removeAttrs,
});

/* Builds a set from a list specifying (name, value) pairs.  To be
   precise, a list [{name = "name1"; value = value1;} ... {name =
   "nameN"; value = valueN;}] is transformed to {name1 = value1;
   ... nameN = valueN;}.  In case of duplicate occurrences of the same
   name, the first takes precedence. */
static void prim_listToAttrs(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    state.forceList(*args[0], pos, "while evaluating the argument passed to builtins.listToAttrs");

    // Step 1. Sort the name-value attrsets in place using the memory we allocate for the result
    auto listView = args[0]->listView();
    size_t listSize = listView.size();
    auto & bindings = *state.mem.allocBindings(listSize);
    using ElemPtr = decltype(&bindings[0].value);

    for (const auto & [n, v2] : enumerate(listView)) {
        state.forceAttrs(*v2, pos, "while evaluating an element of the list passed to builtins.listToAttrs");

        auto j = state.getAttr(state.s.name, v2->attrs(), "in a {name=...; value=...;} pair");

        auto name = state.forceStringNoCtx(
            *j->value,
            j->pos,
            "while evaluating the `name` attribute of an element of the list passed to builtins.listToAttrs");
        auto sym = state.symbols.create(name);

        // (ab)use Attr to store a Value * * instead of a Value *, so that we can stabilize the sort using the Value * *
        bindings[n] = Attr(sym, std::bit_cast<Value *>(&v2));
    }

    std::sort(&bindings[0], &bindings[listSize], [](const Attr & a, const Attr & b) {
        // Note that .value is actually a Value * * that corresponds to the position in the list
        return a < b || (!(a > b) && std::bit_cast<ElemPtr>(a.value) < std::bit_cast<ElemPtr>(b.value));
    });

    // Step 2. Unpack the bindings in place and skip name-value pairs with duplicate names
    Symbol prev;
    for (size_t n = 0; n < listSize; n++) {
        auto attr = bindings[n];
        if (prev == attr.name) {
            continue;
        }
        // Note that .value is actually a Value * *; see earlier comments
        Value * v2 = *std::bit_cast<ElemPtr>(attr.value);

        auto j = state.getAttr(state.s.value, v2->attrs(), "in a {name=...; value=...;} pair");
        prev = attr.name;
        bindings.push_back({prev, j->value, j->pos});
    }
    // help GC and clear end of allocated array
    for (size_t n = bindings.size(); n < listSize; n++) {
        bindings[n] = Attr{};
    }
    v.mkAttrs(&bindings);
}

static RegisterPrimOp primop_listToAttrs({
    .name = "__listToAttrs",
    .args = {"e"},
    .doc = R"(
      Construct a set from a list specifying the names and values of each
      attribute. Each element of the list should be a set consisting of a
      string-valued attribute `name` specifying the name of the attribute,
      and an attribute `value` specifying its value.

      In case of duplicate occurrences of the same name, the first
      takes precedence.

      Example:

      ```nix
      builtins.listToAttrs
        [ { name = "foo"; value = 123; }
          { name = "bar"; value = 456; }
          { name = "bar"; value = 420; }
        ]
      ```

      evaluates to

      ```nix
      { foo = 123; bar = 456; }
      ```
    )",
    .fun = prim_listToAttrs,
});

static void prim_intersectAttrs(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    state.forceAttrs(*args[0], pos, "while evaluating the first argument passed to builtins.intersectAttrs");
    state.forceAttrs(*args[1], pos, "while evaluating the second argument passed to builtins.intersectAttrs");

    auto & left = *args[0]->attrs();
    auto & right = *args[1]->attrs();

    auto attrs = state.buildBindings(std::min(left.size(), right.size()));

    // The current implementation has good asymptotic complexity and is reasonably
    // simple. Further optimization may be possible, but does not seem productive,
    // considering the state of eval performance in 2022.
    //
    // I have looked for reusable and/or standard solutions and these are my
    // findings:
    //
    // STL
    // ===
    // std::set_intersection is not suitable, as it only performs a simultaneous
    // linear scan; not taking advantage of random access. This is O(n + m), so
    // linear in the largest set, which is not acceptable for callPackage in Nixpkgs.
    //
    // Simultaneous scan, with alternating simple binary search
    // ===
    // One alternative algorithm scans the attrsets simultaneously, jumping
    // forward using `lower_bound` in case of inequality. This should perform
    // well on very similar sets, having a local and predictable access pattern.
    // On dissimilar sets, it seems to need more comparisons than the current
    // algorithm, as few consecutive attrs match. `lower_bound` could take
    // advantage of the decreasing remaining search space, but this causes
    // the medians to move, which can mean that they don't stay in the cache
    // like they would with the current naive `find`.
    //
    // Double binary search
    // ===
    // The optimal algorithm may be "Double binary search", which doesn't
    // scan at all, but rather divides both sets simultaneously.
    // See "Fast Intersection Algorithms for Sorted Sequences" by Baeza-Yates et al.
    // https://cs.uwaterloo.ca/~ajsaling/papers/intersection_alg_app10.pdf
    // The only downsides I can think of are not having a linear access pattern
    // for similar sets, and having to maintain a more intricate algorithm.
    //
    // Adaptive
    // ===
    // Finally one could run try a simultaneous scan, count misses and fall back
    // to double binary search when the counter hit some threshold and/or ratio.

    if (left.size() < right.size()) {
        for (auto & l : left) {
            auto r = right.get(l.name);
            if (r)
                attrs.insert(*r);
        }
    } else {
        for (auto & r : right) {
            auto l = left.get(r.name);
            if (l)
                attrs.insert(r);
        }
    }

    v.mkAttrs(attrs.alreadySorted());
}

static RegisterPrimOp primop_intersectAttrs({
    .name = "__intersectAttrs",
    .args = {"e1", "e2"},
    .doc = R"(
      Return a set consisting of the attributes in the set *e2* which have the
      same name as some attribute in *e1*.

      Performs in O(*n* log *m*) where *n* is the size of the smaller set and *m* the larger set's size.
    )",
    .fun = prim_intersectAttrs,
});

static void prim_catAttrs(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    auto attrName = state.symbols.create(
        state.forceStringNoCtx(*args[0], pos, "while evaluating the first argument passed to builtins.catAttrs"));
    state.forceList(*args[1], pos, "while evaluating the second argument passed to builtins.catAttrs");

    SmallValueVector<nonRecursiveStackReservation> res(args[1]->listSize());
    size_t found = 0;

    for (auto v2 : args[1]->listView()) {
        state.forceAttrs(
            *v2, pos, "while evaluating an element in the list passed as second argument to builtins.catAttrs");
        if (auto i = v2->attrs()->get(attrName))
            res[found++] = i->value;
    }

    auto list = state.buildList(found);
    for (size_t n = 0; n < found; ++n)
        list[n] = res[n];
    v.mkList(list);
}

static RegisterPrimOp primop_catAttrs({
    .name = "__catAttrs",
    .args = {"attr", "list"},
    .doc = R"(
      Collect each attribute named *attr* from a list of attribute
      sets.  Attrsets that don't contain the named attribute are
      ignored. For example,

      ```nix
      builtins.catAttrs "a" [{a = 1;} {b = 0;} {a = 2;}]
      ```

      evaluates to `[1 2]`.
    )",
    .fun = prim_catAttrs,
});

static void prim_functionArgs(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    state.forceValue(*args[0], pos);
    if (args[0]->isPrimOpApp() || args[0]->isPrimOp()) {
        v.mkAttrs(&Bindings::emptyBindings);
        return;
    }
    if (!args[0]->isLambda())
        state.error<TypeError>("'functionArgs' requires a function").atPos(pos).debugThrow();

    if (const auto & formals = args[0]->lambda().fun->getFormals()) {
        auto attrs = state.buildBindings(formals->formals.size());
        for (auto & i : formals->formals)
            attrs.insert(i.name, state.getBool(i.def), i.pos);
        /* Optimization: avoid sorting bindings. `formals` must already be sorted according to
           (std::tie(a.name, a.pos) < std::tie(b.name, b.pos)) predicate, so the following assertion
           always holds:
           assert(std::is_sorted(attrs.alreadySorted()->begin(), attrs.alreadySorted()->end()));
           .*/
        v.mkAttrs(attrs.alreadySorted());
    } else {
        v.mkAttrs(&Bindings::emptyBindings);
        return;
    }
}

static RegisterPrimOp primop_functionArgs({
    .name = "__functionArgs",
    .args = {"f"},
    .doc = R"(
      Return a set containing the names of the formal arguments expected
      by the function *f*. The value of each attribute is a Boolean
      denoting whether the corresponding argument has a default value. For
      instance, `functionArgs ({ x, y ? 123}: ...) = { x = false; y =
      true; }`.

      "Formal argument" here refers to the attributes pattern-matched by
      the function. Plain lambdas are not included, e.g. `functionArgs (x:
      ...) = { }`.
    )",
    .fun = prim_functionArgs,
});

/*  */
static void prim_mapAttrs(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    state.forceAttrs(*args[1], pos, "while evaluating the second argument passed to builtins.mapAttrs");

    auto attrs = state.buildBindings(args[1]->attrs()->size());

    for (auto & i : *args[1]->attrs()) {
        Value * vName = Value::toPtr(state.symbols[i.name]);
        Value * vFun2 = state.allocValue();
        vFun2->mkApp(args[0], vName);
        attrs.alloc(i.name).mkApp(vFun2, i.value);
    }

    v.mkAttrs(attrs.alreadySorted());
}

static RegisterPrimOp primop_mapAttrs({
    .name = "__mapAttrs",
    .args = {"f", "attrset"},
    .doc = R"(
      Apply function *f* to every element of *attrset*. For example,

      ```nix
      builtins.mapAttrs (name: value: value * 10) { a = 1; b = 2; }
      ```

      evaluates to `{ a = 10; b = 20; }`.
    )",
    .fun = prim_mapAttrs,
});

static void prim_zipAttrsWith(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    // we will first count how many values are present for each given key.
    // we then allocate a single attrset and pre-populate it with lists of
    // appropriate sizes, stash the pointers to the list elements of each,
    // and populate the lists. after that we replace the list in the every
    // attribute with the merge function application. this way we need not
    // use (slightly slower) temporary storage the GC does not know about.

    struct Item
    {
        size_t size = 0;
        size_t pos = 0;
        std::optional<ListBuilder> list;
    };

    std::map<Symbol, Item, std::less<Symbol>, traceable_allocator<std::pair<const Symbol, Item>>> attrsSeen;

    state.forceFunction(*args[0], pos, "while evaluating the first argument passed to builtins.zipAttrsWith");
    state.forceList(*args[1], pos, "while evaluating the second argument passed to builtins.zipAttrsWith");
    const auto listItems = args[1]->listView();

    for (auto & vElem : listItems) {
        state.forceAttrs(
            *vElem, noPos, "while evaluating a value of the list passed as second argument to builtins.zipAttrsWith");
        for (auto & attr : *vElem->attrs())
            attrsSeen.try_emplace(attr.name).first->second.size++;
    }

    for (auto & [sym, elem] : attrsSeen)
        elem.list.emplace(state.buildList(elem.size));

    for (auto & vElem : listItems) {
        for (auto & attr : *vElem->attrs()) {
            auto & item = attrsSeen.at(attr.name);
            (*item.list)[item.pos++] = attr.value;
        }
    }

    auto attrs = state.buildBindings(attrsSeen.size());

    for (auto & [sym, elem] : attrsSeen) {
        auto name = Value::toPtr(state.symbols[sym]);
        auto call1 = state.allocValue();
        call1->mkApp(args[0], name);
        auto call2 = state.allocValue();
        auto arg = state.allocValue();
        arg->mkList(*elem.list);
        call2->mkApp(call1, arg);
        attrs.insert(sym, call2);
    }

    v.mkAttrs(attrs.alreadySorted());
}

static RegisterPrimOp primop_zipAttrsWith({
    .name = "__zipAttrsWith",
    .args = {"f", "list"},
    .doc = R"(
      Transpose a list of attribute sets into an attribute set of lists,
      then apply `mapAttrs`.

      `f` receives two arguments: the attribute name and a non-empty
      list of all values encountered for that attribute name.

      The result is an attribute set where the attribute names are the
      union of the attribute names in each element of `list`. The attribute
      values are the return values of `f`.

      ```nix
      builtins.zipAttrsWith
        (name: values: { inherit name values; })
        [ { a = "x"; } { a = "y"; b = "z"; } ]
      ```

      evaluates to

      ```
      {
        a = { name = "a"; values = [ "x" "y" ]; };
        b = { name = "b"; values = [ "z" ]; };
      }
      ```
    )",
    .fun = prim_zipAttrsWith,
});

/*************************************************************
 * Lists
 *************************************************************/

/* Determine whether the argument is a list. */
static void prim_isList(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    state.forceValue(*args[0], pos);
    v.mkBool(args[0]->type() == nList);
}

static RegisterPrimOp primop_isList({
    .name = "__isList",
    .args = {"e"},
    .doc = R"(
      Return `true` if *e* evaluates to a list, and `false` otherwise.
    )",
    .fun = prim_isList,
});

/* Return the n-1'th element of a list. */
static void prim_elemAt(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    NixInt::Inner n =
        state.forceInt(*args[1], pos, "while evaluating the second argument passed to 'builtins.elemAt'").value;
    state.forceList(*args[0], pos, "while evaluating the first argument passed to 'builtins.elemAt'");
    if (n < 0 || std::make_unsigned_t<NixInt::Inner>(n) >= args[0]->listSize())
        state.error<EvalError>("'builtins.elemAt' called with index %d on a list of size %d", n, args[0]->listSize())
            .atPos(pos)
            .debugThrow();
    state.forceValue(*args[0]->listView()[n], pos);
    v = *args[0]->listView()[n];
}

static RegisterPrimOp primop_elemAt({
    .name = "__elemAt",
    .args = {"xs", "n"},
    .doc = R"(
      Return element *n* from the list *xs*. Elements are counted starting
      from 0. A fatal error occurs if the index is out of bounds.
    )",
    .fun = prim_elemAt,
});

/* Return the first element of a list. */
static void prim_head(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    state.forceList(*args[0], pos, "while evaluating the first argument passed to 'builtins.head'");
    if (args[0]->listSize() == 0)
        state.error<EvalError>("'builtins.head' called on an empty list").atPos(pos).debugThrow();
    state.forceValue(*args[0]->listView()[0], pos);
    v = *args[0]->listView()[0];
}

static RegisterPrimOp primop_head({
    .name = "__head",
    .args = {"list"},
    .doc = R"(
      Return the first element of a list; abort evaluation if the argument
      isn’t a list or is an empty list. You can test whether a list is
      empty by comparing it with `[]`.
    )",
    .fun = prim_head,
});

/* Return a list consisting of everything but the first element of
   a list.  Warning: this function takes O(n) time, so you probably
   don't want to use it!  */
static void prim_tail(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    state.forceList(*args[0], pos, "while evaluating the first argument passed to 'builtins.tail'");
    if (args[0]->listSize() == 0)
        state.error<EvalError>("'builtins.tail' called on an empty list").atPos(pos).debugThrow();

    auto list = state.buildList(args[0]->listSize() - 1);
    for (const auto & [n, v] : enumerate(list))
        v = args[0]->listView()[n + 1];
    v.mkList(list);
}

static RegisterPrimOp primop_tail({
    .name = "__tail",
    .args = {"list"},
    .doc = R"(
      Return the list without its first item; abort evaluation if
      the argument isn’t a list or is an empty list.

      > **Warning**
      >
      > This function should generally be avoided since it's inefficient:
      > unlike Haskell's `tail`, it takes O(n) time, so recursing over a
      > list by repeatedly calling `tail` takes O(n^2) time.
    )",
    .fun = prim_tail,
});

/* Apply a function to every element of a list. */
static void prim_map(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    state.forceList(*args[1], pos, "while evaluating the second argument passed to builtins.map");

    if (args[1]->listSize() == 0) {
        v = *args[1];
        return;
    }

    state.forceFunction(*args[0], pos, "while evaluating the first argument passed to builtins.map");

    auto list = state.buildList(args[1]->listSize());
    for (const auto & [n, v] : enumerate(list))
        (v = state.allocValue())->mkApp(args[0], args[1]->listView()[n]);
    v.mkList(list);
}

static RegisterPrimOp primop_map({
    .name = "map",
    .args = {"f", "list"},
    .doc = R"(
      Apply the function *f* to each element in the list *list*. For
      example,

      ```nix
      map (x: "foo" + x) [ "bar" "bla" "abc" ]
      ```

      evaluates to `[ "foobar" "foobla" "fooabc" ]`.
    )",
    .fun = prim_map,
});

/* Filter a list using a predicate; that is, return a list containing
   every element from the list for which the predicate function
   returns true. */
static void prim_filter(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    state.forceList(*args[1], pos, "while evaluating the second argument passed to builtins.filter");

    if (args[1]->listSize() == 0) {
        v = *args[1];
        return;
    }

    state.forceFunction(*args[0], pos, "while evaluating the first argument passed to builtins.filter");

    auto len = args[1]->listSize();
    SmallValueVector<nonRecursiveStackReservation> vs(len);
    size_t k = 0;

    bool same = true;
    for (size_t n = 0; n < len; ++n) {
        Value res;
        state.callFunction(*args[0], *args[1]->listView()[n], res, noPos);
        if (state.forceBool(
                res, pos, "while evaluating the return value of the filtering function passed to builtins.filter"))
            vs[k++] = args[1]->listView()[n];
        else
            same = false;
    }

    if (same)
        v = *args[1];
    else {
        auto list = state.buildList(k);
        for (const auto & [n, v] : enumerate(list))
            v = vs[n];
        v.mkList(list);
    }
}

static RegisterPrimOp primop_filter({
    .name = "__filter",
    .args = {"f", "list"},
    .doc = R"(
      Return a list consisting of the elements of *list* for which the
      function *f* returns `true`.
    )",
    .fun = prim_filter,
});

/* Return true if a list contains a given element. */
static void prim_elem(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    bool res = false;
    state.forceList(*args[1], pos, "while evaluating the second argument passed to builtins.elem");
    for (auto elem : args[1]->listView())
        if (state.eqValues(*args[0], *elem, pos, "while searching for the presence of the given element in the list")) {
            res = true;
            break;
        }
    v.mkBool(res);
}

static RegisterPrimOp primop_elem({
    .name = "__elem",
    .args = {"x", "xs"},
    .doc = R"(
      Return `true` if a value equal to *x* occurs in the list *xs*, and
      `false` otherwise.
    )",
    .fun = prim_elem,
});

/* Concatenate a list of lists. */
static void prim_concatLists(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    state.forceList(*args[0], pos, "while evaluating the first argument passed to builtins.concatLists");
    auto listView = args[0]->listView();
    state.concatLists(
        v,
        args[0]->listSize(),
        listView.data(),
        pos,
        "while evaluating a value of the list passed to builtins.concatLists");
}

static RegisterPrimOp primop_concatLists({
    .name = "__concatLists",
    .args = {"lists"},
    .doc = R"(
      Concatenate a list of lists into a single list.
    )",
    .fun = prim_concatLists,
});

/* Return the length of a list.  This is an O(1) time operation. */
static void prim_length(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    state.forceList(*args[0], pos, "while evaluating the first argument passed to builtins.length");
    v.mkInt(args[0]->listSize());
}

static RegisterPrimOp primop_length({
    .name = "__length",
    .args = {"e"},
    .doc = R"(
      Return the length of the list *e*.
    )",
    .fun = prim_length,
});

/* Reduce a list by applying a binary operator, from left to
   right. The operator is applied strictly. */
static void prim_foldlStrict(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    state.forceFunction(*args[0], pos, "while evaluating the first argument passed to builtins.foldlStrict");
    state.forceList(*args[2], pos, "while evaluating the third argument passed to builtins.foldlStrict");

    if (args[2]->listSize()) {
        Value * vCur = args[1];

        auto listView = args[2]->listView();
        for (auto [n, elem] : enumerate(listView)) {
            Value * vs[]{vCur, elem};
            vCur = n == args[2]->listSize() - 1 ? &v : state.allocValue();
            state.callFunction(*args[0], vs, *vCur, pos);
        }
        state.forceValue(v, pos);
    } else {
        state.forceValue(*args[1], pos);
        v = *args[1];
    }
}

static RegisterPrimOp primop_foldlStrict({
    .name = "__foldl'",
    .args = {"op", "nul", "list"},
    .doc = R"(
      Reduce a list by applying a binary operator, from left to right,
      e.g. `foldl' op nul [x0 x1 x2 ...] = op (op (op nul x0) x1) x2)
      ...`.

      For example, `foldl' (acc: elem: acc + elem) 0 [1 2 3]` evaluates
      to `6` and `foldl' (acc: elem: { "${elem}" = elem; } // acc) {}
      ["a" "b"]` evaluates to `{ a = "a"; b = "b"; }`.

      The first argument of `op` is the accumulator whereas the second
      argument is the current element being processed. The return value
      of each application of `op` is evaluated immediately, even for
      intermediate values.
    )",
    .fun = prim_foldlStrict,
});

static void anyOrAll(bool any, EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    state.forceFunction(
        *args[0], pos, std::string("while evaluating the first argument passed to builtins.") + (any ? "any" : "all"));
    state.forceList(
        *args[1], pos, std::string("while evaluating the second argument passed to builtins.") + (any ? "any" : "all"));

    std::string_view errorCtx = any ? "while evaluating the return value of the function passed to builtins.any"
                                    : "while evaluating the return value of the function passed to builtins.all";

    Value vTmp;
    for (auto elem : args[1]->listView()) {
        state.callFunction(*args[0], *elem, vTmp, pos);
        bool res = state.forceBool(vTmp, pos, errorCtx);
        if (res == any) {
            v.mkBool(any);
            return;
        }
    }

    v.mkBool(!any);
}

static void prim_any(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    anyOrAll(true, state, pos, args, v);
}

static RegisterPrimOp primop_any({
    .name = "__any",
    .args = {"pred", "list"},
    .doc = R"(
      Return `true` if the function *pred* returns `true` for at least one
      element of *list*, and `false` otherwise.
    )",
    .fun = prim_any,
});

static void prim_all(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    anyOrAll(false, state, pos, args, v);
}

static RegisterPrimOp primop_all({
    .name = "__all",
    .args = {"pred", "list"},
    .doc = R"(
      Return `true` if the function *pred* returns `true` for all elements
      of *list*, and `false` otherwise.
    )",
    .fun = prim_all,
});

static void prim_genList(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    auto len_ = state.forceInt(*args[1], pos, "while evaluating the second argument passed to builtins.genList").value;

    if (len_ < 0 || std::make_unsigned_t<NixInt::Inner>(len_) > std::numeric_limits<size_t>::max())
        state.error<EvalError>("cannot create list of size %1%", len_).atPos(pos).debugThrow();

    size_t len = size_t(len_);

    // More strict than strictly (!) necessary, but acceptable
    // as evaluating map without accessing any values makes little sense.
    state.forceFunction(*args[0], noPos, "while evaluating the first argument passed to builtins.genList");

    auto list = state.buildList(len);
    for (const auto & [n, v] : enumerate(list)) {
        auto arg = state.allocValue();
        arg->mkInt(n);
        (v = state.allocValue())->mkApp(args[0], arg);
    }
    v.mkList(list);
}

static RegisterPrimOp primop_genList({
    .name = "__genList",
    .args = {"generator", "length"},
    .doc = R"(
      Generate list of size *length*, with each element *i* equal to the
      value returned by *generator* `i`. For example,

      ```nix
      builtins.genList (x: x * x) 5
      ```

      returns the list `[ 0 1 4 9 16 ]`.
    )",
    .fun = prim_genList,
});

static void prim_lessThan(EvalState & state, const PosIdx pos, Value ** args, Value & v);

static void prim_sort(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    state.forceList(*args[1], pos, "while evaluating the second argument passed to builtins.sort");

    auto len = args[1]->listSize();
    if (len == 0) {
        v = *args[1];
        return;
    }

    state.forceFunction(*args[0], pos, "while evaluating the first argument passed to builtins.sort");

    auto list = state.buildList(len);
    for (const auto & [n, v] : enumerate(list))
        state.forceValue(*(v = args[1]->listView()[n]), pos);

    auto comparator = [&](Value * a, Value * b) {
        /* Optimization: if the comparator is lessThan, bypass
           callFunction. */
        if (args[0]->isPrimOp()) {
            auto ptr = args[0]->primOp()->fun.target<decltype(&prim_lessThan)>();
            if (ptr && *ptr == prim_lessThan)
                return CompareValues(state, noPos, "while evaluating the ordering function passed to builtins.sort")(
                    a, b);
        }

        Value * vs[] = {a, b};
        Value vBool;
        state.callFunction(*args[0], vs, vBool, noPos);
        return state.forceBool(
            vBool, pos, "while evaluating the return value of the sorting function passed to builtins.sort");
    };

    /* NOTE: Using custom implementation because std::sort and std::stable_sort
       are not resilient to comparators that violate strict weak ordering. Diagnosing
       incorrect implementations is a O(n^3) problem, so doing the checks is much more
       expensive that doing the sorting. For this reason we choose to use sorting algorithms
       that are can't be broken by invalid comprators. peeksort (mergesort)
       doesn't misbehave when any of the strict weak order properties is
       violated - output is always a reordering of the input. */
    peeksort(list.begin(), list.end(), comparator);

    v.mkList(list);
}

static RegisterPrimOp primop_sort({
    .name = "__sort",
    .args = {"comparator", "list"},
    .doc = R"(
      Return *list* in sorted order. It repeatedly calls the function
      *comparator* with two elements. The comparator should return `true`
      if the first element is less than the second, and `false` otherwise.
      For example,

      ```nix
      builtins.sort builtins.lessThan [ 483 249 526 147 42 77 ]
      ```

      produces the list `[ 42 77 147 249 483 526 ]`.

      This is a stable sort: it preserves the relative order of elements
      deemed equal by the comparator.

      *comparator* must impose a strict weak ordering on the set of values
      in the *list*. This means that for any elements *a*, *b* and *c* from the
      *list*, *comparator* must satisfy the following relations:

        1. Transitivity

        If a is less than b and b is less than c, then it follows that a is less than c.

        ```nix
        comparator a b && comparator b c -> comparator a c
        ```

        1. Irreflexivity

        ```nix
        comparator a a == false
        ```

        1. Transitivity of equivalence

        First, two values a and b are considered equivalent with respect to the comparator if:

        ```
        !comparator a b && !comparator b a
        ```

        In other words, neither is considered "less than" the other.

        Transitivity of equivalence means:

        If a is equivalent to b, and b is equivalent to c, then a must also be equivalent to c.

        ```nix
        let
          equiv = x: y: (!comparator x y && !comparator y x);
        in
          equiv a b && equiv b c -> equiv a c
        ```

      If the *comparator* violates any of these properties, then `builtins.sort`
      reorders elements in an unspecified manner.
    )",
    .fun = prim_sort,
});

static void prim_partition(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    state.forceFunction(*args[0], pos, "while evaluating the first argument passed to builtins.partition");
    state.forceList(*args[1], pos, "while evaluating the second argument passed to builtins.partition");

    auto len = args[1]->listSize();

    ValueVector right, wrong;

    for (size_t n = 0; n < len; ++n) {
        auto vElem = args[1]->listView()[n];
        state.forceValue(*vElem, pos);
        Value res;
        state.callFunction(*args[0], *vElem, res, pos);
        if (state.forceBool(
                res, pos, "while evaluating the return value of the partition function passed to builtins.partition"))
            right.push_back(vElem);
        else
            wrong.push_back(vElem);
    }

    auto attrs = state.buildBindings(2);

    auto rsize = right.size();
    auto rlist = state.buildList(rsize);
    if (rsize)
        memcpy(rlist.elems, right.data(), sizeof(Value *) * rsize);
    attrs.alloc(state.s.right).mkList(rlist);

    auto wsize = wrong.size();
    auto wlist = state.buildList(wsize);
    if (wsize)
        memcpy(wlist.elems, wrong.data(), sizeof(Value *) * wsize);
    attrs.alloc(state.s.wrong).mkList(wlist);

    v.mkAttrs(attrs);
}

static RegisterPrimOp primop_partition({
    .name = "__partition",
    .args = {"pred", "list"},
    .doc = R"(
      Given a predicate function *pred*, this function returns an
      attrset containing a list named `right`, containing the elements
      in *list* for which *pred* returned `true`, and a list named
      `wrong`, containing the elements for which it returned
      `false`. For example,

      ```nix
      builtins.partition (x: x > 10) [1 23 9 3 42]
      ```

      evaluates to

      ```nix
      { right = [ 23 42 ]; wrong = [ 1 9 3 ]; }
      ```
    )",
    .fun = prim_partition,
});

static void prim_groupBy(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    state.forceFunction(*args[0], pos, "while evaluating the first argument passed to builtins.groupBy");
    state.forceList(*args[1], pos, "while evaluating the second argument passed to builtins.groupBy");

    ValueVectorMap attrs;

    for (auto vElem : args[1]->listView()) {
        Value res;
        state.callFunction(*args[0], *vElem, res, pos);
        auto name = state.forceStringNoCtx(
            res, pos, "while evaluating the return value of the grouping function passed to builtins.groupBy");
        auto sym = state.symbols.create(name);
        auto vector = attrs.try_emplace<ValueVector>(sym, {}).first;
        vector->second.push_back(vElem);
    }

    auto attrs2 = state.buildBindings(attrs.size());

    for (auto & i : attrs) {
        auto size = i.second.size();
        auto list = state.buildList(size);
        memcpy(list.elems, i.second.data(), sizeof(Value *) * size);
        attrs2.alloc(i.first).mkList(list);
    }

    v.mkAttrs(attrs2.alreadySorted());
}

static RegisterPrimOp primop_groupBy({
    .name = "__groupBy",
    .args = {"f", "list"},
    .doc = R"(
      Groups elements of *list* together by the string returned from the
      function *f* called on each element. It returns an attribute set
      where each attribute value contains the elements of *list* that are
      mapped to the same corresponding attribute name returned by *f*.

      For example,

      ```nix
      builtins.groupBy (builtins.substring 0 1) ["foo" "bar" "baz"]
      ```

      evaluates to

      ```nix
      { b = [ "bar" "baz" ]; f = [ "foo" ]; }
      ```
    )",
    .fun = prim_groupBy,
});

static void prim_concatMap(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    state.forceFunction(*args[0], pos, "while evaluating the first argument passed to builtins.concatMap");
    state.forceList(*args[1], pos, "while evaluating the second argument passed to builtins.concatMap");
    auto nrLists = args[1]->listSize();

    // List of returned lists before concatenation. References to these Values must NOT be persisted.
    SmallTemporaryValueVector<conservativeStackReservation> lists(nrLists);
    size_t len = 0;

    for (size_t n = 0; n < nrLists; ++n) {
        Value * vElem = args[1]->listView()[n];
        state.callFunction(*args[0], *vElem, lists[n], pos);
        state.forceList(
            lists[n],
            lists[n].determinePos(args[0]->determinePos(pos)),
            "while evaluating the return value of the function passed to builtins.concatMap");
        len += lists[n].listSize();
    }

    auto list = state.buildList(len);
    auto out = list.elems;
    for (size_t n = 0, pos = 0; n < nrLists; ++n) {
        auto listView = lists[n].listView();
        auto l = listView.size();
        if (l)
            memcpy(out + pos, listView.data(), l * sizeof(Value *));
        pos += l;
    }
    v.mkList(list);
}

static RegisterPrimOp primop_concatMap({
    .name = "__concatMap",
    .args = {"f", "list"},
    .doc = R"(
      This function is equivalent to `builtins.concatLists (map f list)`
      but is more efficient.
    )",
    .fun = prim_concatMap,
});

/*************************************************************
 * Integer arithmetic
 *************************************************************/

static void prim_add(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    state.forceValue(*args[0], pos);
    state.forceValue(*args[1], pos);
    if (args[0]->type() == nFloat || args[1]->type() == nFloat)
        v.mkFloat(
            state.forceFloat(*args[0], pos, "while evaluating the first argument of the addition")
            + state.forceFloat(*args[1], pos, "while evaluating the second argument of the addition"));
    else {
        auto i1 = state.forceInt(*args[0], pos, "while evaluating the first argument of the addition");
        auto i2 = state.forceInt(*args[1], pos, "while evaluating the second argument of the addition");

        auto result_ = i1 + i2;
        if (auto result = result_.valueChecked(); result.has_value()) {
            v.mkInt(*result);
        } else {
            state.error<EvalError>("integer overflow in adding %1% + %2%", i1, i2).atPos(pos).debugThrow();
        }
    }
}

static RegisterPrimOp primop_add({
    .name = "__add",
    .args = {"e1", "e2"},
    .doc = R"(
      Return the sum of the numbers *e1* and *e2*.
    )",
    .fun = prim_add,
});

static void prim_sub(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    state.forceValue(*args[0], pos);
    state.forceValue(*args[1], pos);
    if (args[0]->type() == nFloat || args[1]->type() == nFloat)
        v.mkFloat(
            state.forceFloat(*args[0], pos, "while evaluating the first argument of the subtraction")
            - state.forceFloat(*args[1], pos, "while evaluating the second argument of the subtraction"));
    else {
        auto i1 = state.forceInt(*args[0], pos, "while evaluating the first argument of the subtraction");
        auto i2 = state.forceInt(*args[1], pos, "while evaluating the second argument of the subtraction");

        auto result_ = i1 - i2;

        if (auto result = result_.valueChecked(); result.has_value()) {
            v.mkInt(*result);
        } else {
            state.error<EvalError>("integer overflow in subtracting %1% - %2%", i1, i2).atPos(pos).debugThrow();
        }
    }
}

static RegisterPrimOp primop_sub({
    .name = "__sub",
    .args = {"e1", "e2"},
    .doc = R"(
      Return the difference between the numbers *e1* and *e2*.
    )",
    .fun = prim_sub,
});

static void prim_mul(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    state.forceValue(*args[0], pos);
    state.forceValue(*args[1], pos);
    if (args[0]->type() == nFloat || args[1]->type() == nFloat)
        v.mkFloat(
            state.forceFloat(*args[0], pos, "while evaluating the first of the multiplication")
            * state.forceFloat(*args[1], pos, "while evaluating the second argument of the multiplication"));
    else {
        auto i1 = state.forceInt(*args[0], pos, "while evaluating the first argument of the multiplication");
        auto i2 = state.forceInt(*args[1], pos, "while evaluating the second argument of the multiplication");

        auto result_ = i1 * i2;

        if (auto result = result_.valueChecked(); result.has_value()) {
            v.mkInt(*result);
        } else {
            state.error<EvalError>("integer overflow in multiplying %1% * %2%", i1, i2).atPos(pos).debugThrow();
        }
    }
}

static RegisterPrimOp primop_mul({
    .name = "__mul",
    .args = {"e1", "e2"},
    .doc = R"(
      Return the product of the numbers *e1* and *e2*.
    )",
    .fun = prim_mul,
});

static void prim_div(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    state.forceValue(*args[0], pos);
    state.forceValue(*args[1], pos);

    NixFloat f2 = state.forceFloat(*args[1], pos, "while evaluating the second operand of the division");
    if (f2 == 0)
        state.error<EvalError>("division by zero").atPos(pos).debugThrow();

    if (args[0]->type() == nFloat || args[1]->type() == nFloat) {
        v.mkFloat(state.forceFloat(*args[0], pos, "while evaluating the first operand of the division") / f2);
    } else {
        NixInt i1 = state.forceInt(*args[0], pos, "while evaluating the first operand of the division");
        NixInt i2 = state.forceInt(*args[1], pos, "while evaluating the second operand of the division");
        /* Avoid division overflow as it might raise SIGFPE. */
        auto result_ = i1 / i2;
        if (auto result = result_.valueChecked(); result.has_value()) {
            v.mkInt(*result);
        } else {
            state.error<EvalError>("integer overflow in dividing %1% / %2%", i1, i2).atPos(pos).debugThrow();
        }
    }
}

static RegisterPrimOp primop_div({
    .name = "__div",
    .args = {"e1", "e2"},
    .doc = R"(
      Return the quotient of the numbers *e1* and *e2*.
    )",
    .fun = prim_div,
});

static void prim_bitAnd(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    auto i1 = state.forceInt(*args[0], pos, "while evaluating the first argument passed to builtins.bitAnd");
    auto i2 = state.forceInt(*args[1], pos, "while evaluating the second argument passed to builtins.bitAnd");
    v.mkInt(i1.value & i2.value);
}

static RegisterPrimOp primop_bitAnd({
    .name = "__bitAnd",
    .args = {"e1", "e2"},
    .doc = R"(
      Return the bitwise AND of the integers *e1* and *e2*.
    )",
    .fun = prim_bitAnd,
});

static void prim_bitOr(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    auto i1 = state.forceInt(*args[0], pos, "while evaluating the first argument passed to builtins.bitOr");
    auto i2 = state.forceInt(*args[1], pos, "while evaluating the second argument passed to builtins.bitOr");

    v.mkInt(i1.value | i2.value);
}

static RegisterPrimOp primop_bitOr({
    .name = "__bitOr",
    .args = {"e1", "e2"},
    .doc = R"(
      Return the bitwise OR of the integers *e1* and *e2*.
    )",
    .fun = prim_bitOr,
});

static void prim_bitXor(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    auto i1 = state.forceInt(*args[0], pos, "while evaluating the first argument passed to builtins.bitXor");
    auto i2 = state.forceInt(*args[1], pos, "while evaluating the second argument passed to builtins.bitXor");

    v.mkInt(i1.value ^ i2.value);
}

static RegisterPrimOp primop_bitXor({
    .name = "__bitXor",
    .args = {"e1", "e2"},
    .doc = R"(
      Return the bitwise XOR of the integers *e1* and *e2*.
    )",
    .fun = prim_bitXor,
});

static void prim_lessThan(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    state.forceValue(*args[0], pos);
    state.forceValue(*args[1], pos);
    // pos is exact here, no need for a message.
    CompareValues comp(state, noPos, "");
    v.mkBool(comp(args[0], args[1]));
}

static RegisterPrimOp primop_lessThan({
    .name = "__lessThan",
    .args = {"e1", "e2"},
    .doc = R"(
      Return `true` if the value *e1* is less than the value *e2*, and `false` otherwise.
      Evaluation aborts if either *e1* or *e2* does not evaluate to a number, string or path.
      Furthermore, it aborts if *e2* does not match *e1*'s type according to the aforementioned classification of number, string or path.
    )",
    .fun = prim_lessThan,
});

/*************************************************************
 * String manipulation
 *************************************************************/

/* Convert the argument to a string.  Paths are *not* copied to the
   store, so `toString /foo/bar' yields `"/foo/bar"', not
   `"/nix/store/whatever..."'. */
static void prim_toString(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    NixStringContext context;
    auto s = state.coerceToString(
        pos, *args[0], context, "while evaluating the first argument passed to builtins.toString", true, false);
    v.mkString(*s, context, state.mem);
}

static RegisterPrimOp primop_toString({
    .name = "toString",
    .args = {"e"},
    .doc = R"(
      Convert the expression *e* to a string. *e* can be:

        - A string (in which case the string is returned unmodified).

        - A path (e.g., `toString /foo/bar` yields `"/foo/bar"`.

        - A set containing `{ __toString = self: ...; }` or `{ outPath = ...; }`.

        - An integer.

        - A list, in which case the string representations of its elements
          are joined with spaces.

        - A Boolean (`false` yields `""`, `true` yields `"1"`).

        - `null`, which yields the empty string.
    )",
    .fun = prim_toString,
});

/* `substring start len str' returns the substring of `str' starting
   at byte position `min(start, stringLength str)' inclusive and
   ending at `min(start + len, stringLength str)'.  `start' must be
   non-negative. */
static void prim_substring(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    using NixUInt = std::make_unsigned_t<NixInt::Inner>;
    NixInt::Inner start =
        state
            .forceInt(
                *args[0], pos, "while evaluating the first argument (the start offset) passed to builtins.substring")
            .value;

    if (start < 0)
        state.error<EvalError>("negative start position in 'substring'").atPos(pos).debugThrow();

    NixInt::Inner len =
        state
            .forceInt(
                *args[1],
                pos,
                "while evaluating the second argument (the substring length) passed to builtins.substring")
            .value;

    // Negative length may be idiomatically passed to builtins.substring to get
    // the tail of the string.
    auto _len = std::numeric_limits<std::string::size_type>::max();

    // Special-case on empty substring to avoid O(n) strlen
    // This allows for the use of empty substrings to efficiently capture string context
    if (len == 0) {
        state.forceValue(*args[2], pos);
        if (args[2]->type() == nString) {
            v.mkStringNoCopy(""_sds, args[2]->context());
            return;
        }
    }

    if (len >= 0 && NixUInt(len) < _len) {
        _len = len;
    }

    NixStringContext context;
    auto s = state.coerceToString(
        pos, *args[2], context, "while evaluating the third argument (the string) passed to builtins.substring");

    v.mkString(NixUInt(start) >= s->size() ? "" : s->substr(start, _len), context, state.mem);
}

static RegisterPrimOp primop_substring({
    .name = "__substring",
    .args = {"start", "len", "s"},
    .doc = R"(
      Return the substring of *s* from byte position *start*
      (zero-based) up to but not including *start + len*. If *start* is
      greater than the length of the string, an empty string is returned.
      If *start + len* lies beyond the end of the string or *len* is `-1`,
      only the substring up to the end of the string is returned.
      *start* must be non-negative.
      For example,

      ```nix
      builtins.substring 0 3 "nixos"
      ```

      evaluates to `"nix"`.
    )",
    .fun = prim_substring,
});

static void prim_stringLength(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    NixStringContext context;
    auto s =
        state.coerceToString(pos, *args[0], context, "while evaluating the argument passed to builtins.stringLength");
    v.mkInt(NixInt::Inner(s->size()));
}

static RegisterPrimOp primop_stringLength({
    .name = "__stringLength",
    .args = {"e"},
    .doc = R"(
      Return the number of bytes of the string *e*. If *e* is not a string,
      evaluation is aborted.
    )",
    .fun = prim_stringLength,
});

/* Return the cryptographic hash of a string in base-16. */
static void prim_hashString(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    auto algo =
        state.forceStringNoCtx(*args[0], pos, "while evaluating the first argument passed to builtins.hashString");
    std::optional<HashAlgorithm> ha = parseHashAlgo(algo);
    if (!ha)
        state.error<EvalError>("unknown hash algorithm '%1%'", algo).atPos(pos).debugThrow();

    NixStringContext context; // discarded
    auto s =
        state.forceString(*args[1], context, pos, "while evaluating the second argument passed to builtins.hashString");

    v.mkString(hashString(*ha, s).to_string(HashFormat::Base16, false), state.mem);
}

static RegisterPrimOp primop_hashString({
    .name = "__hashString",
    .args = {"type", "s"},
    .doc = R"(
      Return a base-16 representation of the cryptographic hash of string
      *s*. The hash algorithm specified by *type* must be one of `"md5"`,
      `"sha1"`, `"sha256"` or `"sha512"`.
    )",
    .fun = prim_hashString,
});

static void prim_convertHash(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    state.forceAttrs(*args[0], pos, "while evaluating the first argument passed to builtins.convertHash");
    auto inputAttrs = args[0]->attrs();

    auto iteratorHash = state.getAttr(state.symbols.create("hash"), inputAttrs, "while locating the attribute 'hash'");
    auto hash = state.forceStringNoCtx(*iteratorHash->value, pos, "while evaluating the attribute 'hash'");

    auto iteratorHashAlgo = inputAttrs->get(state.symbols.create("hashAlgo"));
    std::optional<HashAlgorithm> ha = std::nullopt;
    if (iteratorHashAlgo)
        ha = parseHashAlgo(
            state.forceStringNoCtx(*iteratorHashAlgo->value, pos, "while evaluating the attribute 'hashAlgo'"));

    auto iteratorToHashFormat = state.getAttr(
        state.symbols.create("toHashFormat"), args[0]->attrs(), "while locating the attribute 'toHashFormat'");
    HashFormat hf = parseHashFormat(
        state.forceStringNoCtx(*iteratorToHashFormat->value, pos, "while evaluating the attribute 'toHashFormat'"));

    v.mkString(Hash::parseAny(hash, ha).to_string(hf, hf == HashFormat::SRI), state.mem);
}

static RegisterPrimOp primop_convertHash({
    .name = "__convertHash",
    .args = {"args"},
    .doc = R"(
      Return the specified representation of a hash string, based on the attributes presented in *args*:

      - `hash`

        The hash to be converted.
        The hash format is detected automatically.

      - `hashAlgo`

        The algorithm used to create the hash. Must be one of
        - `"md5"`
        - `"sha1"`
        - `"sha256"`
        - `"sha512"`

        The attribute may be omitted when `hash` is an [SRI hash](https://www.w3.org/TR/SRI/#the-integrity-attribute) or when the hash is prefixed with the hash algorithm name followed by a colon.
        That `<hashAlgo>:<hashBody>` syntax is supported for backwards compatibility with existing tooling.

      - `toHashFormat`

        The format of the resulting hash. Must be one of
        - `"base16"`
        - `"nix32"`
        - `"base32"` (deprecated alias for `"nix32"`)
        - `"base64"`
        - `"sri"`

      The result hash is the *toHashFormat* representation of the hash *hash*.

      > **Example**
      >
      >   Convert a SHA256 hash in Base16 to SRI:
      >
      > ```nix
      > builtins.convertHash {
      >   hash = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
      >   toHashFormat = "sri";
      >   hashAlgo = "sha256";
      > }
      > ```
      >
      >     "sha256-47DEQpj8HBSa+/TImW+5JCeuQeRkm5NMpJWZG3hSuFU="

      > **Example**
      >
      >   Convert a SHA256 hash in SRI to Base16:
      >
      > ```nix
      > builtins.convertHash {
      >   hash = "sha256-47DEQpj8HBSa+/TImW+5JCeuQeRkm5NMpJWZG3hSuFU=";
      >   toHashFormat = "base16";
      > }
      > ```
      >
      >     "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"

      > **Example**
      >
      >   Convert a hash in the form `<hashAlgo>:<hashBody>` in Base16 to SRI:
      >
      > ```nix
      > builtins.convertHash {
      >   hash = "sha256:e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
      >   toHashFormat = "sri";
      > }
      > ```
      >
      >     "sha256-47DEQpj8HBSa+/TImW+5JCeuQeRkm5NMpJWZG3hSuFU="
    )",
    .fun = prim_convertHash,
});

struct RegexCache
{
    struct Entry
    {
        ref<const std::regex> regex;

        Entry(const char * s, size_t count)
            : regex(make_ref<const std::regex>(s, count, std::regex::extended))
        {
        }
    };

    boost::concurrent_flat_map<std::string, Entry, StringViewHash, std::equal_to<>> cache;

    ref<const std::regex> get(std::string_view re)
    {
        std::optional<ref<const std::regex>> regex;
        cache.try_emplace_and_cvisit(
            re,
            /*s=*/re.data(),
            /*count=*/re.size(),
            [&regex](const auto & kv) { regex = kv.second.regex; },
            [&regex](const auto & kv) { regex = kv.second.regex; });
        return *regex;
    }
};

ref<RegexCache> makeRegexCache()
{
    return make_ref<RegexCache>();
}

void prim_match(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    auto re = state.forceStringNoCtx(*args[0], pos, "while evaluating the first argument passed to builtins.match");

    try {

        auto regex = state.regexCache->get(re);

        NixStringContext context;
        const auto str =
            state.forceString(*args[1], context, pos, "while evaluating the second argument passed to builtins.match");

        std::cmatch match;
        if (!std::regex_match(str.begin(), str.end(), match, *regex)) {
            v.mkNull();
            return;
        }

        // the first match is the whole string
        auto list = state.buildList(match.size() - 1);
        for (const auto & [i, v2] : enumerate(list))
            if (!match[i + 1].matched)
                v2 = &Value::vNull;
            else
                v2 = mkString(state, match[i + 1]);
        v.mkList(list);

    } catch (std::regex_error & e) {
        if (e.code() == std::regex_constants::error_space) {
            // limit is _GLIBCXX_REGEX_STATE_LIMIT for libstdc++
            state.error<EvalError>("memory limit exceeded by regular expression '%s'", re).atPos(pos).debugThrow();
        } else
            state.error<EvalError>("invalid regular expression '%s'", re).atPos(pos).debugThrow();
    }
}

static RegisterPrimOp primop_match({
    .name = "__match",
    .args = {"regex", "str"},
    .doc = R"s(
      Returns a list if the [extended POSIX regular
      expression](http://pubs.opengroup.org/onlinepubs/9699919799/basedefs/V1_chap09.html#tag_09_04)
      *regex* matches *str* precisely, otherwise returns `null`. Each item
      in the list is a regex group.

      ```nix
      builtins.match "ab" "abc"
      ```

      Evaluates to `null`.

      ```nix
      builtins.match "abc" "abc"
      ```

      Evaluates to `[ ]`.

      ```nix
      builtins.match "a(b)(c)" "abc"
      ```

      Evaluates to `[ "b" "c" ]`.

      ```nix
      builtins.match "[[:space:]]+([[:upper:]]+)[[:space:]]+" "  FOO   "
      ```

      Evaluates to `[ "FOO" ]`.
    )s",
    .fun = prim_match,
});

/* Split a string with a regular expression, and return a list of the
   non-matching parts interleaved by the lists of the matching groups. */
void prim_split(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    auto re = state.forceStringNoCtx(*args[0], pos, "while evaluating the first argument passed to builtins.split");

    try {

        auto regex = state.regexCache->get(re);

        NixStringContext context;
        const auto str =
            state.forceString(*args[1], context, pos, "while evaluating the second argument passed to builtins.split");

        auto begin = std::cregex_iterator(str.begin(), str.end(), *regex);
        auto end = std::cregex_iterator();

        // Any matches results are surrounded by non-matching results.
        const size_t len = std::distance(begin, end);
        auto list = state.buildList(2 * len + 1);
        size_t idx = 0;

        if (len == 0) {
            list[0] = args[1];
            v.mkList(list);
            return;
        }

        for (auto i = begin; i != end; ++i) {
            assert(idx <= 2 * len + 1 - 3);
            const auto & match = *i;

            // Add a string for non-matched characters.
            list[idx++] = mkString(state, match.prefix());

            // Add a list for matched substrings.
            const size_t slen = match.size() - 1;

            // Start at 1, because the first match is the whole string.
            auto list2 = state.buildList(slen);
            for (const auto & [si, v2] : enumerate(list2)) {
                if (!match[si + 1].matched)
                    v2 = &Value::vNull;
                else
                    v2 = mkString(state, match[si + 1]);
            }

            (list[idx++] = state.allocValue())->mkList(list2);

            // Add a string for non-matched suffix characters.
            if (idx == 2 * len)
                list[idx++] = mkString(state, match.suffix());
        }

        assert(idx == 2 * len + 1);

        v.mkList(list);

    } catch (std::regex_error & e) {
        if (e.code() == std::regex_constants::error_space) {
            // limit is _GLIBCXX_REGEX_STATE_LIMIT for libstdc++
            state.error<EvalError>("memory limit exceeded by regular expression '%s'", re).atPos(pos).debugThrow();
        } else
            state.error<EvalError>("invalid regular expression '%s'", re).atPos(pos).debugThrow();
    }
}

static RegisterPrimOp primop_split({
    .name = "__split",
    .args = {"regex", "str"},
    .doc = R"s(
      Returns a list composed of non matched strings interleaved with the
      lists of the [extended POSIX regular
      expression](http://pubs.opengroup.org/onlinepubs/9699919799/basedefs/V1_chap09.html#tag_09_04)
      *regex* matches of *str*. Each item in the lists of matched
      sequences is a regex group.

      ```nix
      builtins.split "(a)b" "abc"
      ```

      Evaluates to `[ "" [ "a" ] "c" ]`.

      ```nix
      builtins.split "([ac])" "abc"
      ```

      Evaluates to `[ "" [ "a" ] "b" [ "c" ] "" ]`.

      ```nix
      builtins.split "(a)|(c)" "abc"
      ```

      Evaluates to `[ "" [ "a" null ] "b" [ null "c" ] "" ]`.

      ```nix
      builtins.split "([[:upper:]]+)" " FOO "
      ```

      Evaluates to `[ " " [ "FOO" ] " " ]`.
    )s",
    .fun = prim_split,
});

static void prim_concatStringsSep(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    NixStringContext context;

    auto sep = state.forceString(
        *args[0],
        context,
        pos,
        "while evaluating the first argument (the separator string) passed to builtins.concatStringsSep");
    state.forceList(
        *args[1],
        pos,
        "while evaluating the second argument (the list of strings to concat) passed to builtins.concatStringsSep");

    std::string res;
    res.reserve((args[1]->listSize() + 32) * sep.size());
    bool first = true;

    for (auto elem : args[1]->listView()) {
        if (first)
            first = false;
        else
            res += sep;
        res += *state.coerceToString(
            pos,
            *elem,
            context,
            "while evaluating one element of the list of strings to concat passed to builtins.concatStringsSep");
    }

    v.mkString(res, context, state.mem);
}

static RegisterPrimOp primop_concatStringsSep({
    .name = "__concatStringsSep",
    .args = {"separator", "list"},
    .doc = R"(
      Concatenate a list of strings with a separator between each
      element, e.g. `concatStringsSep "/" ["usr" "local" "bin"] ==
      "usr/local/bin"`.
    )",
    .fun = prim_concatStringsSep,
});

static void prim_replaceStrings(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    state.forceList(*args[0], pos, "while evaluating the first argument passed to builtins.replaceStrings");
    state.forceList(*args[1], pos, "while evaluating the second argument passed to builtins.replaceStrings");
    if (args[0]->listSize() != args[1]->listSize())
        state.error<EvalError>("'from' and 'to' arguments passed to builtins.replaceStrings have different lengths")
            .atPos(pos)
            .debugThrow();

    std::vector<std::string_view> from;
    from.reserve(args[0]->listSize());
    for (auto elem : args[0]->listView())
        from.emplace_back(state.forceString(
            *elem, pos, "while evaluating one of the strings to replace passed to builtins.replaceStrings"));

    boost::unordered_flat_map<size_t, std::string_view> cache;
    auto to = args[1]->listView();

    NixStringContext context;
    auto s = state.forceString(
        *args[2], context, pos, "while evaluating the third argument passed to builtins.replaceStrings");

    std::string res;
    // Loops one past last character to handle the case where 'from' contains an empty string.
    for (size_t p = 0; p <= s.size();) {
        bool found = false;
        auto i = from.begin();
        auto j = to.begin();
        size_t j_index = 0;
        for (; i != from.end(); ++i, ++j, ++j_index)
            if (s.compare(p, i->size(), *i) == 0) {
                found = true;
                auto v = cache.find(j_index);
                if (v == cache.end()) {
                    NixStringContext ctx;
                    auto ts = state.forceString(
                        **j,
                        ctx,
                        pos,
                        "while evaluating one of the replacement strings passed to builtins.replaceStrings");
                    v = (cache.emplace(j_index, ts)).first;
                    for (auto & path : ctx)
                        context.insert(path);
                }
                res += v->second;
                if (i->empty()) {
                    if (p < s.size())
                        res += s[p];
                    p++;
                } else {
                    p += i->size();
                }
                break;
            }
        if (!found) {
            if (p < s.size())
                res += s[p];
            p++;
        }
    }

    v.mkString(res, context, state.mem);
}

static RegisterPrimOp primop_replaceStrings({
    .name = "__replaceStrings",
    .args = {"from", "to", "s"},
    .doc = R"(
      Given string *s*, replace every occurrence of the strings in *from*
      with the corresponding string in *to*.

      The argument *to* is lazy, that is, it is only evaluated when its corresponding pattern in *from* is matched in the string *s*

      Example:

      ```nix
      builtins.replaceStrings ["oo" "a"] ["a" "i"] "foobar"
      ```

      evaluates to `"fabir"`.
    )",
    .fun = prim_replaceStrings,
});

/*************************************************************
 * Versions
 *************************************************************/

static void prim_parseDrvName(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    auto name =
        state.forceStringNoCtx(*args[0], pos, "while evaluating the first argument passed to builtins.parseDrvName");
    DrvName parsed(name);
    auto attrs = state.buildBindings(2);
    attrs.alloc(state.s.name).mkString(parsed.name, state.mem);
    attrs.alloc("version").mkString(parsed.version, state.mem);
    v.mkAttrs(attrs);
}

static RegisterPrimOp primop_parseDrvName({
    .name = "__parseDrvName",
    .args = {"s"},
    .doc = R"(
      Split the string *s* into a package name and version. The package
      name is everything up to but not including the first dash not followed
      by a letter, and the version is everything following that dash. The
      result is returned in a set `{ name, version }`. Thus,
      `builtins.parseDrvName "nix-0.12pre12876"` returns `{ name =
      "nix"; version = "0.12pre12876"; }`.
    )",
    .fun = prim_parseDrvName,
});

static void prim_compareVersions(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    auto version1 =
        state.forceStringNoCtx(*args[0], pos, "while evaluating the first argument passed to builtins.compareVersions");
    auto version2 = state.forceStringNoCtx(
        *args[1], pos, "while evaluating the second argument passed to builtins.compareVersions");
    auto result = compareVersions(version1, version2);
    v.mkInt(result < 0 ? -1 : result > 0 ? 1 : 0);
}

static RegisterPrimOp primop_compareVersions({
    .name = "__compareVersions",
    .args = {"s1", "s2"},
    .doc = R"(
      Compare two strings representing versions and return `-1` if
      version *s1* is older than version *s2*, `0` if they are the same,
      and `1` if *s1* is newer than *s2*. The version comparison
      algorithm is the same as the one used by [`nix-env
      -u`](../command-ref/nix-env/upgrade.md).
    )",
    .fun = prim_compareVersions,
});

static void prim_splitVersion(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    auto version =
        state.forceStringNoCtx(*args[0], pos, "while evaluating the first argument passed to builtins.splitVersion");
    auto iter = version.cbegin();
    Strings components;
    while (iter != version.cend()) {
        auto component = nextComponent(iter, version.cend());
        if (component.empty())
            break;
        components.emplace_back(component);
    }
    auto list = state.buildList(components.size());
    for (const auto & [n, component] : enumerate(components))
        (list[n] = state.allocValue())->mkString(std::move(component), state.mem);
    v.mkList(list);
}

static RegisterPrimOp primop_splitVersion({
    .name = "__splitVersion",
    .args = {"s"},
    .doc = R"(
      Split a string representing a version into its components, by the
      same version splitting logic underlying the version comparison in
      [`nix-env -u`](../command-ref/nix-env/upgrade.md).
    )",
    .fun = prim_splitVersion,
});

/*************************************************************
 * Primop registration
 *************************************************************/

RegisterPrimOp::RegisterPrimOp(PrimOp && primOp)
{
    primOps().push_back(std::move(primOp));
}

void EvalState::createBaseEnv(const EvalSettings & evalSettings)
{
    baseEnv.up = 0;

    /* Add global constants such as `true' to the base environment. */
    Value v;

    /* `builtins' must be first! */
    v.mkAttrs(buildBindings(128).finish());
    addConstant(
        "builtins",
        v,
        {
            .type = nAttrs,
            .doc = R"(
          Contains all the built-in functions and values.

          Since built-in functions were added over time, [testing for attributes](./operators.md#has-attribute) in `builtins` can be used for graceful fallback on older Nix installations:

          ```nix
          # if hasContext is not available, we assume `s` has a context
          if builtins ? hasContext then builtins.hasContext s else true
          ```
        )",
        });

    v.mkBool(true);
    addConstant(
        "true",
        v,
        {
            .type = nBool,
            .doc = R"(
          Primitive value.

          It can be returned by
          [comparison operators](@docroot@/language/operators.md#comparison)
          and used in
          [conditional expressions](@docroot@/language/syntax.md#conditionals).

          The name `true` is not special, and can be shadowed:

          ```nix-repl
          nix-repl> let true = 1; in true
          1
          ```
        )",
        });

    v.mkBool(false);
    addConstant(
        "false",
        v,
        {
            .type = nBool,
            .doc = R"(
          Primitive value.

          It can be returned by
          [comparison operators](@docroot@/language/operators.md#comparison)
          and used in
          [conditional expressions](@docroot@/language/syntax.md#conditionals).

          The name `false` is not special, and can be shadowed:

          ```nix-repl
          nix-repl> let false = 1; in false
          1
          ```
        )",
        });

    addConstant(
        "null",
        &Value::vNull,
        {
            .type = nNull,
            .doc = R"(
          Primitive value.

          The name `null` is not special, and can be shadowed:

          ```nix-repl
          nix-repl> let null = 1; in null
          1
          ```
        )",
        });

    if (!settings.pureEval) {
        v.mkInt(time(0));
    }
    addConstant(
        "__currentTime",
        v,
        {
            .type = nInt,
            .doc = R"(
          Return the [Unix time](https://en.wikipedia.org/wiki/Unix_time) at first evaluation.
          Repeated references to that name re-use the initially obtained value.

          Example:

          ```console
          $ nix repl
          Welcome to Nix 2.15.1 Type :? for help.

          nix-repl> builtins.currentTime
          1683705525

          nix-repl> builtins.currentTime
          1683705525
          ```

          The [store path](@docroot@/store/store-path.md) of a derivation depending on `currentTime` differs for each evaluation, unless both evaluate `builtins.currentTime` in the same second.
        )",
            .impureOnly = true,
        });

    if (!settings.pureEval)
        v.mkString(settings.getCurrentSystem(), mem);
    addConstant(
        "__currentSystem",
        v,
        {
            .type = nString,
            .doc = R"(
          The value of the
          [`eval-system`](@docroot@/command-ref/conf-file.md#conf-eval-system)
          or else
          [`system`](@docroot@/command-ref/conf-file.md#conf-system)
          configuration option.

          It can be used to set the `system` attribute for [`builtins.derivation`](@docroot@/language/derivations.md) such that the resulting derivation can be built on the same system that evaluates the Nix expression:

          ```nix
           builtins.derivation {
             # ...
             system = builtins.currentSystem;
          }
          ```

          It can be overridden in order to create derivations for different system than the current one:

          ```console
          $ nix-instantiate --system "mips64-linux" --eval --expr 'builtins.currentSystem'
          "mips64-linux"
          ```
        )",
            .impureOnly = true,
        });

    v.mkString(nixVersion, mem);
    addConstant(
        "__nixVersion",
        v,
        {
            .type = nString,
            .doc = R"(
          The version of Nix.

          For example, where the command line returns the current Nix version,

          ```shell-session
          $ nix --version
          nix (Nix) 2.16.0
          ```

          the Nix language evaluator returns the same value:

          ```nix-repl
          nix-repl> builtins.nixVersion
          "2.16.0"
          ```
        )",
        });

    v.mkString(store->storeDir, mem);
    addConstant(
        "__storeDir",
        v,
        {
            .type = nString,
            .doc = R"(
          Logical file system location of the [Nix store](@docroot@/glossary.md#gloss-store) currently in use.

          This value is determined by the `store` parameter in [Store URLs](@docroot@/store/types/index.md#store-url-format):

          ```shell-session
          $ nix-instantiate --store 'dummy://?store=/blah' --eval --expr builtins.storeDir
          "/blah"
          ```
        )",
        });

    /* Language version.  This should be increased every time a new
       language feature gets added.  It's not necessary to increase it
       when primops get added, because you can just use `builtins ?
       primOp' to check. */
    v.mkInt(6);
    addConstant(
        "__langVersion",
        v,
        {
            .type = nInt,
            .doc = R"(
          The current version of the Nix language.
        )",
        });

#ifndef _WIN32 // TODO implement on Windows
    // Miscellaneous
    if (settings.enableNativeCode) {
        addPrimOp({
            .name = "__importNative",
            .arity = 2,
            .fun = prim_importNative,
        });
        addPrimOp({
            .name = "__exec",
            .arity = 1,
            .fun = prim_exec,
        });
    }
#endif

    addPrimOp({
        .name = "__traceVerbose",
        .args = {"e1", "e2"},
        .arity = 2,
        .doc = R"(
          Evaluate *e1* and print its abstract syntax representation on standard
          error if `--trace-verbose` is enabled. Then return *e2*. This function
          is useful for debugging.
        )",
        .fun = settings.traceVerbose ? prim_trace : prim_second,
    });

    /* Add a value containing the current Nix expression search path. */
    auto list = buildList(lookupPath.elements.size());
    for (const auto & [n, i] : enumerate(lookupPath.elements)) {
        auto attrs = buildBindings(2);
        attrs.alloc("path").mkString(i.path.s, mem);
        attrs.alloc("prefix").mkString(i.prefix.s, mem);
        (list[n] = allocValue())->mkAttrs(attrs);
    }
    v.mkList(list);
    addConstant(
        "__nixPath",
        v,
        {
            .type = nList,
            .doc = R"(
          A list of search path entries used to resolve [lookup paths](@docroot@/language/constructs/lookup-path.md).
          Its value is primarily determined by the [`nix-path` configuration setting](@docroot@/command-ref/conf-file.md#conf-nix-path), which are
          - Overridden by the [`NIX_PATH`](@docroot@/command-ref/env-common.md#env-NIX_PATH) environment variable or the `--nix-path` option
          - Extended by the [`-I` option](@docroot@/command-ref/opt-common.md#opt-I) or `--extra-nix-path`

          > **Example**
          >
          > ```bash
          > $ NIX_PATH= nix-instantiate --eval --expr "builtins.nixPath" -I foo=bar --no-pure-eval
          > [ { path = "bar"; prefix = "foo"; } ]
          > ```

          Lookup path expressions are [desugared](https://en.wikipedia.org/wiki/Syntactic_sugar) using this and
          [`builtins.findFile`](./builtins.html#builtins-findFile):

          ```nix
          <nixpkgs>
          ```

          is equivalent to:

          ```nix
          builtins.findFile builtins.nixPath "nixpkgs"
          ```
        )",
        });

    for (auto & primOp : RegisterPrimOp::primOps())
        if (experimentalFeatureSettings.isEnabled(primOp.experimentalFeature)) {
            auto primOpAdjusted = primOp;
            primOpAdjusted.arity = std::max(primOp.args.size(), primOp.arity);
            addPrimOp(std::move(primOpAdjusted));
        }

    for (auto & primOp : evalSettings.extraPrimOps) {
        auto primOpAdjusted = primOp;
        primOpAdjusted.arity = std::max(primOp.args.size(), primOp.arity);
        addPrimOp(std::move(primOpAdjusted));
    }

    /* Add a wrapper around the derivation primop that computes the
       `drvPath' and `outPath' attributes lazily.

       Null docs because it is documented separately.
       */
    auto vDerivation = allocValue();
    addConstant(
        "derivation",
        vDerivation,
        {
            .type = nFunction,
        });

    /* Now that we've added all primops, sort the `builtins' set,
       because attribute lookups expect it to be sorted. */
    const_cast<Bindings *>(getBuiltins().attrs())->sort();

    staticBaseEnv->sort();

    /* Note: we have to initialize the 'derivation' constant *after*
       building baseEnv/staticBaseEnv because it uses 'builtins'. */
    evalFile(derivationInternal, *vDerivation);
}

} // namespace nix
