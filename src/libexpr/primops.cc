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


static string processBinding(EvalState & state, Expr e, StoreExpr & ne)
{
    e = evalExpr(state, e);

    ATMatcher m;
    string s;
    ATermList es;

    if (atMatch(m, e) >> "Str" >> s) return s;
    if (atMatch(m, e) >> "Uri" >> s) return s;
    if (atMatch(m, e) >> "Bool" >> "True") return "1";
    if (atMatch(m, e) >> "Bool" >> "False") return "";

    if (atMatch(m, e) >> "Attrs" >> es) {
        Expr a = queryAttr(e, "type");
        if (a && evalString(state, a) == "derivation") {
            a = queryAttr(e, "drvPath");
            if (!a) throw badTerm("derivation name missing", e);
            Path drvPath = evalPath(state, a);

            a = queryAttr(e, "drvHash");
            if (!a) throw badTerm("derivation hash missing", e);
            Hash drvHash = parseHash(evalString(state, a));

            state.drvHashes[drvPath] = drvHash;
            
            return addInput(state, drvPath, ne);
        }
    }

    if (atMatch(m, e) >> "Path" >> s) {
        Path drvPath = copyAtom(state, s);
        return addInput(state, drvPath, ne);
    }
    
    if (atMatch(m, e) >> "List" >> es) {
	string s;
	bool first = true;
        for (ATermIterator i(es); i; ++i) {
            startNest(nest, lvlVomit, format("processing list element"));
	    if (!first) s = s + " "; else first = false;
	    s += processBinding(state, evalExpr(state, *i), ne);
        }
	return s;
    }

    if (atMatch(m, e) >> "Null") return "";
    
    throw badTerm("invalid derivation binding", e);
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

        /* The `args' attribute is special: it supplies the
           command-line arguments to the builder. */
        if (key == "args") {
            throw Error("args not implemented");
#if 0
            ATermList args;
            if (!(ATmatch(value, "[<list>]", &args))
                throw badTerm("list expected", value);
            while (!ATisEmpty(args)) {
                Expr arg = evalExpr(state, ATgetFirst(args));
                ne.derivation.args.push_back(processBinding(state, arg, ne));
                args = ATgetNext(args);
            }
#endif
        }

        /* All other attributes are passed to the builder through the
           environment. */
        else {
            string s = processBinding(state, value, ne);
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
