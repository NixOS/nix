#include "eval.hh"
#include "misc.hh"
#include "globals.hh"
#include "store-api.hh"
#include "util.hh"
#include "archive.hh"
#include "value-to-xml.hh"
#include "value-to-json.hh"
#include "json-to-value.hh"
#include "names.hh"
#include "eval-inline.hh"
#include "download.hh"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <dlfcn.h>


namespace nix {


/*************************************************************
 * Miscellaneous
 *************************************************************/


/* Decode a context string ‘!<name>!<path>’ into a pair <path,
   name>. */
std::pair<string, string> decodeContext(const string & s)
{
    if (s.at(0) == '!') {
        size_t index = s.find("!", 1);
        return std::pair<string, string>(string(s, index + 1), string(s, 1, index - 1));
    } else
        return std::pair<string, string>(s.at(0) == '/' ? s: string(s, 1), "");
}


InvalidPathError::InvalidPathError(const Path & path) :
    EvalError(format("path ‘%1%’ is not valid") % path), path(path) {}

void realiseContext(const PathSet & context)
{
    PathSet drvs;
    for (auto & i : context) {
        std::pair<string, string> decoded = decodeContext(i);
        Path ctx = decoded.first;
        assert(isStorePath(ctx));
        if (!store->isValidPath(ctx))
            throw InvalidPathError(ctx);
        if (!decoded.second.empty() && isDerivation(ctx))
            drvs.insert(decoded.first + "!" + decoded.second);
    }
    if (!drvs.empty()) {
        /* For performance, prefetch all substitute info. */
        PathSet willBuild, willSubstitute, unknown;
        unsigned long long downloadSize, narSize;
        queryMissing(*store, drvs,
            willBuild, willSubstitute, unknown, downloadSize, narSize);

        store->buildPaths(drvs);
    }
}


/* Load and evaluate an expression from path specified by the
   argument. */
static void prim_scopedImport(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    PathSet context;
    Path path = state.coerceToPath(pos, *args[1], context);

    try {
        realiseContext(context);
    } catch (InvalidPathError & e) {
        throw EvalError(format("cannot import ‘%1%’, since path ‘%2%’ is not valid, at %3%")
            % path % e.path % pos);
    }

    path = state.checkSourcePath(path);

    if (isStorePath(path) && store->isValidPath(path) && isDerivation(path)) {
        Derivation drv = readDerivation(path);
        Value & w = *state.allocValue();
        state.mkAttrs(w, 3 + drv.outputs.size());
        Value * v2 = state.allocAttr(w, state.sDrvPath);
        mkString(*v2, path, singleton<PathSet>("=" + path));
        v2 = state.allocAttr(w, state.sName);
        mkString(*v2, drv.env["name"]);
        Value * outputsVal =
            state.allocAttr(w, state.symbols.create("outputs"));
        state.mkList(*outputsVal, drv.outputs.size());
        unsigned int outputs_index = 0;

        for (const auto & o : drv.outputs) {
            v2 = state.allocAttr(w, state.symbols.create(o.first));
            mkString(*v2, o.second.path,
                singleton<PathSet>("!" + o.first + "!" + path));
            outputsVal->listElems()[outputs_index] = state.allocValue();
            mkString(*(outputsVal->listElems()[outputs_index++]), o.first);
        }
        w.attrs->sort();
        Value fun;
        state.evalFile(settings.nixDataDir + "/nix/corepkgs/imported-drv-to-derivation.nix", fun);
        state.forceFunction(fun, pos);
        mkApp(v, fun, w);
        state.forceAttrs(v, pos);
    } else {
        state.forceAttrs(*args[0]);
        if (args[0]->attrs->empty())
            state.evalFile(path, v);
        else {
            Env * env = &state.allocEnv(args[0]->attrs->size());
            env->up = &state.baseEnv;

            StaticEnv staticEnv(false, &state.staticBaseEnv);

            unsigned int displ = 0;
            for (auto & attr : *args[0]->attrs) {
                staticEnv.vars[attr.name] = displ;
                env->values[displ++] = attr.value;
            }

            startNest(nest, lvlTalkative, format("evaluating file ‘%1%’") % path);
            Expr * e = state.parseExprFromFile(resolveExprPath(path), staticEnv);

            e->eval(state, *env, v);
        }
    }
}


/* Want reasonable symbol names, so extern C */
/* !!! Should we pass the Pos or the file name too? */
extern "C" typedef void (*ValueInitializer)(EvalState & state, Value & v);

/* Load a ValueInitializer from a DSO and return whatever it initializes */
static void prim_importNative(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    PathSet context;
    Path path = state.coerceToPath(pos, *args[0], context);

    try {
        realiseContext(context);
    } catch (InvalidPathError & e) {
        throw EvalError(format("cannot import ‘%1%’, since path ‘%2%’ is not valid, at %3%")
            % path % e.path % pos);
    }

    path = state.checkSourcePath(path);

    string sym = state.forceStringNoCtx(*args[1], pos);

    void *handle = dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL);
    if (!handle)
        throw EvalError(format("could not open ‘%1%’: %2%") % path % dlerror());

