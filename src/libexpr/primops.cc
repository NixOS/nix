#include "primops.hh"
#include "normalise.hh"
#include "globals.hh"


Expr primImport(EvalState & state, Expr arg)
{
    ATMatcher m;
    string path;
    if (!(atMatch(m, arg) >> "Path" >> path))
        throw badTerm("path expected", arg);
    return evalFile(state, path);
}


static PathSet storeExprRootsCached(EvalState & state, const Path & nePath)
{
    DrvPaths::iterator i = state.drvPaths.find(nePath);
    if (i != state.drvPaths.end())
        return i->second;
    else {
        PathSet paths = storeExprRoots(nePath);
        state.drvPaths[nePath] = paths;
        return paths;
    }
}


static Hash hashDerivation(EvalState & state, StoreExpr ne)
{
    if (ne.type == StoreExpr::neDerivation) {
	PathSet inputs2;
        for (PathSet::iterator i = ne.derivation.inputs.begin();
             i != ne.derivation.inputs.end(); i++)
        {
            DrvHashes::iterator j = state.drvHashes.find(*i);
            if (j == state.drvHashes.end())
                throw Error(format("don't know expression `%1%'") % (string) *i);
            inputs2.insert(j->second);
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

    Hash drvHash = hashDerivation(state, ne);
    Path drvPath = writeTerm(unparseStoreExpr(ne), "");
    state.drvHashes[drvPath] = drvHash;

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

    ATMatcher m;
    string s;
    ATermList es;
    int n;
    Expr e1, e2;

    if (atMatch(m, e) >> "Str" >> s) ss.push_back(s);
    else if (atMatch(m, e) >> "Uri" >> s) ss.push_back(s);
    else if (atMatch(m, e) >> "Bool" >> "True") ss.push_back("1");
    else if (atMatch(m, e) >> "Bool" >> "False") ss.push_back("");

    else if (atMatch(m, e) >> "Int" >> n) {
        ostringstream st;
        st << n;
        ss.push_back(st.str());
    }

    else if (atMatch(m, e) >> "Attrs") {
        Expr a = queryAttr(e, "type");
        if (a && evalString(state, a) == "derivation") {
            a = queryAttr(e, "drvPath");
            if (!a) throw badTerm("derivation name missing", e);
            Path drvPath = evalPath(state, a);

            a = queryAttr(e, "drvHash");
            if (!a) throw badTerm("derivation hash missing", e);
            Hash drvHash = parseHash(evalString(state, a));

            state.drvHashes[drvPath] = drvHash;
            
            ss.push_back(addInput(state, drvPath, ne));
        } else
            throw badTerm("invalid derivation binding", e);
    }

    else if (atMatch(m, e) >> "Path" >> s) {
        Path drvPath = copyAtom(state, s);
        ss.push_back(addInput(state, drvPath, ne));
    }
    
    else if (atMatch(m, e) >> "List" >> es) {
        for (ATermIterator i(es); i; ++i) {
            startNest(nest, lvlVomit, format("processing list element"));
	    processBinding(state, evalExpr(state, *i), ne, ss);
        }
    }

    else if (atMatch(m, e) >> "Null") ss.push_back("");

    else if (atMatch(m, e) >> "SubPath" >> e1 >> e2) {
        Strings ss2;
        processBinding(state, evalExpr(state, e1), ne, ss2);
        if (ss2.size() != 1)
            throw Error("left-hand side of `~' operator cannot be a list");
        e2 = evalExpr(state, e2);
        if (!(atMatch(m, e2) >> "Str" >> s ||
             (atMatch(m, e2) >> "Path" >> s)))
            throw Error("right-hand side of `~' operator must be a path or string");
        ss.push_back(canonPath(ss2.front() + "/" + s));
    }
    
    else throw badTerm("invalid derivation binding", e);
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


Expr primDerivation(EvalState & state, Expr args)
{
    startNest(nest, lvlVomit, "evaluating derivation");

    ATermMap attrs;
    args = evalExpr(state, args);
    queryAllAttrs(args, attrs);

    /* Build the derivation expression by processing the attributes. */
    StoreExpr ne;
    ne.type = StoreExpr::neDerivation;

    string drvName;
    Path outPath;
    Hash outHash;
    bool outHashGiven = false;

    for (ATermIterator i(attrs.keys()); i; ++i) {
        string key = aterm2String(*i);
        Expr value = attrs.get(key);
        startNest(nest, lvlVomit, format("processing attribute `%1%'") % key);

        Strings ss;
        processBinding(state, value, ne, ss);

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
            else if (key == "outPath") outPath = s;
            else if (key == "id") { 
                outHash = parseHash(s);
                outHashGiven = true;
            }
        }
    }
    
    /* Do we have all required attributes? */
    if (ne.derivation.builder == "")
        throw badTerm("required attribute `builder' missing", args);
    if (ne.derivation.platform == "")
        throw badTerm("required attribute `system' missing", args);
    if (drvName == "")
        throw badTerm("required attribute `name' missing", args);
        
    /* Determine the output path. */
    if (!outHashGiven) outHash = hashDerivation(state, ne);
    if (outPath == "")
        /* Hash the Nix expression with no outputs to produce a
           unique but deterministic path name for this derivation. */
        outPath = canonPath(nixStore + "/" + 
            ((string) outHash).c_str() + "-" + drvName);
    ne.derivation.env["out"] = outPath;
    ne.derivation.outputs.insert(outPath);

    /* Write the resulting term into the Nix store directory. */
    Hash drvHash = outHashGiven
        ? hashString((string) outHash + outPath)
        : hashDerivation(state, ne);
    Path drvPath = writeTerm(unparseStoreExpr(ne), "-d-" + drvName);

    printMsg(lvlChatty, format("instantiated `%1%' -> `%2%'")
        % drvName % drvPath);

    attrs.set("outPath", ATmake("Path(<str>)", outPath.c_str()));
    attrs.set("drvPath", ATmake("Path(<str>)", drvPath.c_str()));
    attrs.set("drvHash", ATmake("Str(<str>)", ((string) drvHash).c_str()));
    attrs.set("type", ATmake("Str(\"derivation\")"));

    return makeAttrs(attrs);
}


Expr primBaseNameOf(EvalState & state, Expr arg)
{
    string s = evalString(state, arg);
    return ATmake("Str(<str>)", baseNameOf(s).c_str());
}


Expr primToString(EvalState & state, Expr arg)
{
    arg = evalExpr(state, arg);
    ATMatcher m;
    string s;
    if (atMatch(m, arg) >> "Str" >> s ||
        atMatch(m, arg) >> "Path" >> s ||
        atMatch(m, arg) >> "Uri" >> s)
        return ATmake("Str(<str>)", s.c_str());
    else throw badTerm("cannot coerce to string", arg);
}


Expr primTrue(EvalState & state)
{
    return ATmake("Bool(True)");
}


Expr primFalse(EvalState & state)
{
    return ATmake("Bool(False)");
}


Expr primNull(EvalState & state)
{
    return ATmake("Null");
}


Expr primIsNull(EvalState & state, Expr arg)
{
    arg = evalExpr(state, arg);
    ATMatcher m;
    return makeBool(atMatch(m, arg) >> "Null");
}
