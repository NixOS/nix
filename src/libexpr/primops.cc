#include "archive.hh"
#include "derivations.hh"
#include "eval-inline.hh"
#include "eval.hh"
#include "globals.hh"
#include "json-to-value.hh"
#include "names.hh"
#include "store-api.hh"
#include "util.hh"
#include "json.hh"
#include "value-to-json.hh"
#include "value-to-xml.hh"
#include "primops.hh"

#include <boost/container/small_vector.hpp>

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


InvalidPathError::InvalidPathError(const Path & path) :
    EvalError("path '%s' is not valid", path), path(path) {}

StringMap EvalState::realiseContext(const PathSet & context)
{
    std::vector<DerivedPath::Built> drvs;
    StringMap res;

    for (auto & i : context) {
        auto [ctx, outputName] = decodeContext(*store, i);
        auto ctxS = store->printStorePath(ctx);
        if (!store->isValidPath(ctx))
            throw InvalidPathError(store->printStorePath(ctx));
        if (!outputName.empty() && ctx.isDerivation()) {
            drvs.push_back({ctx, {outputName}});
        } else {
            res.insert_or_assign(ctxS, ctxS);
        }
    }

    if (drvs.empty()) return {};

    if (!evalSettings.enableImportFromDerivation)
        throw Error(
            "cannot build '%1%' during evaluation because the option 'allow-import-from-derivation' is disabled",
            store->printStorePath(drvs.begin()->drvPath));

    /* Build/substitute the context. */
    std::vector<DerivedPath> buildReqs;
    for (auto & d : drvs) buildReqs.emplace_back(DerivedPath { d });
    store->buildPaths(buildReqs);

    /* Get all the output paths corresponding to the placeholders we had */
    for (auto & [drvPath, outputs] : drvs) {
        auto outputPaths = store->queryDerivationOutputMap(drvPath);
        for (auto & outputName : outputs) {
            if (outputPaths.count(outputName) == 0)
                throw Error("derivation '%s' does not have an output named '%s'",
                        store->printStorePath(drvPath), outputName);
            res.insert_or_assign(
                downstreamPlaceholder(*store, drvPath, outputName),
                store->printStorePath(outputPaths.at(outputName))
            );
        }
    }

    /* Add the output of this derivations to the allowed
       paths. */
    if (allowedPaths) {
        for (auto & [_placeholder, outputPath] : res) {
            allowPath(store->toRealPath(outputPath));
        }
    }

    return res;
}

struct RealisePathFlags {
    // Whether to check that the path is allowed in pure eval mode
    bool checkForPureEval = true;
};

static Path realisePath(EvalState & state, const Pos & pos, Value & v, const RealisePathFlags flags = {})
{
    PathSet context;

    auto path = [&]()
    {
        try {
            return state.coerceToPath(pos, v, context);
        } catch (Error & e) {
            e.addTrace(pos, "while realising the context of a path");
            throw;
        }
    }();

    try {
        StringMap rewrites = state.realiseContext(context);

        auto realPath = state.toRealPath(rewriteStrings(path, rewrites), context);

        return flags.checkForPureEval
            ? state.checkSourcePath(realPath)
            : realPath;
    } catch (Error & e) {
        e.addTrace(pos, "while realising the context of path '%s'", path);
        throw;
    }
}

/* Add and attribute to the given attribute map from the output name to
   the output path, or a placeholder.

   Where possible the path is used, but for floating CA derivations we
   may not know it. For sake of determinism we always assume we don't
   and instead put in a place holder. In either case, however, the
   string context will contain the drv path and output name, so
   downstream derivations will have the proper dependency, and in
   addition, before building, the placeholder will be rewritten to be
   the actual path.

   The 'drv' and 'drvPath' outputs must correspond. */
static void mkOutputString(
    EvalState & state,
    BindingsBuilder & attrs,
    const StorePath & drvPath,
    const BasicDerivation & drv,
    const std::pair<std::string, DerivationOutput> & o)
{
    auto optOutputPath = o.second.path(*state.store, drv.name, o.first);
    attrs.alloc(o.first).mkString(
        optOutputPath
            ? state.store->printStorePath(*optOutputPath)
            /* Downstream we would substitute this for an actual path once
               we build the floating CA derivation */
            /* FIXME: we need to depend on the basic derivation, not
               derivation */
            : downstreamPlaceholder(*state.store, drvPath, o.first),
        {"!" + o.first + "!" + state.store->printStorePath(drvPath)});
}

/* Load and evaluate an expression from path specified by the
   argument. */
