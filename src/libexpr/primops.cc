#include "normalise.hh"
#include "eval.hh"
#include "globals.hh"


/* Load and evaluate an expression from path specified by the
   argument. */ 
static Expr primImport(EvalState & state, const ATermVector & args)
{
    ATMatcher m;
    string path;
    if (!(atMatch(m, args[0]) >> "Path" >> path))
        throw Error("path expected");
    return evalFile(state, path);
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
            if (!a) throw Error("derivation name missing");
            Path drvPath = evalPath(state, a);

            a = queryAttr(e, "drvHash");
            if (!a) throw Error("derivation hash missing");
            Hash drvHash = parseHash(evalString(state, a));

            a = queryAttr(e, "outPath");
            if (!a) throw Error("output path missing");
            PathSet drvRoots;
            drvRoots.insert(evalPath(state, a));
            
            state.drvHashes[drvPath] = drvHash;
            state.drvRoots[drvPath] = drvRoots;

            ss.push_back(addInput(state, drvPath, ne));
        } else
            throw Error("invalid derivation attribute");
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
    Hash outHash;
    bool outHashGiven = false;

    for (ATermIterator i(attrs.keys()); i; ++i) {
        string key = aterm2String(*i);
        ATerm value;
        Expr pos;
        ATerm rhs = attrs.get(key);
        ATMatcher m;
        if (!(atMatch(m, rhs) >> "" >> value >> pos)) abort();
        startNest(nest, lvlVomit, format("processing attribute `%1%'") % key);

        Strings ss;
        try {
            processBinding(state, value, ne, ss);
        } catch (Error & e) {
            throw Error(format("while processing derivation attribute `%1%' at %2%:\n%3%")
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
            else if (key == "id") { 
                outHash = parseHash(s);
                outHashGiven = true;
            }
        }
    }
    
    /* Do we have all required attributes? */
    if (ne.derivation.builder == "")
        throw Error("required attribute `builder' missing");
    if (ne.derivation.platform == "")
        throw Error("required attribute `system' missing");
    if (drvName == "")
        throw Error("required attribute `name' missing");

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
        
    /* Determine the output path by hashing the Nix expression with no
       outputs to produce a unique but deterministic path name for
       this derivation. */
    if (!outHashGiven) outHash = hashDerivation(state, ne);
    Path outPath = canonPath(nixStore + "/" + 
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

    attrs.set("outPath", ATmake("(Path(<str>), NoPos)", outPath.c_str()));
    attrs.set("drvPath", ATmake("(Path(<str>), NoPos)", drvPath.c_str()));
    attrs.set("drvHash", ATmake("(Str(<str>), NoPos)", ((string) drvHash).c_str()));
    attrs.set("type", ATmake("(Str(\"derivation\"), NoPos)"));

    return makeAttrs(attrs);
}


/* Return the base name of the given string, i.e., everything
   following the last slash. */
static Expr primBaseNameOf(EvalState & state, const ATermVector & args)
{
    string s = evalString(state, args[0]);
    return ATmake("Str(<str>)", baseNameOf(s).c_str());
}


/* Convert the argument (which can be a path or a uri) to a string. */
static Expr primToString(EvalState & state, const ATermVector & args)
{
    Expr arg = evalExpr(state, args[0]);
    ATMatcher m;
    string s;
    if (atMatch(m, arg) >> "Str" >> s ||
        atMatch(m, arg) >> "Path" >> s ||
        atMatch(m, arg) >> "Uri" >> s)
        return ATmake("Str(<str>)", s.c_str());
    else throw Error("cannot coerce value to string");
}


/* Boolean constructors. */
static Expr primTrue(EvalState & state, const ATermVector & args)
{
    return ATmake("Bool(True)");
}


static Expr primFalse(EvalState & state, const ATermVector & args)
{
    return ATmake("Bool(False)");
}


/* Return the null value. */
Expr primNull(EvalState & state, const ATermVector & args)
{
    return ATmake("Null");
}


/* Determine whether the argument is the null value. */
Expr primIsNull(EvalState & state, const ATermVector & args)
{
    Expr arg = evalExpr(state, args[0]);
    ATMatcher m;
    return makeBool(atMatch(m, arg) >> "Null");
}


/* Apply a function to every element of a list. */
Expr primMap(EvalState & state, const ATermVector & args)
{
    Expr fun = evalExpr(state, args[0]);
    Expr list = evalExpr(state, args[1]);

    ATMatcher m;

    ATermList list2;
    if (!(atMatch(m, list) >> "List" >> list2))
        throw Error("`map' expects a list as its second argument");

    ATermList list3 = ATempty;
    for (ATermIterator i(list2); i; ++i)
        list3 = ATinsert(list3,
            ATmake("Call(<term>, <term>)", fun, *i));

    return ATmake("List(<term>)", ATreverse(list3));
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
