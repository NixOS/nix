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

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <regex>
#include <dlfcn.h>


namespace nix {


/*************************************************************
 * Miscellaneous
 *************************************************************/


InvalidPathError::InvalidPathError(const Path & path) :
    EvalError("path '%s' is not valid", path), path(path) {}

void EvalState::realiseContext(const PathSet & context)
{
    std::vector<StorePathWithOutputs> drvs;

    for (auto & i : context) {
        auto [ctxS, outputName] = decodeContext(i);
        auto ctx = store->parseStorePath(ctxS);
        if (!store->isValidPath(ctx))
            throw InvalidPathError(store->printStorePath(ctx));
        if (!outputName.empty() && ctx.isDerivation()) {
            drvs.push_back(StorePathWithOutputs{ctx, {outputName}});

            /* Add the output of this derivation to the allowed
               paths. */
            if (allowedPaths) {
                auto drv = store->derivationFromPath(ctx);
                DerivationOutputs::iterator i = drv.outputs.find(outputName);
                if (i == drv.outputs.end())
                    throw Error("derivation '%s' does not have an output named '%s'", ctxS, outputName);
                allowedPaths->insert(store->printStorePath(i->second.path(*store, drv.name)));
            }
        }
    }

    if (drvs.empty()) return;

    if (!evalSettings.enableImportFromDerivation)
        throw EvalError("attempted to realize '%1%' during evaluation but 'allow-import-from-derivation' is false",
            store->printStorePath(drvs.begin()->path));

    /* For performance, prefetch all substitute info. */
    StorePathSet willBuild, willSubstitute, unknown;
    uint64_t downloadSize, narSize;
    store->queryMissing(drvs, willBuild, willSubstitute, unknown, downloadSize, narSize);

    store->buildPaths(drvs);
}


/* Load and evaluate an expression from path specified by the
   argument. */
static void prim_scopedImport(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    PathSet context;
    Path path = state.coerceToPath(pos, *args[1], context);

    try {
        state.realiseContext(context);
    } catch (InvalidPathError & e) {
        throw EvalError({
            .hint = hintfmt("cannot import '%1%', since path '%2%' is not valid", path, e.path),
            .errPos = pos
        });
    }

    Path realPath = state.checkSourcePath(state.toRealPath(path, context));

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
        Value & w = *state.allocValue();
        state.mkAttrs(w, 3 + drv.outputs.size());
        Value * v2 = state.allocAttr(w, state.sDrvPath);
        mkString(*v2, path, {"=" + path});
        v2 = state.allocAttr(w, state.sName);
        mkString(*v2, drv.env["name"]);
        Value * outputsVal =
            state.allocAttr(w, state.symbols.create("outputs"));
        state.mkList(*outputsVal, drv.outputs.size());
        unsigned int outputs_index = 0;

        for (const auto & o : drv.outputsAndPaths(*state.store)) {
            v2 = state.allocAttr(w, state.symbols.create(o.first));
            mkString(*v2, state.store->printStorePath(o.second.second), {"!" + o.first + "!" + path});
            outputsVal->listElems()[outputs_index] = state.allocValue();
            mkString(*(outputsVal->listElems()[outputs_index++]), o.first);
        }
        w.attrs->sort();

        static RootValue fun;
        if (!fun) {
            fun = allocRootValue(state.allocValue());
            state.eval(state.parseExprFromString(
                #include "imported-drv-to-derivation.nix.gen.hh"
                , "/"), **fun);
        }

        state.forceFunction(**fun, pos);
        mkApp(v, **fun, w);
        state.forceAttrs(v, pos);
    } else {
        state.forceAttrs(*args[0]);
        if (args[0]->attrs->empty())
            state.evalFile(realPath, v);
        else {
            Env * env = &state.allocEnv(args[0]->attrs->size());
            env->up = &state.baseEnv;

            StaticEnv staticEnv(false, &state.staticBaseEnv);

            unsigned int displ = 0;
            for (auto & attr : *args[0]->attrs) {
                staticEnv.vars[attr.name] = displ;
                env->values[displ++] = attr.value;
            }

            printTalkative("evaluating file '%1%'", realPath);
            Expr * e = state.parseExprFromFile(resolveExprPath(realPath), staticEnv);

            e->eval(state, *env, v);
        }
    }
}


/* Want reasonable symbol names, so extern C */
/* !!! Should we pass the Pos or the file name too? */
extern "C" typedef void (*ValueInitializer)(EvalState & state, Value & v);

/* Load a ValueInitializer from a DSO and return whatever it initializes */
void prim_importNative(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    PathSet context;
    Path path = state.coerceToPath(pos, *args[0], context);

    try {
        state.realiseContext(context);
    } catch (InvalidPathError & e) {
        throw EvalError({
            .hint = hintfmt(
                "cannot import '%1%', since path '%2%' is not valid",
                path, e.path),
            .errPos = pos
        });
    }

    path = state.checkSourcePath(path);

    string sym = state.forceStringNoCtx(*args[1], pos);

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
            .hint = hintfmt("at least one argument to 'exec' required"),
            .errPos = pos
        });
    }
    PathSet context;
    auto program = state.coerceToString(pos, *elems[0], context, false, false);
    Strings commandArgs;
    for (unsigned int i = 1; i < args[0]->listSize(); ++i) {
        commandArgs.emplace_back(state.coerceToString(pos, *elems[i], context, false, false));
    }
    try {
        state.realiseContext(context);
    } catch (InvalidPathError & e) {
        throw EvalError({
            .hint = hintfmt("cannot execute '%1%', since path '%2%' is not valid",
                program, e.path),
            .errPos = pos
        });
    }

    auto output = runProgram(program, true, commandArgs);
    Expr * parsed;
    try {
        parsed = state.parseExprFromString(output, pos.file);
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
    string t;
    switch (args[0]->type) {
        case tInt: t = "int"; break;
        case tBool: t = "bool"; break;
        case tString: t = "string"; break;
        case tPath: t = "path"; break;
        case tNull: t = "null"; break;
        case tAttrs: t = "set"; break;
        case tList1: case tList2: case tListN: t = "list"; break;
        case tLambda:
        case tPrimOp:
        case tPrimOpApp:
            t = "lambda";
            break;
        case tExternal:
            t = args[0]->external->typeOf();
            break;
        case tFloat: t = "float"; break;
        default: abort();
    }
    mkString(v, state.symbols.create(t));
}


/* Determine whether the argument is the null value. */
static void prim_isNull(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceValue(*args[0], pos);
    mkBool(v, args[0]->type == tNull);
}


/* Determine whether the argument is a function. */
static void prim_isFunction(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceValue(*args[0], pos);
    bool res;
    switch (args[0]->type) {
        case tLambda:
        case tPrimOp:
        case tPrimOpApp:
            res = true;
            break;
        default:
            res = false;
            break;
    }
    mkBool(v, res);
}


/* Determine whether the argument is an integer. */
static void prim_isInt(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceValue(*args[0], pos);
    mkBool(v, args[0]->type == tInt);
}

/* Determine whether the argument is a float. */
static void prim_isFloat(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceValue(*args[0], pos);
    mkBool(v, args[0]->type == tFloat);
}

/* Determine whether the argument is a string. */
static void prim_isString(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceValue(*args[0], pos);
    mkBool(v, args[0]->type == tString);
}


/* Determine whether the argument is a Boolean. */
static void prim_isBool(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceValue(*args[0], pos);
    mkBool(v, args[0]->type == tBool);
}

/* Determine whether the argument is a path. */
static void prim_isPath(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceValue(*args[0], pos);
    mkBool(v, args[0]->type == tPath);
}

