#include "normalise.hh"
#include "eval.hh"
#include "globals.hh"
#include "nixexpr-ast.hh"


/* Load and evaluate an expression from path specified by the
   argument. */ 
static Expr primImport(EvalState & state, const ATermVector & args)
{
    ATerm path;
    Expr fn = evalExpr(state, args[0]);
    if (!matchPath(fn, path))
        throw Error("path expected");
    return evalFile(state, aterm2String(path));
}


static PathSet storeExprRootsCached(EvalState & state, const Path & nePath)
{
    DrvRoots::iterator i = state.drvRoots.find(nePath);
    if (i != state.drvRoots.end())
        return i->second;
    else {
        PathSet paths = storeExprRoots(nePath);
        state.drvRoots[nePath] = paths;
        return paths;
    }
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
static Hash hashDerivationModulo(EvalState & state, StoreExpr ne)
{
    if (ne.type == StoreExpr::neDerivation) {

        /* Return a fixed hash for fixed-output derivations. */
        if (ne.derivation.outputs.size() == 1) {
            DerivationOutputs::iterator i = ne.derivation.outputs.begin();
            if (i->first == "out" &&
                i->second.hash != "")
            {
                return hashString(htSHA256, "fixed:out:"
                    + i->second.hashAlgo + ":"
                    + i->second.hash + ":"
                    + i->second.path);
            }
        }

        /* For other derivations, replace the inputs paths with
           recursive calls to this function.*/
	PathSet inputs2;
        for (PathSet::iterator i = ne.derivation.inputs.begin();
             i != ne.derivation.inputs.end(); ++i)
        {
            Hash h = state.drvHashes[*i];
            if (h.type == htUnknown) {
                StoreExpr ne2 = storeExprFromPath(*i);
                h = hashDerivationModulo(state, ne2);
                state.drvHashes[*i] = h;
            }
            inputs2.insert(printHash(h));
        }
	ne.derivation.inputs = inputs2;
    }
    
    return hashTerm(unparseStoreExpr(ne));
}


static Path copyAtom(EvalState & state, const Path & srcPath)
{
    /* !!! should be cached */
    Path dstPath(addToStore(srcPath));

    ClosureElem elem;
    StoreExpr ne;
    ne.type = StoreExpr::neClosure;
    ne.closure.roots.insert(dstPath);
    ne.closure.elems[dstPath] = elem;

    Path drvPath = writeTerm(unparseStoreExpr(ne), "c");

    state.drvRoots[drvPath] = ne.closure.roots;

    printMsg(lvlChatty, format("copied `%1%' -> closure `%2%'")
        % srcPath % drvPath);
    return drvPath;
}


static string addInput(EvalState & state, 
    Path & nePath, StoreExpr & ne)
{
    PathSet paths = storeExprRootsCached(state, nePath);
    if (paths.size() != 1) abort();
    Path path = *(paths.begin());
    ne.derivation.inputs.insert(nePath);
    return path;
}


static void processBinding(EvalState & state, Expr e, StoreExpr & ne,
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
            PathSet drvRoots;
            drvRoots.insert(evalPath(state, a));
            
            state.drvRoots[drvPath] = drvRoots;

            ss.push_back(addInput(state, drvPath, ne));
        } else
            throw Error("invalid derivation attribute");
    }

    else if (matchPath(e, s)) {
        Path drvPath = copyAtom(state, aterm2String(s));
        ss.push_back(addInput(state, drvPath, ne));
    }
    
    else if (matchList(e, es)) {
        for (ATermIterator i(es); i; ++i) {
            startNest(nest, lvlVomit, format("processing list element"));
	    processBinding(state, evalExpr(state, *i), ne, ss);
        }
    }

    else if (matchNull(e)) ss.push_back("");

    else if (matchSubPath(e, e1, e2)) {
        Strings ss2;
        processBinding(state, evalExpr(state, e1), ne, ss2);
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
static Expr primDerivation(EvalState & state, const ATermVector & _args)
{
    startNest(nest, lvlVomit, "evaluating derivation");

    ATermMap attrs;
    Expr args = _args[0];
    args = evalExpr(state, args);
    queryAllAttrs(args, attrs, true);

    /* Build the derivation expression by processing the attributes. */
    StoreExpr ne;
    ne.type = StoreExpr::neDerivation;

    string drvName;
    
    string outputHash;
    string outputHashAlgo;

    for (ATermIterator i(attrs.keys()); i; ++i) {
        string key = aterm2String(*i);
        ATerm value;
        Expr pos;
        ATerm rhs = attrs.get(key);
        if (!matchAttrRHS(rhs, value, pos)) abort();
        startNest(nest, lvlVomit, format("processing attribute `%1%'") % key);

        Strings ss;
        try {
            processBinding(state, value, ne, ss);
        } catch (Error & e) {
            throw Error(format("while processing the derivation attribute `%1%' at %2%:\n%3%")
                % key % showPos(pos) % e.msg());
        }

        /* The `args' attribute is special: it supplies the
           command-line arguments to the builder. */
        if (key == "args") {
            for (Strings::iterator i = ss.begin(); i != ss.end(); ++i)
                ne.derivation.args.push_back(*i);
        }

        /* All other attributes are passed to the builder through the
           environment. */
        else {
            string s = concatStrings(ss);
            ne.derivation.env[key] = s;
            if (key == "builder") ne.derivation.builder = s;
            else if (key == "system") ne.derivation.platform = s;
            else if (key == "name") drvName = s;
            else if (key == "outputHash") outputHash = s;
            else if (key == "outputHashAlgo") outputHashAlgo = s;
        }
    }
    
    /* Do we have all required attributes? */
    if (ne.derivation.builder == "")
        throw Error("required attribute `builder' missing");
    if (ne.derivation.platform == "")
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
    }

    /* Check the derivation name.  It shouldn't contain whitespace,
       but we are conservative here: we check whether only
       alphanumerics and some other characters appear. */
    string validChars = "+-._?=";
    for (string::iterator i = drvName.begin(); i != drvName.end(); ++i)
        if (!((*i >= 'A' && *i <= 'Z') ||
              (*i >= 'a' && *i <= 'z') ||
              (*i >= '0' && *i <= '9') ||
              validChars.find(*i) != string::npos))
        {
            throw Error(format("invalid character `%1%' in derivation name `%2%'")
                % *i % drvName);
        }

    /* Construct the "masked" derivation store expression, which is
       the final one except that in the list of outputs, the output
       paths are empty, and the corresponding environment variables
       have an empty value.  This ensures that changes in the set of
       output names do get reflected in the hash. */
    ne.derivation.env["out"] = "";
    ne.derivation.outputs["out"] =
        DerivationOutput("", outputHashAlgo, outputHash);
        
    /* Use the masked derivation expression to compute the output
       path. */
    Path outPath = makeStorePath("output:out",
        hashDerivationModulo(state, ne), drvName);

    /* Construct the final derivation store expression. */
    ne.derivation.env["out"] = outPath;
    ne.derivation.outputs["out"] =
        DerivationOutput(outPath, outputHashAlgo, outputHash);

    /* Write the resulting term into the Nix store directory. */
    Path drvPath = writeTerm(unparseStoreExpr(ne), "d-" + drvName);

    printMsg(lvlChatty, format("instantiated `%1%' -> `%2%'")
        % drvName % drvPath);

    /* !!! assumes a single output */
    attrs.set("outPath", makeAttrRHS(makePath(toATerm(outPath)), makeNoPos()));
    attrs.set("drvPath", makeAttrRHS(makePath(toATerm(drvPath)), makeNoPos()));
    attrs.set("type", makeAttrRHS(makeStr(toATerm("derivation")), makeNoPos()));

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
    else throw Error("cannot coerce value to string");
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
Expr primNull(EvalState & state, const ATermVector & args)
{
    return makeNull();
}


/* Determine whether the argument is the null value. */
Expr primIsNull(EvalState & state, const ATermVector & args)
{
    return makeBool(matchNull(evalExpr(state, args[0])));
}


/* Apply a function to every element of a list. */
Expr primMap(EvalState & state, const ATermVector & args)
{
    Expr fun = evalExpr(state, args[0]);
    Expr list = evalExpr(state, args[1]);

    ATermList list2;
    if (!matchList(list, list2))
        throw Error("`map' expects a list as its second argument");

    ATermList list3 = ATempty;
    for (ATermIterator i(list2); i; ++i)
        list3 = ATinsert(list3, makeCall(fun, *i));

    return makeList(ATreverse(list3));
}


void EvalState::addPrimOps()
{
    addPrimOp("true", 0, primTrue);
    addPrimOp("false", 0, primFalse);
    addPrimOp("null", 0, primNull);

    addPrimOp("import", 1, primImport);
    addPrimOp("derivation", 1, primDerivation);
    addPrimOp("baseNameOf", 1, primBaseNameOf);
    addPrimOp("toString", 1, primToString);
    addPrimOp("isNull", 1, primIsNull);

    addPrimOp("map", 2, primMap);
}