    dlerror();
    ValueInitializer func = (ValueInitializer) dlsym(handle, sym.c_str());
    if(!func) {
        char *message = dlerror();
        if (message)
            throw EvalError(format("could not load symbol ‘%1%’ from ‘%2%’: %3%") % sym % path % message);
        else
            throw EvalError(format("symbol ‘%1%’ from ‘%2%’ resolved to NULL when a function pointer was expected")
                    % sym % path);
    }

    (func)(state, v);

    /* We don't dlclose because v may be a primop referencing a function in the shared object file */
}


/* Execute a program and parse its output */
static void prim_exec(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceList(*args[0], pos);
    auto elems = args[0]->listElems();
    auto count = args[0]->listSize();
    if (count == 0) {
        throw EvalError(format("at least one argument to 'exec' required, at %1%") % pos);
    }
    PathSet context;
    auto program = state.coerceToString(pos, *elems[0], context, false, false);
    Strings commandArgs;
    for (unsigned int i = 1; i < args[0]->listSize(); ++i) {
        commandArgs.emplace_back(state.coerceToString(pos, *elems[i], context, false, false));
    }
    try {
        realiseContext(context);
    } catch (InvalidPathError & e) {
        throw EvalError(format("cannot execute ‘%1%’, since path ‘%2%’ is not valid, at %3%")
            % program % e.path % pos);
    }

    auto output = runProgram(program, true, commandArgs);
    Expr * parsed;
    try {
        parsed = state.parseExprFromString(output, pos.file);
    } catch (Error & e) {
        e.addPrefix(format("While parsing the output from ‘%1%’, at %2%\n") % program % pos);
        throw;
    }
    try {
        state.eval(parsed, v);
    } catch (Error & e) {
        e.addPrefix(format("While evaluating the output from ‘%1%’, at %2%\n") % program % pos);
        throw;
    }
}


/* Return a string representing the type of the expression. */
static void prim_typeOf(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceValue(*args[0]);
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
        default: abort();
    }
    mkString(v, state.symbols.create(t));
}


/* Determine whether the argument is the null value. */
static void prim_isNull(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceValue(*args[0]);
    mkBool(v, args[0]->type == tNull);
}


/* Determine whether the argument is a function. */
static void prim_isFunction(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceValue(*args[0]);
    mkBool(v, args[0]->type == tLambda);
}


/* Determine whether the argument is an integer. */
static void prim_isInt(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceValue(*args[0]);
    mkBool(v, args[0]->type == tInt);
}


/* Determine whether the argument is a string. */
static void prim_isString(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceValue(*args[0]);
    mkBool(v, args[0]->type == tString);
}


/* Determine whether the argument is a Boolean. */
static void prim_isBool(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceValue(*args[0]);
    mkBool(v, args[0]->type == tBool);
}


