#include "archive.hh"
#include "derivations.hh"
#include "downstream-placeholder.hh"
#include "eval-inline.hh"
#include "eval.hh"
#include "eval-settings.hh"
#include "gc-small-vector.hh"
#include "globals.hh"
#include "json-to-value.hh"
#include "names.hh"
#include "path-references.hh"
#include "store-api.hh"
#include "util.hh"
#include "processes.hh"
#include "value-to-json.hh"
#include "value-to-xml.hh"
#include "primops.hh"
#include "fs-input-accessor.hh"
#include "fetch-to-store.hh"

#include <boost/container/small_vector.hpp>
#include <nlohmann/json.hpp>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <regex>
#include <dlfcn.h>

#include <cmath>

namespace nix {


/*************************************************************
 * Miscellaneous
 *************************************************************/

StringMap EvalState::realiseContext(const NixStringContext & context)
{
    std::vector<DerivedPath::Built> drvs;
    StringMap res;

    for (auto & c : context) {
        auto ensureValid = [&](const StorePath & p) {
            if (!store->isValidPath(p))
                error<InvalidPathError>(store->printStorePath(p)).debugThrow();
        };
        std::visit(overloaded {
            [&](const NixStringContextElem::Built & b) {
                drvs.push_back(DerivedPath::Built {
                    .drvPath = b.drvPath,
                    .outputs = OutputsSpec::Names { b.output },
                });
                ensureValid(b.drvPath->getBaseStorePath());
            },
            [&](const NixStringContextElem::Opaque & o) {
                auto ctxS = store->printStorePath(o.path);
                res.insert_or_assign(ctxS, ctxS);
                ensureValid(o.path);
            },
            [&](const NixStringContextElem::DrvDeep & d) {
                /* Treat same as Opaque */
                auto ctxS = store->printStorePath(d.drvPath);
                res.insert_or_assign(ctxS, ctxS);
                ensureValid(d.drvPath);
            },
        }, c.raw);
    }

    if (drvs.empty()) return {};

    if (!evalSettings.enableImportFromDerivation)
        error<EvalError>(
            "cannot build '%1%' during evaluation because the option 'allow-import-from-derivation' is disabled",
            drvs.begin()->to_string(*store)
        ).debugThrow();

    /* Build/substitute the context. */
    std::vector<DerivedPath> buildReqs;
    for (auto & d : drvs) buildReqs.emplace_back(DerivedPath { d });
    buildStore->buildPaths(buildReqs, bmNormal, store);

    StorePathSet outputsToCopyAndAllow;

    for (auto & drv : drvs) {
        auto outputs = resolveDerivedPath(*buildStore, drv, &*store);
        for (auto & [outputName, outputPath] : outputs) {
            outputsToCopyAndAllow.insert(outputPath);

            /* Get all the output paths corresponding to the placeholders we had */
            if (experimentalFeatureSettings.isEnabled(Xp::CaDerivations)) {
                res.insert_or_assign(
                    DownstreamPlaceholder::fromSingleDerivedPathBuilt(
                        SingleDerivedPath::Built {
                            .drvPath = drv.drvPath,
                            .output = outputName,
                        }).render(),
                    buildStore->printStorePath(outputPath)
                );
            }
        }
    }

    if (store != buildStore) copyClosure(*buildStore, *store, outputsToCopyAndAllow);
    for (auto & outputPath : outputsToCopyAndAllow) {
        /* Add the output of this derivations to the allowed
           paths. */
        allowPath(outputPath);
    }

    return res;
}

static SourcePath realisePath(EvalState & state, const PosIdx pos, Value & v, std::optional<SymlinkResolution> resolveSymlinks = SymlinkResolution::Full)
{
    NixStringContext context;

    auto path = state.coerceToPath(noPos, v, context, "while realising the context of a path");

    try {
        if (!context.empty() && path.accessor == state.rootFS) {
            auto rewrites = state.realiseContext(context);
            auto realPath = state.toRealPath(rewriteStrings(path.path.abs(), rewrites), context);
            path = {path.accessor, CanonPath(realPath)};
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
        SingleDerivedPath::Built {
            .drvPath = makeConstantStorePathRef(drvPath),
            .output = o.first,
        },
        o.second.path(*state.store, Derivation::nameFromPath(drvPath), o.first));
}

/* Load and evaluate an expression from path specified by the
   argument. */
static void import(EvalState & state, const PosIdx pos, Value & vPath, Value * vScope, Value & v)
{
    auto path = realisePath(state, pos, vPath, std::nullopt);
    auto path2 = path.path.abs();

#if 0
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
        Derivation drv = state.store->readDerivation(*storePath);
        auto attrs = state.buildBindings(3 + drv.outputs.size());
        attrs.alloc(state.sDrvPath).mkString(path2, {
            NixStringContextElem::DrvDeep { .drvPath = *storePath },
        });
        attrs.alloc(state.sName).mkString(drv.env["name"]);
        auto & outputsVal = attrs.alloc(state.sOutputs);
        state.mkList(outputsVal, drv.outputs.size());

        for (const auto & [i, o] : enumerate(drv.outputs)) {
            mkOutputString(state, attrs, *storePath, o);
            (outputsVal.listElems()[i] = state.allocValue())->mkString(o.first);
        }

        auto w = state.allocValue();
        w->mkAttrs(attrs);

        if (!state.vImportedDrvToDerivation) {
            state.vImportedDrvToDerivation = allocRootValue(state.allocValue());
            state.eval(state.parseExprFromString(
                #include "imported-drv-to-derivation.nix.gen.hh"
                , state.rootPath(CanonPath::root)), **state.vImportedDrvToDerivation);
        }

        state.forceFunction(**state.vImportedDrvToDerivation, pos, "while evaluating imported-drv-to-derivation.nix.gen.hh");
        v.mkApp(*state.vImportedDrvToDerivation, w);
        state.forceAttrs(v, pos, "while calling imported-drv-to-derivation.nix.gen.hh");
    }

    else
#endif
    {
        if (!vScope)
            state.evalFile(path, v);
        else {
            state.forceAttrs(*vScope, pos, "while evaluating the first argument passed to builtins.scopedImport");

            Env * env = &state.allocEnv(vScope->attrs->size());
            env->up = &state.baseEnv;

            auto staticEnv = std::make_shared<StaticEnv>(nullptr, state.staticBaseEnv.get(), vScope->attrs->size());

            unsigned int displ = 0;
            for (auto & attr : *vScope->attrs) {
                staticEnv->vars.emplace_back(attr.name, displ);
                env->values[displ++] = attr.value;
            }

            // No need to call staticEnv.sort(), because
            // args[0]->attrs is already sorted.

            printTalkative("evaluating file '%1%'", path);
            Expr * e = state.parseExprFromFile(resolveExprPath(path), staticEnv);

            e->eval(state, *env, v);
        }
    }
}

static RegisterPrimOp primop_scopedImport(PrimOp {
    .name = "scopedImport",
    .arity = 2,
    .fun = [](EvalState & state, const PosIdx pos, Value * * args, Value & v)
    {
        import(state, pos, *args[1], args[0], v);
    }
});

static RegisterPrimOp primop_import({
    .name = "import",
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
      >  then the following `foo.nix` will give an error:
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
    .fun = [](EvalState & state, const PosIdx pos, Value * * args, Value & v)
    {
        import(state, pos, *args[0], nullptr, v);
    }
});

/* Want reasonable symbol names, so extern C */
/* !!! Should we pass the Pos or the file name too? */
extern "C" typedef void (*ValueInitializer)(EvalState & state, Value & v);

/* Load a ValueInitializer from a DSO and return whatever it initializes */
void prim_importNative(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    throw UnimplementedError("importNative");

    #if 0
    auto path = realisePath(state, pos, *args[0]);

    std::string sym(state.forceStringNoCtx(*args[1], pos, "while evaluating the second argument passed to builtins.importNative"));

    void *handle = dlopen(path.path.c_str(), RTLD_LAZY | RTLD_LOCAL);
    if (!handle)
        state.error<EvalError>("could not open '%1%': %2%", path, dlerror()).debugThrow();

    dlerror();
    ValueInitializer func = (ValueInitializer) dlsym(handle, sym.c_str());
    if(!func) {
        char *message = dlerror();
        if (message)
            state.error<EvalError>("could not load symbol '%1%' from '%2%': %3%", sym, path, message).debugThrow();
        else
            state.error<EvalError>("symbol '%1%' from '%2%' resolved to NULL when a function pointer was expected", sym, path).debugThrow();
    }

    (func)(state, v);

    /* We don't dlclose because v may be a primop referencing a function in the shared object file */
    #endif
}


/* Execute a program and parse its output */
void prim_exec(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    state.forceList(*args[0], pos, "while evaluating the first argument passed to builtins.exec");
    auto elems = args[0]->listElems();
    auto count = args[0]->listSize();
    if (count == 0)
        state.error<EvalError>("at least one argument to 'exec' required").atPos(pos).debugThrow();
    NixStringContext context;
    auto program = state.coerceToString(pos, *elems[0], context,
            "while evaluating the first element of the argument passed to builtins.exec",
            false, false).toOwned();
    Strings commandArgs;
    for (unsigned int i = 1; i < args[0]->listSize(); ++i) {
        commandArgs.push_back(
                state.coerceToString(pos, *elems[i], context,
                        "while evaluating an element of the argument passed to builtins.exec",
                        false, false).toOwned());
    }
    try {
        auto _ = state.realiseContext(context); // FIXME: Handle CA derivations
    } catch (InvalidPathError & e) {
        state.error<EvalError>("cannot execute '%1%', since path '%2%' is not valid", program, e.path).atPos(pos).debugThrow();
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

/* Return a string representing the type of the expression. */
static void prim_typeOf(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    state.forceValue(*args[0], pos);
    std::string t;
    switch (args[0]->type()) {
        case nInt: t = "int"; break;
        case nBool: t = "bool"; break;
        case nString: t = "string"; break;
        case nPath: t = "path"; break;
        case nNull: t = "null"; break;
        case nAttrs: t = "set"; break;
        case nList: t = "list"; break;
        case nFunction: t = "lambda"; break;
        case nExternal:
            t = args[0]->external->typeOf();
            break;
        case nFloat: t = "float"; break;
        case nThunk: abort();
    }
    v.mkString(t);
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
static void prim_isNull(EvalState & state, const PosIdx pos, Value * * args, Value & v)
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
static void prim_isFunction(EvalState & state, const PosIdx pos, Value * * args, Value & v)
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
static void prim_isInt(EvalState & state, const PosIdx pos, Value * * args, Value & v)
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
static void prim_isFloat(EvalState & state, const PosIdx pos, Value * * args, Value & v)
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
static void prim_isString(EvalState & state, const PosIdx pos, Value * * args, Value & v)
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
static void prim_isBool(EvalState & state, const PosIdx pos, Value * * args, Value & v)
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
static void prim_isPath(EvalState & state, const PosIdx pos, Value * * args, Value & v)
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
 static inline void withExceptionContext(Trace trace, Callable&& func)
{
    try
    {
        func();
    }
    catch(Error & e)
    {
        e.pushTrace(trace);
        throw;
    }
}

struct CompareValues
{
    EvalState & state;
    const PosIdx pos;
    const std::string_view errorCtx;

    CompareValues(EvalState & state, const PosIdx pos, const std::string_view && errorCtx) : state(state), pos(pos), errorCtx(errorCtx) { };

    bool operator () (Value * v1, Value * v2) const
    {
        return (*this)(v1, v2, errorCtx);
    }

    bool operator () (Value * v1, Value * v2, std::string_view errorCtx) const
    {
        try {
            if (v1->type() == nFloat && v2->type() == nInt)
                return v1->fpoint < v2->integer;
            if (v1->type() == nInt && v2->type() == nFloat)
                return v1->integer < v2->fpoint;
            if (v1->type() != v2->type())
                state.error<EvalError>("cannot compare %s with %s", showType(*v1), showType(*v2)).debugThrow();
            // Allow selecting a subset of enum values
            #pragma GCC diagnostic push
            #pragma GCC diagnostic ignored "-Wswitch-enum"
            switch (v1->type()) {
                case nInt:
                    return v1->integer < v2->integer;
                case nFloat:
                    return v1->fpoint < v2->fpoint;
                case nString:
                    return strcmp(v1->c_str(), v2->c_str()) < 0;
                case nPath:
                    // Note: we don't take the accessor into account
                    // since it's not obvious how to compare them in a
                    // reproducible way.
                    return strcmp(v1->_path.path, v2->_path.path) < 0;
                case nList:
                    // Lexicographic comparison
                    for (size_t i = 0;; i++) {
                        if (i == v2->listSize()) {
                            return false;
                        } else if (i == v1->listSize()) {
                            return true;
                        } else if (!state.eqValues(*v1->listElems()[i], *v2->listElems()[i], pos, errorCtx)) {
                            return (*this)(v1->listElems()[i], v2->listElems()[i], "while comparing two list elements");
                        }
                    }
                default:
                    state.error<EvalError>("cannot compare %s with %s; values of that type are incomparable", showType(*v1), showType(*v2)).debugThrow();
            #pragma GCC diagnostic pop
            }
        } catch (Error & e) {
            if (!errorCtx.empty())
                e.addTrace(nullptr, errorCtx);
            throw;
        }
    }
};


#if HAVE_BOEHMGC
typedef std::list<Value *, gc_allocator<Value *>> ValueList;
#else
typedef std::list<Value *> ValueList;
#endif


static Bindings::iterator getAttr(
    EvalState & state,
    Symbol attrSym,
    Bindings * attrSet,
    std::string_view errorCtx)
{
    Bindings::iterator value = attrSet->find(attrSym);
    if (value == attrSet->end()) {
        state.error<TypeError>("attribute '%s' missing", state.symbols[attrSym]).withTrace(noPos, errorCtx).debugThrow();
    }
    return value;
}

static void prim_genericClosure(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    state.forceAttrs(*args[0], noPos, "while evaluating the first argument passed to builtins.genericClosure");

    /* Get the start set. */
    Bindings::iterator startSet = getAttr(state, state.sStartSet, args[0]->attrs, "in the attrset passed as argument to builtins.genericClosure");

    state.forceList(*startSet->value, noPos, "while evaluating the 'startSet' attribute passed as argument to builtins.genericClosure");

    ValueList workSet;
    for (auto elem : startSet->value->listItems())
        workSet.push_back(elem);

    if (startSet->value->listSize() == 0) {
        v = *startSet->value;
        return;
    }

    /* Get the operator. */
    Bindings::iterator op = getAttr(state, state.sOperator, args[0]->attrs, "in the attrset passed as argument to builtins.genericClosure");
    state.forceFunction(*op->value, noPos, "while evaluating the 'operator' attribute passed as argument to builtins.genericClosure");

    /* Construct the closure by applying the operator to elements of
       `workSet', adding the result to `workSet', continuing until
       no new elements are found. */
    ValueList res;
    // `doneKeys' doesn't need to be a GC root, because its values are
    // reachable from res.
    auto cmp = CompareValues(state, noPos, "while comparing the `key` attributes of two genericClosure elements");
    std::set<Value *, decltype(cmp)> doneKeys(cmp);
    while (!workSet.empty()) {
        Value * e = *(workSet.begin());
        workSet.pop_front();

        state.forceAttrs(*e, noPos, "while evaluating one of the elements generated by (or initially passed to) builtins.genericClosure");

        Bindings::iterator key = getAttr(state, state.sKey, e->attrs, "in one of the attrsets generated by (or initially passed to) builtins.genericClosure");
        state.forceValue(*key->value, noPos);

        if (!doneKeys.insert(key->value).second) continue;
        res.push_back(e);

        /* Call the `operator' function with `e' as argument. */
        Value newElements;
        state.callFunction(*op->value, 1, &e, newElements, noPos);
        state.forceList(newElements, noPos, "while evaluating the return value of the `operator` passed to builtins.genericClosure");

        /* Add the values returned by the operator to the work set. */
        for (auto elem : newElements.listItems()) {
            state.forceValue(*elem, noPos); // "while evaluating one one of the elements returned by the `operator` passed to builtins.genericClosure");
            workSet.push_back(elem);
        }
    }

    /* Create the result list. */
    state.mkList(v, res.size());
    unsigned int n = 0;
    for (auto & i : res)
        v.listElems()[n++] = i;
}

static RegisterPrimOp primop_genericClosure(PrimOp {
    .name = "__genericClosure",
    .args = {"attrset"},
    .arity = 1,
    .doc = R"(
      Take an *attrset* with values named `startSet` and `operator` in order to
      return a *list of attrsets* by starting with the `startSet` and recursively
      applying the `operator` function to each `item`. The *attrsets* in the
      `startSet` and the *attrsets* produced by `operator` must contain a value
      named `key` which is comparable. The result is produced by calling `operator`
      for each `item` with a value for `key` that has not been called yet including
      newly produced `item`s. The function terminates when no new `item`s are
      produced. The resulting *list of attrsets* contains only *attrsets* with a
      unique key. For example,

      ```
      builtins.genericClosure {
        startSet = [ {key = 5;} ];
        operator = item: [{
          key = if (item.key / 2 ) * 2 == item.key
               then item.key / 2
               else 3 * item.key + 1;
        }];
      }
      ```
      evaluates to
      ```
      [ { key = 5; } { key = 16; } { key = 8; } { key = 4; } { key = 2; } { key = 1; } ]
      ```

      `key` can be one of the following types:
      - [Number](@docroot@/language/values.md#type-number)
      - [Boolean](@docroot@/language/values.md#type-boolean)
      - [String](@docroot@/language/values.md#type-string)
      - [Path](@docroot@/language/values.md#type-path)
      - [List](@docroot@/language/values.md#list)

      )",
    .fun = prim_genericClosure,
});


static RegisterPrimOp primop_break({
    .name = "break",
    .args = {"v"},
    .doc = R"(
      In debug mode (enabled using `--debugger`), pause Nix expression evaluation and enter the REPL.
      Otherwise, return the argument `v`.
    )",
    .fun = [](EvalState & state, const PosIdx pos, Value * * args, Value & v)
    {
        if (state.debugRepl && !state.debugTraces.empty()) {
            auto error = Error(ErrorInfo {
                .level = lvlInfo,
                .msg = HintFmt("breakpoint reached"),
                .pos = state.positions[pos],
            });

            auto & dt = state.debugTraces.front();
            state.runDebugRepl(&error, dt.env, dt.expr);

            if (state.debugQuit) {
                // If the user elects to quit the repl, throw an exception.
                throw Error(ErrorInfo{
                    .level = lvlInfo,
                    .msg = HintFmt("quit the debugger"),
                    .pos = nullptr,
                });
            }
        }

        // Return the value we were passed.
        v = *args[0];
    }
});

static RegisterPrimOp primop_abort({
    .name = "abort",
    .args = {"s"},
    .doc = R"(
      Abort Nix expression evaluation and print the error message *s*.
    )",
    .fun = [](EvalState & state, const PosIdx pos, Value * * args, Value & v)
    {
        NixStringContext context;
        auto s = state.coerceToString(pos, *args[0], context,
                "while evaluating the error message passed to 'builtins.abort'").toOwned();
        state.error<Abort>("evaluation aborted with the following error message: '%1%'", s).debugThrow();
    }
});

static RegisterPrimOp primop_throw({
    .name = "throw",
    .args = {"s"},
    .doc = R"(
      Throw an error message *s*. This usually aborts Nix expression
      evaluation, but in `nix-env -qa` and other commands that try to
      evaluate a set of derivations to get information about those
      derivations, a derivation that throws an error is silently skipped
      (which is not the case for `abort`).
    )",
    .fun = [](EvalState & state, const PosIdx pos, Value * * args, Value & v)
    {
      NixStringContext context;
      auto s = state.coerceToString(pos, *args[0], context,
              "while evaluating the error message passed to 'builtin.throw'").toOwned();
      state.error<ThrownError>(s).debugThrow();
    }
});

static void prim_addErrorContext(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    try {
        state.forceValue(*args[1], pos);
        v = *args[1];
    } catch (Error & e) {
        NixStringContext context;
        auto message = state.coerceToString(pos, *args[0], context,
                "while evaluating the error message passed to 'builtins.addErrorContext'",
                false, false).toOwned();
        e.addTrace(nullptr, HintFmt(message), true);
        throw;
    }
}

static RegisterPrimOp primop_addErrorContext(PrimOp {
    .name = "__addErrorContext",
    .arity = 2,
    .fun = prim_addErrorContext,
});

static void prim_ceil(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    auto value = state.forceFloat(*args[0], args[0]->determinePos(pos),
            "while evaluating the first argument passed to builtins.ceil");
    v.mkInt(ceil(value));
}

static RegisterPrimOp primop_ceil({
    .name = "__ceil",
    .args = {"double"},
    .doc = R"(
        Converts an IEEE-754 double-precision floating-point number (*double*) to
        the next higher integer.

        If the datatype is neither an integer nor a "float", an evaluation error will be
        thrown.
    )",
    .fun = prim_ceil,
});

static void prim_floor(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    auto value = state.forceFloat(*args[0], args[0]->determinePos(pos), "while evaluating the first argument passed to builtins.floor");
    v.mkInt(floor(value));
}

static RegisterPrimOp primop_floor({
    .name = "__floor",
    .args = {"double"},
    .doc = R"(
        Converts an IEEE-754 double-precision floating-point number (*double*) to
        the next lower integer.

        If the datatype is neither an integer nor a "float", an evaluation error will be
        thrown.
    )",
    .fun = prim_floor,
});

/* Try evaluating the argument. Success => {success=true; value=something;},
 * else => {success=false; value=false;} */
static void prim_tryEval(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    auto attrs = state.buildBindings(2);

    /* increment state.trylevel, and decrement it when this function returns. */
    MaintainCount trylevel(state.trylevel);

    void (* savedDebugRepl)(ref<EvalState> es, const ValMap & extraEnv) = nullptr;
    if (state.debugRepl && evalSettings.ignoreExceptionsDuringTry)
    {
        /* to prevent starting the repl from exceptions withing a tryEval, null it. */
        savedDebugRepl = state.debugRepl;
        state.debugRepl = nullptr;
    }

    try {
        state.forceValue(*args[0], pos);
        attrs.insert(state.sValue, args[0]);
        attrs.alloc("success").mkBool(true);
    } catch (AssertionError & e) {
        attrs.alloc(state.sValue).mkBool(false);
        attrs.alloc("success").mkBool(false);
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
      successful and `false` otherwise. `tryEval` will only prevent
      errors created by `throw` or `assert` from being thrown.
      Errors `tryEval` will not catch are for example those created
      by `abort` and type errors generated by builtins. Also note that
      this doesn't evaluate *e* deeply, so `let e = { x = throw ""; };
      in (builtins.tryEval e).success` will be `true`. Using
      `builtins.deepSeq` one can get the expected result:
      `let e = { x = throw ""; }; in
      (builtins.tryEval (builtins.deepSeq e e)).success` will be
      `false`.
    )",
    .fun = prim_tryEval,
});

/* Return an environment variable.  Use with care. */
static void prim_getEnv(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    std::string name(state.forceStringNoCtx(*args[0], pos, "while evaluating the first argument passed to builtins.getEnv"));
    v.mkString(evalSettings.restrictEval || evalSettings.pureEval ? "" : getEnv(name).value_or(""));
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
static void prim_seq(EvalState & state, const PosIdx pos, Value * * args, Value & v)
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
static void prim_deepSeq(EvalState & state, const PosIdx pos, Value * * args, Value & v)
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
static void prim_trace(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    state.forceValue(*args[0], pos);
    if (args[0]->type() == nString)
        printError("trace: %1%", state.decodePaths(args[0]->string_view()));
    else
        printError("trace: %1%", ValuePrinter(state, *args[0]));
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
    )",
    .fun = prim_trace,
});


/* Takes two arguments and evaluates to the second one. Used as the
 * builtins.traceVerbose implementation when --trace-verbose is not enabled
 */
static void prim_second(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    state.forceValue(*args[1], pos);
    v = *args[1];
}

/*************************************************************
 * Derivations
 *************************************************************/

static void derivationStrictInternal(EvalState & state, const std::string & name, Bindings * attrs, Value & v);

/* Construct (as a unobservable side effect) a Nix derivation
   expression that performs the derivation described by the argument
   set.  Returns the original set extended with the following
   attributes: `outPath' containing the primary output path of the
   derivation; `drvPath' containing the path of the Nix expression;
   and `type' set to `derivation' to indicate that this is a
   derivation. */
static void prim_derivationStrict(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    state.forceAttrs(*args[0], pos, "while evaluating the argument passed to builtins.derivationStrict");

    Bindings * attrs = args[0]->attrs;

    /* Figure out the name first (for stack backtraces). */
    Bindings::iterator nameAttr = getAttr(state, state.sName, attrs, "in the attrset passed as argument to builtins.derivationStrict");

    std::string drvName;
    try {
        drvName = state.forceStringNoCtx(*nameAttr->value, pos, "while evaluating the `name` attribute passed to builtins.derivationStrict");
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
        e.addTrace(nullptr, HintFmt(
                "while evaluating derivation '%s'\n"
                "  whose name attribute is located at %s",
                drvName, pos), true);
        throw;
    }
}

static void derivationStrictInternal(EvalState & state, const std::string &
drvName, Bindings * attrs, Value & v)
{
    /* Check whether attributes should be passed as a JSON file. */
    using nlohmann::json;
    std::optional<json> jsonObject;
    auto pos = v.determinePos(noPos);
    auto attr = attrs->find(state.sStructuredAttrs);
    if (attr != attrs->end() &&
        state.forceBool(*attr->value, pos,
                        "while evaluating the `__structuredAttrs` "
                        "attribute passed to builtins.derivationStrict"))
        jsonObject = json::object();

    /* Check whether null attributes should be ignored. */
    bool ignoreNulls = false;
    attr = attrs->find(state.sIgnoreNulls);
    if (attr != attrs->end())
        ignoreNulls = state.forceBool(*attr->value, pos, "while evaluating the `__ignoreNulls` attribute " "passed to builtins.derivationStrict");

    /* Build the derivation expression by processing the attributes. */
    Derivation drv;
    drv.name = drvName;

    NixStringContext context;

    bool contentAddressed = false;
    bool isImpure = false;
    std::optional<std::string> outputHash;
    std::string outputHashAlgo;
    std::optional<ContentAddressMethod> ingestionMethod;

    StringSet outputs;
    outputs.insert("out");

    for (auto & i : attrs->lexicographicOrder(state.symbols)) {
        if (i->name == state.sIgnoreNulls) continue;
        const std::string & key = state.symbols[i->name];
        vomit("processing attribute '%1%'", key);

        auto handleHashMode = [&](const std::string_view s) {
            if (s == "recursive") ingestionMethod = FileIngestionMethod::Recursive;
            else if (s == "flat") ingestionMethod = FileIngestionMethod::Flat;
            else if (s == "text") {
                experimentalFeatureSettings.require(Xp::DynamicDerivations);
                ingestionMethod = TextIngestionMethod {};
            } else
                state.error<EvalError>(
                    "invalid value '%s' for 'outputHashMode' attribute", s
                ).atPos(v).debugThrow();
        };

        auto handleOutputs = [&](const Strings & ss) {
            outputs.clear();
            for (auto & j : ss) {
                if (outputs.find(j) != outputs.end())
                    state.error<EvalError>("duplicate derivation output '%1%'", j)
                        .atPos(v)
                        .debugThrow();
                /* !!! Check whether j is a valid attribute
                   name. */
                /* Derivations cannot be named ‘drv’, because
                   then we'd have an attribute ‘drvPath’ in
                   the resulting set. */
                if (j == "drv")
                    state.error<EvalError>("invalid derivation output name 'drv'")
                        .atPos(v)
                        .debugThrow();
                outputs.insert(j);
            }
            if (outputs.empty())
                state.error<EvalError>("derivation cannot have an empty set of outputs")
                    .atPos(v)
                    .debugThrow();
        };

        try {
            // This try-catch block adds context for most errors.
            // Use this empty error context to signify that we defer to it.
            const std::string_view context_below("");

            if (ignoreNulls) {
                state.forceValue(*i->value, pos);
                if (i->value->type() == nNull) continue;
            }

            if (i->name == state.sContentAddressed && state.forceBool(*i->value, pos, context_below)) {
                contentAddressed = true;
                experimentalFeatureSettings.require(Xp::CaDerivations);
            }

            else if (i->name == state.sImpure && state.forceBool(*i->value, pos, context_below)) {
                isImpure = true;
                experimentalFeatureSettings.require(Xp::ImpureDerivations);
            }

            /* The `args' attribute is special: it supplies the
               command-line arguments to the builder. */
            else if (i->name == state.sArgs) {
                state.forceList(*i->value, pos, context_below);
                for (auto elem : i->value->listItems()) {
                    auto s = state.coerceToString(pos, *elem, context,
                            "while evaluating an element of the argument list",
                            true).toOwned();
                    drv.args.push_back(s);
                }
            }

            /* All other attributes are passed to the builder through
               the environment. */
            else {

                if (jsonObject) {

                    if (i->name == state.sStructuredAttrs) continue;

                    (*jsonObject)[key] = printValueAsJSON(state, true, *i->value, pos, context);

                    if (i->name == state.sBuilder)
                        drv.builder = state.forceString(*i->value, context, pos, context_below);
                    else if (i->name == state.sSystem)
                        drv.platform = state.forceStringNoCtx(*i->value, pos, context_below);
                    else if (i->name == state.sOutputHash)
                        outputHash = state.forceStringNoCtx(*i->value, pos, context_below);
                    else if (i->name == state.sOutputHashAlgo)
                        outputHashAlgo = state.forceStringNoCtx(*i->value, pos, context_below);
                    else if (i->name == state.sOutputHashMode)
                        handleHashMode(state.forceStringNoCtx(*i->value, pos, context_below));
                    else if (i->name == state.sOutputs) {
                        /* Require ‘outputs’ to be a list of strings. */
                        state.forceList(*i->value, pos, context_below);
                        Strings ss;
                        for (auto elem : i->value->listItems())
                            ss.emplace_back(state.forceStringNoCtx(*elem, pos, context_below));
                        handleOutputs(ss);
                    }

                } else {
                    auto s = state.coerceToString(pos, *i->value, context, context_below, true).toOwned();
                    drv.env.emplace(key, s);
                    if (i->name == state.sBuilder) drv.builder = std::move(s);
                    else if (i->name == state.sSystem) drv.platform = std::move(s);
                    else if (i->name == state.sOutputHash) outputHash = std::move(s);
                    else if (i->name == state.sOutputHashAlgo) outputHashAlgo = std::move(s);
                    else if (i->name == state.sOutputHashMode) handleHashMode(s);
                    else if (i->name == state.sOutputs)
                        handleOutputs(tokenizeString<Strings>(s));
                }

            }

        } catch (Error & e) {
            e.addTrace(state.positions[i->pos],
                HintFmt("while evaluating attribute '%1%' of derivation '%2%'", key, drvName),
                true);
            throw;
        }
    }

    if (jsonObject) {
        drv.env.emplace("__json", jsonObject->dump());
        jsonObject.reset();
    }

    /* Everything in the context of the strings in the derivation
       attributes should be added as dependencies of the resulting
       derivation. */
    for (auto & c : context) {
        std::visit(overloaded {
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
            [&](const NixStringContextElem::Opaque & o) {
                drv.inputSrcs.insert(o.path);
            },
        }, c.raw);
    }

    /* Do we have all required attributes? */
    if (drv.builder == "")
        state.error<EvalError>("required attribute 'builder' missing")
            .atPos(v)
            .debugThrow();

    if (drv.platform == "")
        state.error<EvalError>("required attribute 'system' missing")
            .atPos(v)
            .debugThrow();

    /* Check whether the derivation name is valid. */
    if (isDerivation(drvName) &&
        !(ingestionMethod == ContentAddressMethod { TextIngestionMethod { } } &&
          outputs.size() == 1 &&
          *(outputs.begin()) == "out"))
    {
        state.error<EvalError>(
            "derivation names are allowed to end in '%s' only if they produce a single derivation file",
            drvExtension
        ).atPos(v).debugThrow();
    }

    if (outputHash) {
        /* Handle fixed-output derivations.

           Ignore `__contentAddressed` because fixed output derivations are
           already content addressed. */
        if (outputs.size() != 1 || *(outputs.begin()) != "out")
            state.error<EvalError>(
                "multiple outputs are not supported in fixed-output derivations"
            ).atPos(v).debugThrow();

        auto h = newHashAllowEmpty(*outputHash, parseHashAlgoOpt(outputHashAlgo));

        auto method = ingestionMethod.value_or(FileIngestionMethod::Flat);

        DerivationOutput::CAFixed dof {
            .ca = ContentAddress {
                .method = std::move(method),
                .hash = std::move(h),
            },
        };

        drv.env["out"] = state.store->printStorePath(dof.path(*state.store, drvName, "out"));
        drv.outputs.insert_or_assign("out", std::move(dof));
    }

    else if (contentAddressed || isImpure) {
        if (contentAddressed && isImpure)
            state.error<EvalError>("derivation cannot be both content-addressed and impure")
                .atPos(v).debugThrow();

        auto ha = parseHashAlgoOpt(outputHashAlgo).value_or(HashAlgorithm::SHA256);
        auto method = ingestionMethod.value_or(FileIngestionMethod::Recursive);

        for (auto & i : outputs) {
            drv.env[i] = hashPlaceholder(i);
            if (isImpure)
                drv.outputs.insert_or_assign(i,
                    DerivationOutput::Impure {
                        .method = method,
                        .hashAlgo = ha,
                    });
            else
                drv.outputs.insert_or_assign(i,
                    DerivationOutput::CAFloating {
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
            drv.outputs.insert_or_assign(i,
                DerivationOutput::Deferred { });
        }

        auto hashModulo = hashDerivationModulo(*state.store, Derivation(drv), true);
        switch (hashModulo.kind) {
        case DrvHash::Kind::Regular:
            for (auto & i : outputs) {
                auto h = get(hashModulo.hashes, i);
                if (!h)
                    state.error<AssertionError>(
                        "derivation produced no hash for output '%s'",
                        i
                    ).atPos(v).debugThrow();
                auto outPath = state.store->makeOutputPath(i, *h, drvName);
                drv.env[i] = state.store->printStorePath(outPath);
                drv.outputs.insert_or_assign(
                    i,
                    DerivationOutput::InputAddressed {
                        .path = std::move(outPath),
                    });
            }
            break;
            ;
        case DrvHash::Kind::Deferred:
            for (auto & i : outputs) {
                drv.outputs.insert_or_assign(i, DerivationOutput::Deferred {});
            }
        }
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
        drvHashes.lock()->insert_or_assign(drvPath, h);
    }

    auto result = state.buildBindings(1 + drv.outputs.size());
    result.alloc(state.sDrvPath).mkString(drvPathS, {
        NixStringContextElem::DrvDeep { .drvPath = drvPath },
    });
    for (auto & i : drv.outputs)
        mkOutputString(state, result, drvPath, i);

    v.mkAttrs(result);
}

static RegisterPrimOp primop_derivationStrict(PrimOp {
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
static void prim_placeholder(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    v.mkString(hashPlaceholder(state.forceStringNoCtx(*args[0], pos, "while evaluating the first argument passed to builtins.placeholder")));
}

static RegisterPrimOp primop_placeholder({
    .name = "placeholder",
    .args = {"output"},
    .doc = R"(
      Return a placeholder string for the specified *output* that will be
      substituted by the corresponding output path at build time. Typical
      outputs would be `"out"`, `"bin"` or `"dev"`.
    )",
    .fun = prim_placeholder,
});


/*************************************************************
 * Paths
 *************************************************************/


/* Convert the argument to a path and then to a string (confusing,
   eh?).  !!! obsolete? */
static void prim_toPath(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    NixStringContext context;
    auto path = state.coerceToPath(pos, *args[0], context, "while evaluating the first argument passed to builtins.toPath");
    v.mkString(path.path.abs(), context);
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
static void prim_storePath(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    if (evalSettings.pureEval)
        state.error<EvalError>(
            "'%s' is not allowed in pure evaluation mode",
            "builtins.storePath"
        ).atPos(pos).debugThrow();

    NixStringContext context;
    auto path = state.coerceToPath(pos, *args[0], context, "while evaluating the first argument passed to 'builtins.storePath'").path;
    /* Resolve symlinks in ‘path’, unless ‘path’ itself is a symlink
       directly in the store.  The latter condition is necessary so
       e.g. nix-push does the right thing. */
    if (!state.store->isStorePath(path.abs()))
        path = CanonPath(canonPath(path.abs(), true));
    if (!state.store->isInStore(path.abs()))
        state.error<EvalError>("path '%1%' is not in the Nix store", path)
            .atPos(pos).debugThrow();
    auto path2 = state.store->toStorePath(path.abs()).first;
    if (!settings.readOnlyMode)
        state.store->ensurePath(path2);
    context.insert(NixStringContextElem::Opaque { .path = path2 });
    v.mkString(path.abs(), context);
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

static void prim_pathExists(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    try {
        auto & arg = *args[0];

        /* SourcePath doesn't know about trailing slash. */
        state.forceValue(arg, pos);
        auto mustBeDir = arg.type() == nString
            && (arg.string_view().ends_with("/")
                || arg.string_view().ends_with("/."));

        auto symlinkResolution =
            mustBeDir ? SymlinkResolution::Full : SymlinkResolution::Ancestors;
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

/* Return the base name of the given string, i.e., everything
   following the last slash. */
static void prim_baseNameOf(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    NixStringContext context;
    v.mkString(baseNameOf(*state.coerceToString(pos, *args[0], context,
            "while evaluating the first argument passed to builtins.baseNameOf",
            false, false)), context);
}

static RegisterPrimOp primop_baseNameOf({
    .name = "baseNameOf",
    .args = {"s"},
    .doc = R"(
      Return the *base name* of the string *s*, that is, everything
      following the final slash in the string. This is similar to the GNU
      `basename` command.
    )",
    .fun = prim_baseNameOf,
});

/* Return the directory of the given path, i.e., everything before the
   last slash.  Return either a path or a string depending on the type
   of the argument. */
static void prim_dirOf(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    state.forceValue(*args[0], pos);
    if (args[0]->type() == nPath) {
        auto path = args[0]->path();
        v.mkPath(path.path.isRoot() ? path : path.parent());
    } else {
        NixStringContext context;
        auto path = state.coerceToString(pos, *args[0], context,
            "while evaluating the first argument passed to 'builtins.dirOf'",
            false, false);
        auto dir = dirOf(*path);
        v.mkString(dir, context);
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
static void prim_readFile(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    auto path = realisePath(state, pos, *args[0]);
    auto s = path.readFile();
    if (s.find((char) 0) != std::string::npos)
        state.error<EvalError>(
            "the contents of the file '%1%' cannot be represented as a Nix string",
            path
        ).atPos(pos).debugThrow();
    StorePathSet refs;
    if (state.store->isInStore(path.path.abs())) {
        try {
            // FIXME: only do queryPathInfo if path.accessor is the store accessor
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
        context.insert(NixStringContextElem::Opaque {
            .path = std::move((StorePath &&)p),
        });
    }
    v.mkString(s, context);
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
static void prim_findFile(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    state.forceList(*args[0], pos, "while evaluating the first argument passed to builtins.findFile");

    SearchPath searchPath;

    for (auto v2 : args[0]->listItems()) {
        state.forceAttrs(*v2, pos, "while evaluating an element of the list passed to builtins.findFile");

        std::string prefix;
        Bindings::iterator i = v2->attrs->find(state.sPrefix);
        if (i != v2->attrs->end())
            prefix = state.forceStringNoCtx(*i->value, pos, "while evaluating the `prefix` attribute of an element of the list passed to builtins.findFile");

        i = getAttr(state, state.sPath, v2->attrs, "in an element of the __nixPath");

        NixStringContext context;
        auto path = state.coerceToString(pos, *i->value, context,
                "while evaluating the `path` attribute of an element of the list passed to builtins.findFile",
                false, false).toOwned();

        try {
            auto rewrites = state.realiseContext(context);
            path = rewriteStrings(path, rewrites);
        } catch (InvalidPathError & e) {
            state.error<EvalError>(
                "cannot find '%1%', since path '%2%' is not valid",
                path,
                e.path
            ).atPos(pos).debugThrow();
        }

        searchPath.elements.emplace_back(SearchPath::Elem {
            .prefix = SearchPath::Prefix { .s = prefix },
            .path = SearchPath::Path { .s = path },
        });
    }

    auto path = state.forceStringNoCtx(*args[1], pos, "while evaluating the second argument passed to builtins.findFile");

    v.mkPath(state.findFile(searchPath, path, pos));
}

static RegisterPrimOp primop_findFile(PrimOp {
    .name = "__findFile",
    .args = {"search-path", "lookup-path"},
    .doc = R"(
      Find *lookup-path* in *search-path*.

      A search path is represented list of [attribute sets](./values.md#attribute-set) with two attributes:
      - `prefix` is a relative path.
      - `path` denotes a file system location
      The exact syntax depends on the command line interface.

      Examples of search path attribute sets:

      - ```
        {
          prefix = "nixos-config";
          path = "/etc/nixos/configuration.nix";
        }
        ```

      - ```
        {
          prefix = "";
          path = "/nix/var/nix/profiles/per-user/root/channels";
        }
        ```

      The lookup algorithm checks each entry until a match is found, returning a [path value](@docroot@/language/values.html#type-path) of the match:

      - If *lookup-path* matches `prefix`, then the remainder of *lookup-path* (the "suffix") is searched for within the directory denoted by `path`.
        Note that the `path` may need to be downloaded at this point to look inside.
      - If the suffix is found inside that directory, then the entry is a match.
        The combined absolute path of the directory (now downloaded if need be) and the suffix is returned.

      [Lookup path](@docroot@/language/constructs/lookup-path.md) expressions can be [desugared](https://en.wikipedia.org/wiki/Syntactic_sugar) using this and [`builtins.nixPath`](@docroot@/language/builtin-constants.md#builtins-nixPath):

      ```nix
      <nixpkgs>
      ```

      is equivalent to:

      ```nix
      builtins.findFile builtins.nixPath "nixpkgs"
      ```
    )",
    .fun = prim_findFile,
});

/* Return the cryptographic hash of a file in base-16. */
static void prim_hashFile(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    auto algo = state.forceStringNoCtx(*args[0], pos, "while evaluating the first argument passed to builtins.hashFile");
    std::optional<HashAlgorithm> ha = parseHashAlgo(algo);
    if (!ha)
        state.error<EvalError>("unknown hash algorithm '%1%'", algo).atPos(pos).debugThrow();

    auto path = realisePath(state, pos, *args[1]);

    v.mkString(hashString(*ha, path.readFile()).to_string(HashFormat::Base16, false));
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

static std::string_view fileTypeToString(InputAccessor::Type type)
{
    return
        type == InputAccessor::Type::tRegular ? "regular" :
        type == InputAccessor::Type::tDirectory ? "directory" :
        type == InputAccessor::Type::tSymlink ? "symlink" :
        "unknown";
}

static void prim_readFileType(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    auto path = realisePath(state, pos, *args[0], std::nullopt);
    /* Retrieve the directory entry type and stringize it. */
    v.mkString(fileTypeToString(path.lstat().type));
}

static RegisterPrimOp primop_readFileType({
    .name = "__readFileType",
    .args = {"p"},
    .doc = R"(
      Determine the directory entry type of a filesystem node, being
      one of "directory", "regular", "symlink", or "unknown".
    )",
    .fun = prim_readFileType,
});

/* Read a directory (without . or ..) */
static void prim_readDir(EvalState & state, const PosIdx pos, Value * * args, Value & v)
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
        auto & attr = attrs.alloc(name);
        if (!type) {
            // Some filesystems or operating systems may not be able to return
            // detailed node info quickly in this case we produce a thunk to
            // query the file type lazily.
            auto epath = state.allocValue();
            epath->mkPath(path / name);
            if (!readFileType)
                readFileType = &state.getBuiltin("readFileType");
            attr.mkApp(readFileType, epath);
        } else {
            // This branch of the conditional is much more likely.
            // Here we just stringize the directory entry type.
            attr.mkString(fileTypeToString(*type));
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
      `C`, then `builtins.readDir ./A` will return the set

      ```nix
      { B = "regular"; C = "directory"; }
      ```

      The possible values for the file type are `"regular"`,
      `"directory"`, `"symlink"` and `"unknown"`.
    )",
    .fun = prim_readDir,
});

/* Extend single element string context with another output. */
static void prim_outputOf(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    SingleDerivedPath drvPath = state.coerceToSingleDerivedPath(pos, *args[0], "while evaluating the first argument to builtins.outputOf");

    OutputNameView outputName = state.forceStringNoCtx(*args[1], pos, "while evaluating the second argument to builtins.outputOf");

    state.mkSingleDerivedPathString(
        SingleDerivedPath::Built {
            .drvPath = make_ref<SingleDerivedPath>(drvPath),
            .output = std::string { outputName },
        },
        v);
}

static RegisterPrimOp primop_outputOf({
    .name = "__outputOf",
    .args = {"derivation-reference", "output-name"},
    .doc = R"(
      Return the output path of a derivation, literally or using a placeholder if needed.

      If the derivation has a statically-known output path (i.e. the derivation output is input-addressed, or fixed content-addresed), the output path will just be returned.
      But if the derivation is content-addressed or if the derivation is itself not-statically produced (i.e. is the output of another derivation), a placeholder will be returned instead.

      *`derivation reference`* must be a string that may contain a regular store path to a derivation, or may be a placeholder reference. If the derivation is produced by a derivation, you must explicitly select `drv.outPath`.
      This primop can be chained arbitrarily deeply.
      For instance,
      ```nix
      builtins.outputOf
        (builtins.outputOf myDrv "out")
        "out"
      ```
      will return a placeholder for the output of the output of `myDrv`.

      This primop corresponds to the `^` sigil for derivable paths, e.g. as part of installable syntax on the command line.
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
static void prim_toXML(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    std::ostringstream out;
    NixStringContext context;
    printValueAsXML(state, true, false, *args[0], out, context, pos);
    v.mkString(out.str(), context);
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
static void prim_toJSON(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    std::ostringstream out;
    NixStringContext context;
    printValueAsJSON(state, true, *args[0], pos, out, context);
    v.mkString(out.str(), context);
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
static void prim_fromJSON(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    auto s = state.forceStringNoCtx(*args[0], pos, "while evaluating the first argument passed to builtins.fromJSON");
    try {
        parseJSON(state, s, v);
    } catch (JSONParseError &e) {
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
static void prim_toFile(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    NixStringContext context;
    std::string name(state.forceStringNoCtx(*args[0], pos, "while evaluating the first argument passed to builtins.toFile"));
    std::string contents(state.forceString(*args[1], context, pos, "while evaluating the second argument passed to builtins.toFile"));

    StorePathSet refs;

    for (auto c : context) {
        if (auto p = std::get_if<NixStringContextElem::Opaque>(&c.raw))
            refs.insert(p->path);
        else
            state.error<EvalError>(
                "files created by %1% may not reference derivations, but %2% references %3%",
                "builtins.toFile",
                name,
                c.to_string()
            ).atPos(pos).debugThrow();
    }

    auto storePath = settings.readOnlyMode
        ? state.store->makeFixedOutputPathFromCA(name, TextInfo {
            .hash = hashString(HashAlgorithm::SHA256, contents),
            .references = std::move(refs),
        })
        : ({
            StringSource s { contents };
            state.store->addToStoreFromDump(s, name, TextIngestionMethod {}, HashAlgorithm::SHA256, refs, state.repair);
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
      [string interpolation](@docroot@/language/values.md#type-string), so the result of the
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

bool EvalState::callPathFilter(
    Value * filterFun,
    const SourcePath & path,
    std::string_view pathArg,
    PosIdx pos)
{
    auto st = path.lstat();

    /* Call the filter function.  The first argument is the path, the
       second is a string indicating the type of the file. */
    Value arg1;
    arg1.mkString(pathArg);

    Value arg2;
    // assert that type is not "unknown"
    arg2.mkString(fileTypeToString(st.type));

    Value * args []{&arg1, &arg2};
    Value res;
    callFunction(*filterFun, 2, args, res, pos);

    return forceBool(res, pos, "while evaluating the return value of the path filter function");
}

static void addPath(
    EvalState & state,
    const PosIdx pos,
    std::string_view name,
    SourcePath path,
    Value * filterFun,
    FileIngestionMethod method,
    const std::optional<Hash> expectedHash,
    Value & v,
    const NixStringContext & context)
{
    try {
        StorePathSet refs;

        if (path.accessor == state.rootFS && state.store->isInStore(path.path.abs())) {
            // FIXME: handle CA derivation outputs (where path needs to
            // be rewritten to the actual output).
            auto rewrites = state.realiseContext(context);
            path = {state.rootFS, CanonPath(state.toRealPath(rewriteStrings(path.path.abs(), rewrites), context))};

            try {
                auto [storePath, subPath] = state.store->toStorePath(path.path.abs());
                // FIXME: we should scanForReferences on the path before adding it
                refs = state.store->queryPathInfo(storePath)->references;
                path = {state.rootFS, CanonPath(state.store->toRealPath(storePath) + subPath)};
            } catch (Error &) { // FIXME: should be InvalidPathError
            }
        }

        std::unique_ptr<PathFilter> filter;
        if (filterFun)
            filter = std::make_unique<PathFilter>([&](const Path & p) {
                auto p2 = CanonPath(p);
                return state.callPathFilter(filterFun, {path.accessor, p2}, p2.abs(), pos);
            });

        std::optional<StorePath> expectedStorePath;
        if (expectedHash)
            expectedStorePath = state.store->makeFixedOutputPath(name, FixedOutputInfo {
                .method = method,
                .hash = *expectedHash,
                .references = {},
            });

        // FIXME: instead of a store path, we could return a
        // SourcePath that applies the filter lazily and copies to the
        // store on-demand.

        if (!expectedHash || !state.store->isValidPath(*expectedStorePath)) {
            auto dstPath = fetchToStore(*state.store, path.resolveSymlinks(), name, method, filter.get(), state.repair);
            if (expectedHash && expectedStorePath != dstPath)
                state.error<EvalError>(
                    "store path mismatch in (possibly filtered) path added from '%s'",
                    path
                ).atPos(pos).debugThrow();
            state.allowAndSetStorePathString(dstPath, v);
        } else
            state.allowAndSetStorePathString(*expectedStorePath, v);
    } catch (Error & e) {
        e.addTrace(state.positions[pos], "while adding path '%s'", path);
        throw;
    }
}


static void prim_filterSource(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    NixStringContext context;
    auto path = state.coerceToPath(pos, *args[1], context,
        "while evaluating the second argument (the path to filter) passed to 'builtins.filterSource'");
    state.forceFunction(*args[0], pos, "while evaluating the first argument passed to builtins.filterSource");

    addPath(state, pos, path.baseName(), path, args[0], FileIngestionMethod::Recursive, std::nullopt, v, context);
}

static RegisterPrimOp primop_filterSource({
    .name = "__filterSource",
    .args = {"e1", "e2"},
    .doc = R"(
      > **Warning**
      >
      > `filterSource` should not be used to filter store paths. Since
      > `filterSource` uses the name of the input directory while naming
      > the output directory, doing so will produce a directory name in
      > the form of `<hash2>-<hash>-<name>`, where `<hash>-<name>` is
      > the name of the input directory. Since `<hash>` depends on the
      > unfiltered directory, the name of the output directory will
      > indirectly depend on files that are filtered out by the
      > function. This will trigger a rebuild even when a filtered out
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

      However, if `source-dir` is a Subversion working copy, then all
      those annoying `.svn` subdirectories will also be copied to the
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
      `true` for them, the copy will fail). If you exclude a directory,
      the entire corresponding subtree of *e2* will be excluded.
    )",
    .fun = prim_filterSource,
});

static void prim_path(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    std::optional<SourcePath> path;
    std::string name;
    Value * filterFun = nullptr;
    auto method = FileIngestionMethod::Recursive;
    std::optional<Hash> expectedHash;
    NixStringContext context;

    state.forceAttrs(*args[0], pos, "while evaluating the argument passed to 'builtins.path'");

    for (auto & attr : *args[0]->attrs) {
        auto n = state.symbols[attr.name];
        if (n == "path")
            path.emplace(state.coerceToPath(attr.pos, *attr.value, context, "while evaluating the 'path' attribute passed to 'builtins.path'"));
        else if (attr.name == state.sName)
            name = state.forceStringNoCtx(*attr.value, attr.pos, "while evaluating the `name` attribute passed to builtins.path");
        else if (n == "filter")
            state.forceFunction(*(filterFun = attr.value), attr.pos, "while evaluating the `filter` parameter passed to builtins.path");
        else if (n == "recursive")
            method = FileIngestionMethod { state.forceBool(*attr.value, attr.pos, "while evaluating the `recursive` attribute passed to builtins.path") };
        else if (n == "sha256")
            expectedHash = newHashAllowEmpty(state.forceStringNoCtx(*attr.value, attr.pos, "while evaluating the `sha256` attribute passed to builtins.path"), HashAlgorithm::SHA256);
        else
            state.error<EvalError>(
                "unsupported argument '%1%' to 'addPath'",
                state.symbols[attr.name]
            ).atPos(attr.pos).debugThrow();
    }
    if (!path)
        state.error<EvalError>(
            "missing required 'path' attribute in the first argument to builtins.path"
        ).atPos(pos).debugThrow();
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
          path. Evaluation will fail if the hash is incorrect, and
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
static void prim_attrNames(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    state.forceAttrs(*args[0], pos, "while evaluating the argument passed to builtins.attrNames");

    state.mkList(v, args[0]->attrs->size());

    size_t n = 0;
    for (auto & i : *args[0]->attrs)
        (v.listElems()[n++] = state.allocValue())->mkString(state.symbols[i.name]);

    std::sort(v.listElems(), v.listElems() + n,
              [](Value * v1, Value * v2) { return strcmp(v1->c_str(), v2->c_str()) < 0; });
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
static void prim_attrValues(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    state.forceAttrs(*args[0], pos, "while evaluating the argument passed to builtins.attrValues");

    state.mkList(v, args[0]->attrs->size());

    unsigned int n = 0;
    for (auto & i : *args[0]->attrs)
        v.listElems()[n++] = (Value *) &i;

    std::sort(v.listElems(), v.listElems() + n,
        [&](Value * v1, Value * v2) {
            std::string_view s1 = state.symbols[((Attr *) v1)->name],
                s2 = state.symbols[((Attr *) v2)->name];
            return s1 < s2;
        });

    for (unsigned int i = 0; i < n; ++i)
        v.listElems()[i] = ((Attr *) v.listElems()[i])->value;
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
void prim_getAttr(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    auto attr = state.forceStringNoCtx(*args[0], pos, "while evaluating the first argument passed to builtins.getAttr");
    state.forceAttrs(*args[1], pos, "while evaluating the second argument passed to builtins.getAttr");
    Bindings::iterator i = getAttr(
        state,
        state.symbols.create(attr),
        args[1]->attrs,
        "in the attribute set under consideration"
    );
    // !!! add to stack trace?
    if (state.countCalls && i->pos) state.attrSelects[i->pos]++;
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
static void prim_unsafeGetAttrPos(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    auto attr = state.forceStringNoCtx(*args[0], pos, "while evaluating the first argument passed to builtins.unsafeGetAttrPos");
    state.forceAttrs(*args[1], pos, "while evaluating the second argument passed to builtins.unsafeGetAttrPos");
    Bindings::iterator i = args[1]->attrs->find(state.symbols.create(attr));
    if (i == args[1]->attrs->end())
        v.mkNull();
    else
        state.mkPos(v, i->pos);
}

static RegisterPrimOp primop_unsafeGetAttrPos(PrimOp {
    .name = "__unsafeGetAttrPos",
    .arity = 2,
    .fun = prim_unsafeGetAttrPos,
});

/* Dynamic version of the `?' operator. */
static void prim_hasAttr(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    auto attr = state.forceStringNoCtx(*args[0], pos, "while evaluating the first argument passed to builtins.hasAttr");
    state.forceAttrs(*args[1], pos, "while evaluating the second argument passed to builtins.hasAttr");
    v.mkBool(args[1]->attrs->find(state.symbols.create(attr)) != args[1]->attrs->end());
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
static void prim_isAttrs(EvalState & state, const PosIdx pos, Value * * args, Value & v)
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

static void prim_removeAttrs(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    state.forceAttrs(*args[0], pos, "while evaluating the first argument passed to builtins.removeAttrs");
    state.forceList(*args[1], pos, "while evaluating the second argument passed to builtins.removeAttrs");

    /* Get the attribute names to be removed.
       We keep them as Attrs instead of Symbols so std::set_difference
       can be used to remove them from attrs[0]. */
    // 64: large enough to fit the attributes of a derivation
    boost::container::small_vector<Attr, 64> names;
    names.reserve(args[1]->listSize());
    for (auto elem : args[1]->listItems()) {
        state.forceStringNoCtx(*elem, pos, "while evaluating the values of the second argument passed to builtins.removeAttrs");
        names.emplace_back(state.symbols.create(elem->string_view()), nullptr);
    }
    std::sort(names.begin(), names.end());

    /* Copy all attributes not in that set.  Note that we don't need
       to sort v.attrs because it's a subset of an already sorted
       vector. */
    auto attrs = state.buildBindings(args[0]->attrs->size());
    std::set_difference(
        args[0]->attrs->begin(), args[0]->attrs->end(),
        names.begin(), names.end(),
        std::back_inserter(attrs));
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
static void prim_listToAttrs(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    state.forceList(*args[0], pos, "while evaluating the argument passed to builtins.listToAttrs");

    auto attrs = state.buildBindings(args[0]->listSize());

    std::set<Symbol> seen;

    for (auto v2 : args[0]->listItems()) {
        state.forceAttrs(*v2, pos, "while evaluating an element of the list passed to builtins.listToAttrs");

        Bindings::iterator j = getAttr(state, state.sName, v2->attrs, "in a {name=...; value=...;} pair");

        auto name = state.forceStringNoCtx(*j->value, j->pos, "while evaluating the `name` attribute of an element of the list passed to builtins.listToAttrs");

        auto sym = state.symbols.create(name);
        if (seen.insert(sym).second) {
            Bindings::iterator j2 = getAttr(state, state.sValue, v2->attrs, "in a {name=...; value=...;} pair");
            attrs.insert(sym, j2->value, j2->pos);
        }
    }

    v.mkAttrs(attrs);
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

static void prim_intersectAttrs(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    state.forceAttrs(*args[0], pos, "while evaluating the first argument passed to builtins.intersectAttrs");
    state.forceAttrs(*args[1], pos, "while evaluating the second argument passed to builtins.intersectAttrs");

    Bindings &left = *args[0]->attrs;
    Bindings &right = *args[1]->attrs;

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
            Bindings::iterator r = right.find(l.name);
            if (r != right.end())
                attrs.insert(*r);
        }
    }
    else {
        for (auto & r : right) {
            Bindings::iterator l = left.find(r.name);
            if (l != left.end())
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

static void prim_catAttrs(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    auto attrName = state.symbols.create(state.forceStringNoCtx(*args[0], pos, "while evaluating the first argument passed to builtins.catAttrs"));
    state.forceList(*args[1], pos, "while evaluating the second argument passed to builtins.catAttrs");

    SmallValueVector<nonRecursiveStackReservation> res(args[1]->listSize());
    size_t found = 0;

    for (auto v2 : args[1]->listItems()) {
        state.forceAttrs(*v2, pos, "while evaluating an element in the list passed as second argument to builtins.catAttrs");
        Bindings::iterator i = v2->attrs->find(attrName);
        if (i != v2->attrs->end())
            res[found++] = i->value;
    }

    state.mkList(v, found);
    for (unsigned int n = 0; n < found; ++n)
        v.listElems()[n] = res[n];
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

static void prim_functionArgs(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    state.forceValue(*args[0], pos);
    if (args[0]->isPrimOpApp() || args[0]->isPrimOp()) {
        v.mkAttrs(&state.emptyBindings);
        return;
    }
    if (!args[0]->isLambda())
        state.error<TypeError>("'functionArgs' requires a function").atPos(pos).debugThrow();

    if (!args[0]->lambda.fun->hasFormals()) {
        v.mkAttrs(&state.emptyBindings);
        return;
    }

    auto attrs = state.buildBindings(args[0]->lambda.fun->formals->formals.size());
    for (auto & i : args[0]->lambda.fun->formals->formals)
        // !!! should optimise booleans (allocate only once)
        attrs.alloc(i.name, i.pos).mkBool(i.def);
    v.mkAttrs(attrs);
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
static void prim_mapAttrs(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    state.forceAttrs(*args[1], pos, "while evaluating the second argument passed to builtins.mapAttrs");

    auto attrs = state.buildBindings(args[1]->attrs->size());

    for (auto & i : *args[1]->attrs) {
        Value * vName = state.allocValue();
        Value * vFun2 = state.allocValue();
        vName->mkString(state.symbols[i.name]);
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

static void prim_zipAttrsWith(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    // we will first count how many values are present for each given key.
    // we then allocate a single attrset and pre-populate it with lists of
    // appropriate sizes, stash the pointers to the list elements of each,
    // and populate the lists. after that we replace the list in the every
    // attribute with the merge function application. this way we need not
    // use (slightly slower) temporary storage the GC does not know about.

    std::map<Symbol, std::pair<size_t, Value * *>> attrsSeen;

    state.forceFunction(*args[0], pos, "while evaluating the first argument passed to builtins.zipAttrsWith");
    state.forceList(*args[1], pos, "while evaluating the second argument passed to builtins.zipAttrsWith");
    const auto listSize = args[1]->listSize();
    const auto listElems = args[1]->listElems();

    for (unsigned int n = 0; n < listSize; ++n) {
        Value * vElem = listElems[n];
        state.forceAttrs(*vElem, noPos, "while evaluating a value of the list passed as second argument to builtins.zipAttrsWith");
        for (auto & attr : *vElem->attrs)
            attrsSeen[attr.name].first++;
    }

    auto attrs = state.buildBindings(attrsSeen.size());
    for (auto & [sym, elem] : attrsSeen) {
        auto & list = attrs.alloc(sym);
        state.mkList(list, elem.first);
        elem.second = list.listElems();
    }
    v.mkAttrs(attrs.alreadySorted());

    for (unsigned int n = 0; n < listSize; ++n) {
        Value * vElem = listElems[n];
        for (auto & attr : *vElem->attrs)
            *attrsSeen[attr.name].second++ = attr.value;
    }

    for (auto & attr : *v.attrs) {
        auto name = state.allocValue();
        name->mkString(state.symbols[attr.name]);
        auto call1 = state.allocValue();
        call1->mkApp(args[0], name);
        auto call2 = state.allocValue();
        call2->mkApp(call1, attr.value);
        attr.value = call2;
    }
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
static void prim_isList(EvalState & state, const PosIdx pos, Value * * args, Value & v)
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

static void elemAt(EvalState & state, const PosIdx pos, Value & list, int n, Value & v)
{
    state.forceList(list, pos, "while evaluating the first argument passed to builtins.elemAt");
    if (n < 0 || (unsigned int) n >= list.listSize())
        state.error<EvalError>(
            "list index %1% is out of bounds",
            n
        ).atPos(pos).debugThrow();
    state.forceValue(*list.listElems()[n], pos);
    v = *list.listElems()[n];
}

/* Return the n-1'th element of a list. */
static void prim_elemAt(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    elemAt(state, pos, *args[0], state.forceInt(*args[1], pos, "while evaluating the second argument passed to builtins.elemAt"), v);
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
static void prim_head(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    elemAt(state, pos, *args[0], 0, v);
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
static void prim_tail(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    state.forceList(*args[0], pos, "while evaluating the first argument passed to builtins.tail");
    if (args[0]->listSize() == 0)
        state.error<EvalError>("'tail' called on an empty list").atPos(pos).debugThrow();

    state.mkList(v, args[0]->listSize() - 1);
    for (unsigned int n = 0; n < v.listSize(); ++n)
        v.listElems()[n] = args[0]->listElems()[n + 1];
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
static void prim_map(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    state.forceList(*args[1], pos, "while evaluating the second argument passed to builtins.map");

    if (args[1]->listSize() == 0) {
        v = *args[1];
        return;
    }

    state.forceFunction(*args[0], pos, "while evaluating the first argument passed to builtins.map");

    state.mkList(v, args[1]->listSize());
    for (unsigned int n = 0; n < v.listSize(); ++n)
        (v.listElems()[n] = state.allocValue())->mkApp(
            args[0], args[1]->listElems()[n]);
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
static void prim_filter(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    state.forceList(*args[1], pos, "while evaluating the second argument passed to builtins.filter");

    if (args[1]->listSize() == 0) {
        v = *args[1];
        return;
    }

    state.forceFunction(*args[0], pos, "while evaluating the first argument passed to builtins.filter");

    SmallValueVector<nonRecursiveStackReservation> vs(args[1]->listSize());
    size_t k = 0;

    bool same = true;
    for (unsigned int n = 0; n < args[1]->listSize(); ++n) {
        Value res;
        state.callFunction(*args[0], *args[1]->listElems()[n], res, noPos);
        if (state.forceBool(res, pos, "while evaluating the return value of the filtering function passed to builtins.filter"))
            vs[k++] = args[1]->listElems()[n];
        else
            same = false;
    }

    if (same)
        v = *args[1];
    else {
        state.mkList(v, k);
        for (unsigned int n = 0; n < k; ++n) v.listElems()[n] = vs[n];
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
static void prim_elem(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    bool res = false;
    state.forceList(*args[1], pos, "while evaluating the second argument passed to builtins.elem");
    for (auto elem : args[1]->listItems())
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
static void prim_concatLists(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    state.forceList(*args[0], pos, "while evaluating the first argument passed to builtins.concatLists");
    state.concatLists(v, args[0]->listSize(), args[0]->listElems(), pos, "while evaluating a value of the list passed to builtins.concatLists");
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
static void prim_length(EvalState & state, const PosIdx pos, Value * * args, Value & v)
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
static void prim_foldlStrict(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    state.forceFunction(*args[0], pos, "while evaluating the first argument passed to builtins.foldlStrict");
    state.forceList(*args[2], pos, "while evaluating the third argument passed to builtins.foldlStrict");

    if (args[2]->listSize()) {
        Value * vCur = args[1];

        for (auto [n, elem] : enumerate(args[2]->listItems())) {
            Value * vs []{vCur, elem};
            vCur = n == args[2]->listSize() - 1 ? &v : state.allocValue();
            state.callFunction(*args[0], 2, vs, *vCur, pos);
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

static void anyOrAll(bool any, EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    state.forceFunction(*args[0], pos, std::string("while evaluating the first argument passed to builtins.") + (any ? "any" : "all"));
    state.forceList(*args[1], pos, std::string("while evaluating the second argument passed to builtins.") + (any ? "any" : "all"));

    std::string_view errorCtx = any
        ? "while evaluating the return value of the function passed to builtins.any"
        : "while evaluating the return value of the function passed to builtins.all";

    Value vTmp;
    for (auto elem : args[1]->listItems()) {
        state.callFunction(*args[0], *elem, vTmp, pos);
        bool res = state.forceBool(vTmp, pos, errorCtx);
        if (res == any) {
            v.mkBool(any);
            return;
        }
    }

    v.mkBool(!any);
}


static void prim_any(EvalState & state, const PosIdx pos, Value * * args, Value & v)
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

static void prim_all(EvalState & state, const PosIdx pos, Value * * args, Value & v)
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

static void prim_genList(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    auto len = state.forceInt(*args[1], pos, "while evaluating the second argument passed to builtins.genList");

    if (len < 0)
        state.error<EvalError>("cannot create list of size %1%", len).atPos(pos).debugThrow();

    // More strict than striclty (!) necessary, but acceptable
    // as evaluating map without accessing any values makes little sense.
    state.forceFunction(*args[0], noPos, "while evaluating the first argument passed to builtins.genList");

    state.mkList(v, len);
    for (unsigned int n = 0; n < (unsigned int) len; ++n) {
        auto arg = state.allocValue();
        arg->mkInt(n);
        (v.listElems()[n] = state.allocValue())->mkApp(args[0], arg);
    }
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

static void prim_lessThan(EvalState & state, const PosIdx pos, Value * * args, Value & v);


static void prim_sort(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    state.forceList(*args[1], pos, "while evaluating the second argument passed to builtins.sort");

    auto len = args[1]->listSize();
    if (len == 0) {
        v = *args[1];
        return;
    }

    state.forceFunction(*args[0], pos, "while evaluating the first argument passed to builtins.sort");

    state.mkList(v, len);
    for (unsigned int n = 0; n < len; ++n) {
        state.forceValue(*args[1]->listElems()[n], pos);
        v.listElems()[n] = args[1]->listElems()[n];
    }

    auto comparator = [&](Value * a, Value * b) {
        /* Optimization: if the comparator is lessThan, bypass
           callFunction. */
        /* TODO: (layus) this is absurd. An optimisation like this
           should be outside the lambda creation */
        if (args[0]->isPrimOp() && args[0]->primOp->fun == prim_lessThan)
            return CompareValues(state, noPos, "while evaluating the ordering function passed to builtins.sort")(a, b);

        Value * vs[] = {a, b};
        Value vBool;
        state.callFunction(*args[0], 2, vs, vBool, noPos);
        return state.forceBool(vBool, pos, "while evaluating the return value of the sorting function passed to builtins.sort");
    };

    /* FIXME: std::sort can segfault if the comparator is not a strict
       weak ordering. What to do? std::stable_sort() seems more
       resilient, but no guarantees... */
    std::stable_sort(v.listElems(), v.listElems() + len, comparator);
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
    )",
    .fun = prim_sort,
});

static void prim_partition(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    state.forceFunction(*args[0], pos, "while evaluating the first argument passed to builtins.partition");
    state.forceList(*args[1], pos, "while evaluating the second argument passed to builtins.partition");

    auto len = args[1]->listSize();

    ValueVector right, wrong;

    for (unsigned int n = 0; n < len; ++n) {
        auto vElem = args[1]->listElems()[n];
        state.forceValue(*vElem, pos);
        Value res;
        state.callFunction(*args[0], *vElem, res, pos);
        if (state.forceBool(res, pos, "while evaluating the return value of the partition function passed to builtins.partition"))
            right.push_back(vElem);
        else
            wrong.push_back(vElem);
    }

    auto attrs = state.buildBindings(2);

    auto & vRight = attrs.alloc(state.sRight);
    auto rsize = right.size();
    state.mkList(vRight, rsize);
    if (rsize)
        memcpy(vRight.listElems(), right.data(), sizeof(Value *) * rsize);

    auto & vWrong = attrs.alloc(state.sWrong);
    auto wsize = wrong.size();
    state.mkList(vWrong, wsize);
    if (wsize)
        memcpy(vWrong.listElems(), wrong.data(), sizeof(Value *) * wsize);

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

static void prim_groupBy(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    state.forceFunction(*args[0], pos, "while evaluating the first argument passed to builtins.groupBy");
    state.forceList(*args[1], pos, "while evaluating the second argument passed to builtins.groupBy");

    ValueVectorMap attrs;

    for (auto vElem : args[1]->listItems()) {
        Value res;
        state.callFunction(*args[0], *vElem, res, pos);
        auto name = state.forceStringNoCtx(res, pos, "while evaluating the return value of the grouping function passed to builtins.groupBy");
        auto sym = state.symbols.create(name);
        auto vector = attrs.try_emplace(sym, ValueVector()).first;
        vector->second.push_back(vElem);
    }

    auto attrs2 = state.buildBindings(attrs.size());

    for (auto & i : attrs) {
        auto & list = attrs2.alloc(i.first);
        auto size = i.second.size();
        state.mkList(list, size);
        memcpy(list.listElems(), i.second.data(), sizeof(Value *) * size);
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

static void prim_concatMap(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    state.forceFunction(*args[0], pos, "while evaluating the first argument passed to builtins.concatMap");
    state.forceList(*args[1], pos, "while evaluating the second argument passed to builtins.concatMap");
    auto nrLists = args[1]->listSize();

    // List of returned lists before concatenation. References to these Values must NOT be persisted.
    SmallTemporaryValueVector<conservativeStackReservation> lists(nrLists);
    size_t len = 0;

    for (unsigned int n = 0; n < nrLists; ++n) {
        Value * vElem = args[1]->listElems()[n];
        state.callFunction(*args[0], *vElem, lists[n], pos);
        state.forceList(lists[n], lists[n].determinePos(args[0]->determinePos(pos)), "while evaluating the return value of the function passed to builtins.concatMap");
        len += lists[n].listSize();
    }

    state.mkList(v, len);
    auto out = v.listElems();
    for (unsigned int n = 0, pos = 0; n < nrLists; ++n) {
        auto l = lists[n].listSize();
        if (l)
            memcpy(out + pos, lists[n].listElems(), l * sizeof(Value *));
        pos += l;
    }
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


static void prim_add(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    state.forceValue(*args[0], pos);
    state.forceValue(*args[1], pos);
    if (args[0]->type() == nFloat || args[1]->type() == nFloat)
        v.mkFloat(state.forceFloat(*args[0], pos, "while evaluating the first argument of the addition")
                + state.forceFloat(*args[1], pos, "while evaluating the second argument of the addition"));
    else
        v.mkInt(  state.forceInt(*args[0], pos, "while evaluating the first argument of the addition")
                + state.forceInt(*args[1], pos, "while evaluating the second argument of the addition"));
}

static RegisterPrimOp primop_add({
    .name = "__add",
    .args = {"e1", "e2"},
    .doc = R"(
      Return the sum of the numbers *e1* and *e2*.
    )",
    .fun = prim_add,
});

static void prim_sub(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    state.forceValue(*args[0], pos);
    state.forceValue(*args[1], pos);
    if (args[0]->type() == nFloat || args[1]->type() == nFloat)
        v.mkFloat(state.forceFloat(*args[0], pos, "while evaluating the first argument of the subtraction")
                - state.forceFloat(*args[1], pos, "while evaluating the second argument of the subtraction"));
    else
        v.mkInt(  state.forceInt(*args[0], pos, "while evaluating the first argument of the subtraction")
                - state.forceInt(*args[1], pos, "while evaluating the second argument of the subtraction"));
}

static RegisterPrimOp primop_sub({
    .name = "__sub",
    .args = {"e1", "e2"},
    .doc = R"(
      Return the difference between the numbers *e1* and *e2*.
    )",
    .fun = prim_sub,
});

static void prim_mul(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    state.forceValue(*args[0], pos);
    state.forceValue(*args[1], pos);
    if (args[0]->type() == nFloat || args[1]->type() == nFloat)
        v.mkFloat(state.forceFloat(*args[0], pos, "while evaluating the first of the multiplication")
                * state.forceFloat(*args[1], pos, "while evaluating the second argument of the multiplication"));
    else
        v.mkInt(  state.forceInt(*args[0], pos, "while evaluating the first argument of the multiplication")
                * state.forceInt(*args[1], pos, "while evaluating the second argument of the multiplication"));
}

static RegisterPrimOp primop_mul({
    .name = "__mul",
    .args = {"e1", "e2"},
    .doc = R"(
      Return the product of the numbers *e1* and *e2*.
    )",
    .fun = prim_mul,
});

static void prim_div(EvalState & state, const PosIdx pos, Value * * args, Value & v)
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
        if (i1 == std::numeric_limits<NixInt>::min() && i2 == -1)
            state.error<EvalError>("overflow in integer division").atPos(pos).debugThrow();

        v.mkInt(i1 / i2);
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

static void prim_bitAnd(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    v.mkInt(state.forceInt(*args[0], pos, "while evaluating the first argument passed to builtins.bitAnd")
            & state.forceInt(*args[1], pos, "while evaluating the second argument passed to builtins.bitAnd"));
}

static RegisterPrimOp primop_bitAnd({
    .name = "__bitAnd",
    .args = {"e1", "e2"},
    .doc = R"(
      Return the bitwise AND of the integers *e1* and *e2*.
    )",
    .fun = prim_bitAnd,
});

static void prim_bitOr(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    v.mkInt(state.forceInt(*args[0], pos, "while evaluating the first argument passed to builtins.bitOr")
            | state.forceInt(*args[1], pos, "while evaluating the second argument passed to builtins.bitOr"));
}

static RegisterPrimOp primop_bitOr({
    .name = "__bitOr",
    .args = {"e1", "e2"},
    .doc = R"(
      Return the bitwise OR of the integers *e1* and *e2*.
    )",
    .fun = prim_bitOr,
});

static void prim_bitXor(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    v.mkInt(state.forceInt(*args[0], pos, "while evaluating the first argument passed to builtins.bitXor")
            ^ state.forceInt(*args[1], pos, "while evaluating the second argument passed to builtins.bitXor"));
}

static RegisterPrimOp primop_bitXor({
    .name = "__bitXor",
    .args = {"e1", "e2"},
    .doc = R"(
      Return the bitwise XOR of the integers *e1* and *e2*.
    )",
    .fun = prim_bitXor,
});

static void prim_lessThan(EvalState & state, const PosIdx pos, Value * * args, Value & v)
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
      Return `true` if the number *e1* is less than the number *e2*, and
      `false` otherwise. Evaluation aborts if either *e1* or *e2* does not
      evaluate to a number.
    )",
    .fun = prim_lessThan,
});


/*************************************************************
 * String manipulation
 *************************************************************/


/* Convert the argument to a string.  Paths are *not* copied to the
   store, so `toString /foo/bar' yields `"/foo/bar"', not
   `"/nix/store/whatever..."'. */
static void prim_toString(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    NixStringContext context;
    auto s = state.coerceToString(pos, *args[0], context,
            "while evaluating the first argument passed to builtins.toString",
            true, false);
    v.mkString(*s, context);
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
   at character position `min(start, stringLength str)' inclusive and
   ending at `min(start + len, stringLength str)'.  `start' must be
   non-negative. */
static void prim_substring(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    int start = state.forceInt(*args[0], pos, "while evaluating the first argument (the start offset) passed to builtins.substring");

    if (start < 0)
        state.error<EvalError>("negative start position in 'substring'").atPos(pos).debugThrow();


    int len = state.forceInt(*args[1], pos, "while evaluating the second argument (the substring length) passed to builtins.substring");

    // Special-case on empty substring to avoid O(n) strlen
    // This allows for the use of empty substrings to efficently capture string context
    if (len == 0) {
        state.forceValue(*args[2], pos);
        if (args[2]->type() == nString) {
            v.mkString("", args[2]->context());
            return;
        }
    }

    NixStringContext context;
    auto s = state.coerceToString(pos, *args[2], context, "while evaluating the third argument (the string) passed to builtins.substring");

    v.mkString((unsigned int) start >= s->size() ? "" : s->substr(start, len), context);
}

static RegisterPrimOp primop_substring({
    .name = "__substring",
    .args = {"start", "len", "s"},
    .doc = R"(
      Return the substring of *s* from character position *start*
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

static void prim_stringLength(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    NixStringContext context;
    auto s = state.coerceToString(pos, *args[0], context, "while evaluating the argument passed to builtins.stringLength");
    v.mkInt(s->size());
}

static RegisterPrimOp primop_stringLength({
    .name = "__stringLength",
    .args = {"e"},
    .doc = R"(
      Return the length of the string *e*. If *e* is not a string,
      evaluation is aborted.
    )",
    .fun = prim_stringLength,
});

/* Return the cryptographic hash of a string in base-16. */
static void prim_hashString(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    auto algo = state.forceStringNoCtx(*args[0], pos, "while evaluating the first argument passed to builtins.hashString");
    std::optional<HashAlgorithm> ha = parseHashAlgo(algo);
    if (!ha)
        state.error<EvalError>("unknown hash algorithm '%1%'", algo).atPos(pos).debugThrow();

    NixStringContext context; // discarded
    auto s = state.forceString(*args[1], context, pos, "while evaluating the second argument passed to builtins.hashString");

    v.mkString(hashString(*ha, s).to_string(HashFormat::Base16, false));
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

static void prim_convertHash(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    state.forceAttrs(*args[0], pos, "while evaluating the first argument passed to builtins.convertHash");
    auto &inputAttrs = args[0]->attrs;

    Bindings::iterator iteratorHash = getAttr(state, state.symbols.create("hash"), inputAttrs, "while locating the attribute 'hash'");
    auto hash = state.forceStringNoCtx(*iteratorHash->value, pos, "while evaluating the attribute 'hash'");

    Bindings::iterator iteratorHashAlgo = inputAttrs->find(state.symbols.create("hashAlgo"));
    std::optional<HashAlgorithm> ha = std::nullopt;
    if (iteratorHashAlgo != inputAttrs->end()) {
        ha = parseHashAlgo(state.forceStringNoCtx(*iteratorHashAlgo->value, pos, "while evaluating the attribute 'hashAlgo'"));
    }

    Bindings::iterator iteratorToHashFormat = getAttr(state, state.symbols.create("toHashFormat"), args[0]->attrs, "while locating the attribute 'toHashFormat'");
    HashFormat hf = parseHashFormat(state.forceStringNoCtx(*iteratorToHashFormat->value, pos, "while evaluating the attribute 'toHashFormat'"));

    v.mkString(Hash::parseAny(hash, ha).to_string(hf, hf == HashFormat::SRI));
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
    // TODO use C++20 transparent comparison when available
    std::unordered_map<std::string_view, std::regex> cache;
    std::list<std::string> keys;

    std::regex get(std::string_view re)
    {
        auto it = cache.find(re);
        if (it != cache.end())
            return it->second;
        keys.emplace_back(re);
        return cache.emplace(keys.back(), std::regex(keys.back(), std::regex::extended)).first->second;
    }
};

std::shared_ptr<RegexCache> makeRegexCache()
{
    return std::make_shared<RegexCache>();
}

void prim_match(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    auto re = state.forceStringNoCtx(*args[0], pos, "while evaluating the first argument passed to builtins.match");

    try {

        auto regex = state.regexCache->get(re);

        NixStringContext context;
        const auto str = state.forceString(*args[1], context, pos, "while evaluating the second argument passed to builtins.match");

        std::cmatch match;
        if (!std::regex_match(str.begin(), str.end(), match, regex)) {
            v.mkNull();
            return;
        }

        // the first match is the whole string
        const size_t len = match.size() - 1;
        state.mkList(v, len);
        for (size_t i = 0; i < len; ++i) {
            if (!match[i+1].matched)
                (v.listElems()[i] = state.allocValue())->mkNull();
            else
                (v.listElems()[i] = state.allocValue())->mkString(match[i + 1].str());
        }

    } catch (std::regex_error & e) {
        if (e.code() == std::regex_constants::error_space) {
            // limit is _GLIBCXX_REGEX_STATE_LIMIT for libstdc++
            state.error<EvalError>("memory limit exceeded by regular expression '%s'", re)
                .atPos(pos)
                .debugThrow();
        } else
            state.error<EvalError>("invalid regular expression '%s'", re)
                .atPos(pos)
                .debugThrow();
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
void prim_split(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    auto re = state.forceStringNoCtx(*args[0], pos, "while evaluating the first argument passed to builtins.split");

    try {

        auto regex = state.regexCache->get(re);

        NixStringContext context;
        const auto str = state.forceString(*args[1], context, pos, "while evaluating the second argument passed to builtins.split");

        auto begin = std::cregex_iterator(str.begin(), str.end(), regex);
        auto end = std::cregex_iterator();

        // Any matches results are surrounded by non-matching results.
        const size_t len = std::distance(begin, end);
        state.mkList(v, 2 * len + 1);
        size_t idx = 0;

        if (len == 0) {
            v.listElems()[idx++] = args[1];
            return;
        }

        for (auto i = begin; i != end; ++i) {
            assert(idx <= 2 * len + 1 - 3);
            auto match = *i;

            // Add a string for non-matched characters.
            (v.listElems()[idx++] = state.allocValue())->mkString(match.prefix().str());

            // Add a list for matched substrings.
            const size_t slen = match.size() - 1;
            auto elem = v.listElems()[idx++] = state.allocValue();

            // Start at 1, beacause the first match is the whole string.
            state.mkList(*elem, slen);
            for (size_t si = 0; si < slen; ++si) {
                if (!match[si + 1].matched)
                    (elem->listElems()[si] = state.allocValue())->mkNull();
                else
                    (elem->listElems()[si] = state.allocValue())->mkString(match[si + 1].str());
            }

            // Add a string for non-matched suffix characters.
            if (idx == 2 * len)
                (v.listElems()[idx++] = state.allocValue())->mkString(match.suffix().str());
        }

        assert(idx == 2 * len + 1);

    } catch (std::regex_error & e) {
        if (e.code() == std::regex_constants::error_space) {
            // limit is _GLIBCXX_REGEX_STATE_LIMIT for libstdc++
            state.error<EvalError>("memory limit exceeded by regular expression '%s'", re)
                .atPos(pos)
                .debugThrow();
        } else
            state.error<EvalError>("invalid regular expression '%s'", re)
                .atPos(pos)
                .debugThrow();
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

static void prim_concatStringsSep(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    NixStringContext context;

    auto sep = state.forceString(*args[0], context, pos, "while evaluating the first argument (the separator string) passed to builtins.concatStringsSep");
    state.forceList(*args[1], pos, "while evaluating the second argument (the list of strings to concat) passed to builtins.concatStringsSep");

    std::string res;
    res.reserve((args[1]->listSize() + 32) * sep.size());
    bool first = true;

    for (auto elem : args[1]->listItems()) {
        if (first) first = false; else res += sep;
        res += *state.coerceToString(pos, *elem, context, "while evaluating one element of the list of strings to concat passed to builtins.concatStringsSep");
    }

    v.mkString(res, context);
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

static void prim_replaceStrings(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    state.forceList(*args[0], pos, "while evaluating the first argument passed to builtins.replaceStrings");
    state.forceList(*args[1], pos, "while evaluating the second argument passed to builtins.replaceStrings");
    if (args[0]->listSize() != args[1]->listSize())
        state.error<EvalError>(
            "'from' and 'to' arguments passed to builtins.replaceStrings have different lengths"
        ).atPos(pos).debugThrow();

    std::vector<std::string> from;
    from.reserve(args[0]->listSize());
    for (auto elem : args[0]->listItems())
        from.emplace_back(state.forceString(*elem, pos, "while evaluating one of the strings to replace passed to builtins.replaceStrings"));

    std::unordered_map<size_t, std::string> cache;
    auto to = args[1]->listItems();

    NixStringContext context;
    auto s = state.forceString(*args[2], context, pos, "while evaluating the third argument passed to builtins.replaceStrings");

    std::string res;
    // Loops one past last character to handle the case where 'from' contains an empty string.
    for (size_t p = 0; p <= s.size(); ) {
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
                    auto ts = state.forceString(**j, ctx, pos, "while evaluating one of the replacement strings passed to builtins.replaceStrings");
                    v = (cache.emplace(j_index, ts)).first;
                    for (auto& path : ctx)
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

    v.mkString(res, context);
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


static void prim_parseDrvName(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    auto name = state.forceStringNoCtx(*args[0], pos, "while evaluating the first argument passed to builtins.parseDrvName");
    DrvName parsed(name);
    auto attrs = state.buildBindings(2);
    attrs.alloc(state.sName).mkString(parsed.name);
    attrs.alloc("version").mkString(parsed.version);
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

static void prim_compareVersions(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    auto version1 = state.forceStringNoCtx(*args[0], pos, "while evaluating the first argument passed to builtins.compareVersions");
    auto version2 = state.forceStringNoCtx(*args[1], pos, "while evaluating the second argument passed to builtins.compareVersions");
    v.mkInt(compareVersions(version1, version2));
}

static RegisterPrimOp primop_compareVersions({
    .name = "__compareVersions",
    .args = {"s1", "s2"},
    .doc = R"(
      Compare two strings representing versions and return `-1` if
      version *s1* is older than version *s2*, `0` if they are the same,
      and `1` if *s1* is newer than *s2*. The version comparison
      algorithm is the same as the one used by [`nix-env
      -u`](../command-ref/nix-env.md#operation---upgrade).
    )",
    .fun = prim_compareVersions,
});

static void prim_splitVersion(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    auto version = state.forceStringNoCtx(*args[0], pos, "while evaluating the first argument passed to builtins.splitVersion");
    auto iter = version.cbegin();
    Strings components;
    while (iter != version.cend()) {
        auto component = nextComponent(iter, version.cend());
        if (component.empty())
            break;
        components.emplace_back(component);
    }
    state.mkList(v, components.size());
    for (const auto & [n, component] : enumerate(components))
        (v.listElems()[n] = state.allocValue())->mkString(std::move(component));
}

static RegisterPrimOp primop_splitVersion({
    .name = "__splitVersion",
    .args = {"s"},
    .doc = R"(
      Split a string representing a version into its components, by the
      same version splitting logic underlying the version comparison in
      [`nix-env -u`](../command-ref/nix-env.md#operation---upgrade).
    )",
    .fun = prim_splitVersion,
});


/*************************************************************
 * Primop registration
 *************************************************************/


RegisterPrimOp::PrimOps * RegisterPrimOp::primOps;


RegisterPrimOp::RegisterPrimOp(PrimOp && primOp)
{
    if (!primOps) primOps = new PrimOps;
    primOps->push_back(std::move(primOp));
}


void EvalState::createBaseEnv()
{
    baseEnv.up = 0;

    /* Add global constants such as `true' to the base environment. */
    Value v;

    /* `builtins' must be first! */
    v.mkAttrs(buildBindings(128).finish());
    addConstant("builtins", v, {
        .type = nAttrs,
        .doc = R"(
          Contains all the [built-in functions](@docroot@/language/builtins.md) and values.

          Since built-in functions were added over time, [testing for attributes](./operators.md#has-attribute) in `builtins` can be used for graceful fallback on older Nix installations:

          ```nix
          # if hasContext is not available, we assume `s` has a context
          if builtins ? hasContext then builtins.hasContext s else true
          ```
        )",
    });

    v.mkBool(true);
    addConstant("true", v, {
        .type = nBool,
        .doc = R"(
          Primitive value.

          It can be returned by
          [comparison operators](@docroot@/language/operators.md#Comparison)
          and used in
          [conditional expressions](@docroot@/language/constructs.md#Conditionals).

          The name `true` is not special, and can be shadowed:

          ```nix-repl
          nix-repl> let true = 1; in true
          1
          ```
        )",
    });

    v.mkBool(false);
    addConstant("false", v, {
        .type = nBool,
        .doc = R"(
          Primitive value.

          It can be returned by
          [comparison operators](@docroot@/language/operators.md#Comparison)
          and used in
          [conditional expressions](@docroot@/language/constructs.md#Conditionals).

          The name `false` is not special, and can be shadowed:

          ```nix-repl
          nix-repl> let false = 1; in false
          1
          ```
        )",
    });

    v.mkNull();
    addConstant("null", v, {
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

    if (!evalSettings.pureEval) {
        v.mkInt(time(0));
    }
    addConstant("__currentTime", v, {
        .type = nInt,
        .doc = R"(
          Return the [Unix time](https://en.wikipedia.org/wiki/Unix_time) at first evaluation.
          Repeated references to that name will re-use the initially obtained value.

          Example:

          ```console
          $ nix repl
          Welcome to Nix 2.15.1 Type :? for help.

          nix-repl> builtins.currentTime
          1683705525

          nix-repl> builtins.currentTime
          1683705525
          ```

          The [store path](@docroot@/glossary.md#gloss-store-path) of a derivation depending on `currentTime` will differ for each evaluation, unless both evaluate `builtins.currentTime` in the same second.
        )",
        .impureOnly = true,
    });

    if (!evalSettings.pureEval)
        v.mkString(evalSettings.getCurrentSystem());
    addConstant("__currentSystem", v, {
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

    v.mkString(nixVersion);
    addConstant("__nixVersion", v, {
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

    v.mkString(store->storeDir);
    addConstant("__storeDir", v, {
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
    addConstant("__langVersion", v, {
        .type = nInt,
        .doc = R"(
          The current version of the Nix language.
        )",
    });

    // Miscellaneous
    if (evalSettings.enableNativeCode) {
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

    addPrimOp({
        .name = "__traceVerbose",
        .args = { "e1", "e2" },
        .arity = 2,
        .doc = R"(
          Evaluate *e1* and print its abstract syntax representation on standard
          error if `--trace-verbose` is enabled. Then return *e2*. This function
          is useful for debugging.
        )",
        .fun = evalSettings.traceVerbose ? prim_trace : prim_second,
    });

    /* Add a value containing the current Nix expression search path. */
    mkList(v, searchPath.elements.size());
    int n = 0;
    for (auto & i : searchPath.elements) {
        auto attrs = buildBindings(2);
        attrs.alloc("path").mkString(i.path.s);
        attrs.alloc("prefix").mkString(i.prefix.s);
        (v.listElems()[n++] = allocValue())->mkAttrs(attrs);
    }
    addConstant("__nixPath", v, {
        .type = nList,
        .doc = R"(
          List of search path entries used to resolve [lookup paths](@docroot@/language/constructs/lookup-path.md).

          Lookup path expressions can be
          [desugared](https://en.wikipedia.org/wiki/Syntactic_sugar)
          using this and
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

    if (RegisterPrimOp::primOps)
        for (auto & primOp : *RegisterPrimOp::primOps)
            if (experimentalFeatureSettings.isEnabled(primOp.experimentalFeature))
            {
                auto primOpAdjusted = primOp;
                primOpAdjusted.arity = std::max(primOp.args.size(), primOp.arity);
                addPrimOp(std::move(primOpAdjusted));
            }

    /* Add a wrapper around the derivation primop that computes the
       `drvPath' and `outPath' attributes lazily.

       Null docs because it is documented separately.
       */
    auto vDerivation = allocValue();
    addConstant("derivation", vDerivation, {
        .type = nFunction,
    });

    /* Now that we've added all primops, sort the `builtins' set,
       because attribute lookups expect it to be sorted. */
    baseEnv.values[0]->attrs->sort();

    staticBaseEnv->sort();

    /* Note: we have to initialize the 'derivation' constant *after*
       building baseEnv/staticBaseEnv because it uses 'builtins'. */
    evalFile(derivationInternal, *vDerivation);
}


}