struct CompareValues
{
    bool operator () (const Value * v1, const Value * v2) const
    {
        if (v1->type == tFloat && v2->type == tInt)
            return v1->fpoint < v2->integer;
        if (v1->type == tInt && v2->type == tFloat)
            return v1->integer < v2->fpoint;
        if (v1->type != v2->type)
            throw EvalError("cannot compare %1% with %2%", showType(*v1), showType(*v2));
        switch (v1->type) {
            case tInt:
                return v1->integer < v2->integer;
            case tFloat:
                return v1->fpoint < v2->fpoint;
            case tString:
                return strcmp(v1->string.s, v2->string.s) < 0;
            case tPath:
                return strcmp(v1->path, v2->path) < 0;
            default:
                throw EvalError("cannot compare %1% with %2%", showType(*v1), showType(*v2));
        }
    }
};


#if HAVE_BOEHMGC
typedef list<Value *, gc_allocator<Value *> > ValueList;
#else
typedef list<Value *> ValueList;
#endif


static void prim_genericClosure(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceAttrs(*args[0], pos);

    /* Get the start set. */
    Bindings::iterator startSet =
        args[0]->attrs->find(state.symbols.create("startSet"));
    if (startSet == args[0]->attrs->end())
        throw EvalError({
            .hint = hintfmt("attribute 'startSet' required"),
            .errPos = pos
        });
    state.forceList(*startSet->value, pos);

    ValueList workSet;
    for (unsigned int n = 0; n < startSet->value->listSize(); ++n)
        workSet.push_back(startSet->value->listElems()[n]);

    /* Get the operator. */
    Bindings::iterator op =
        args[0]->attrs->find(state.symbols.create("operator"));
    if (op == args[0]->attrs->end())
        throw EvalError({
            .hint = hintfmt("attribute 'operator' required"),
            .errPos = pos
        });
    state.forceValue(*op->value, pos);

    /* Construct the closure by applying the operator to element of
       `workSet', adding the result to `workSet', continuing until
       no new elements are found. */
    ValueList res;
    // `doneKeys' doesn't need to be a GC root, because its values are
    // reachable from res.
    set<Value *, CompareValues> doneKeys;
    while (!workSet.empty()) {
        Value * e = *(workSet.begin());
        workSet.pop_front();

        state.forceAttrs(*e, pos);

        Bindings::iterator key =
            e->attrs->find(state.symbols.create("key"));
        if (key == e->attrs->end())
            throw EvalError({
                .hint = hintfmt("attribute 'key' required"),
                .errPos = pos
            });
        state.forceValue(*key->value, pos);

        if (!doneKeys.insert(key->value).second) continue;
        res.push_back(e);

        /* Call the `operator' function with `e' as argument. */
        Value call;
        mkApp(call, *op->value, *e);
        state.forceList(call, pos);

        /* Add the values returned by the operator to the work set. */
        for (unsigned int n = 0; n < call.listSize(); ++n) {
            state.forceValue(*call.listElems()[n], pos);
            workSet.push_back(call.listElems()[n]);
        }
    }

    /* Create the result list. */
    state.mkList(v, res.size());
    unsigned int n = 0;
    for (auto & i : res)
        v.listElems()[n++] = i;
}


static void prim_abort(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    PathSet context;
    string s = state.coerceToString(pos, *args[0], context);
    throw Abort("evaluation aborted with the following error message: '%1%'", s);
}


static void prim_throw(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    PathSet context;
    string s = state.coerceToString(pos, *args[0], context);
    throw ThrownError(s);
}


static void prim_addErrorContext(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    try {
        state.forceValue(*args[1], pos);
        v = *args[1];
    } catch (Error & e) {
        PathSet context;
        e.addTrace(std::nullopt, state.coerceToString(pos, *args[0], context));
        throw;
    }
}


/* Try evaluating the argument. Success => {success=true; value=something;},
 * else => {success=false; value=false;} */
static void prim_tryEval(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.mkAttrs(v, 2);
    try {
        state.forceValue(*args[0], pos);
        v.attrs->push_back(Attr(state.sValue, args[0]));
        mkBool(*state.allocAttr(v, state.symbols.create("success")), true);
    } catch (AssertionError & e) {
        mkBool(*state.allocAttr(v, state.sValue), false);
        mkBool(*state.allocAttr(v, state.symbols.create("success")), false);
    }
    v.attrs->sort();
}


/* Return an environment variable.  Use with care. */
static void prim_getEnv(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    string name = state.forceStringNoCtx(*args[0], pos);
    mkString(v, evalSettings.restrictEval || evalSettings.pureEval ? "" : getEnv(name).value_or(""));
}


/* Evaluate the first argument, then return the second argument. */
static void prim_seq(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceValue(*args[0], pos);
    state.forceValue(*args[1], pos);
    v = *args[1];
}


/* Evaluate the first argument deeply (i.e. recursing into lists and
   attrsets), then return the second argument. */
static void prim_deepSeq(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceValueDeep(*args[0]);
    state.forceValue(*args[1], pos);
    v = *args[1];
}


/* Evaluate the first expression and print it on standard error.  Then
   return the second expression.  Useful for debugging. */
