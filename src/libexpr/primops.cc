#include "build.hh"
#include "eval.hh"
#include "globals.hh"
#include "nixexpr-ast.hh"


/* Load and evaluate an expression from path specified by the
   argument. */ 
static Expr primImport(EvalState & state, const ATermVector & args)
{
    ATermList es;
    Path path;

    Expr arg = evalExpr(state, args[0]), arg2;
    
    if (matchPath(arg, arg2))
        path = aterm2String(arg2);

    else if (matchAttrs(arg, es)) {
        Expr a = queryAttr(arg, "type");

        /* If it is a derivation, we have to realise it and load the
           Nix expression created at the derivation's output path. */
        if (a && evalString(state, a) == "derivation") {
            a = queryAttr(arg, "drvPath");
            if (!a) throw Error("bad derivation in import");
            Path drvPath = evalPath(state, a);

            buildDerivations(singleton<PathSet>(drvPath));
 
            a = queryAttr(arg, "outPath");
            if (!a) throw Error("bad derivation in import");
            path = evalPath(state, a);
        }
    }

    if (path == "")
        throw Error("path or derivation expected in import");
    
    return evalFile(state, path);
}


/* Returns the hash of a derivation modulo fixed-output
   subderivations.  A fixed-output derivation is a derivation with one
   output (`out') for which an expected hash and hash algorithm are
   specified (using the `outputHash' and `outputHashAlgo'
   attributes).  We don't want changes to such derivations to
   propagate upwards through the dependency graph, changing output
   paths everywhere.

   For instance, if we change the url in a call to the `fetchurl'
   function, we do not want to rebuild everything depending on it
   (after all, (the hash of) the file being downloaded is unchanged).
   So the *output paths* should not change.  On the other hand, the
   *derivation store expression paths* should change to reflect the
   new dependency graph.

   That's what this function does: it returns a hash which is just the
   of the derivation ATerm, except that any input store expression
   paths have been replaced by the result of a recursive call to this
   function, and that for fixed-output derivations we return
   (basically) its outputHash. */
static Hash hashDerivationModulo(EvalState & state, Derivation drv)
{
    /* Return a fixed hash for fixed-output derivations. */
    if (drv.outputs.size() == 1) {
        DerivationOutputs::const_iterator i = drv.outputs.begin();
        if (i->first == "out" &&
            i->second.hash != "")
        {
            return hashString(htSHA256, "fixed:out:"
                + i->second.hashAlgo + ":"
                + i->second.hash + ":"
                + i->second.path);
        }
    }

    /* For other derivations, replace the inputs paths with recursive
       calls to this function.*/
    DerivationInputs inputs2;
    for (DerivationInputs::iterator i = drv.inputDrvs.begin();
         i != drv.inputDrvs.end(); ++i)
    {
        Hash h = state.drvHashes[i->first];
        if (h.type == htUnknown) {
            Derivation drv2 = derivationFromPath(i->first);
            h = hashDerivationModulo(state, drv2);
            state.drvHashes[i->first] = h;
        }
        inputs2[printHash(h)] = i->second;
    }
    drv.inputDrvs = inputs2;
    
    return hashTerm(unparseDerivation(drv));
}


static void processBinding(EvalState & state, Expr e, Derivation & drv,
    Strings & ss)
{
    e = evalExpr(state, e);

    ATerm s;
    ATermList es;
    int n;
    Expr e1, e2;

    if (matchStr(e, s)) ss.push_back(aterm2String(s));
    else if (matchUri(e, s)) ss.push_back(aterm2String(s));
    else if (e == eTrue) ss.push_back("1");
    else if (e == eFalse) ss.push_back("");

    else if (matchInt(e, n)) {
        ostringstream st;
        st << n;
        ss.push_back(st.str());
    }

    else if (matchAttrs(e, es)) {
        Expr a = queryAttr(e, "type");
        
        if (a && evalString(state, a) == "derivation") {
            a = queryAttr(e, "drvPath");
            if (!a) throw Error("derivation name missing");
            Path drvPath = evalPath(state, a);

            a = queryAttr(e, "outPath");
            if (!a) throw Error("output path missing");
            /* !!! supports only single output path */
            Path outPath = evalPath(state, a);

            drv.inputDrvs[drvPath] = singleton<StringSet>("out");
            ss.push_back(outPath);
        }

        else if (a && evalString(state, a) == "storePath") {

            a = queryAttr(e, "outPath");
            if (!a) throw Error("output path missing");
            /* !!! supports only single output path */
            Path outPath = evalPath(state, a);

            drv.inputSrcs.insert(outPath);
            ss.push_back(outPath);
        }

        else throw Error("invalid derivation attribute");
    }

    else if (matchPath(e, s)) {
        Path srcPath(canonPath(aterm2String(s)));

        if (isStorePath(srcPath)) {
            printMsg(lvlChatty, format("using store path `%1%' as source")
                % srcPath);
            drv.inputSrcs.insert(srcPath);
            ss.push_back(srcPath);
        }

        else {
            if (isDerivation(srcPath))
                throw Error(format("file names are not allowed to end in `%1%'")
                    % drvExtension);
            Path dstPath(addToStore(srcPath));
            printMsg(lvlChatty, format("copied source `%1%' -> `%2%'")
                % srcPath % dstPath);
            drv.inputSrcs.insert(dstPath);
            ss.push_back(dstPath);
        }
    }
    
    else if (matchList(e, es)) {
        for (ATermIterator i(es); i; ++i) {
            startNest(nest, lvlVomit, format("processing list element"));
	    processBinding(state, evalExpr(state, *i), drv, ss);
        }
    }

    else if (matchNull(e)) ss.push_back("");

    else if (matchSubPath(e, e1, e2)) {
        Strings ss2;
        processBinding(state, evalExpr(state, e1), drv, ss2);
        if (ss2.size() != 1)
            throw Error("left-hand side of `~' operator cannot be a list");
        e2 = evalExpr(state, e2);
        if (!(matchStr(e2, s) || matchPath(e2, s)))
            throw Error("right-hand side of `~' operator must be a path or string");
        ss.push_back(canonPath(ss2.front() + "/" + aterm2String(s)));
    }
    
    else throw Error("invalid derivation attribute");
}