static void import(EvalState & state, const Pos & pos, Value & vPath, Value * vScope, Value & v)
{
    auto path = realisePath(state, pos, vPath);

    // FIXME
    auto isValidDerivationInStore = [&]() -> std::optional<StorePath> {
        if (!state.store->isStorePath(path))
            return std::nullopt;
        auto storePath = state.store->parseStorePath(path);
        if (!(state.store->isValidPath(storePath) && isDerivation(path)))
            return std::nullopt;
        return storePath;
    };

    if (auto optStorePath = isValidDerivationInStore()) {
        auto storePath = *optStorePath;
        Derivation drv = state.store->readDerivation(storePath);
        auto attrs = state.buildBindings(3 + drv.outputs.size());
        attrs.alloc(state.sDrvPath).mkString(path, {"=" + path});
        attrs.alloc(state.sName).mkString(drv.env["name"]);
        auto & outputsVal = attrs.alloc(state.sOutputs);
        state.mkList(outputsVal, drv.outputs.size());

        for (const auto & [i, o] : enumerate(drv.outputs)) {
            mkOutputString(state, attrs, storePath, drv, o);
            (outputsVal.listElems()[i] = state.allocValue())->mkString(o.first);
        }

        auto w = state.allocValue();
        w->mkAttrs(attrs);

        if (!state.vImportedDrvToDerivation) {
            state.vImportedDrvToDerivation = allocRootValue(state.allocValue());
            state.eval(state.parseExprFromString(
                #include "imported-drv-to-derivation.nix.gen.hh"
                , "/"), **state.vImportedDrvToDerivation);
        }

        state.forceFunction(**state.vImportedDrvToDerivation, pos);
        v.mkApp(*state.vImportedDrvToDerivation, w);
        state.forceAttrs(v, pos);
    }

    else if (path == corepkgsPrefix + "fetchurl.nix") {
        state.eval(state.parseExprFromString(
            #include "fetchurl.nix.gen.hh"
            , "/"), v);
    }

    else {
        if (!vScope)
            state.evalFile(path, v);
        else {
            state.forceAttrs(*vScope, pos);

            Env * env = &state.allocEnv(vScope->attrs->size());
            env->up = &state.baseEnv;

            StaticEnv staticEnv(false, &state.staticBaseEnv, vScope->attrs->size());

            unsigned int displ = 0;
            for (auto & attr : *vScope->attrs) {
                staticEnv.vars.emplace_back(attr.name, displ);
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

static RegisterPrimOp primop_scopedImport(RegisterPrimOp::Info {
    .name = "scopedImport",
    .arity = 2,
    .fun = [](EvalState & state, const Pos & pos, Value * * args, Value & v)
    {
        import(state, pos, *args[1], args[0], v);
    }
});

static RegisterPrimOp primop_import({
    .name = "import",
    .args = {"path"},
    .doc = R"(
      Load, parse and return the Nix expression in the file *path*. If
      *path* is a directory, the file ` default.nix ` in that directory
      is loaded. Evaluation aborts if the file doesn’t exist or contains
      an incorrect Nix expression. `import` implements Nix’s module
      system: you can put any Nix expression (such as a set or a
      function) in a separate file, and use it from Nix expressions in
      other files.

      > **Note**
      >
      > Unlike some languages, `import` is a regular function in Nix.
      > Paths using the angle bracket syntax (e.g., `import` *\<foo\>*)
      > are [normal path values](language-values.md).

      A Nix expression loaded by `import` must not contain any *free
      variables* (identifiers that are not defined in the Nix expression
      itself and are not built-in). Therefore, it cannot refer to
      variables that are in scope at the call site. For instance, if you
      have a calling expression

      ```nix
      rec {
        x = 123;
        y = import ./foo.nix;
      }
      ```

      then the following `foo.nix` will give an error:

      ```nix
      x + 456
      ```

      since `x` is not in scope in `foo.nix`. If you want `x` to be
      available in `foo.nix`, you should pass it as a function argument:

      ```nix
      rec {
        x = 123;
        y = import ./foo.nix x;
      }
      ```

      and

      ```nix
      x: x + 456
      ```

      (The function argument doesn’t have to be called `x` in `foo.nix`;
      any name would work.)
    )",
    .fun = [](EvalState & state, const Pos & pos, Value * * args, Value & v)
    {
        import(state, pos, *args[0], nullptr, v);
    }
});

/* Want reasonable symbol names, so extern C */
/* !!! Should we pass the Pos or the file name too? */
extern "C" typedef void (*ValueInitializer)(EvalState & state, Value & v);

/* Load a ValueInitializer from a DSO and return whatever it initializes */
void prim_importNative(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    auto path = realisePath(state, pos, *args[0]);

    std::string sym(state.forceStringNoCtx(*args[1], pos));

    void *handle = dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL);
    if (!handle)
        throw EvalError("could not open '%1%': %2%", path, dlerror());

    dlerror();
    ValueInitializer func = (ValueInitializer) dlsym(handle, sym.c_str());
    if(!func) {
        char *message = dlerror();
        if (message)
            throw EvalError("could not load symbol '%1%' from '%2%': %3%", sym, path, message);
        else
            throw EvalError("symbol '%1%' from '%2%' resolved to NULL when a function pointer was expected",
                sym, path);
    }

    (func)(state, v);

    /* We don't dlclose because v may be a primop referencing a function in the shared object file */
}


/* Execute a program and parse its output */
void prim_exec(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceList(*args[0], pos);
    auto elems = args[0]->listElems();
    auto count = args[0]->listSize();
    if (count == 0) {
        throw EvalError({
            .msg = hintfmt("at least one argument to 'exec' required"),
            .errPos = pos
        });
    }
    PathSet context;
    auto program = state.coerceToString(pos, *elems[0], context, false, false).toOwned();
    Strings commandArgs;
    for (unsigned int i = 1; i < args[0]->listSize(); ++i) {
        commandArgs.push_back(state.coerceToString(pos, *elems[i], context, false, false).toOwned());
    }
    try {
        auto _ = state.realiseContext(context); // FIXME: Handle CA derivations
    } catch (InvalidPathError & e) {
        throw EvalError({
            .msg = hintfmt("cannot execute '%1%', since path '%2%' is not valid",
                program, e.path),
            .errPos = pos
        });
    }

    auto output = runProgram(program, true, commandArgs);
    Expr * parsed;
    try {
        parsed = state.parseExprFromString(std::move(output), pos.file);
    } catch (Error & e) {
        e.addTrace(pos, "While parsing the output from '%1%'", program);
        throw;
    }
    try {
        state.eval(parsed, v);
    } catch (Error & e) {
        e.addTrace(pos, "While evaluating the output from '%1%'", program);
        throw;
    }
}


/* Return a string representing the type of the expression. */
static void prim_typeOf(EvalState & state, const Pos & pos, Value * * args, Value & v)
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
    v.mkString(state.symbols.create(t));
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
static void prim_isNull(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceValue(*args[0], pos);
    v.mkBool(args[0]->type() == nNull);
}

static RegisterPrimOp primop_isNull({
    .name = "isNull",
    .args = {"e"},
    .doc = R"(
      Return `true` if *e* evaluates to `null`, and `false` otherwise.

      > **Warning**
      >
      > This function is *deprecated*; just write `e == null` instead.
    )",
    .fun = prim_isNull,
});

/* Determine whether the argument is a function. */
static void prim_isFunction(EvalState & state, const Pos & pos, Value * * args, Value & v)
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
static void prim_isInt(EvalState & state, const Pos & pos, Value * * args, Value & v)
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
static void prim_isFloat(EvalState & state, const Pos & pos, Value * * args, Value & v)
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
static void prim_isString(EvalState & state, const Pos & pos, Value * * args, Value & v)
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
static void prim_isBool(EvalState & state, const Pos & pos, Value * * args, Value & v)
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
static void prim_isPath(EvalState & state, const Pos & pos, Value * * args, Value & v)
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

struct CompareValues
{
    EvalState & state;

    CompareValues(EvalState & state) : state(state) { };

    bool operator () (Value * v1, Value * v2) const
    {
        if (v1->type() == nFloat && v2->type() == nInt)
            return v1->fpoint < v2->integer;
        if (v1->type() == nInt && v2->type() == nFloat)
            return v1->integer < v2->fpoint;
        if (v1->type() != v2->type())
            throw EvalError("cannot compare %1% with %2%", showType(*v1), showType(*v2));
        switch (v1->type()) {
            case nInt:
                return v1->integer < v2->integer;
            case nFloat:
                return v1->fpoint < v2->fpoint;
            case nString:
                return strcmp(v1->string.s, v2->string.s) < 0;
            case nPath:
                return strcmp(v1->path, v2->path) < 0;
            case nList:
                // Lexicographic comparison
                for (size_t i = 0;; i++) {
                    if (i == v2->listSize()) {
                        return false;
                    } else if (i == v1->listSize()) {
                        return true;
                    } else if (!state.eqValues(*v1->listElems()[i], *v2->listElems()[i])) {
                        return (*this)(v1->listElems()[i], v2->listElems()[i]);
                    }
                }
            default:
                throw EvalError("cannot compare %1% with %2%", showType(*v1), showType(*v2));
        }
    }
};


#if HAVE_BOEHMGC
typedef std::list<Value *, gc_allocator<Value *> > ValueList;
#else
typedef std::list<Value *> ValueList;
#endif


static Bindings::iterator getAttr(
    EvalState & state,
    std::string_view funcName,
    Symbol attrSym,
    Bindings * attrSet,
    const Pos & pos)
{
    Bindings::iterator value = attrSet->find(attrSym);
    if (value == attrSet->end()) {
        hintformat errorMsg = hintfmt(
            "attribute '%s' missing for call to '%s'",
            attrSym,
            funcName
        );

        Pos aPos = *attrSet->pos;
        if (aPos == noPos) {
            throw TypeError({
                .msg = errorMsg,
                .errPos = pos,
            });
        } else {
            auto e = TypeError({
                .msg = errorMsg,
                .errPos = aPos,
            });

            // Adding another trace for the function name to make it clear
            // which call received wrong arguments.
            e.addTrace(pos, hintfmt("while invoking '%s'", funcName));
            throw e;
        }
    }

    return value;
}

static void prim_genericClosure(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceAttrs(*args[0], pos);

    /* Get the start set. */
    Bindings::iterator startSet = getAttr(
        state,
        "genericClosure",
        state.sStartSet,
        args[0]->attrs,
        pos
    );

    state.forceList(*startSet->value, pos);

    ValueList workSet;
    for (auto elem : startSet->value->listItems())
        workSet.push_back(elem);

    /* Get the operator. */
    Bindings::iterator op = getAttr(
        state,
        "genericClosure",
        state.sOperator,
        args[0]->attrs,
        pos
    );

    state.forceValue(*op->value, pos);

    /* Construct the closure by applying the operator to element of
       `workSet', adding the result to `workSet', continuing until
       no new elements are found. */
    ValueList res;
    // `doneKeys' doesn't need to be a GC root, because its values are
    // reachable from res.
    auto cmp = CompareValues(state);
    std::set<Value *, decltype(cmp)> doneKeys(cmp);
    while (!workSet.empty()) {
        Value * e = *(workSet.begin());
        workSet.pop_front();

        state.forceAttrs(*e, pos);

        Bindings::iterator key =
            e->attrs->find(state.sKey);
        if (key == e->attrs->end())
            throw EvalError({
                .msg = hintfmt("attribute 'key' required"),
                .errPos = pos
            });
        state.forceValue(*key->value, pos);

        if (!doneKeys.insert(key->value).second) continue;
        res.push_back(e);

        /* Call the `operator' function with `e' as argument. */
        Value call;
        call.mkApp(op->value, e);
        state.forceList(call, pos);

        /* Add the values returned by the operator to the work set. */
        for (auto elem : call.listItems()) {
            state.forceValue(*elem, pos);
            workSet.push_back(elem);
        }
    }

    /* Create the result list. */
    state.mkList(v, res.size());
    unsigned int n = 0;
    for (auto & i : res)
        v.listElems()[n++] = i;
}

static RegisterPrimOp primop_genericClosure(RegisterPrimOp::Info {
    .name = "__genericClosure",
    .args = {"attrset"},
    .arity = 1,
    .doc = R"(
      Take an *attrset* with values named `startSet` and `operator` in order to
      return a *list of attrsets* by starting with the `startSet`, recursively
      applying the `operator` function to each element. The *attrsets* in the
      `startSet` and produced by the `operator` must each contain value named
      `key` which are comparable to each other. The result is produced by
      repeatedly calling the operator for each element encountered with a
      unique key, terminating when no new elements are produced. For example,

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
      )",
    .fun = prim_genericClosure,
});