static void prim_trace(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceValue(*args[0], pos);
    if (args[0]->type == tString)
        printError("trace: %1%", args[0]->string.s);
    else
        printError("trace: %1%", *args[0]);
    state.forceValue(*args[1], pos);
    v = *args[1];
}


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
    Bindings::iterator attr = args[0]->attrs->find(state.sName);
    if (attr == args[0]->attrs->end())
        throw EvalError({
            .hint = hintfmt("required attribute 'name' missing"),
            .errPos = pos
        });
    string drvName;
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
    std::optional<std::string> outputHash;
    std::string outputHashAlgo;
    auto ingestionMethod = FileIngestionMethod::Flat;

    StringSet outputs;
    outputs.insert("out");

    for (auto & i : args[0]->attrs->lexicographicOrder()) {
        if (i->name == state.sIgnoreNulls) continue;
        const string & key = i->name;
        vomit("processing attribute '%1%'", key);

        auto handleHashMode = [&](const std::string & s) {
            if (s == "recursive") ingestionMethod = FileIngestionMethod::Recursive;
            else if (s == "flat") ingestionMethod = FileIngestionMethod::Flat;
            else
                throw EvalError({
                    .hint = hintfmt("invalid value '%s' for 'outputHashMode' attribute", s),
                    .errPos = posDrvName
                });
        };

        auto handleOutputs = [&](const Strings & ss) {
            outputs.clear();
            for (auto & j : ss) {
                if (outputs.find(j) != outputs.end())
                    throw EvalError({
                        .hint = hintfmt("duplicate derivation output '%1%'", j),
                        .errPos = posDrvName
                    });
                /* !!! Check whether j is a valid attribute
                   name. */
                /* Derivations cannot be named ‘drv’, because
                   then we'd have an attribute ‘drvPath’ in
                   the resulting set. */
                if (j == "drv")
                    throw EvalError({
                        .hint = hintfmt("invalid derivation output name 'drv'" ),
                        .errPos = posDrvName
                    });
                outputs.insert(j);
            }
            if (outputs.empty())
                throw EvalError({
                    .hint = hintfmt("derivation cannot have an empty set of outputs"),
                    .errPos = posDrvName
                });
        };

        try {

            if (ignoreNulls) {
                state.forceValue(*i->value, pos);
                if (i->value->type == tNull) continue;
            }

            if (i->name == state.sContentAddressed) {
                settings.requireExperimentalFeature("ca-derivations");
                contentAddressed = state.forceBool(*i->value, pos);
            }

            /* The `args' attribute is special: it supplies the
               command-line arguments to the builder. */
            else if (i->name == state.sArgs) {
                state.forceList(*i->value, pos);
                for (unsigned int n = 0; n < i->value->listSize(); ++n) {
                    string s = state.coerceToString(posDrvName, *i->value->listElems()[n], context, true);
                    drv.args.push_back(s);
                }
            }

            /* All other attributes are passed to the builder through
               the environment. */
            else {

                if (jsonObject) {

                    if (i->name == state.sStructuredAttrs) continue;

                    auto placeholder(jsonObject->placeholder(key));
                    printValueAsJSON(state, true, *i->value, placeholder, context);

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
                        for (unsigned int n = 0; n < i->value->listSize(); ++n)
                            ss.emplace_back(state.forceStringNoCtx(*i->value->listElems()[n], posDrvName));
                        handleOutputs(ss);
                    }

                } else {
                    auto s = state.coerceToString(posDrvName, *i->value, context, true);
                    drv.env.emplace(key, s);
                    if (i->name == state.sBuilder) drv.builder = s;
                    else if (i->name == state.sSystem) drv.platform = s;
                    else if (i->name == state.sOutputHash) outputHash = s;
                    else if (i->name == state.sOutputHashAlgo) outputHashAlgo = s;
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
            std::pair<string, string> ctx = decodeContext(path);
            drv.inputDrvs[state.store->parseStorePath(ctx.first)].insert(ctx.second);
        }

        /* Otherwise it's a source file. */
        else
            drv.inputSrcs.insert(state.store->parseStorePath(path));
    }

    /* Do we have all required attributes? */
    if (drv.builder == "")
        throw EvalError({
            .hint = hintfmt("required attribute 'builder' missing"),
            .errPos = posDrvName
        });

    if (drv.platform == "")
        throw EvalError({
            .hint = hintfmt("required attribute 'system' missing"),
            .errPos = posDrvName
        });

    /* Check whether the derivation name is valid. */
    if (isDerivation(drvName))
        throw EvalError({
            .hint = hintfmt("derivation names are not allowed to end in '%s'", drvExtension),
            .errPos = posDrvName
        });

    if (outputHash) {
        /* Handle fixed-output derivations.

           Ignore `__contentAddressed` because fixed output derivations are
           already content addressed. */
        if (outputs.size() != 1 || *(outputs.begin()) != "out")
            throw Error({
                .hint = hintfmt("multiple outputs are not supported in fixed-output derivations"),
                .errPos = posDrvName
            });

        std::optional<HashType> ht = parseHashTypeOpt(outputHashAlgo);
        Hash h = newHashAllowEmpty(*outputHash, ht);

        auto outPath = state.store->makeFixedOutputPath(drvName, FixedOutputInfo {
            {
                .method = ingestionMethod,
                .hash = h,
            },
            {},
        });
        drv.env["out"] = state.store->printStorePath(outPath);
        drv.outputs.insert_or_assign("out", DerivationOutput {
                .output = DerivationOutputCAFixed {
                    .hash = FixedOutputHash {
                        .method = ingestionMethod,
                        .hash = std::move(h),
                    },
                },
        });
    }

    else if (contentAddressed) {
        HashType ht = parseHashType(outputHashAlgo);
        for (auto & i : outputs) {
            drv.env[i] = hashPlaceholder(i);
            drv.outputs.insert_or_assign(i, DerivationOutput {
                .output = DerivationOutputCAFloating {
                    .method = ingestionMethod,
                    .hashType = std::move(ht),
                },
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
                DerivationOutput {
                    .output = DerivationOutputInputAddressed {
                        .path = StorePath::dummy,
                    },
                });
        }

        // Regular, non-CA derivation should always return a single hash and not
        // hash per output.
        Hash h = std::get<0>(hashDerivationModulo(*state.store, Derivation(drv), true));

        for (auto & i : outputs) {
            auto outPath = state.store->makeOutputPath(i, h, drvName);
            drv.env[i] = state.store->printStorePath(outPath);
            drv.outputs.insert_or_assign(i,
                DerivationOutput {
                    .output = DerivationOutputInputAddressed {
                        .path = std::move(outPath),
                    },
                });
        }
    }

    /* Write the resulting term into the Nix store directory. */
    auto drvPath = writeDerivation(*state.store, drv, state.repair);
    auto drvPathS = state.store->printStorePath(drvPath);

    printMsg(lvlChatty, "instantiated '%1%' -> '%2%'", drvName, drvPathS);

    /* Optimisation, but required in read-only mode! because in that
       case we don't actually write store derivations, so we can't
       read them later. */
    drvHashes.insert_or_assign(drvPath,
        hashDerivationModulo(*state.store, Derivation(drv), false));

    state.mkAttrs(v, 1 + drv.outputs.size());
    mkString(*state.allocAttr(v, state.sDrvPath), drvPathS, {"=" + drvPathS});
    for (auto & i : drv.outputsAndPaths(*state.store)) {
        mkString(*state.allocAttr(v, state.symbols.create(i.first)),
            state.store->printStorePath(i.second.second), {"!" + i.first + "!" + drvPathS});
    }
    v.attrs->sort();
}


/* Return a placeholder string for the specified output that will be
   substituted by the corresponding output path at build time. For
   example, 'placeholder "out"' returns the string
   /1rz4g4znpzjwh1xymhjpm42vipw92pr73vdgl6xs1hycac8kf2n9. At build
   time, any occurence of this string in an derivation attribute will
   be replaced with the concrete path in the Nix store of the output
   ‘out’. */
static void prim_placeholder(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    mkString(v, hashPlaceholder(state.forceStringNoCtx(*args[0], pos)));
}


/*************************************************************
 * Paths
 *************************************************************/


/* Convert the argument to a path.  !!! obsolete? */
static void prim_toPath(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    PathSet context;
    Path path = state.coerceToPath(pos, *args[0], context);
    mkString(v, canonPath(path), context);
}


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
    PathSet context;
    Path path = state.checkSourcePath(state.coerceToPath(pos, *args[0], context));
    /* Resolve symlinks in ‘path’, unless ‘path’ itself is a symlink
       directly in the store.  The latter condition is necessary so
       e.g. nix-push does the right thing. */
    if (!state.store->isStorePath(path)) path = canonPath(path, true);
    if (!state.store->isInStore(path))
        throw EvalError({
            .hint = hintfmt("path '%1%' is not in the Nix store", path),
            .errPos = pos
        });
    auto path2 = state.store->toStorePath(path).first;
    if (!settings.readOnlyMode)
        state.store->ensurePath(path2);
    context.insert(state.store->printStorePath(path2));
    mkString(v, path, context);
}


static void prim_pathExists(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    PathSet context;
    Path path = state.coerceToPath(pos, *args[0], context);
    try {
        state.realiseContext(context);
    } catch (InvalidPathError & e) {
        throw EvalError({
            .hint = hintfmt(
                "cannot check the existence of '%1%', since path '%2%' is not valid",
                path, e.path),
            .errPos = pos
        });
    }

    try {
        mkBool(v, pathExists(state.checkSourcePath(path)));
    } catch (SysError & e) {
        /* Don't give away info from errors while canonicalising
           ‘path’ in restricted mode. */
        mkBool(v, false);
    } catch (RestrictedPathError & e) {
        mkBool(v, false);
    }
}


/* Return the base name of the given string, i.e., everything
   following the last slash. */
static void prim_baseNameOf(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    PathSet context;
    mkString(v, baseNameOf(state.coerceToString(pos, *args[0], context, false, false)), context);
}


/* Return the directory of the given path, i.e., everything before the
   last slash.  Return either a path or a string depending on the type
   of the argument. */
static void prim_dirOf(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    PathSet context;
    Path dir = dirOf(state.coerceToString(pos, *args[0], context, false, false));
    if (args[0]->type == tPath) mkPath(v, dir.c_str()); else mkString(v, dir, context);
}


/* Return the contents of a file as a string. */
static void prim_readFile(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    PathSet context;
    Path path = state.coerceToPath(pos, *args[0], context);
    try {
        state.realiseContext(context);
    } catch (InvalidPathError & e) {
        throw EvalError({
            .hint = hintfmt("cannot read '%1%', since path '%2%' is not valid", path, e.path),
            .errPos = pos
        });
    }
    string s = readFile(state.checkSourcePath(state.toRealPath(path, context)));
    if (s.find((char) 0) != string::npos)
        throw Error("the contents of the file '%1%' cannot be represented as a Nix string", path);
    mkString(v, s.c_str());
}