static string concatStrings(const Strings & ss)
{
    string s;
    bool first = true;
    for (Strings::const_iterator i = ss.begin(); i != ss.end(); ++i) {
        if (!first) s += " "; else first = false;
        s += *i;
    }
    return s;
}


/* Construct (as a unobservable side effect) a Nix derivation
   expression that performs the derivation described by the argument
   set.  Returns the original set extended with the following
   attributes: `outPath' containing the primary output path of the
   derivation; `drvPath' containing the path of the Nix expression;
   and `type' set to `derivation' to indicate that this is a
   derivation. */
static Expr primDerivationStrict(EvalState & state, const ATermVector & args)
{
    startNest(nest, lvlVomit, "evaluating derivation");

    ATermMap attrs;
    queryAllAttrs(evalExpr(state, args[0]), attrs, true);

    /* Build the derivation expression by processing the attributes. */
    Derivation drv;

    string drvName;
    
    string outputHash;
    string outputHashAlgo;
    bool outputHashRecursive = false;

    for (ATermIterator i(attrs.keys()); i; ++i) {
        string key = aterm2String(*i);
        ATerm value;
        Expr pos;
        ATerm rhs = attrs.get(key);
        if (!matchAttrRHS(rhs, value, pos)) abort();
        startNest(nest, lvlVomit, format("processing attribute `%1%'") % key);

        Strings ss;
        try {
            processBinding(state, value, drv, ss);
        } catch (Error & e) {
            throw Error(format("while processing the derivation attribute `%1%' at %2%:\n%3%")
                % key % showPos(pos) % e.msg());
        }

        /* The `args' attribute is special: it supplies the
           command-line arguments to the builder. */
        if (key == "args") {
            for (Strings::iterator i = ss.begin(); i != ss.end(); ++i)
                drv.args.push_back(*i);
        }

        /* All other attributes are passed to the builder through the
           environment. */
        else {
            string s = concatStrings(ss);
            drv.env[key] = s;
            if (key == "builder") drv.builder = s;
            else if (key == "system") drv.platform = s;
            else if (key == "name") drvName = s;
            else if (key == "outputHash") outputHash = s;
            else if (key == "outputHashAlgo") outputHashAlgo = s;
            else if (key == "outputHashMode") {
                if (s == "recursive") outputHashRecursive = true; 
                else if (s == "flat") outputHashRecursive = false;
                else throw Error(format("invalid value `%1%' for `outputHashMode' attribute") % s);
            }
        }
    }
    
    /* Do we have all required attributes? */
    if (drv.builder == "")
        throw Error("required attribute `builder' missing");
    if (drv.platform == "")
        throw Error("required attribute `system' missing");
    if (drvName == "")
        throw Error("required attribute `name' missing");

    /* If an output hash was given, check it. */
    if (outputHash == "")
        outputHashAlgo = "";
    else {
        HashType ht = parseHashType(outputHashAlgo);
        if (ht == htUnknown)
            throw Error(format("unknown hash algorithm `%1%'") % outputHashAlgo);
        Hash h;
        if (outputHash.size() == Hash(ht).hashSize * 2)
            /* hexadecimal representation */
            h = parseHash(ht, outputHash);
        else
            /* base-32 representation */
            h = parseHash32(ht, outputHash);
        string s = outputHash;
        outputHash = printHash(h);
        if (outputHashRecursive) outputHashAlgo = "r:" + outputHashAlgo;
    }

    /* Check the derivation name.  It shouldn't contain whitespace,
       but we are conservative here: we check whether only
       alphanumerics and some other characters appear. */
    checkStoreName(drvName);
    if (isDerivation(drvName))
        throw Error(format("derivation names are not allowed to end in `%1%'")
            % drvExtension);

    /* !!! the name should not end in the derivation extension (.drv).
       Likewise for sources. */

    /* Construct the "masked" derivation store expression, which is
       the final one except that in the list of outputs, the output
       paths are empty, and the corresponding environment variables
       have an empty value.  This ensures that changes in the set of
       output names do get reflected in the hash. */
    drv.env["out"] = "";
    drv.outputs["out"] =
        DerivationOutput("", outputHashAlgo, outputHash);
        
    /* Use the masked derivation expression to compute the output
       path. */
    Path outPath = makeStorePath("output:out",
        hashDerivationModulo(state, drv), drvName);

    /* Construct the final derivation store expression. */
    drv.env["out"] = outPath;
    drv.outputs["out"] =
        DerivationOutput(outPath, outputHashAlgo, outputHash);

    /* Write the resulting term into the Nix store directory. */
    Path drvPath = writeDerivation(drv, drvName);

    printMsg(lvlChatty, format("instantiated `%1%' -> `%2%'")
        % drvName % drvPath);

    /* Optimisation, but required in read-only mode! because in that
       case we don't actually write store expressions, so we can't
       read them later. */
    state.drvHashes[drvPath] = hashDerivationModulo(state, drv);

    /* !!! assumes a single output */
    ATermMap outAttrs;
    outAttrs.set("outPath", makeAttrRHS(makePath(toATerm(outPath)), makeNoPos()));
    outAttrs.set("drvPath", makeAttrRHS(makePath(toATerm(drvPath)), makeNoPos()));

    return makeAttrs(outAttrs);
}