struct CompareValues
{
    bool operator () (const Value * v1, const Value * v2) const
    {
        if (v1->type != v2->type)
            throw EvalError(format("cannot compare %1% with %2%") % showType(*v1) % showType(*v2));
        switch (v1->type) {
            case tInt:
                return v1->integer < v2->integer;
            case tString:
                return strcmp(v1->string.s, v2->string.s) < 0;
            case tPath:
                return strcmp(v1->path, v2->path) < 0;
            default:
                throw EvalError(format("cannot compare %1% with %2%") % showType(*v1) % showType(*v2));
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
    startNest(nest, lvlDebug, "finding dependencies");

    state.forceAttrs(*args[0], pos);

    /* Get the start set. */
    Bindings::iterator startSet =
        args[0]->attrs->find(state.symbols.create("startSet"));
    if (startSet == args[0]->attrs->end())
        throw EvalError(format("attribute ‘startSet’ required, at %1%") % pos);
    state.forceList(*startSet->value, pos);

    ValueList workSet;
    for (unsigned int n = 0; n < startSet->value->listSize(); ++n)
        workSet.push_back(startSet->value->listElems()[n]);

    /* Get the operator. */
    Bindings::iterator op =
        args[0]->attrs->find(state.symbols.create("operator"));
    if (op == args[0]->attrs->end())
        throw EvalError(format("attribute ‘operator’ required, at %1%") % pos);
    state.forceValue(*op->value);

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
            throw EvalError(format("attribute ‘key’ required, at %1%") % pos);
        state.forceValue(*key->value);

        if (doneKeys.find(key->value) != doneKeys.end()) continue;
        doneKeys.insert(key->value);
        res.push_back(e);

        /* Call the `operator' function with `e' as argument. */
        Value call;
        mkApp(call, *op->value, *e);
        state.forceList(call, pos);

        /* Add the values returned by the operator to the work set. */
        for (unsigned int n = 0; n < call.listSize(); ++n) {
            state.forceValue(*call.listElems()[n]);
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
    throw Abort(format("evaluation aborted with the following error message: ‘%1%’") % s);
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
        state.forceValue(*args[1]);
        v = *args[1];
    } catch (Error & e) {
        PathSet context;
        e.addPrefix(format("%1%\n") % state.coerceToString(pos, *args[0], context));
        throw;
    }
}


/* Try evaluating the argument. Success => {success=true; value=something;},
 * else => {success=false; value=false;} */
static void prim_tryEval(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.mkAttrs(v, 2);
    try {
        state.forceValue(*args[0]);
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
    mkString(v, state.restricted ? "" : getEnv(name));
}


/* Evaluate the first argument, then return the second argument. */
static void prim_seq(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceValue(*args[0]);
    state.forceValue(*args[1]);
    v = *args[1];
}


/* Evaluate the first argument deeply (i.e. recursing into lists and
   attrsets), then return the second argument. */
static void prim_deepSeq(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceValueDeep(*args[0]);
    state.forceValue(*args[1]);
    v = *args[1];
}


/* Evaluate the first expression and print it on standard error.  Then
   return the second expression.  Useful for debugging. */
static void prim_trace(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceValue(*args[0]);
    if (args[0]->type == tString)
        printMsg(lvlError, format("trace: %1%") % args[0]->string.s);
    else
        printMsg(lvlError, format("trace: %1%") % *args[0]);
    state.forceValue(*args[1]);
    v = *args[1];
}


void prim_valueSize(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    /* We're not forcing the argument on purpose. */
    mkInt(v, valueSize(*args[0]));
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
    startNest(nest, lvlVomit, "evaluating derivation");

    state.forceAttrs(*args[0], pos);

    /* Figure out the name first (for stack backtraces). */
    Bindings::iterator attr = args[0]->attrs->find(state.sName);
    if (attr == args[0]->attrs->end())
        throw EvalError(format("required attribute ‘name’ missing, at %1%") % pos);
    string drvName;
    Pos & posDrvName(*attr->pos);
    try {
        drvName = state.forceStringNoCtx(*attr->value, pos);
    } catch (Error & e) {
        e.addPrefix(format("while evaluating the derivation attribute ‘name’ at %1%:\n") % posDrvName);
        throw;
    }

    /* Check whether null attributes should be ignored. */
    bool ignoreNulls = false;
    attr = args[0]->attrs->find(state.sIgnoreNulls);
    if (attr != args[0]->attrs->end())
        ignoreNulls = state.forceBool(*attr->value);

    /* Build the derivation expression by processing the attributes. */
    Derivation drv;

    PathSet context;

    string outputHash, outputHashAlgo;
    bool outputHashRecursive = false;

    StringSet outputs;
    outputs.insert("out");

    for (auto & i : *args[0]->attrs) {
        if (i.name == state.sIgnoreNulls) continue;
        string key = i.name;
        startNest(nest, lvlVomit, format("processing attribute ‘%1%’") % key);

        try {

            if (ignoreNulls) {
                state.forceValue(*i.value);
                if (i.value->type == tNull) continue;
            }

            /* The `args' attribute is special: it supplies the
               command-line arguments to the builder. */
            if (key == "args") {
                state.forceList(*i.value, pos);
                for (unsigned int n = 0; n < i.value->listSize(); ++n) {
                    string s = state.coerceToString(posDrvName, *i.value->listElems()[n], context, true);
                    drv.args.push_back(s);
                }
            }

            /* All other attributes are passed to the builder through
               the environment. */
            else {
                string s = state.coerceToString(posDrvName, *i.value, context, true);
                drv.env[key] = s;
                if (key == "builder") drv.builder = s;
                else if (i.name == state.sSystem) drv.platform = s;
                else if (i.name == state.sName) {
                    drvName = s;
                    printMsg(lvlVomit, format("derivation name is ‘%1%’") % drvName);
                }
                else if (key == "outputHash") outputHash = s;
                else if (key == "outputHashAlgo") outputHashAlgo = s;
                else if (key == "outputHashMode") {
                    if (s == "recursive") outputHashRecursive = true;
                    else if (s == "flat") outputHashRecursive = false;
                    else throw EvalError(format("invalid value ‘%1%’ for ‘outputHashMode’ attribute, at %2%") % s % posDrvName);
                }
                else if (key == "outputs") {
                    Strings tmp = tokenizeString<Strings>(s);
                    outputs.clear();
                    for (auto & j : tmp) {
                        if (outputs.find(j) != outputs.end())
                            throw EvalError(format("duplicate derivation output ‘%1%’, at %2%") % j % posDrvName);
                        /* !!! Check whether j is a valid attribute
                           name. */
                        /* Derivations cannot be named ‘drv’, because
                           then we'd have an attribute ‘drvPath’ in
                           the resulting set. */
                        if (j == "drv")
                            throw EvalError(format("invalid derivation output name ‘drv’, at %1%") % posDrvName);
                        outputs.insert(j);
                    }
                    if (outputs.empty())
                        throw EvalError(format("derivation cannot have an empty set of outputs, at %1%") % posDrvName);
                }
            }

        } catch (Error & e) {
            e.addPrefix(format("while evaluating the attribute ‘%1%’ of the derivation ‘%2%’ at %3%:\n")
                % key % drvName % posDrvName);
            throw;
        }
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
            PathSet refs; computeFSClosure(*store, string(path, 1), refs);
            for (auto & j : refs) {
                drv.inputSrcs.insert(j);
                if (isDerivation(j))
                    drv.inputDrvs[j] = store->queryDerivationOutputNames(j);
            }
        }

        /* See prim_unsafeDiscardOutputDependency. */
        else if (path.at(0) == '~')
            drv.inputSrcs.insert(string(path, 1));

        /* Handle derivation outputs of the form ‘!<name>!<path>’. */
        else if (path.at(0) == '!') {
            std::pair<string, string> ctx = decodeContext(path);
            drv.inputDrvs[ctx.first].insert(ctx.second);
        }

        /* Handle derivation contexts returned by
           ‘builtins.storePath’. */
        else if (isDerivation(path))
            drv.inputDrvs[path] = store->queryDerivationOutputNames(path);

        /* Otherwise it's a source file. */
        else
            drv.inputSrcs.insert(path);
    }

    /* Do we have all required attributes? */
    if (drv.builder == "")
        throw EvalError(format("required attribute ‘builder’ missing, at %1%") % posDrvName);
    if (drv.platform == "")
        throw EvalError(format("required attribute ‘system’ missing, at %1%") % posDrvName);

    /* Check whether the derivation name is valid. */
    checkStoreName(drvName);
    if (isDerivation(drvName))
        throw EvalError(format("derivation names are not allowed to end in ‘%1%’, at %2%")
            % drvExtension % posDrvName);

    if (outputHash != "") {
        /* Handle fixed-output derivations. */
        if (outputs.size() != 1 || *(outputs.begin()) != "out")
            throw Error(format("multiple outputs are not supported in fixed-output derivations, at %1%") % posDrvName);

        HashType ht = parseHashType(outputHashAlgo);
        if (ht == htUnknown)
            throw EvalError(format("unknown hash algorithm ‘%1%’, at %2%") % outputHashAlgo % posDrvName);
        Hash h = parseHash16or32(ht, outputHash);
        outputHash = printHash(h);
        if (outputHashRecursive) outputHashAlgo = "r:" + outputHashAlgo;

        Path outPath = makeFixedOutputPath(outputHashRecursive, ht, h, drvName);
        drv.env["out"] = outPath;
        drv.outputs["out"] = DerivationOutput(outPath, outputHashAlgo, outputHash);
    }

    else {
        /* Construct the "masked" store derivation, which is the final
           one except that in the list of outputs, the output paths
           are empty, and the corresponding environment variables have
           an empty value.  This ensures that changes in the set of
           output names do get reflected in the hash. */
        for (auto & i : outputs) {
            drv.env[i] = "";
            drv.outputs[i] = DerivationOutput("", "", "");
        }

        /* Use the masked derivation expression to compute the output
           path. */
        Hash h = hashDerivationModulo(*store, drv);

        for (auto & i : drv.outputs)
            if (i.second.path == "") {
                Path outPath = makeOutputPath(i.first, h, drvName);
                drv.env[i.first] = outPath;
                i.second.path = outPath;
            }
    }

    /* Write the resulting term into the Nix store directory. */
    Path drvPath = writeDerivation(*store, drv, drvName, state.repair);

    printMsg(lvlChatty, format("instantiated ‘%1%’ -> ‘%2%’")
        % drvName % drvPath);

    /* Optimisation, but required in read-only mode! because in that
       case we don't actually write store derivations, so we can't
       read them later. */
    drvHashes[drvPath] = hashDerivationModulo(*store, drv);

    state.mkAttrs(v, 1 + drv.outputs.size());
    mkString(*state.allocAttr(v, state.sDrvPath), drvPath, singleton<PathSet>("=" + drvPath));
    for (auto & i : drv.outputs) {
        mkString(*state.allocAttr(v, state.symbols.create(i.first)),
            i.second.path, singleton<PathSet>("!" + i.first + "!" + drvPath));
    }
    v.attrs->sort();
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
    if (!isStorePath(path)) path = canonPath(path, true);
    if (!isInStore(path))
        throw EvalError(format("path ‘%1%’ is not in the Nix store, at %2%") % path % pos);
    Path path2 = toStorePath(path);
    if (!settings.readOnlyMode)
        store->ensurePath(path2);
    context.insert(path2);
    mkString(v, path, context);
}


static void prim_pathExists(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    PathSet context;
    Path path = state.coerceToPath(pos, *args[0], context);
    if (!context.empty())
        throw EvalError(format("string ‘%1%’ cannot refer to other paths, at %2%") % path % pos);
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
    Path dir = dirOf(state.coerceToPath(pos, *args[0], context));
    if (args[0]->type == tPath) mkPath(v, dir.c_str()); else mkString(v, dir, context);
}


/* Return the contents of a file as a string. */
static void prim_readFile(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    PathSet context;
    Path path = state.coerceToPath(pos, *args[0], context);
    try {
        realiseContext(context);
    } catch (InvalidPathError & e) {
        throw EvalError(format("cannot read ‘%1%’, since path ‘%2%’ is not valid, at %3%")
            % path % e.path % pos);
    }
    string s = readFile(state.checkSourcePath(path));
    if (s.find((char) 0) != string::npos)
        throw Error(format("the contents of the file ‘%1%’ cannot be represented as a Nix string") % path);
    mkString(v, s.c_str());
}


/* Find a file in the Nix search path. Used to implement <x> paths,
   which are desugared to ‘findFile __nixPath "x"’. */
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
            throw EvalError(format("attribute ‘path’ missing, at %1%") % pos);

        PathSet context;
        string path = state.coerceToString(pos, *i->value, context, false, false);

        try {
            realiseContext(context);
        } catch (InvalidPathError & e) {
            throw EvalError(format("cannot find ‘%1%’, since path ‘%2%’ is not valid, at %3%")
                % path % e.path % pos);
        }

        searchPath.emplace_back(prefix, path);
    }