/* Find a file in the Nix search path. Used to implement <x> paths,
   which are desugared to 'findFile __nixPath "x"'. */
static void prim_findFile(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceList(*args[0], pos);

    SearchPath searchPath;

    for (unsigned int n = 0; n < args[0]->listSize(); ++n) {
        Value & v2(*args[0]->listElems()[n]);
        state.forceAttrs(v2, pos);

        string prefix;
        Bindings::iterator i = v2.attrs->find(state.symbols.create("prefix"));
        if (i != v2.attrs->end())
            prefix = state.forceStringNoCtx(*i->value, pos);

        i = v2.attrs->find(state.symbols.create("path"));
        if (i == v2.attrs->end())
            throw EvalError({
                .hint = hintfmt("attribute 'path' missing"),
                .errPos = pos
            });

        PathSet context;
        string path = state.coerceToString(pos, *i->value, context, false, false);

        try {
            state.realiseContext(context);
        } catch (InvalidPathError & e) {
            throw EvalError({
                .hint = hintfmt("cannot find '%1%', since path '%2%' is not valid", path, e.path),
                .errPos = pos
            });
        }

        searchPath.emplace_back(prefix, path);
    }

    string path = state.forceStringNoCtx(*args[1], pos);

    mkPath(v, state.checkSourcePath(state.findFile(searchPath, path, pos)).c_str());
}

/* Return the cryptographic hash of a file in base-16. */
static void prim_hashFile(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    string type = state.forceStringNoCtx(*args[0], pos);
    std::optional<HashType> ht = parseHashType(type);
    if (!ht)
      throw Error({
          .hint = hintfmt("unknown hash type '%1%'", type),
          .errPos = pos
      });

    PathSet context; // discarded
    Path p = state.coerceToPath(pos, *args[1], context);

    mkString(v, hashFile(*ht, state.checkSourcePath(p)).to_string(Base16, false), context);
}

/* Read a directory (without . or ..) */
static void prim_readDir(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    PathSet ctx;
    Path path = state.coerceToPath(pos, *args[0], ctx);
    try {
        state.realiseContext(ctx);
    } catch (InvalidPathError & e) {
        throw EvalError({
            .hint = hintfmt("cannot read '%1%', since path '%2%' is not valid", path, e.path),
            .errPos = pos
        });
    }

    DirEntries entries = readDirectory(state.checkSourcePath(path));
    state.mkAttrs(v, entries.size());

    for (auto & ent : entries) {
        Value * ent_val = state.allocAttr(v, state.symbols.create(ent.name));
        if (ent.type == DT_UNKNOWN)
            ent.type = getFileType(path + "/" + ent.name);
        mkStringNoCopy(*ent_val,
            ent.type == DT_REG ? "regular" :
            ent.type == DT_DIR ? "directory" :
            ent.type == DT_LNK ? "symlink" :
            "unknown");
    }

    v.attrs->sort();
}


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
    printValueAsXML(state, true, false, *args[0], out, context);
    mkString(v, out.str(), context);
}


/* Convert the argument (which can be any Nix expression) to a JSON
   string.  Not all Nix expressions can be sensibly or completely
   represented (e.g., functions). */
static void prim_toJSON(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    std::ostringstream out;
    PathSet context;
    printValueAsJSON(state, true, *args[0], out, context);
    mkString(v, out.str(), context);
}


/* Parse a JSON string to a value. */
static void prim_fromJSON(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    string s = state.forceStringNoCtx(*args[0], pos);
    parseJSON(state, s, v);
}


/* Store a string in the Nix store as a source file that can be used
   as an input by derivations. */
static void prim_toFile(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    PathSet context;
    string name = state.forceStringNoCtx(*args[0], pos);
    string contents = state.forceString(*args[1], context, pos);

    StorePathSet refs;

    for (auto path : context) {
        if (path.at(0) != '/')
            throw EvalError( {
                .hint = hintfmt(
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

    mkString(v, storePath, {storePath});
}


static void addPath(EvalState & state, const Pos & pos, const string & name, const Path & path_,
    Value * filterFun, FileIngestionMethod method, const std::optional<Hash> expectedHash, Value & v)
{
    const auto path = evalSettings.pureEval && expectedHash ?
        path_ :
        state.checkSourcePath(path_);
    PathFilter filter = filterFun ? ([&](const Path & path) {
        auto st = lstat(path);

        /* Call the filter function.  The first argument is the path,
           the second is a string indicating the type of the file. */
        Value arg1;
        mkString(arg1, path);

        Value fun2;
        state.callFunction(*filterFun, arg1, fun2, noPos);

        Value arg2;
        mkString(arg2,
            S_ISREG(st.st_mode) ? "regular" :
            S_ISDIR(st.st_mode) ? "directory" :
            S_ISLNK(st.st_mode) ? "symlink" :
            "unknown" /* not supported, will fail! */);

        Value res;
        state.callFunction(fun2, arg2, res, noPos);

        return state.forceBool(res, pos);
    }) : defaultPathFilter;

    std::optional<StorePath> expectedStorePath;
    if (expectedHash)
        expectedStorePath = state.store->makeFixedOutputPath(name, FixedOutputInfo {
            {
                .method = method,
                .hash = *expectedHash,
            },
            {},
        });
    Path dstPath;
    if (!expectedHash || !state.store->isValidPath(*expectedStorePath)) {
        dstPath = state.store->printStorePath(settings.readOnlyMode
            ? state.store->computeStorePathForPath(name, path, method, htSHA256, filter).first
            : state.store->addToStore(name, path, method, htSHA256, filter, state.repair));
        if (expectedHash && expectedStorePath != state.store->parseStorePath(dstPath))
            throw Error("store path mismatch in (possibly filtered) path added from '%s'", path);
    } else
        dstPath = state.store->printStorePath(*expectedStorePath);

    mkString(v, dstPath, {dstPath});
}


static void prim_filterSource(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    PathSet context;
    Path path = state.coerceToPath(pos, *args[1], context);
    if (!context.empty())
        throw EvalError({
            .hint = hintfmt("string '%1%' cannot refer to other paths", path),
            .errPos = pos
        });

    state.forceValue(*args[0], pos);
    if (args[0]->type != tLambda)
        throw TypeError({
            .hint = hintfmt(
                "first argument in call to 'filterSource' is not a function but %1%",
                showType(*args[0])),
            .errPos = pos
        });

    addPath(state, pos, std::string(baseNameOf(path)), path, args[0], FileIngestionMethod::Recursive, std::nullopt, v);
}

static void prim_path(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceAttrs(*args[0], pos);
    Path path;
    string name;
    Value * filterFun = nullptr;
    auto method = FileIngestionMethod::Recursive;
    std::optional<Hash> expectedHash;

    for (auto & attr : *args[0]->attrs) {
        const string & n(attr.name);
        if (n == "path") {
            PathSet context;
            path = state.coerceToPath(*attr.pos, *attr.value, context);
            if (!context.empty())
                throw EvalError({
                    .hint = hintfmt("string '%1%' cannot refer to other paths", path),
                    .errPos = *attr.pos
                });
        } else if (attr.name == state.sName)
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
                .hint = hintfmt("unsupported argument '%1%' to 'addPath'", attr.name),
                .errPos = *attr.pos
            });
    }
    if (path.empty())
        throw EvalError({
            .hint = hintfmt("'path' required"),
            .errPos = pos
        });
    if (name.empty())
        name = baseNameOf(path);

    addPath(state, pos, name, path, filterFun, method, expectedHash, v);
}


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
        mkString(*(v.listElems()[n++] = state.allocValue()), i.name);

    std::sort(v.listElems(), v.listElems() + n,
              [](Value * v1, Value * v2) { return strcmp(v1->string.s, v2->string.s) < 0; });
}


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
        [](Value * v1, Value * v2) { return (string) ((Attr *) v1)->name < (string) ((Attr *) v2)->name; });

    for (unsigned int i = 0; i < n; ++i)
        v.listElems()[i] = ((Attr *) v.listElems()[i])->value;
}