static Expr primDerivationLazy(EvalState & state, const ATermVector & args)
{
    Expr eAttrs = evalExpr(state, args[0]);
    ATermMap attrs;
    queryAllAttrs(eAttrs, attrs, true);

    attrs.set("type", makeAttrRHS(makeStr(toATerm("derivation")), makeNoPos()));

    Expr drvStrict = makeCall(makeVar(toATerm("derivation!")), eAttrs);

    attrs.set("outPath", makeAttrRHS(makeSelect(drvStrict, toATerm("outPath")), makeNoPos()));
    attrs.set("drvPath", makeAttrRHS(makeSelect(drvStrict, toATerm("drvPath")), makeNoPos()));
    
    return makeAttrs(attrs);
}


/* Return the base name of the given string, i.e., everything
   following the last slash. */
static Expr primBaseNameOf(EvalState & state, const ATermVector & args)
{
    return makeStr(toATerm(baseNameOf(evalString(state, args[0]))));
}


/* Convert the argument (which can be a path or a uri) to a string. */
static Expr primToString(EvalState & state, const ATermVector & args)
{
    Expr arg = evalExpr(state, args[0]);
    ATerm s;
    if (matchStr(arg, s) || matchPath(arg, s) || matchUri(arg, s))
        return makeStr(s);
    throw Error("cannot coerce value to string");
}


/* Boolean constructors. */
static Expr primTrue(EvalState & state, const ATermVector & args)
{
    return eTrue;
}


static Expr primFalse(EvalState & state, const ATermVector & args)
{
    return eFalse;
}


/* Return the null value. */
static Expr primNull(EvalState & state, const ATermVector & args)
{
    return makeNull();
}


/* Determine whether the argument is the null value. */
static Expr primIsNull(EvalState & state, const ATermVector & args)
{
    return makeBool(matchNull(evalExpr(state, args[0])));
}


/* Apply a function to every element of a list. */
static Expr primMap(EvalState & state, const ATermVector & args)
{
    Expr fun = evalExpr(state, args[0]);
    ATermList list = evalList(state, args[1]);

    ATermList res = ATempty;
    for (ATermIterator i(list); i; ++i)
        res = ATinsert(res, makeCall(fun, *i));

    return makeList(ATreverse(res));
}


/* Return a string constant representing the current platform.  Note!
   that differs between platforms, so Nix expressions using
   `__currentSystem' can evaluate to different values on different
   platforms. */
static Expr primCurrentSystem(EvalState & state, const ATermVector & args)
{
    return makeStr(toATerm(thisSystem));
}


static Expr primCurrentTime(EvalState & state, const ATermVector & args)
{
    return ATmake("Int(<int>)", time(0));
}


static Expr primRemoveAttrs(EvalState & state, const ATermVector & args)
{
    ATermMap attrs;
    queryAllAttrs(evalExpr(state, args[0]), attrs, true);
    
    ATermList list = evalList(state, args[1]);

    for (ATermIterator i(list); i; ++i)
        /* It's not an error for *i not to exist. */
        attrs.remove(evalString(state, *i));

    return makeAttrs(attrs);
}


void EvalState::addPrimOps()
{
    addPrimOp("true", 0, primTrue);
    addPrimOp("false", 0, primFalse);
    addPrimOp("null", 0, primNull);
    addPrimOp("__currentSystem", 0, primCurrentSystem);
    addPrimOp("__currentTime", 0, primCurrentTime);

    addPrimOp("import", 1, primImport);
    addPrimOp("derivation!", 1, primDerivationStrict);
    addPrimOp("derivation", 1, primDerivationLazy);
    addPrimOp("baseNameOf", 1, primBaseNameOf);
    addPrimOp("toString", 1, primToString);
    addPrimOp("isNull", 1, primIsNull);

    addPrimOp("map", 2, primMap);
    addPrimOp("removeAttrs", 2, primRemoveAttrs);
}


