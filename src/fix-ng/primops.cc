#include "primops.hh"
#include "normalise.hh"
#include "globals.hh"


Expr primImport(EvalState & state, Expr arg)
{
    char * path;
    if (!ATmatch(arg, "Path(<str>)", &path))
        throw badTerm("path expected", arg);
    return evalFile(state, path);
}


static PathSet nixExprRootsCached(EvalState & state, const Path & nePath)
{
    DrvPaths::iterator i = state.drvPaths.find(nePath);
    if (i != state.drvPaths.end())
        return i->second;
    else {
        PathSet paths = nixExprRoots(nePath);
        state.drvPaths[nePath] = paths;
        return paths;
    }
}


static Hash hashDerivation(EvalState & state, NixExpr ne)
{
    if (ne.type == NixExpr::neDerivation) {
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
    return hashTerm(unparseNixExpr(ne));
}


static Path copyAtom(EvalState & state, const Path & srcPath)
{
    /* !!! should be cached */
    Path dstPath(addToStore(srcPath));

    ClosureElem elem;
    NixExpr ne;
    ne.type = NixExpr::neClosure;
    ne.closure.roots.insert(dstPath);
    ne.closure.elems[dstPath] = elem;

    Hash drvHash = hashDerivation(state, ne);
    Path drvPath = writeTerm(unparseNixExpr(ne), "");
    state.drvHashes[drvPath] = drvHash;

    msg(lvlChatty, format("copied `%1%' -> closure `%2%'")
        % srcPath % drvPath);
    return drvPath;
}


static string addInput(EvalState & state, 
    Path & nePath, NixExpr & ne)
{
    PathSet paths = nixExprRootsCached(state, nePath);
    if (paths.size() != 1) abort();
    Path path = *(paths.begin());
    ne.derivation.inputs.insert(nePath);
    return path;
}


static string processBinding(EvalState & state, Expr e, NixExpr & ne)
{
    e = evalExpr(state, e);

    char * s;
    ATermList es;

    if (ATmatch(e, "Str(<str>)", &s)) return s;
    if (ATmatch(e, "Uri(<str>)", &s)) return s;
    if (ATmatch(e, "Bool(True)")) return "1";
    if (ATmatch(e, "Bool(False)")) return "";

    if (ATmatch(e, "Attrs([<list>])", &es)) {
        Expr a = queryAttr(e, "type");
        if (a && evalString(state, a) == "derivation") {
            a = queryAttr(e, "drvPath");
            if (a) {
                Path drvPath = evalPath(state, a);
                return addInput(state, drvPath, ne);
            }
        }
    }

    if (ATmatch(e, "Path(<str>)", &s)) {
        Path drvPath = copyAtom(state, s);
        return addInput(state, drvPath, ne);
    }
    
    if (ATmatch(e, "List([<list>])", &es)) {
	string s;
	bool first = true;
        while (!ATisEmpty(es)) {
            Nest nest(lvlVomit, format("processing list element"));
	    if (!first) s = s + " "; else first = false;
	    s += processBinding(state, evalExpr(state, ATgetFirst(es)), ne);
            es = ATgetNext(es);
        }
	return s;
    }
    
    throw badTerm("invalid derivation binding", e);
}


Expr primDerivation(EvalState & state, Expr args)
{
    Nest nest(lvlVomit, "evaluating derivation");

    ATermMap attrs;
    args = evalExpr(state, args);
    queryAllAttrs(args, attrs);

    /* Build the derivation expression by processing the attributes. */
    NixExpr ne;
    ne.type = NixExpr::neDerivation;

    string drvName;
    Path outPath;
    Hash outHash;
    bool outHashGiven = false;

    for (ATermList keys = attrs.keys(); !ATisEmpty(keys); 
         keys = ATgetNext(keys))
    {
        string key = aterm2String(ATgetFirst(keys));
        Expr value = attrs.get(key);
        Nest nest(lvlVomit, format("processing attribute `%1%'") % key);

        /* The `args' attribute is special: it supplies the
           command-line arguments to the builder. */
        if (key == "args") {
            ATermList args;
            if (!ATmatch(value, "[<list>]", &args))
                throw badTerm("list expected", value);
            while (!ATisEmpty(args)) {
                Expr arg = evalExpr(state, ATgetFirst(args));
                ne.derivation.args.push_back(processBinding(state, arg, ne));
                args = ATgetNext(args);
            }
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
    Path drvPath = writeTerm(unparseNixExpr(ne), "-d-" + drvName);
    state.drvHashes[drvPath] = drvHash;

    msg(lvlChatty, format("instantiated `%1%' -> `%2%'")
        % drvName % drvPath);

    attrs.set("outPath", ATmake("Path(<str>)", outPath.c_str()));
    attrs.set("drvPath", ATmake("Path(<str>)", drvPath.c_str()));
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
    char * s;
    if (ATmatch(arg, "Str(<str>)", &s) ||
        ATmatch(arg, "Path(<str>)", &s) ||
        ATmatch(arg, "Uri(<str>)", &s))
        return ATmake("Str(<str>)", s);
    else throw badTerm("cannot coerce to string", arg);
}