/* Dynamic version of the `.' operator. */
void prim_getAttr(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    string attr = state.forceStringNoCtx(*args[0], pos);
    state.forceAttrs(*args[1], pos);
    // !!! Should we create a symbol here or just do a lookup?
    Bindings::iterator i = args[1]->attrs->find(state.symbols.create(attr));
    if (i == args[1]->attrs->end())
        throw EvalError({
            .hint = hintfmt("attribute '%1%' missing", attr),
            .errPos = pos
        });
    // !!! add to stack trace?
    if (state.countCalls && i->pos) state.attrSelects[*i->pos]++;
    state.forceValue(*i->value, pos);
    v = *i->value;
}


/* Return position information of the specified attribute. */
void prim_unsafeGetAttrPos(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    string attr = state.forceStringNoCtx(*args[0], pos);
    state.forceAttrs(*args[1], pos);
    Bindings::iterator i = args[1]->attrs->find(state.symbols.create(attr));
    if (i == args[1]->attrs->end())
        mkNull(v);
    else
        state.mkPos(v, i->pos);
}


/* Dynamic version of the `?' operator. */
static void prim_hasAttr(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    string attr = state.forceStringNoCtx(*args[0], pos);
    state.forceAttrs(*args[1], pos);
    mkBool(v, args[1]->attrs->find(state.symbols.create(attr)) != args[1]->attrs->end());
}


/* Determine whether the argument is a set. */
static void prim_isAttrs(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceValue(*args[0], pos);
    mkBool(v, args[0]->type == tAttrs);
}


static void prim_removeAttrs(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceAttrs(*args[0], pos);
    state.forceList(*args[1], pos);

    /* Get the attribute names to be removed. */
    std::set<Symbol> names;
    for (unsigned int i = 0; i < args[1]->listSize(); ++i) {
        state.forceStringNoCtx(*args[1]->listElems()[i], pos);
        names.insert(state.symbols.create(args[1]->listElems()[i]->string.s));
    }

    /* Copy all attributes not in that set.  Note that we don't need
       to sort v.attrs because it's a subset of an already sorted
       vector. */
    state.mkAttrs(v, args[0]->attrs->size());
    for (auto & i : *args[0]->attrs) {
        if (names.find(i.name) == names.end())
            v.attrs->push_back(i);
    }
}


/* Builds a set from a list specifying (name, value) pairs.  To be
   precise, a list [{name = "name1"; value = value1;} ... {name =
   "nameN"; value = valueN;}] is transformed to {name1 = value1;
   ... nameN = valueN;}.  In case of duplicate occurences of the same
   name, the first takes precedence. */
static void prim_listToAttrs(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceList(*args[0], pos);

    state.mkAttrs(v, args[0]->listSize());

    std::set<Symbol> seen;

    for (unsigned int i = 0; i < args[0]->listSize(); ++i) {
        Value & v2(*args[0]->listElems()[i]);
        state.forceAttrs(v2, pos);

        Bindings::iterator j = v2.attrs->find(state.sName);
        if (j == v2.attrs->end())
            throw TypeError({
                .hint = hintfmt("'name' attribute missing in a call to 'listToAttrs'"),
                .errPos = pos
            });
        string name = state.forceStringNoCtx(*j->value, pos);

        Symbol sym = state.symbols.create(name);
        if (seen.insert(sym).second) {
            Bindings::iterator j2 = v2.attrs->find(state.symbols.create(state.sValue));
            if (j2 == v2.attrs->end())
                throw TypeError({
                    .hint = hintfmt("'value' attribute missing in a call to 'listToAttrs'"),
                    .errPos = pos
                });
            v.attrs->push_back(Attr(sym, j2->value, j2->pos));
        }
    }

    v.attrs->sort();
}


/* Return the right-biased intersection of two sets as1 and as2,
   i.e. a set that contains every attribute from as2 that is also a
   member of as1. */
static void prim_intersectAttrs(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceAttrs(*args[0], pos);
    state.forceAttrs(*args[1], pos);

    state.mkAttrs(v, std::min(args[0]->attrs->size(), args[1]->attrs->size()));

    for (auto & i : *args[0]->attrs) {
        Bindings::iterator j = args[1]->attrs->find(i.name);
        if (j != args[1]->attrs->end())
            v.attrs->push_back(*j);
    }
}


/* Collect each attribute named `attr' from a list of attribute sets.
   Sets that don't contain the named attribute are ignored.

   Example:
     catAttrs "a" [{a = 1;} {b = 0;} {a = 2;}]
     => [1 2]
*/
static void prim_catAttrs(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    Symbol attrName = state.symbols.create(state.forceStringNoCtx(*args[0], pos));
    state.forceList(*args[1], pos);

    Value * res[args[1]->listSize()];
    unsigned int found = 0;

    for (unsigned int n = 0; n < args[1]->listSize(); ++n) {
        Value & v2(*args[1]->listElems()[n]);
        state.forceAttrs(v2, pos);
        Bindings::iterator i = v2.attrs->find(attrName);
        if (i != v2.attrs->end())
            res[found++] = i->value;
    }

    state.mkList(v, found);
    for (unsigned int n = 0; n < found; ++n)
        v.listElems()[n] = res[n];
}


/* Return a set containing the names of the formal arguments expected
   by the function `f'.  The value of each attribute is a Boolean
   denoting whether the corresponding argument has a default value.  For instance,

      functionArgs ({ x, y ? 123}: ...)
   => { x = false; y = true; }

   "Formal argument" here refers to the attributes pattern-matched by
   the function.  Plain lambdas are not included, e.g.

      functionArgs (x: ...)
   => { }
*/
static void prim_functionArgs(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceValue(*args[0], pos);
    if (args[0]->type != tLambda)
        throw TypeError({
            .hint = hintfmt("'functionArgs' requires a function"),
            .errPos = pos
        });

    if (!args[0]->lambda.fun->matchAttrs) {
        state.mkAttrs(v, 0);
        return;
    }

    state.mkAttrs(v, args[0]->lambda.fun->formals->formals.size());
    for (auto & i : args[0]->lambda.fun->formals->formals) {
        // !!! should optimise booleans (allocate only once)
        Value * value = state.allocValue();
        v.attrs->push_back(Attr(i.name, value, &i.pos));
        mkBool(*value, i.def);
    }
    v.attrs->sort();
}


/* Apply a function to every element of an attribute set. */
static void prim_mapAttrs(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceAttrs(*args[1], pos);

    state.mkAttrs(v, args[1]->attrs->size());

    for (auto & i : *args[1]->attrs) {
        Value * vName = state.allocValue();
        Value * vFun2 = state.allocValue();
        mkString(*vName, i.name);
        mkApp(*vFun2, *args[0], *vName);
        mkApp(*state.allocAttr(v, i.name), *vFun2, *i.value);
    }
}



/*************************************************************
 * Lists
 *************************************************************/


/* Determine whether the argument is a list. */
static void prim_isList(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceValue(*args[0], pos);
    mkBool(v, args[0]->isList());
}