    string path = state.forceStringNoCtx(*args[1], pos);

    mkPath(v, state.checkSourcePath(state.findFile(searchPath, path, pos)).c_str());
}

/* Read a directory (without . or ..) */
static void prim_readDir(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    PathSet ctx;
    Path path = state.coerceToPath(pos, *args[0], ctx);
    try {
        realiseContext(ctx);
    } catch (InvalidPathError & e) {
        throw EvalError(format("cannot read ‘%1%’, since path ‘%2%’ is not valid, at %3%")
            % path % e.path % pos);
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

    PathSet refs;

    for (auto path : context) {
        if (path.at(0) == '=') path = string(path, 1);
        if (isDerivation(path)) {
            /* See prim_unsafeDiscardOutputDependency. */
            if (path.at(0) != '~')
                throw EvalError(format("in ‘toFile’: the file ‘%1%’ cannot refer to derivation outputs, at %2%") % name % pos);
            path = string(path, 1);
	}
        refs.insert(path);
    }

    Path storePath = settings.readOnlyMode
        ? computeStorePathForText(name, contents, refs)
        : store->addTextToStore(name, contents, refs, state.repair);

    /* Note: we don't need to add `context' to the context of the
       result, since `storePath' itself has references to the paths
       used in args[1]. */

    mkString(v, storePath, singleton<PathSet>(storePath));
}


struct FilterFromExpr : PathFilter
{
    EvalState & state;
    Value & filter;

    FilterFromExpr(EvalState & state, Value & filter)
        : state(state), filter(filter)
    {
    }

    bool operator () (const Path & path)
    {
        struct stat st;
        if (lstat(path.c_str(), &st))
            throw SysError(format("getting attributes of path ‘%1%’") % path);

        /* Call the filter function.  The first argument is the path,
           the second is a string indicating the type of the file. */
        Value arg1;
        mkString(arg1, path);

        Value fun2;
        state.callFunction(filter, arg1, fun2, noPos);

        Value arg2;
        mkString(arg2,
            S_ISREG(st.st_mode) ? "regular" :
            S_ISDIR(st.st_mode) ? "directory" :
            S_ISLNK(st.st_mode) ? "symlink" :
            "unknown" /* not supported, will fail! */);

        Value res;
        state.callFunction(fun2, arg2, res, noPos);

        return state.forceBool(res);
    }
};


static void prim_filterSource(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    PathSet context;
    Path path = state.coerceToPath(pos, *args[1], context);
    if (!context.empty())
        throw EvalError(format("string ‘%1%’ cannot refer to other paths, at %2%") % path % pos);

    state.forceValue(*args[0]);
    if (args[0]->type != tLambda)
        throw TypeError(format("first argument in call to ‘filterSource’ is not a function but %1%, at %2%") % showType(*args[0]) % pos);

    FilterFromExpr filter(state, *args[0]);

    path = state.checkSourcePath(path);

    Path dstPath = settings.readOnlyMode
        ? computeStorePathForPath(path, true, htSHA256, filter).first
        : store->addToStore(baseNameOf(path), path, true, htSHA256, filter, state.repair);

    mkString(v, dstPath, singleton<PathSet>(dstPath));
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

    unsigned int n = 0;
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
        throw EvalError(format("attribute ‘%1%’ missing, at %2%") % attr % pos);
    // !!! add to stack trace?
    if (state.countCalls && i->pos) state.attrSelects[*i->pos]++;
    state.forceValue(*i->value);
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
    state.forceValue(*args[0]);
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
            throw TypeError(format("‘name’ attribute missing in a call to ‘listToAttrs’, at %1%") % pos);
        string name = state.forceStringNoCtx(*j->value, pos);

        Symbol sym = state.symbols.create(name);
        if (seen.find(sym) == seen.end()) {
            Bindings::iterator j2 = v2.attrs->find(state.symbols.create(state.sValue));
            if (j2 == v2.attrs->end())
                throw TypeError(format("‘value’ attribute missing in a call to ‘listToAttrs’, at %1%") % pos);

            v.attrs->push_back(Attr(sym, j2->value, j2->pos));
            seen.insert(sym);
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
    state.forceValue(*args[0]);
    if (args[0]->type != tLambda)
        throw TypeError(format("‘functionArgs’ requires a function, at %1%") % pos);

    if (!args[0]->lambda.fun->matchAttrs) {
        state.mkAttrs(v, 0);
        return;
    }

    state.mkAttrs(v, args[0]->lambda.fun->formals->formals.size());
    for (auto & i : args[0]->lambda.fun->formals->formals)
        // !!! should optimise booleans (allocate only once)
        mkBool(*state.allocAttr(v, i.name), i.def);
    v.attrs->sort();
}


/*************************************************************
 * Lists
 *************************************************************/


/* Determine whether the argument is a list. */
static void prim_isList(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceValue(*args[0]);
    mkBool(v, args[0]->isList());
}


static void elemAt(EvalState & state, const Pos & pos, Value & list, int n, Value & v)
{
    state.forceList(list, pos);
    if (n < 0 || (unsigned int) n >= list.listSize())
        throw Error(format("list index %1% is out of bounds, at %2%") % n % pos);
    state.forceValue(*list.listElems()[n]);
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
        throw Error(format("‘tail’ called on an empty list, at %1%") % pos);
    state.mkList(v, args[0]->listSize() - 1);
    for (unsigned int n = 0; n < v.listSize(); ++n)
        v.listElems()[n] = args[0]->listElems()[n + 1];
}


/* Apply a function to every element of a list. */
static void prim_map(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceFunction(*args[0], pos);
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
        if (state.forceBool(res))
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

    Value * vCur = args[1];

    if (args[2]->listSize())
        for (unsigned int n = 0; n < args[2]->listSize(); ++n) {
            Value vTmp;
            state.callFunction(*args[0], *vCur, vTmp, pos);
            vCur = n == args[2]->listSize() - 1 ? &v : state.allocValue();
            state.callFunction(vTmp, *args[2]->listElems()[n], *vCur, pos);
        }
    else
        v = *vCur;

    state.forceValue(v);
}


static void anyOrAll(bool any, EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceFunction(*args[0], pos);
    state.forceList(*args[1], pos);

    Value vTmp;
    for (unsigned int n = 0; n < args[1]->listSize(); ++n) {
        state.callFunction(*args[0], *args[1]->listElems()[n], vTmp, pos);
        bool res = state.forceBool(vTmp);
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
    state.forceFunction(*args[0], pos);
    auto len = state.forceInt(*args[1], pos);

    if (len < 0)
        throw EvalError(format("cannot create list of size %1%, at %2%") % len % pos);

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
        state.forceValue(*args[1]->listElems()[n]);
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
        return state.forceBool(vTmp2);
    };

    /* FIXME: std::sort can segfault if the comparator is not a strict
       weak ordering. What to do? std::stable_sort() seems more
       resilient, but no guarantees... */
    std::stable_sort(v.listElems(), v.listElems() + len, comparator);
}


/*************************************************************
 * Integer arithmetic
 *************************************************************/


static void prim_add(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    mkInt(v, state.forceInt(*args[0], pos) + state.forceInt(*args[1], pos));
}


static void prim_sub(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    mkInt(v, state.forceInt(*args[0], pos) - state.forceInt(*args[1], pos));
}


static void prim_mul(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    mkInt(v, state.forceInt(*args[0], pos) * state.forceInt(*args[1], pos));
}


static void prim_div(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    NixInt i2 = state.forceInt(*args[1], pos);
    if (i2 == 0) throw EvalError(format("division by zero, at %1%") % pos);
    mkInt(v, state.forceInt(*args[0], pos) / i2);
}


static void prim_lessThan(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    state.forceValue(*args[0]);
    state.forceValue(*args[1]);
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

    if (start < 0) throw EvalError(format("negative start position in ‘substring’, at %1%") % pos);

    mkString(v, (unsigned int) start >= s.size() ? "" : string(s, start, len), context);
}


static void prim_stringLength(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    PathSet context;
    string s = state.coerceToString(pos, *args[0], context);
    mkInt(v, s.size());
}


static void prim_unsafeDiscardStringContext(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    PathSet context;
    string s = state.coerceToString(pos, *args[0], context);
    mkString(v, s, PathSet());
}


/* Sometimes we want to pass a derivation path (i.e. pkg.drvPath) to a
   builder without causing the derivation to be built (for instance,
   in the derivation that builds NARs in nix-push, when doing
   source-only deployment).  This primop marks the string context so
   that builtins.derivation adds the path to drv.inputSrcs rather than
   drv.inputDrvs. */
static void prim_unsafeDiscardOutputDependency(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    PathSet context;
    string s = state.coerceToString(pos, *args[0], context);

    PathSet context2;
    for (auto & p : context)
        context2.insert(p.at(0) == '=' ? "~" + string(p, 1) : p);

    mkString(v, s, context2);
}


/* Return the cryptographic hash of a string in base-16. */
static void prim_hashString(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    string type = state.forceStringNoCtx(*args[0], pos);
    HashType ht = parseHashType(type);
    if (ht == htUnknown)
      throw Error(format("unknown hash type ‘%1%’, at %2%") % type % pos);

    PathSet context; // discarded
    string s = state.forceString(*args[1], context, pos);

    mkString(v, printHash(hashString(ht, s)), context);
}


/* Match a regular expression against a string and return either
   ‘null’ or a list containing substring matches. */
static void prim_match(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    Regex regex(state.forceStringNoCtx(*args[0], pos), true);

    PathSet context;
    string s = state.forceString(*args[1], context, pos);

    Regex::Subs subs;
    if (!regex.matches(s, subs)) {
        mkNull(v);
        return;
    }

    unsigned int len = subs.empty() ? 0 : subs.rbegin()->first + 1;
    state.mkList(v, len);
    for (unsigned int n = 0; n < len; ++n) {
        auto i = subs.find(n);
        if (i == subs.end())
            mkNull(*(v.listElems()[n] = state.allocValue()));
        else
            mkString(*(v.listElems()[n] = state.allocValue()), i->second);
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
        throw EvalError(format("‘from’ and ‘to’ arguments to ‘replaceStrings’ have different lengths, at %1%") % pos);

    Strings from;
    for (unsigned int n = 0; n < args[0]->listSize(); ++n)
        from.push_back(state.forceStringNoCtx(*args[0]->listElems()[n], pos));

    Strings to;
    for (unsigned int n = 0; n < args[1]->listSize(); ++n)
        to.push_back(state.forceStringNoCtx(*args[1]->listElems()[n], pos));

    PathSet context;
    auto s = state.forceString(*args[2], context, pos);

    string res;
    for (size_t p = 0; p < s.size(); ) {
        bool found = false;
        for (auto i = from.begin(), j = to.begin(); i != from.end(); ++i, ++j)
            if (s.compare(p, i->size(), *i) == 0) {
                found = true;
                p += i->size();
                res += *j;
                break;
            }
        if (!found) res += s[p++];
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


/*************************************************************
 * Networking
 *************************************************************/


void fetch(EvalState & state, const Pos & pos, Value * * args, Value & v,
    const string & who, bool unpack)
{
    if (state.restricted) throw Error(format("‘%1%’ is not allowed in restricted mode") % who);

    string url;
    string name;

    state.forceValue(*args[0]);

    if (args[0]->type == tAttrs) {

        state.forceAttrs(*args[0], pos);

        for (auto & attr : *args[0]->attrs) {
            string n(attr.name);
            if (n == "url")
                url = state.forceStringNoCtx(*attr.value, *attr.pos);
            else if (n == "name")
                name = state.forceStringNoCtx(*attr.value, *attr.pos);
            else
                throw EvalError(format("unsupported argument ‘%1%’ to ‘%2%’, at %3%") % attr.name % who % attr.pos);
        }

        if (url.empty())
            throw EvalError(format("‘url’ argument required, at %1%") % pos);

    } else
        url = state.forceStringNoCtx(*args[0], pos);

    Path res = downloadFileCached(url, unpack, name);
    mkString(v, res, PathSet({res}));
}


static void prim_fetchurl(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    fetch(state, pos, args, v, "fetchurl", false);
}


static void prim_fetchTarball(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    fetch(state, pos, args, v, "fetchTarball", true);
}


/*************************************************************
 * Primop registration
 *************************************************************/


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

    mkInt(v, time(0));
    addConstant("__currentTime", v);

    mkString(v, settings.thisSystem);
    addConstant("__currentSystem", v);

    mkString(v, nixVersion);
    addConstant("__nixVersion", v);

    mkString(v, settings.nixStore);
    addConstant("__storeDir", v);

    /* Language version.  This should be increased every time a new
       language feature gets added.  It's not necessary to increase it
       when primops get added, because you can just use `builtins ?
       primOp' to check. */
    mkInt(v, 3);
    addConstant("__langVersion", v);

    // Miscellaneous
    addPrimOp("scopedImport", 2, prim_scopedImport);
    Value * v2 = allocValue();
    mkAttrs(*v2, 0);
    mkApp(v, *baseEnv.values[baseEnvDispl - 1], *v2);
    forceValue(v);
    addConstant("import", v);
    if (settings.enableNativeCode) {
        addPrimOp("__importNative", 2, prim_importNative);
        addPrimOp("__exec", 1, prim_exec);
    }
    addPrimOp("__typeOf", 1, prim_typeOf);
    addPrimOp("isNull", 1, prim_isNull);
    addPrimOp("__isFunction", 1, prim_isFunction);
    addPrimOp("__isString", 1, prim_isString);
    addPrimOp("__isInt", 1, prim_isInt);
    addPrimOp("__isBool", 1, prim_isBool);
    addPrimOp("__genericClosure", 1, prim_genericClosure);
    addPrimOp("abort", 1, prim_abort);
    addPrimOp("throw", 1, prim_throw);
    addPrimOp("__addErrorContext", 2, prim_addErrorContext);
    addPrimOp("__tryEval", 1, prim_tryEval);
    addPrimOp("__getEnv", 1, prim_getEnv);

    // Strictness
    addPrimOp("__seq", 2, prim_seq);
    addPrimOp("__deepSeq", 2, prim_deepSeq);

    // Debugging
    addPrimOp("__trace", 2, prim_trace);
    addPrimOp("__valueSize", 1, prim_valueSize);

    // Paths
    addPrimOp("__toPath", 1, prim_toPath);
    addPrimOp("__storePath", 1, prim_storePath);
    addPrimOp("__pathExists", 1, prim_pathExists);
    addPrimOp("baseNameOf", 1, prim_baseNameOf);
    addPrimOp("dirOf", 1, prim_dirOf);
    addPrimOp("__readFile", 1, prim_readFile);
    addPrimOp("__readDir", 1, prim_readDir);
    addPrimOp("__findFile", 2, prim_findFile);

    // Creating files
    addPrimOp("__toXML", 1, prim_toXML);
    addPrimOp("__toJSON", 1, prim_toJSON);
    addPrimOp("__fromJSON", 1, prim_fromJSON);
    addPrimOp("__toFile", 2, prim_toFile);
    addPrimOp("__filterSource", 2, prim_filterSource);

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

    // Integer arithmetic
    addPrimOp("__add", 2, prim_add);
    addPrimOp("__sub", 2, prim_sub);
    addPrimOp("__mul", 2, prim_mul);
    addPrimOp("__div", 2, prim_div);
    addPrimOp("__lessThan", 2, prim_lessThan);

    // String manipulation
    addPrimOp("toString", 1, prim_toString);
    addPrimOp("__substring", 3, prim_substring);
    addPrimOp("__stringLength", 1, prim_stringLength);
    addPrimOp("__unsafeDiscardStringContext", 1, prim_unsafeDiscardStringContext);
    addPrimOp("__unsafeDiscardOutputDependency", 1, prim_unsafeDiscardOutputDependency);
    addPrimOp("__hashString", 2, prim_hashString);
    addPrimOp("__match", 2, prim_match);
    addPrimOp("__concatStringsSep", 2, prim_concatStringSep);
    addPrimOp("__replaceStrings", 3, prim_replaceStrings);

    // Versions
    addPrimOp("__parseDrvName", 1, prim_parseDrvName);
    addPrimOp("__compareVersions", 2, prim_compareVersions);

    // Derivations
    addPrimOp("derivationStrict", 1, prim_derivationStrict);

    // Networking
    addPrimOp("__fetchurl", 1, prim_fetchurl);
    addPrimOp("fetchTarball", 1, prim_fetchTarball);

    /* Add a wrapper around the derivation primop that computes the
       `drvPath' and `outPath' attributes lazily. */
    string path = settings.nixDataDir + "/nix/corepkgs/derivation.nix";
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

    /* Now that we've added all primops, sort the `builtins' set,
       because attribute lookups expect it to be sorted. */
    baseEnv.values[0]->attrs->sort();
}


}