static RegisterPrimOp primop_abort({
    .name = "abort",
    .args = {"s"},
    .doc = R"(
      Abort Nix expression evaluation and print the error message *s*.
    )",
    .fun = [](EvalState & state, const Pos & pos, Value * * args, Value & v)
    {
        PathSet context;
        auto s = state.coerceToString(pos, *args[0], context).toOwned();
        throw Abort("evaluation aborted with the following error message: '%1%'", s);
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
    .fun = [](EvalState & state, const Pos & pos, Value * * args, Value & v)
    {
      PathSet context;
      auto s = state.coerceToString(pos, *args[0], context).toOwned();
      throw ThrownError(s);
    }
});

static void prim_addErrorContext(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    try {
        state.forceValue(*args[1], pos);
        v = *args[1];
    } catch (Error & e) {
        PathSet context;
        e.addTrace(std::nullopt, state.coerceToString(pos, *args[0], context).toOwned());
        throw;
    }
}

static RegisterPrimOp primop_addErrorContext(RegisterPrimOp::Info {
    .name = "__addErrorContext",
    .arity = 2,
    .fun = prim_addErrorContext,
});

static void prim_ceil(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    auto value = state.forceFloat(*args[0], args[0]->determinePos(pos));
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

static void prim_floor(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    auto value = state.forceFloat(*args[0], args[0]->determinePos(pos));
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
static void prim_tryEval(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    auto attrs = state.buildBindings(2);
    try {
        state.forceValue(*args[0], pos);
        attrs.insert(state.sValue, args[0]);
        attrs.alloc("success").mkBool(true);
    } catch (AssertionError & e) {
        attrs.alloc(state.sValue).mkBool(false);
        attrs.alloc("success").mkBool(false);
    }
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
static void prim_getEnv(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    std::string name(state.forceStringNoCtx(*args[0], pos));
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
static void prim_seq(EvalState & state, const Pos & pos, Value * * args, Value & v)
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
static void prim_deepSeq(EvalState & state, const Pos & pos, Value * * args, Value & v)
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
static void prim_trace(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceValue(*args[0], pos);
    if (args[0]->type() == nString)
        printError("trace: %1%", args[0]->string.s);
    else
        printError("trace: %1%", *args[0]);
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


/*************************************************************
 * Derivations
 *************************************************************/


/* Construct (as a unobservable side effect) a Nix derivation
   expression that performs the derivation described by the argument
   set.  Returns the original set extended with the following
   attributes: `outPath' containing the primary output path of the
   derivation; `drvPath' containing the path of the Nix expression;
   and `type' set to `derivation' to indicate that this is a
   derivation. */
static void prim_derivationStrict(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceAttrs(*args[0], pos);

    /* Figure out the name first (for stack backtraces). */
    Bindings::iterator attr = getAttr(
        state,
        "derivationStrict",
        state.sName,
        args[0]->attrs,
        pos
    );

    std::string drvName;
    Pos & posDrvName(*attr->pos);
    try {
        drvName = state.forceStringNoCtx(*attr->value, pos);
    } catch (Error & e) {
        e.addTrace(posDrvName, "while evaluating the derivation attribute 'name'");
        throw;
    }

    /* Check whether attributes should be passed as a JSON file. */
    std::ostringstream jsonBuf;
    std::unique_ptr<JSONObject> jsonObject;
    attr = args[0]->attrs->find(state.sStructuredAttrs);
    if (attr != args[0]->attrs->end() && state.forceBool(*attr->value, pos))
        jsonObject = std::make_unique<JSONObject>(jsonBuf);

    /* Check whether null attributes should be ignored. */
    bool ignoreNulls = false;
    attr = args[0]->attrs->find(state.sIgnoreNulls);
    if (attr != args[0]->attrs->end())
        ignoreNulls = state.forceBool(*attr->value, pos);

    /* Build the derivation expression by processing the attributes. */
    Derivation drv;
    drv.name = drvName;

    PathSet context;

    bool contentAddressed = false;
    bool isImpure = false;
    std::optional<std::string> outputHash;
    std::string outputHashAlgo;
    std::optional<FileIngestionMethod> ingestionMethod;

    StringSet outputs;
    outputs.insert("out");

    for (auto & i : args[0]->attrs->lexicographicOrder()) {
        if (i->name == state.sIgnoreNulls) continue;
        const std::string & key = i->name;
        vomit("processing attribute '%1%'", key);

        auto handleHashMode = [&](const std::string_view s) {
            if (s == "recursive") ingestionMethod = FileIngestionMethod::Recursive;
            else if (s == "flat") ingestionMethod = FileIngestionMethod::Flat;
            else
                throw EvalError({
                    .msg = hintfmt("invalid value '%s' for 'outputHashMode' attribute", s),
                    .errPos = posDrvName
                });
        };

        auto handleOutputs = [&](const Strings & ss) {
            outputs.clear();
            for (auto & j : ss) {
                if (outputs.find(j) != outputs.end())
                    throw EvalError({
                        .msg = hintfmt("duplicate derivation output '%1%'", j),
                        .errPos = posDrvName
                    });
                /* !!! Check whether j is a valid attribute
                   name. */
                /* Derivations cannot be named ‘drv’, because
                   then we'd have an attribute ‘drvPath’ in
                   the resulting set. */
                if (j == "drv")
                    throw EvalError({
                        .msg = hintfmt("invalid derivation output name 'drv'" ),
                        .errPos = posDrvName
                    });
                outputs.insert(j);
            }
            if (outputs.empty())
                throw EvalError({
                    .msg = hintfmt("derivation cannot have an empty set of outputs"),
                    .errPos = posDrvName
                });
        };

        try {

            if (ignoreNulls) {
                state.forceValue(*i->value, pos);
                if (i->value->type() == nNull) continue;
            }

            if (i->name == state.sContentAddressed) {
                contentAddressed = state.forceBool(*i->value, pos);
                if (contentAddressed)
                    settings.requireExperimentalFeature(Xp::CaDerivations);
            }

            else if (i->name == state.sImpure) {
                isImpure = state.forceBool(*i->value, pos);
                if (isImpure)
                    settings.requireExperimentalFeature(Xp::ImpureDerivations);
            }

            /* The `args' attribute is special: it supplies the
               command-line arguments to the builder. */
            else if (i->name == state.sArgs) {
                state.forceList(*i->value, pos);
                for (auto elem : i->value->listItems()) {
                    auto s = state.coerceToString(posDrvName, *elem, context, true).toOwned();
                    drv.args.push_back(s);
                }
            }

            /* All other attributes are passed to the builder through
               the environment. */
            else {

                if (jsonObject) {

                    if (i->name == state.sStructuredAttrs) continue;

                    auto placeholder(jsonObject->placeholder(key));
                    printValueAsJSON(state, true, *i->value, pos, placeholder, context);

                    if (i->name == state.sBuilder)
                        drv.builder = state.forceString(*i->value, context, posDrvName);
                    else if (i->name == state.sSystem)
                        drv.platform = state.forceStringNoCtx(*i->value, posDrvName);
                    else if (i->name == state.sOutputHash)
                        outputHash = state.forceStringNoCtx(*i->value, posDrvName);
                    else if (i->name == state.sOutputHashAlgo)
                        outputHashAlgo = state.forceStringNoCtx(*i->value, posDrvName);
                    else if (i->name == state.sOutputHashMode)
                        handleHashMode(state.forceStringNoCtx(*i->value, posDrvName));
                    else if (i->name == state.sOutputs) {
                        /* Require ‘outputs’ to be a list of strings. */
                        state.forceList(*i->value, posDrvName);
                        Strings ss;
                        for (auto elem : i->value->listItems())
                            ss.emplace_back(state.forceStringNoCtx(*elem, posDrvName));
                        handleOutputs(ss);
                    }

                } else {
                    auto s = state.coerceToString(*i->pos, *i->value, context, true).toOwned();
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
            e.addTrace(posDrvName,
                "while evaluating the attribute '%1%' of the derivation '%2%'",
                key, drvName);
            throw;
        }
    }

    if (jsonObject) {
        jsonObject.reset();
        drv.env.emplace("__json", jsonBuf.str());
    }

    /* Everything in the context of the strings in the derivation
       attributes should be added as dependencies of the resulting
       derivation. */
    for (auto & path : context) {

        /* Paths marked with `=' denote that the path of a derivation
           is explicitly passed to the builder.  Since that allows the
           builder to gain access to every path in the dependency
           graph of the derivation (including all outputs), all paths
           in the graph must be added to this derivation's list of
           inputs to ensure that they are available when the builder
           runs. */
        if (path.at(0) == '=') {
            /* !!! This doesn't work if readOnlyMode is set. */
            StorePathSet refs;
            state.store->computeFSClosure(state.store->parseStorePath(std::string_view(path).substr(1)), refs);
            for (auto & j : refs) {
                drv.inputSrcs.insert(j);
                if (j.isDerivation())
                    drv.inputDrvs[j] = state.store->readDerivation(j).outputNames();
            }
        }

        /* Handle derivation outputs of the form ‘!<name>!<path>’. */
        else if (path.at(0) == '!') {
            auto ctx = decodeContext(*state.store, path);
            drv.inputDrvs[ctx.first].insert(ctx.second);
        }

        /* Otherwise it's a source file. */
        else
            drv.inputSrcs.insert(state.store->parseStorePath(path));
    }

    /* Do we have all required attributes? */
    if (drv.builder == "")
        throw EvalError({
            .msg = hintfmt("required attribute 'builder' missing"),
            .errPos = posDrvName
        });

    if (drv.platform == "")
        throw EvalError({
            .msg = hintfmt("required attribute 'system' missing"),
            .errPos = posDrvName
        });

    /* Check whether the derivation name is valid. */
    if (isDerivation(drvName))
        throw EvalError({
            .msg = hintfmt("derivation names are not allowed to end in '%s'", drvExtension),
            .errPos = posDrvName
        });

    if (outputHash) {
        /* Handle fixed-output derivations.

           Ignore `__contentAddressed` because fixed output derivations are
           already content addressed. */
        if (outputs.size() != 1 || *(outputs.begin()) != "out")
            throw Error({
                .msg = hintfmt("multiple outputs are not supported in fixed-output derivations"),
                .errPos = posDrvName
            });

        auto h = newHashAllowEmpty(*outputHash, parseHashTypeOpt(outputHashAlgo));

        auto method = ingestionMethod.value_or(FileIngestionMethod::Flat);
        auto outPath = state.store->makeFixedOutputPath(method, h, drvName);
        drv.env["out"] = state.store->printStorePath(outPath);
        drv.outputs.insert_or_assign("out",
            DerivationOutput::CAFixed {
                .hash = FixedOutputHash {
                    .method = method,
                    .hash = std::move(h),
                },
            });
    }

    else if (contentAddressed || isImpure) {
        if (contentAddressed && isImpure)
            throw EvalError({
                .msg = hintfmt("derivation cannot be both content-addressed and impure"),
                .errPos = posDrvName
            });

        auto ht = parseHashTypeOpt(outputHashAlgo).value_or(htSHA256);
        auto method = ingestionMethod.value_or(FileIngestionMethod::Recursive);

        for (auto & i : outputs) {
            drv.env[i] = hashPlaceholder(i);
            if (isImpure)
                drv.outputs.insert_or_assign(i,
                    DerivationOutput::Impure {
                        .method = method,
                        .hashType = ht,
                    });
            else
                drv.outputs.insert_or_assign(i,
                    DerivationOutput::CAFloating {
                        .method = method,
                        .hashType = ht,
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
                auto h = hashModulo.hashes.at(i);
                auto outPath = state.store->makeOutputPath(i, h, drvName);
                drv.env[i] = state.store->printStorePath(outPath);
                drv.outputs.insert_or_assign(
                    i,
                    DerivationOutputInputAddressed {
                        .path = std::move(outPath),
                    });
            }
            break;
            ;
        case DrvHash::Kind::Deferred:
            for (auto & i : outputs) {
                drv.outputs.insert_or_assign(i, DerivationOutputDeferred {});
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

    auto attrs = state.buildBindings(1 + drv.outputs.size());
    attrs.alloc(state.sDrvPath).mkString(drvPathS, {"=" + drvPathS});
    for (auto & i : drv.outputs)
        mkOutputString(state, attrs, drvPath, drv, i);
    v.mkAttrs(attrs);
}

static RegisterPrimOp primop_derivationStrict(RegisterPrimOp::Info {
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
static void prim_placeholder(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    v.mkString(hashPlaceholder(state.forceStringNoCtx(*args[0], pos)));
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


/* Convert the argument to a path.  !!! obsolete? */
static void prim_toPath(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    PathSet context;
    Path path = state.coerceToPath(pos, *args[0], context);
    v.mkString(canonPath(path), context);
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
static void prim_storePath(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    if (evalSettings.pureEval)
        throw EvalError({
            .msg = hintfmt("'%s' is not allowed in pure evaluation mode", "builtins.storePath"),
            .errPos = pos
        });

    PathSet context;
    Path path = state.checkSourcePath(state.coerceToPath(pos, *args[0], context));
    /* Resolve symlinks in ‘path’, unless ‘path’ itself is a symlink
       directly in the store.  The latter condition is necessary so
       e.g. nix-push does the right thing. */
    if (!state.store->isStorePath(path)) path = canonPath(path, true);
    if (!state.store->isInStore(path))
        throw EvalError({
            .msg = hintfmt("path '%1%' is not in the Nix store", path),
            .errPos = pos
        });
    auto path2 = state.store->toStorePath(path).first;
    if (!settings.readOnlyMode)
        state.store->ensurePath(path2);
    context.insert(state.store->printStorePath(path2));
    v.mkString(path, context);
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

      This function is not available in pure evaluation mode.
    )",
    .fun = prim_storePath,
});

static void prim_pathExists(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    /* We don’t check the path right now, because we don’t want to
      throw if the path isn’t allowed, but just return false (and we
      can’t just catch the exception here because we still want to
      throw if something in the evaluation of `*args[0]` tries to
      access an unauthorized path). */
    auto path = realisePath(state, pos, *args[0], { .checkForPureEval = false });

    try {
        v.mkBool(pathExists(state.checkSourcePath(path)));
    } catch (SysError & e) {
        /* Don't give away info from errors while canonicalising
           ‘path’ in restricted mode. */
        v.mkBool(false);
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
static void prim_baseNameOf(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    PathSet context;
    v.mkString(baseNameOf(*state.coerceToString(pos, *args[0], context, false, false)), context);
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
static void prim_dirOf(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    PathSet context;
    auto path = state.coerceToString(pos, *args[0], context, false, false);
    auto dir = dirOf(*path);
    if (args[0]->type() == nPath) v.mkPath(dir); else v.mkString(dir, context);
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
static void prim_readFile(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    auto path = realisePath(state, pos, *args[0]);
    auto s = readFile(path);
    if (s.find((char) 0) != std::string::npos)
        throw Error("the contents of the file '%1%' cannot be represented as a Nix string", path);
    StorePathSet refs;
    if (state.store->isInStore(path)) {
        try {
            refs = state.store->queryPathInfo(state.store->toStorePath(path).first)->references;
        } catch (Error &) { // FIXME: should be InvalidPathError
        }
    }
    auto context = state.store->printStorePathSet(refs);
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
static void prim_findFile(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceList(*args[0], pos);

    SearchPath searchPath;

    for (auto v2 : args[0]->listItems()) {
        state.forceAttrs(*v2, pos);

        std::string prefix;
        Bindings::iterator i = v2->attrs->find(state.sPrefix);
        if (i != v2->attrs->end())
            prefix = state.forceStringNoCtx(*i->value, pos);

        i = getAttr(
            state,
            "findFile",
            state.sPath,
            v2->attrs,
            pos
        );

        PathSet context;
        auto path = state.coerceToString(pos, *i->value, context, false, false).toOwned();

        try {
            auto rewrites = state.realiseContext(context);
            path = rewriteStrings(path, rewrites);
        } catch (InvalidPathError & e) {
            throw EvalError({
                .msg = hintfmt("cannot find '%1%', since path '%2%' is not valid", path, e.path),
                .errPos = pos
            });
        }


        searchPath.emplace_back(prefix, path);
    }

    auto path = state.forceStringNoCtx(*args[1], pos);

    v.mkPath(state.checkSourcePath(state.findFile(searchPath, path, pos)));
}

static RegisterPrimOp primop_findFile(RegisterPrimOp::Info {
    .name = "__findFile",
    .arity = 2,
    .fun = prim_findFile,
});

/* Return the cryptographic hash of a file in base-16. */
static void prim_hashFile(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    auto type = state.forceStringNoCtx(*args[0], pos);
    std::optional<HashType> ht = parseHashType(type);
    if (!ht)
        throw Error({
            .msg = hintfmt("unknown hash type '%1%'", type),
            .errPos = pos
        });

    auto path = realisePath(state, pos, *args[1]);

    v.mkString(hashFile(*ht, path).to_string(Base16, false));
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

/* Read a directory (without . or ..) */
static void prim_readDir(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    auto path = realisePath(state, pos, *args[0]);

    DirEntries entries = readDirectory(path);

    auto attrs = state.buildBindings(entries.size());

    for (auto & ent : entries) {
        if (ent.type == DT_UNKNOWN)
            ent.type = getFileType(path + "/" + ent.name);
        attrs.alloc(ent.name).mkString(
            ent.type == DT_REG ? "regular" :
            ent.type == DT_DIR ? "directory" :
            ent.type == DT_LNK ? "symlink" :
            "unknown");
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


/*************************************************************
 * Creating files
 *************************************************************/


/* Convert the argument (which can be any Nix expression) to an XML
   representation returned in a string.  Not all Nix expressions can
   be sensibly or completely represented (e.g., functions). */
static void prim_toXML(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    std::ostringstream out;
    PathSet context;
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
static void prim_toJSON(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    std::ostringstream out;
    PathSet context;
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
static void prim_fromJSON(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    auto s = state.forceStringNoCtx(*args[0], pos);
    try {
        parseJSON(state, s, v);
    } catch (JSONParseError &e) {
        e.addTrace(pos, "while decoding a JSON string");
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
static void prim_toFile(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    PathSet context;
    std::string name(state.forceStringNoCtx(*args[0], pos));
    std::string contents(state.forceString(*args[1], context, pos));

    StorePathSet refs;

    for (auto path : context) {
        if (path.at(0) != '/')
            throw EvalError( {
                .msg = hintfmt(
                    "in 'toFile': the file named '%1%' must not contain a reference "
                    "to a derivation but contains (%2%)",
                    name, path),
                .errPos = pos
            });
        refs.insert(state.store->parseStorePath(path));
    }

    auto storePath = state.store->printStorePath(settings.readOnlyMode
        ? state.store->computeStorePathForText(name, contents, refs)
        : state.store->addTextToStore(name, contents, refs, state.repair));

    /* Note: we don't need to add `context' to the context of the
       result, since `storePath' itself has references to the paths
       used in args[1]. */

    v.mkString(storePath, {storePath});
}

static RegisterPrimOp primop_toFile({
    .name = "__toFile",
    .args = {"name", "s"},
    .doc = R"(
      Store the string *s* in a file in the Nix store and return its
      path.  The file has suffix *name*. This file can be used as an
      input to derivations. One application is to write builders
      “inline”. For instance, the following Nix expression combines the
      [Nix expression for GNU Hello](expression-syntax.md) and its
      [build script](build-script.md) into one file:

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

      Note that `${configFile}` is an
      [antiquotation](language-values.md), so the result of the
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

static void addPath(
    EvalState & state,
    const Pos & pos,
    const std::string & name,
    Path path,
    Value * filterFun,
    FileIngestionMethod method,
    const std::optional<Hash> expectedHash,
    Value & v,
    const PathSet & context)
{
    try {
        // FIXME: handle CA derivation outputs (where path needs to
        // be rewritten to the actual output).
        auto rewrites = state.realiseContext(context);
        path = state.toRealPath(rewriteStrings(path, rewrites), context);

        StorePathSet refs;

        if (state.store->isInStore(path)) {
            try {
                auto [storePath, subPath] = state.store->toStorePath(path);
                // FIXME: we should scanForReferences on the path before adding it
                refs = state.store->queryPathInfo(storePath)->references;
                path = state.store->toRealPath(storePath) + subPath;
            } catch (Error &) { // FIXME: should be InvalidPathError
            }
        }

        path = evalSettings.pureEval && expectedHash
            ? path
            : state.checkSourcePath(path);

        PathFilter filter = filterFun ? ([&](const Path & path) {
            auto st = lstat(path);

            /* Call the filter function.  The first argument is the path,
               the second is a string indicating the type of the file. */
            Value arg1;
            arg1.mkString(path);

            Value arg2;
            arg2.mkString(
                S_ISREG(st.st_mode) ? "regular" :
                S_ISDIR(st.st_mode) ? "directory" :
                S_ISLNK(st.st_mode) ? "symlink" :
                "unknown" /* not supported, will fail! */);

            Value * args []{&arg1, &arg2};
            Value res;
            state.callFunction(*filterFun, 2, args, res, pos);

            return state.forceBool(res, pos);
        }) : defaultPathFilter;

        std::optional<StorePath> expectedStorePath;
        if (expectedHash)
            expectedStorePath = state.store->makeFixedOutputPath(method, *expectedHash, name);

        if (!expectedHash || !state.store->isValidPath(*expectedStorePath)) {
            StorePath dstPath = settings.readOnlyMode
                ? state.store->computeStorePathForPath(name, path, method, htSHA256, filter).first
                : state.store->addToStore(name, path, method, htSHA256, filter, state.repair, refs);
            if (expectedHash && expectedStorePath != dstPath)
                throw Error("store path mismatch in (possibly filtered) path added from '%s'", path);
            state.allowAndSetStorePathString(dstPath, v);
        } else
            state.allowAndSetStorePathString(*expectedStorePath, v);
    } catch (Error & e) {
        e.addTrace(pos, "while adding path '%s'", path);
        throw;
    }
}


static void prim_filterSource(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    PathSet context;
    Path path = state.coerceToPath(pos, *args[1], context);

    state.forceValue(*args[0], pos);
    if (args[0]->type() != nFunction)
        throw TypeError({
            .msg = hintfmt(
                "first argument in call to 'filterSource' is not a function but %1%",
                showType(*args[0])),
            .errPos = pos
        });

    addPath(state, pos, std::string(baseNameOf(path)), path, args[0], FileIngestionMethod::Recursive, std::nullopt, v, context);
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

static void prim_path(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceAttrs(*args[0], pos);
    Path path;
    std::string name;
    Value * filterFun = nullptr;
    auto method = FileIngestionMethod::Recursive;
    std::optional<Hash> expectedHash;
    PathSet context;

    for (auto & attr : *args[0]->attrs) {
        auto & n(attr.name);
        if (n == "path")
            path = state.coerceToPath(*attr.pos, *attr.value, context);
        else if (attr.name == state.sName)
            name = state.forceStringNoCtx(*attr.value, *attr.pos);
        else if (n == "filter") {
            state.forceValue(*attr.value, pos);
            filterFun = attr.value;
        } else if (n == "recursive")
            method = FileIngestionMethod { state.forceBool(*attr.value, *attr.pos) };
        else if (n == "sha256")
            expectedHash = newHashAllowEmpty(state.forceStringNoCtx(*attr.value, *attr.pos), htSHA256);
        else
            throw EvalError({
                .msg = hintfmt("unsupported argument '%1%' to 'addPath'", attr.name),
                .errPos = *attr.pos
            });
    }
    if (path.empty())
        throw EvalError({
            .msg = hintfmt("'path' required"),
            .errPos = pos
        });
    if (name.empty())
        name = baseNameOf(path);

    addPath(state, pos, name, path, filterFun, method, expectedHash, v, context);
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
          A function of the type expected by `builtins.filterSource`,
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
static void prim_attrNames(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceAttrs(*args[0], pos);

    state.mkList(v, args[0]->attrs->size());

    size_t n = 0;
    for (auto & i : *args[0]->attrs)
        (v.listElems()[n++] = state.allocValue())->mkString(i.name);

    std::sort(v.listElems(), v.listElems() + n,
              [](Value * v1, Value * v2) { return strcmp(v1->string.s, v2->string.s) < 0; });
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
static void prim_attrValues(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceAttrs(*args[0], pos);

    state.mkList(v, args[0]->attrs->size());

    unsigned int n = 0;
    for (auto & i : *args[0]->attrs)
        v.listElems()[n++] = (Value *) &i;

    std::sort(v.listElems(), v.listElems() + n,
        [](Value * v1, Value * v2) {
            std::string_view s1 = ((Attr *) v1)->name, s2 = ((Attr *) v2)->name;
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
void prim_getAttr(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    auto attr = state.forceStringNoCtx(*args[0], pos);
    state.forceAttrs(*args[1], pos);
    Bindings::iterator i = getAttr(
        state,
        "getAttr",
        state.symbols.create(attr),
        args[1]->attrs,
        pos
    );
    // !!! add to stack trace?
    if (state.countCalls && *i->pos != noPos) state.attrSelects[*i->pos]++;
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
static void prim_unsafeGetAttrPos(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    auto attr = state.forceStringNoCtx(*args[0], pos);
    state.forceAttrs(*args[1], pos);
    Bindings::iterator i = args[1]->attrs->find(state.symbols.create(attr));
    if (i == args[1]->attrs->end())
        v.mkNull();
    else
        state.mkPos(v, i->pos);
}

static RegisterPrimOp primop_unsafeGetAttrPos(RegisterPrimOp::Info {
    .name = "__unsafeGetAttrPos",
    .arity = 2,
    .fun = prim_unsafeGetAttrPos,
});

/* Dynamic version of the `?' operator. */
static void prim_hasAttr(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    auto attr = state.forceStringNoCtx(*args[0], pos);
    state.forceAttrs(*args[1], pos);
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
static void prim_isAttrs(EvalState & state, const Pos & pos, Value * * args, Value & v)
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

static void prim_removeAttrs(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceAttrs(*args[0], pos);
    state.forceList(*args[1], pos);

    /* Get the attribute names to be removed.
       We keep them as Attrs instead of Symbols so std::set_difference
       can be used to remove them from attrs[0]. */
    boost::container::small_vector<Attr, 64> names;
    names.reserve(args[1]->listSize());
    for (auto elem : args[1]->listItems()) {
        state.forceStringNoCtx(*elem, pos);
        names.emplace_back(state.symbols.create(elem->string.s), nullptr);
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
static void prim_listToAttrs(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceList(*args[0], pos);

    auto attrs = state.buildBindings(args[0]->listSize());

    std::set<Symbol> seen;

    for (auto v2 : args[0]->listItems()) {
        state.forceAttrs(*v2, pos);

        Bindings::iterator j = getAttr(
            state,
            "listToAttrs",
            state.sName,
            v2->attrs,
            pos
        );

        auto name = state.forceStringNoCtx(*j->value, *j->pos);

        Symbol sym = state.symbols.create(name);
        if (seen.insert(sym).second) {
            Bindings::iterator j2 = getAttr(
                state,
                "listToAttrs",
                state.sValue,
                v2->attrs,
                pos
            );
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
      and an attribute `value` specifying its value. Example:

      ```nix
      builtins.listToAttrs
        [ { name = "foo"; value = 123; }
          { name = "bar"; value = 456; }
        ]
      ```

      evaluates to

      ```nix
      { foo = 123; bar = 456; }
      ```
    )",
    .fun = prim_listToAttrs,
});

static void prim_intersectAttrs(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceAttrs(*args[0], pos);
    state.forceAttrs(*args[1], pos);

    auto attrs = state.buildBindings(std::min(args[0]->attrs->size(), args[1]->attrs->size()));

    for (auto & i : *args[0]->attrs) {
        Bindings::iterator j = args[1]->attrs->find(i.name);
        if (j != args[1]->attrs->end())
            attrs.insert(*j);
    }

    v.mkAttrs(attrs.alreadySorted());
}

static RegisterPrimOp primop_intersectAttrs({
    .name = "__intersectAttrs",
    .args = {"e1", "e2"},
    .doc = R"(
      Return a set consisting of the attributes in the set *e2* that also
      exist in the set *e1*.
    )",
    .fun = prim_intersectAttrs,
});

static void prim_catAttrs(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    Symbol attrName = state.symbols.create(state.forceStringNoCtx(*args[0], pos));
    state.forceList(*args[1], pos);

    Value * res[args[1]->listSize()];
    unsigned int found = 0;

    for (auto v2 : args[1]->listItems()) {
        state.forceAttrs(*v2, pos);
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

static void prim_functionArgs(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceValue(*args[0], pos);
    if (args[0]->isPrimOpApp() || args[0]->isPrimOp()) {
        v.mkAttrs(&state.emptyBindings);
        return;
    }
    if (!args[0]->isLambda())
        throw TypeError({
            .msg = hintfmt("'functionArgs' requires a function"),
            .errPos = pos
        });

    if (!args[0]->lambda.fun->hasFormals()) {
        v.mkAttrs(&state.emptyBindings);
        return;
    }

    auto attrs = state.buildBindings(args[0]->lambda.fun->formals->formals.size());
    for (auto & i : args[0]->lambda.fun->formals->formals)
        // !!! should optimise booleans (allocate only once)
        attrs.alloc(i.name, ptr(&i.pos)).mkBool(i.def);
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
static void prim_mapAttrs(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceAttrs(*args[1], pos);

    auto attrs = state.buildBindings(args[1]->attrs->size());

    for (auto & i : *args[1]->attrs) {
        Value * vName = state.allocValue();
        Value * vFun2 = state.allocValue();
        vName->mkString(i.name);
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

static void prim_zipAttrsWith(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    // we will first count how many values are present for each given key.
    // we then allocate a single attrset and pre-populate it with lists of
    // appropriate sizes, stash the pointers to the list elements of each,
    // and populate the lists. after that we replace the list in the every
    // attribute with the merge function application. this way we need not
    // use (slightly slower) temporary storage the GC does not know about.

    std::map<Symbol, std::pair<size_t, Value * *>> attrsSeen;

    state.forceFunction(*args[0], pos);
    state.forceList(*args[1], pos);
    const auto listSize = args[1]->listSize();
    const auto listElems = args[1]->listElems();

    for (unsigned int n = 0; n < listSize; ++n) {
        Value * vElem = listElems[n];
        try {
            state.forceAttrs(*vElem, noPos);
            for (auto & attr : *vElem->attrs)
                attrsSeen[attr.name].first++;
        } catch (TypeError & e) {
            e.addTrace(pos, hintfmt("while invoking '%s'", "zipAttrsWith"));
            throw;
        }
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
        name->mkString(attr.name);
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
static void prim_isList(EvalState & state, const Pos & pos, Value * * args, Value & v)
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

static void elemAt(EvalState & state, const Pos & pos, Value & list, int n, Value & v)
{
    state.forceList(list, pos);
    if (n < 0 || (unsigned int) n >= list.listSize())
        throw Error({
            .msg = hintfmt("list index %1% is out of bounds", n),
            .errPos = pos
        });
    state.forceValue(*list.listElems()[n], pos);
    v = *list.listElems()[n];
}

/* Return the n-1'th element of a list. */
static void prim_elemAt(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    elemAt(state, pos, *args[0], state.forceInt(*args[1], pos), v);
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
static void prim_head(EvalState & state, const Pos & pos, Value * * args, Value & v)
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
static void prim_tail(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceList(*args[0], pos);
    if (args[0]->listSize() == 0)
        throw Error({
            .msg = hintfmt("'tail' called on an empty list"),
            .errPos = pos
        });

    state.mkList(v, args[0]->listSize() - 1);
    for (unsigned int n = 0; n < v.listSize(); ++n)
        v.listElems()[n] = args[0]->listElems()[n + 1];
}

static RegisterPrimOp primop_tail({
    .name = "__tail",
    .args = {"list"},
    .doc = R"(
      Return the second to last elements of a list; abort evaluation if
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
static void prim_map(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceList(*args[1], pos);

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
static void prim_filter(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceFunction(*args[0], pos);
    state.forceList(*args[1], pos);

    // FIXME: putting this on the stack is risky.
    Value * vs[args[1]->listSize()];
    unsigned int k = 0;

    bool same = true;
    for (unsigned int n = 0; n < args[1]->listSize(); ++n) {
        Value res;
        state.callFunction(*args[0], *args[1]->listElems()[n], res, noPos);
        if (state.forceBool(res, pos))
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
static void prim_elem(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    bool res = false;
    state.forceList(*args[1], pos);
    for (auto elem : args[1]->listItems())
        if (state.eqValues(*args[0], *elem)) {
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
static void prim_concatLists(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceList(*args[0], pos);
    state.concatLists(v, args[0]->listSize(), args[0]->listElems(), pos);
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
static void prim_length(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceList(*args[0], pos);
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
static void prim_foldlStrict(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceFunction(*args[0], pos);
    state.forceList(*args[2], pos);

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
      ...`. The operator is applied strictly, i.e., its arguments are
      evaluated first. For example, `foldl' (x: y: x + y) 0 [1 2 3]`
      evaluates to 6.
    )",
    .fun = prim_foldlStrict,
});

static void anyOrAll(bool any, EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceFunction(*args[0], pos);
    state.forceList(*args[1], pos);

    Value vTmp;
    for (auto elem : args[1]->listItems()) {
        state.callFunction(*args[0], *elem, vTmp, pos);
        bool res = state.forceBool(vTmp, pos);
        if (res == any) {
            v.mkBool(any);
            return;
        }
    }

    v.mkBool(!any);
}


static void prim_any(EvalState & state, const Pos & pos, Value * * args, Value & v)
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

static void prim_all(EvalState & state, const Pos & pos, Value * * args, Value & v)
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

static void prim_genList(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    auto len = state.forceInt(*args[1], pos);

    if (len < 0)
        throw EvalError({
            .msg = hintfmt("cannot create list of size %1%", len),
            .errPos = pos
        });

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

static void prim_lessThan(EvalState & state, const Pos & pos, Value * * args, Value & v);


static void prim_sort(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceFunction(*args[0], pos);
    state.forceList(*args[1], pos);

    auto len = args[1]->listSize();
    state.mkList(v, len);
    for (unsigned int n = 0; n < len; ++n) {
        state.forceValue(*args[1]->listElems()[n], pos);
        v.listElems()[n] = args[1]->listElems()[n];
    }

    auto comparator = [&](Value * a, Value * b) {
        /* Optimization: if the comparator is lessThan, bypass
           callFunction. */
        if (args[0]->isPrimOp() && args[0]->primOp->fun == prim_lessThan)
            return CompareValues(state)(a, b);

        Value * vs[] = {a, b};
        Value vBool;
        state.callFunction(*args[0], 2, vs, vBool, pos);
        return state.forceBool(vBool, pos);
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

static void prim_partition(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceFunction(*args[0], pos);
    state.forceList(*args[1], pos);

    auto len = args[1]->listSize();

    ValueVector right, wrong;

    for (unsigned int n = 0; n < len; ++n) {
        auto vElem = args[1]->listElems()[n];
        state.forceValue(*vElem, pos);
        Value res;
        state.callFunction(*args[0], *vElem, res, pos);
        if (state.forceBool(res, pos))
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

static void prim_groupBy(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceFunction(*args[0], pos);
    state.forceList(*args[1], pos);

    ValueVectorMap attrs;

    for (auto vElem : args[1]->listItems()) {
        Value res;
        state.callFunction(*args[0], *vElem, res, pos);
        auto name = state.forceStringNoCtx(res, pos);
        Symbol sym = state.symbols.create(name);
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

static void prim_concatMap(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceFunction(*args[0], pos);
    state.forceList(*args[1], pos);
    auto nrLists = args[1]->listSize();

    Value lists[nrLists];
    size_t len = 0;

    for (unsigned int n = 0; n < nrLists; ++n) {
        Value * vElem = args[1]->listElems()[n];
        state.callFunction(*args[0], *vElem, lists[n], pos);
        try {
            state.forceList(lists[n], lists[n].determinePos(args[0]->determinePos(pos)));
        } catch (TypeError &e) {
            e.addTrace(pos, hintfmt("while invoking '%s'", "concatMap"));
            throw;
        }
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


static void prim_add(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceValue(*args[0], pos);
    state.forceValue(*args[1], pos);
    if (args[0]->type() == nFloat || args[1]->type() == nFloat)
        v.mkFloat(state.forceFloat(*args[0], pos) + state.forceFloat(*args[1], pos));
    else
        v.mkInt(state.forceInt(*args[0], pos) + state.forceInt(*args[1], pos));
}

static RegisterPrimOp primop_add({
    .name = "__add",
    .args = {"e1", "e2"},
    .doc = R"(
      Return the sum of the numbers *e1* and *e2*.
    )",
    .fun = prim_add,
});

static void prim_sub(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceValue(*args[0], pos);
    state.forceValue(*args[1], pos);
    if (args[0]->type() == nFloat || args[1]->type() == nFloat)
        v.mkFloat(state.forceFloat(*args[0], pos) - state.forceFloat(*args[1], pos));
    else
        v.mkInt(state.forceInt(*args[0], pos) - state.forceInt(*args[1], pos));
}

static RegisterPrimOp primop_sub({
    .name = "__sub",
    .args = {"e1", "e2"},
    .doc = R"(
      Return the difference between the numbers *e1* and *e2*.
    )",
    .fun = prim_sub,
});

static void prim_mul(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceValue(*args[0], pos);
    state.forceValue(*args[1], pos);
    if (args[0]->type() == nFloat || args[1]->type() == nFloat)
        v.mkFloat(state.forceFloat(*args[0], pos) * state.forceFloat(*args[1], pos));
    else
        v.mkInt(state.forceInt(*args[0], pos) * state.forceInt(*args[1], pos));
}

static RegisterPrimOp primop_mul({
    .name = "__mul",
    .args = {"e1", "e2"},
    .doc = R"(
      Return the product of the numbers *e1* and *e2*.
    )",
    .fun = prim_mul,
});

static void prim_div(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceValue(*args[0], pos);
    state.forceValue(*args[1], pos);

    NixFloat f2 = state.forceFloat(*args[1], pos);
    if (f2 == 0)
        throw EvalError({
            .msg = hintfmt("division by zero"),
            .errPos = pos
        });

    if (args[0]->type() == nFloat || args[1]->type() == nFloat) {
        v.mkFloat(state.forceFloat(*args[0], pos) / state.forceFloat(*args[1], pos));
    } else {
        NixInt i1 = state.forceInt(*args[0], pos);
        NixInt i2 = state.forceInt(*args[1], pos);
        /* Avoid division overflow as it might raise SIGFPE. */
        if (i1 == std::numeric_limits<NixInt>::min() && i2 == -1)
            throw EvalError({
                .msg = hintfmt("overflow in integer division"),
                .errPos = pos
            });

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

static void prim_bitAnd(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    v.mkInt(state.forceInt(*args[0], pos) & state.forceInt(*args[1], pos));
}

static RegisterPrimOp primop_bitAnd({
    .name = "__bitAnd",
    .args = {"e1", "e2"},
    .doc = R"(
      Return the bitwise AND of the integers *e1* and *e2*.
    )",
    .fun = prim_bitAnd,
});

static void prim_bitOr(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    v.mkInt(state.forceInt(*args[0], pos) | state.forceInt(*args[1], pos));
}

static RegisterPrimOp primop_bitOr({
    .name = "__bitOr",
    .args = {"e1", "e2"},
    .doc = R"(
      Return the bitwise OR of the integers *e1* and *e2*.
    )",
    .fun = prim_bitOr,
});

static void prim_bitXor(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    v.mkInt(state.forceInt(*args[0], pos) ^ state.forceInt(*args[1], pos));
}

static RegisterPrimOp primop_bitXor({
    .name = "__bitXor",
    .args = {"e1", "e2"},
    .doc = R"(
      Return the bitwise XOR of the integers *e1* and *e2*.
    )",
    .fun = prim_bitXor,
});

static void prim_lessThan(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceValue(*args[0], pos);
    state.forceValue(*args[1], pos);
    CompareValues comp{state};
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
static void prim_toString(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    PathSet context;
    auto s = state.coerceToString(pos, *args[0], context, true, false);
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
static void prim_substring(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    int start = state.forceInt(*args[0], pos);
    int len = state.forceInt(*args[1], pos);
    PathSet context;
    auto s = state.coerceToString(pos, *args[2], context);

    if (start < 0)
        throw EvalError({
            .msg = hintfmt("negative start position in 'substring'"),
            .errPos = pos
        });

    v.mkString((unsigned int) start >= s->size() ? "" : s->substr(start, len), context);
}

static RegisterPrimOp primop_substring({
    .name = "__substring",
    .args = {"start", "len", "s"},
    .doc = R"(
      Return the substring of *s* from character position *start*
      (zero-based) up to but not including *start + len*. If *start* is
      greater than the length of the string, an empty string is returned,
      and if *start + len* lies beyond the end of the string, only the
      substring up to the end of the string is returned. *start* must be
      non-negative. For example,

      ```nix
      builtins.substring 0 3 "nixos"
      ```

      evaluates to `"nix"`.
    )",
    .fun = prim_substring,
});

static void prim_stringLength(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    PathSet context;
    auto s = state.coerceToString(pos, *args[0], context);
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
static void prim_hashString(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    auto type = state.forceStringNoCtx(*args[0], pos);
    std::optional<HashType> ht = parseHashType(type);
    if (!ht)
        throw Error({
            .msg = hintfmt("unknown hash type '%1%'", type),
            .errPos = pos
        });

    PathSet context; // discarded
    auto s = state.forceString(*args[1], context, pos);

    v.mkString(hashString(*ht, s).to_string(Base16, false));
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

void prim_match(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    auto re = state.forceStringNoCtx(*args[0], pos);

    try {

        auto regex = state.regexCache->get(re);

        PathSet context;
        const auto str = state.forceString(*args[1], context, pos);

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

    } catch (std::regex_error &e) {
        if (e.code() == std::regex_constants::error_space) {
            // limit is _GLIBCXX_REGEX_STATE_LIMIT for libstdc++
            throw EvalError({
                .msg = hintfmt("memory limit exceeded by regular expression '%s'", re),
                .errPos = pos
            });
        } else {
            throw EvalError({
                .msg = hintfmt("invalid regular expression '%s'", re),
                .errPos = pos
            });
        }
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

      Evaluates to `[ "foo" ]`.
    )s",
    .fun = prim_match,
});

/* Split a string with a regular expression, and return a list of the
   non-matching parts interleaved by the lists of the matching groups. */
void prim_split(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    auto re = state.forceStringNoCtx(*args[0], pos);

    try {

        auto regex = state.regexCache->get(re);

        PathSet context;
        const auto str = state.forceString(*args[1], context, pos);

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

    } catch (std::regex_error &e) {
        if (e.code() == std::regex_constants::error_space) {
            // limit is _GLIBCXX_REGEX_STATE_LIMIT for libstdc++
            throw EvalError({
                .msg = hintfmt("memory limit exceeded by regular expression '%s'", re),
                .errPos = pos
            });
        } else {
            throw EvalError({
                .msg = hintfmt("invalid regular expression '%s'", re),
                .errPos = pos
            });
        }
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
      builtins.split "([[:upper:]]+)" "  FOO   "
      ```

      Evaluates to `[ " " [ "FOO" ] " " ]`.
    )s",
    .fun = prim_split,
});

static void prim_concatStringsSep(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    PathSet context;

    auto sep = state.forceString(*args[0], context, pos);
    state.forceList(*args[1], pos);

    std::string res;
    res.reserve((args[1]->listSize() + 32) * sep.size());
    bool first = true;

    for (auto elem : args[1]->listItems()) {
        if (first) first = false; else res += sep;
        res += *state.coerceToString(pos, *elem, context);
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

static void prim_replaceStrings(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceList(*args[0], pos);
    state.forceList(*args[1], pos);
    if (args[0]->listSize() != args[1]->listSize())
        throw EvalError({
            .msg = hintfmt("'from' and 'to' arguments to 'replaceStrings' have different lengths"),
            .errPos = pos
        });

    std::vector<std::string> from;
    from.reserve(args[0]->listSize());
    for (auto elem : args[0]->listItems())
        from.emplace_back(state.forceString(*elem, pos));

    std::vector<std::pair<std::string, PathSet>> to;
    to.reserve(args[1]->listSize());
    for (auto elem : args[1]->listItems()) {
        PathSet ctx;
        auto s = state.forceString(*elem, ctx, pos);
        to.emplace_back(s, std::move(ctx));
    }

    PathSet context;
    auto s = state.forceString(*args[2], context, pos);

    std::string res;
    // Loops one past last character to handle the case where 'from' contains an empty string.
    for (size_t p = 0; p <= s.size(); ) {
        bool found = false;
        auto i = from.begin();
        auto j = to.begin();
        for (; i != from.end(); ++i, ++j)
            if (s.compare(p, i->size(), *i) == 0) {
                found = true;
                res += j->first;
                if (i->empty()) {
                    if (p < s.size())
                        res += s[p];
                    p++;
                } else {
                    p += i->size();
                }
                for (auto& path : j->second)
                    context.insert(path);
                j->second.clear();
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
      with the corresponding string in *to*. For example,

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


static void prim_parseDrvName(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    auto name = state.forceStringNoCtx(*args[0], pos);
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
      name is everything up to but not including the first dash followed
      by a digit, and the version is everything following that dash. The
      result is returned in a set `{ name, version }`. Thus,
      `builtins.parseDrvName "nix-0.12pre12876"` returns `{ name =
      "nix"; version = "0.12pre12876"; }`.
    )",
    .fun = prim_parseDrvName,
});

static void prim_compareVersions(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    auto version1 = state.forceStringNoCtx(*args[0], pos);
    auto version2 = state.forceStringNoCtx(*args[1], pos);
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

static void prim_splitVersion(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    auto version = state.forceStringNoCtx(*args[0], pos);
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


RegisterPrimOp::RegisterPrimOp(std::string name, size_t arity, PrimOpFun fun)
{
    if (!primOps) primOps = new PrimOps;
    primOps->push_back({
        .name = name,
        .args = {},
        .arity = arity,
        .fun = fun,
    });
}


RegisterPrimOp::RegisterPrimOp(Info && info)
{
    if (!primOps) primOps = new PrimOps;
    primOps->push_back(std::move(info));
}


void EvalState::createBaseEnv()
{
    baseEnv.up = 0;

    /* Add global constants such as `true' to the base environment. */
    Value v;

    /* `builtins' must be first! */
    v.mkAttrs(buildBindings(128).finish());
    addConstant("builtins", v);

    v.mkBool(true);
    addConstant("true", v);

    v.mkBool(false);
    addConstant("false", v);

    v.mkNull();
    addConstant("null", v);

    if (!evalSettings.pureEval) {
        v.mkInt(time(0));
        addConstant("__currentTime", v);

        v.mkString(settings.thisSystem.get());
        addConstant("__currentSystem", v);
    }

    v.mkString(nixVersion);
    addConstant("__nixVersion", v);

    v.mkString(store->storeDir);
    addConstant("__storeDir", v);

    /* Language version.  This should be increased every time a new
       language feature gets added.  It's not necessary to increase it
       when primops get added, because you can just use `builtins ?
       primOp' to check. */
    v.mkInt(6);
    addConstant("__langVersion", v);

    // Miscellaneous
    if (evalSettings.enableNativeCode) {
        addPrimOp("__importNative", 2, prim_importNative);
        addPrimOp("__exec", 1, prim_exec);
    }

    /* Add a value containing the current Nix expression search path. */
    mkList(v, searchPath.size());
    int n = 0;
    for (auto & i : searchPath) {
        auto attrs = buildBindings(2);
        attrs.alloc("path").mkString(i.second);
        attrs.alloc("prefix").mkString(i.first);
        (v.listElems()[n++] = allocValue())->mkAttrs(attrs);
    }
    addConstant("__nixPath", v);

    if (RegisterPrimOp::primOps)
        for (auto & primOp : *RegisterPrimOp::primOps)
            if (!primOp.experimentalFeature
                || settings.isExperimentalFeatureEnabled(*primOp.experimentalFeature))
            {
                addPrimOp({
                    .fun = primOp.fun,
                    .arity = std::max(primOp.args.size(), primOp.arity),
                    .name = symbols.create(primOp.name),
                    .args = primOp.args,
                    .doc = primOp.doc,
                });
            }

    /* Add a wrapper around the derivation primop that computes the
       `drvPath' and `outPath' attributes lazily. */
    sDerivationNix = symbols.create("//builtin/derivation.nix");
    auto vDerivation = allocValue();
    addConstant("derivation", vDerivation);

    /* Now that we've added all primops, sort the `builtins' set,
       because attribute lookups expect it to be sorted. */
    baseEnv.values[0]->attrs->sort();

    staticBaseEnv.sort();

    /* Note: we have to initialize the 'derivation' constant *after*
       building baseEnv/staticBaseEnv because it uses 'builtins'. */
    char code[] =
        #include "primops/derivation.nix.gen.hh"
        // the parser needs two NUL bytes as terminators; one of them
        // is implied by being a C string.
        "\0";
    eval(parse(code, sizeof(code), foFile, sDerivationNix, "/", staticBaseEnv), *vDerivation);
}


}