static void elemAt(EvalState & state, const Pos & pos, Value & list, int n, Value & v)
{
    state.forceList(list, pos);
    if (n < 0 || (unsigned int) n >= list.listSize())
        throw Error({
            .hint = hintfmt("list index %1% is out of bounds", n),
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


/* Return the first element of a list. */
static void prim_head(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    elemAt(state, pos, *args[0], 0, v);
}


/* Return a list consisting of everything but the first element of
   a list.  Warning: this function takes O(n) time, so you probably
   don't want to use it!  */
static void prim_tail(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceList(*args[0], pos);
    if (args[0]->listSize() == 0)
        throw Error({
            .hint = hintfmt("'tail' called on an empty list"),
            .errPos = pos
        });

    state.mkList(v, args[0]->listSize() - 1);
    for (unsigned int n = 0; n < v.listSize(); ++n)
        v.listElems()[n] = args[0]->listElems()[n + 1];
}


/* Apply a function to every element of a list. */
static void prim_map(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceList(*args[1], pos);

    state.mkList(v, args[1]->listSize());

    for (unsigned int n = 0; n < v.listSize(); ++n)
        mkApp(*(v.listElems()[n] = state.allocValue()),
            *args[0], *args[1]->listElems()[n]);
}


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


/* Return true if a list contains a given element. */
static void prim_elem(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    bool res = false;
    state.forceList(*args[1], pos);
    for (unsigned int n = 0; n < args[1]->listSize(); ++n)
        if (state.eqValues(*args[0], *args[1]->listElems()[n])) {
            res = true;
            break;
        }
    mkBool(v, res);
}


/* Concatenate a list of lists. */
static void prim_concatLists(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceList(*args[0], pos);
    state.concatLists(v, args[0]->listSize(), args[0]->listElems(), pos);
}


/* Return the length of a list.  This is an O(1) time operation. */
static void prim_length(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceList(*args[0], pos);
    mkInt(v, args[0]->listSize());
}


/* Reduce a list by applying a binary operator, from left to
   right. The operator is applied strictly. */
static void prim_foldlStrict(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceFunction(*args[0], pos);
    state.forceList(*args[2], pos);

    if (args[2]->listSize()) {
        Value * vCur = args[1];

        for (unsigned int n = 0; n < args[2]->listSize(); ++n) {
            Value vTmp;
            state.callFunction(*args[0], *vCur, vTmp, pos);
            vCur = n == args[2]->listSize() - 1 ? &v : state.allocValue();
            state.callFunction(vTmp, *args[2]->listElems()[n], *vCur, pos);
        }
        state.forceValue(v, pos);
    } else {
        state.forceValue(*args[1], pos);
        v = *args[1];
    }
}


static void anyOrAll(bool any, EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceFunction(*args[0], pos);
    state.forceList(*args[1], pos);

    Value vTmp;
    for (unsigned int n = 0; n < args[1]->listSize(); ++n) {
        state.callFunction(*args[0], *args[1]->listElems()[n], vTmp, pos);
        bool res = state.forceBool(vTmp, pos);
        if (res == any) {
            mkBool(v, any);
            return;
        }
    }

    mkBool(v, !any);
}


static void prim_any(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    anyOrAll(true, state, pos, args, v);
}


static void prim_all(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    anyOrAll(false, state, pos, args, v);
}


static void prim_genList(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    auto len = state.forceInt(*args[1], pos);

    if (len < 0)
        throw EvalError({
            .hint = hintfmt("cannot create list of size %1%", len),
            .errPos = pos
        });

    state.mkList(v, len);

    for (unsigned int n = 0; n < (unsigned int) len; ++n) {
        Value * arg = state.allocValue();
        mkInt(*arg, n);
        mkApp(*(v.listElems()[n] = state.allocValue()), *args[0], *arg);
    }
}


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
        if (args[0]->type == tPrimOp && args[0]->primOp->fun == prim_lessThan)
            return CompareValues()(a, b);

        Value vTmp1, vTmp2;
        state.callFunction(*args[0], *a, vTmp1, pos);
        state.callFunction(vTmp1, *b, vTmp2, pos);
        return state.forceBool(vTmp2, pos);
    };

    /* FIXME: std::sort can segfault if the comparator is not a strict
       weak ordering. What to do? std::stable_sort() seems more
       resilient, but no guarantees... */
    std::stable_sort(v.listElems(), v.listElems() + len, comparator);
}


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

    state.mkAttrs(v, 2);

    Value * vRight = state.allocAttr(v, state.sRight);
    auto rsize = right.size();
    state.mkList(*vRight, rsize);
    if (rsize)
        memcpy(vRight->listElems(), right.data(), sizeof(Value *) * rsize);

    Value * vWrong = state.allocAttr(v, state.sWrong);
    auto wsize = wrong.size();
    state.mkList(*vWrong, wsize);
    if (wsize)
        memcpy(vWrong->listElems(), wrong.data(), sizeof(Value *) * wsize);

    v.attrs->sort();
}


/* concatMap = f: list: concatLists (map f list); */
/* C++-version is to avoid allocating `mkApp', call `f' eagerly */
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
        state.forceList(lists[n], pos);
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


/*************************************************************
 * Integer arithmetic
 *************************************************************/


static void prim_add(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceValue(*args[0], pos);
    state.forceValue(*args[1], pos);
    if (args[0]->type == tFloat || args[1]->type == tFloat)
        mkFloat(v, state.forceFloat(*args[0], pos) + state.forceFloat(*args[1], pos));
    else
        mkInt(v, state.forceInt(*args[0], pos) + state.forceInt(*args[1], pos));
}


static void prim_sub(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceValue(*args[0], pos);
    state.forceValue(*args[1], pos);
    if (args[0]->type == tFloat || args[1]->type == tFloat)
        mkFloat(v, state.forceFloat(*args[0], pos) - state.forceFloat(*args[1], pos));
    else
        mkInt(v, state.forceInt(*args[0], pos) - state.forceInt(*args[1], pos));
}


static void prim_mul(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceValue(*args[0], pos);
    state.forceValue(*args[1], pos);
    if (args[0]->type == tFloat || args[1]->type == tFloat)
        mkFloat(v, state.forceFloat(*args[0], pos) * state.forceFloat(*args[1], pos));
    else
        mkInt(v, state.forceInt(*args[0], pos) * state.forceInt(*args[1], pos));
}


static void prim_div(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceValue(*args[0], pos);
    state.forceValue(*args[1], pos);

    NixFloat f2 = state.forceFloat(*args[1], pos);
    if (f2 == 0)
        throw EvalError({
            .hint = hintfmt("division by zero"),
            .errPos = pos
        });

    if (args[0]->type == tFloat || args[1]->type == tFloat) {
        mkFloat(v, state.forceFloat(*args[0], pos) / state.forceFloat(*args[1], pos));
    } else {
        NixInt i1 = state.forceInt(*args[0], pos);
        NixInt i2 = state.forceInt(*args[1], pos);
        /* Avoid division overflow as it might raise SIGFPE. */
        if (i1 == std::numeric_limits<NixInt>::min() && i2 == -1)
            throw EvalError({
                .hint = hintfmt("overflow in integer division"),
                .errPos = pos
            });

        mkInt(v, i1 / i2);
    }
}

static void prim_bitAnd(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    mkInt(v, state.forceInt(*args[0], pos) & state.forceInt(*args[1], pos));
}

static void prim_bitOr(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    mkInt(v, state.forceInt(*args[0], pos) | state.forceInt(*args[1], pos));
}

static void prim_bitXor(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    mkInt(v, state.forceInt(*args[0], pos) ^ state.forceInt(*args[1], pos));
}

static void prim_lessThan(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceValue(*args[0], pos);
    state.forceValue(*args[1], pos);
    CompareValues comp;
    mkBool(v, comp(args[0], args[1]));
}


/*************************************************************
 * String manipulation
 *************************************************************/


/* Convert the argument to a string.  Paths are *not* copied to the
   store, so `toString /foo/bar' yields `"/foo/bar"', not
   `"/nix/store/whatever..."'. */
static void prim_toString(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    PathSet context;
    string s = state.coerceToString(pos, *args[0], context, true, false);
    mkString(v, s, context);
}


/* `substring start len str' returns the substring of `str' starting
   at character position `min(start, stringLength str)' inclusive and
   ending at `min(start + len, stringLength str)'.  `start' must be
   non-negative. */
static void prim_substring(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    int start = state.forceInt(*args[0], pos);
    int len = state.forceInt(*args[1], pos);
    PathSet context;
    string s = state.coerceToString(pos, *args[2], context);

    if (start < 0)
        throw EvalError({
            .hint = hintfmt("negative start position in 'substring'"),
            .errPos = pos
        });

    mkString(v, (unsigned int) start >= s.size() ? "" : string(s, start, len), context);
}


static void prim_stringLength(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    PathSet context;
    string s = state.coerceToString(pos, *args[0], context);
    mkInt(v, s.size());
}


/* Return the cryptographic hash of a string in base-16. */
static void prim_hashString(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    string type = state.forceStringNoCtx(*args[0], pos);
    std::optional<HashType> ht = parseHashType(type);
    if (!ht)
        throw Error({
            .hint = hintfmt("unknown hash type '%1%'", type),
            .errPos = pos
        });

    PathSet context; // discarded
    string s = state.forceString(*args[1], context, pos);

    mkString(v, hashString(*ht, s).to_string(Base16, false), context);
}


/* Match a regular expression against a string and return either
   ‘null’ or a list containing substring matches. */
void prim_match(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    auto re = state.forceStringNoCtx(*args[0], pos);

    try {

        auto regex = state.regexCache.find(re);
        if (regex == state.regexCache.end())
            regex = state.regexCache.emplace(re, std::regex(re, std::regex::extended)).first;

        PathSet context;
        const std::string str = state.forceString(*args[1], context, pos);

        std::smatch match;
        if (!std::regex_match(str, match, regex->second)) {
            mkNull(v);
            return;
        }

        // the first match is the whole string
        const size_t len = match.size() - 1;
        state.mkList(v, len);
        for (size_t i = 0; i < len; ++i) {
            if (!match[i+1].matched)
                mkNull(*(v.listElems()[i] = state.allocValue()));
            else
                mkString(*(v.listElems()[i] = state.allocValue()), match[i + 1].str().c_str());
        }

    } catch (std::regex_error &e) {
        if (e.code() == std::regex_constants::error_space) {
            // limit is _GLIBCXX_REGEX_STATE_LIMIT for libstdc++
            throw EvalError({
                .hint = hintfmt("memory limit exceeded by regular expression '%s'", re),
                .errPos = pos
            });
        } else {
            throw EvalError({
                .hint = hintfmt("invalid regular expression '%s'", re),
                .errPos = pos
            });
        }
    }
}


/* Split a string with a regular expression, and return a list of the
   non-matching parts interleaved by the lists of the matching groups. */
static void prim_split(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    auto re = state.forceStringNoCtx(*args[0], pos);

    try {

        std::regex regex(re, std::regex::extended);

        PathSet context;
        const std::string str = state.forceString(*args[1], context, pos);

        auto begin = std::sregex_iterator(str.begin(), str.end(), regex);
        auto end = std::sregex_iterator();

        // Any matches results are surrounded by non-matching results.
        const size_t len = std::distance(begin, end);
        state.mkList(v, 2 * len + 1);
        size_t idx = 0;
        Value * elem;

        if (len == 0) {
            v.listElems()[idx++] = args[1];
            return;
        }

        for (std::sregex_iterator i = begin; i != end; ++i) {
            assert(idx <= 2 * len + 1 - 3);
            std::smatch match = *i;

            // Add a string for non-matched characters.
            elem = v.listElems()[idx++] = state.allocValue();
            mkString(*elem, match.prefix().str().c_str());

            // Add a list for matched substrings.
            const size_t slen = match.size() - 1;
            elem = v.listElems()[idx++] = state.allocValue();

            // Start at 1, beacause the first match is the whole string.
            state.mkList(*elem, slen);
            for (size_t si = 0; si < slen; ++si) {
                if (!match[si + 1].matched)
                    mkNull(*(elem->listElems()[si] = state.allocValue()));
                else
                    mkString(*(elem->listElems()[si] = state.allocValue()), match[si + 1].str().c_str());
            }

            // Add a string for non-matched suffix characters.
            if (idx == 2 * len) {
                elem = v.listElems()[idx++] = state.allocValue();
                mkString(*elem, match.suffix().str().c_str());
            }
        }
        assert(idx == 2 * len + 1);

    } catch (std::regex_error &e) {
        if (e.code() == std::regex_constants::error_space) {
            // limit is _GLIBCXX_REGEX_STATE_LIMIT for libstdc++
            throw EvalError({
                .hint = hintfmt("memory limit exceeded by regular expression '%s'", re),
                .errPos = pos
            });
        } else {
            throw EvalError({
                .hint = hintfmt("invalid regular expression '%s'", re),
                .errPos = pos
            });
        }
    }
}


static void prim_concatStringSep(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    PathSet context;

    auto sep = state.forceString(*args[0], context, pos);
    state.forceList(*args[1], pos);

    string res;
    res.reserve((args[1]->listSize() + 32) * sep.size());
    bool first = true;

    for (unsigned int n = 0; n < args[1]->listSize(); ++n) {
        if (first) first = false; else res += sep;
        res += state.coerceToString(pos, *args[1]->listElems()[n], context);
    }

    mkString(v, res, context);
}


static void prim_replaceStrings(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceList(*args[0], pos);
    state.forceList(*args[1], pos);
    if (args[0]->listSize() != args[1]->listSize())
        throw EvalError({
            .hint = hintfmt("'from' and 'to' arguments to 'replaceStrings' have different lengths"),
            .errPos = pos
        });

    vector<string> from;
    from.reserve(args[0]->listSize());
    for (unsigned int n = 0; n < args[0]->listSize(); ++n)
        from.push_back(state.forceString(*args[0]->listElems()[n], pos));

    vector<std::pair<string, PathSet>> to;
    to.reserve(args[1]->listSize());
    for (unsigned int n = 0; n < args[1]->listSize(); ++n) {
        PathSet ctx;
        auto s = state.forceString(*args[1]->listElems()[n], ctx, pos);
        to.push_back(std::make_pair(std::move(s), std::move(ctx)));
    }

    PathSet context;
    auto s = state.forceString(*args[2], context, pos);

    string res;
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

    mkString(v, res, context);
}


/*************************************************************
 * Versions
 *************************************************************/


static void prim_parseDrvName(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    string name = state.forceStringNoCtx(*args[0], pos);
    DrvName parsed(name);
    state.mkAttrs(v, 2);
    mkString(*state.allocAttr(v, state.sName), parsed.name);
    mkString(*state.allocAttr(v, state.symbols.create("version")), parsed.version);
    v.attrs->sort();
}


static void prim_compareVersions(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    string version1 = state.forceStringNoCtx(*args[0], pos);
    string version2 = state.forceStringNoCtx(*args[1], pos);
    mkInt(v, compareVersions(version1, version2));
}


static void prim_splitVersion(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    string version = state.forceStringNoCtx(*args[0], pos);
    auto iter = version.cbegin();
    Strings components;
    while (iter != version.cend()) {
        auto component = nextComponent(iter, version.cend());
        if (component.empty())
            break;
        components.emplace_back(std::move(component));
    }
    state.mkList(v, components.size());
    unsigned int n = 0;
    for (auto & component : components) {
        auto listElem = v.listElems()[n++] = state.allocValue();
        mkString(*listElem, std::move(component));
    }
}


/*************************************************************
 * Primop registration
 *************************************************************/


RegisterPrimOp::PrimOps * RegisterPrimOp::primOps;


RegisterPrimOp::RegisterPrimOp(std::string name, size_t arity, PrimOpFun fun,
    std::optional<std::string> requiredFeature)
{
    if (!primOps) primOps = new PrimOps;
    primOps->push_back({name, arity, fun, requiredFeature});
}


void EvalState::createBaseEnv()
{
    baseEnv.up = 0;

    /* Add global constants such as `true' to the base environment. */
    Value v;

    /* `builtins' must be first! */
    mkAttrs(v, 128);
    addConstant("builtins", v);

    mkBool(v, true);
    addConstant("true", v);

    mkBool(v, false);
    addConstant("false", v);

    mkNull(v);
    addConstant("null", v);

    auto vThrow = addPrimOp("throw", 1, prim_throw);

    auto addPurityError = [&](const std::string & name) {
        Value * v2 = allocValue();
        mkString(*v2, fmt("'%s' is not allowed in pure evaluation mode", name));
        mkApp(v, *vThrow, *v2);
        addConstant(name, v);
    };

    if (!evalSettings.pureEval) {
        mkInt(v, time(0));
        addConstant("__currentTime", v);
    }

    if (!evalSettings.pureEval) {
        mkString(v, settings.thisSystem.get());
        addConstant("__currentSystem", v);
    }

    mkString(v, nixVersion);
    addConstant("__nixVersion", v);

    mkString(v, store->storeDir);
    addConstant("__storeDir", v);

    /* Language version.  This should be increased every time a new
       language feature gets added.  It's not necessary to increase it
       when primops get added, because you can just use `builtins ?
       primOp' to check. */
    mkInt(v, 5);
    addConstant("__langVersion", v);

    // Miscellaneous
    auto vScopedImport = addPrimOp("scopedImport", 2, prim_scopedImport);
    Value * v2 = allocValue();
    mkAttrs(*v2, 0);
    mkApp(v, *vScopedImport, *v2);
    forceValue(v);
    addConstant("import", v);
    if (evalSettings.enableNativeCode) {
        addPrimOp("__importNative", 2, prim_importNative);
        addPrimOp("__exec", 1, prim_exec);
    }
    addPrimOp("__typeOf", 1, prim_typeOf);
    addPrimOp("isNull", 1, prim_isNull);
    addPrimOp("__isFunction", 1, prim_isFunction);
    addPrimOp("__isString", 1, prim_isString);
    addPrimOp("__isInt", 1, prim_isInt);
    addPrimOp("__isFloat", 1, prim_isFloat);
    addPrimOp("__isBool", 1, prim_isBool);
    addPrimOp("__isPath", 1, prim_isPath);
    addPrimOp("__genericClosure", 1, prim_genericClosure);
    addPrimOp("abort", 1, prim_abort);
    addPrimOp("__addErrorContext", 2, prim_addErrorContext);
    addPrimOp("__tryEval", 1, prim_tryEval);
    addPrimOp("__getEnv", 1, prim_getEnv);

    // Strictness
    addPrimOp("__seq", 2, prim_seq);
    addPrimOp("__deepSeq", 2, prim_deepSeq);

    // Debugging
    addPrimOp("__trace", 2, prim_trace);

    // Paths
    addPrimOp("__toPath", 1, prim_toPath);
    if (evalSettings.pureEval)
        addPurityError("__storePath");
    else
        addPrimOp("__storePath", 1, prim_storePath);
    addPrimOp("__pathExists", 1, prim_pathExists);
    addPrimOp("baseNameOf", 1, prim_baseNameOf);
    addPrimOp("dirOf", 1, prim_dirOf);
    addPrimOp("__readFile", 1, prim_readFile);
    addPrimOp("__readDir", 1, prim_readDir);
    addPrimOp("__findFile", 2, prim_findFile);
    addPrimOp("__hashFile", 2, prim_hashFile);

    // Creating files
    addPrimOp("__toXML", 1, prim_toXML);
    addPrimOp("__toJSON", 1, prim_toJSON);
    addPrimOp("__fromJSON", 1, prim_fromJSON);
    addPrimOp("__toFile", 2, prim_toFile);
    addPrimOp("__filterSource", 2, prim_filterSource);
    addPrimOp("__path", 1, prim_path);

    // Sets
    addPrimOp("__attrNames", 1, prim_attrNames);
    addPrimOp("__attrValues", 1, prim_attrValues);
    addPrimOp("__getAttr", 2, prim_getAttr);
    addPrimOp("__unsafeGetAttrPos", 2, prim_unsafeGetAttrPos);
    addPrimOp("__hasAttr", 2, prim_hasAttr);
    addPrimOp("__isAttrs", 1, prim_isAttrs);
    addPrimOp("removeAttrs", 2, prim_removeAttrs);
    addPrimOp("__listToAttrs", 1, prim_listToAttrs);
    addPrimOp("__intersectAttrs", 2, prim_intersectAttrs);
    addPrimOp("__catAttrs", 2, prim_catAttrs);
    addPrimOp("__functionArgs", 1, prim_functionArgs);
    addPrimOp("__mapAttrs", 2, prim_mapAttrs);

    // Lists
    addPrimOp("__isList", 1, prim_isList);
    addPrimOp("__elemAt", 2, prim_elemAt);
    addPrimOp("__head", 1, prim_head);
    addPrimOp("__tail", 1, prim_tail);
    addPrimOp("map", 2, prim_map);
    addPrimOp("__filter", 2, prim_filter);
    addPrimOp("__elem", 2, prim_elem);
    addPrimOp("__concatLists", 1, prim_concatLists);
    addPrimOp("__length", 1, prim_length);
    addPrimOp("__foldl'", 3, prim_foldlStrict);
    addPrimOp("__any", 2, prim_any);
    addPrimOp("__all", 2, prim_all);
    addPrimOp("__genList", 2, prim_genList);
    addPrimOp("__sort", 2, prim_sort);
    addPrimOp("__partition", 2, prim_partition);
    addPrimOp("__concatMap", 2, prim_concatMap);

    // Integer arithmetic
    addPrimOp("__add", 2, prim_add);
    addPrimOp("__sub", 2, prim_sub);
    addPrimOp("__mul", 2, prim_mul);
    addPrimOp("__div", 2, prim_div);
    addPrimOp("__bitAnd", 2, prim_bitAnd);
    addPrimOp("__bitOr", 2, prim_bitOr);
    addPrimOp("__bitXor", 2, prim_bitXor);
    addPrimOp("__lessThan", 2, prim_lessThan);

    // String manipulation
    addPrimOp("toString", 1, prim_toString);
    addPrimOp("__substring", 3, prim_substring);
    addPrimOp("__stringLength", 1, prim_stringLength);
    addPrimOp("__hashString", 2, prim_hashString);
    addPrimOp("__match", 2, prim_match);
    addPrimOp("__split", 2, prim_split);
    addPrimOp("__concatStringsSep", 2, prim_concatStringSep);
    addPrimOp("__replaceStrings", 3, prim_replaceStrings);

    // Versions
    addPrimOp("__parseDrvName", 1, prim_parseDrvName);
    addPrimOp("__compareVersions", 2, prim_compareVersions);
    addPrimOp("__splitVersion", 1, prim_splitVersion);

    // Derivations
    addPrimOp("derivationStrict", 1, prim_derivationStrict);
    addPrimOp("placeholder", 1, prim_placeholder);

    /* Add a wrapper around the derivation primop that computes the
       `drvPath' and `outPath' attributes lazily. */
    string path = canonPath(settings.nixDataDir + "/nix/corepkgs/derivation.nix", true);
    sDerivationNix = symbols.create(path);
    evalFile(path, v);
    addConstant("derivation", v);

    /* Add a value containing the current Nix expression search path. */
    mkList(v, searchPath.size());
    int n = 0;
    for (auto & i : searchPath) {
        v2 = v.listElems()[n++] = allocValue();
        mkAttrs(*v2, 2);
        mkString(*allocAttr(*v2, symbols.create("path")), i.second);
        mkString(*allocAttr(*v2, symbols.create("prefix")), i.first);
        v2->attrs->sort();
    }
    addConstant("__nixPath", v);

    if (RegisterPrimOp::primOps)
        for (auto & primOp : *RegisterPrimOp::primOps)
            if (!primOp.requiredFeature || settings.isExperimentalFeatureEnabled(*primOp.requiredFeature))
                addPrimOp(primOp.name, primOp.arity, primOp.primOp);

    /* Now that we've added all primops, sort the `builtins' set,
       because attribute lookups expect it to be sorted. */
    baseEnv.values[0]->attrs->sort();
}


}
