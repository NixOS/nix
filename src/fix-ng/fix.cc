#include <map>
#include <iostream>

#include "parser.hh"
#include "globals.hh"
#include "normalise.hh"
#include "shared.hh"


typedef map<ATerm, ATerm> NormalForms;
typedef map<Path, PathSet> PkgPaths;
typedef map<Path, Hash> PkgHashes;

struct EvalState 
{
    Paths searchDirs;
    NormalForms normalForms;
    PkgPaths pkgPaths;
    PkgHashes pkgHashes; /* normalised package hashes */
    Expr blackHole;

    EvalState()
    {
        blackHole = ATmake("BlackHole()");
        if (!blackHole) throw Error("cannot build black hole");
    }
};


static Expr evalFile(EvalState & state, const Path & path);
static Expr evalExpr(EvalState & state, Expr e);


static Path searchPath(const Paths & searchDirs, const Path & relPath)
{
    if (string(relPath, 0, 1) == "/") return relPath;

    for (Paths::const_iterator i = searchDirs.begin();
         i != searchDirs.end(); i++)
    {
        Path path = *i + "/" + relPath;
        if (pathExists(path)) return path;
    }

    throw Error(
        format("path `%1%' not found in any of the search directories")
        % relPath);
}


static Expr substExpr(string x, Expr rep, Expr e)
{
    char * s;
    Expr e2;

    if (ATmatch(e, "Var(<str>)", &s))
        if (x == s)
            return rep;
        else
            return e;

    ATermList formals;
    if (ATmatch(e, "Function([<list>], <term>)", &formals, &e2)) {
        while (!ATisEmpty(formals)) {
            if (!ATmatch(ATgetFirst(formals), "<str>", &s))
                throw badTerm("not a list of formals", (ATerm) formals);
            if (x == (string) s)
                return e;
            formals = ATgetNext(formals);
        }
    }

    /* Generically substitute in subterms. */

    if (ATgetType(e) == AT_APPL) {
        AFun fun = ATgetAFun(e);
        int arity = ATgetArity(fun);
        ATermList args = ATempty;

        for (int i = arity - 1; i >= 0; i--)
            args = ATinsert(args, substExpr(x, rep, ATgetArgument(e, i)));
        
        return (ATerm) ATmakeApplList(fun, args);
    }

    if (ATgetType(e) == AT_LIST) {
        ATermList in = (ATermList) e;
        ATermList out = ATempty;

        while (!ATisEmpty(in)) {
            out = ATinsert(out, substExpr(x, rep, ATgetFirst(in)));
            in = ATgetNext(in);
        }

        return (ATerm) ATreverse(out);
    }

    throw badTerm("do not know how to substitute", e);
}


static Expr substExprMany(ATermList formals, ATermList args, Expr body)
{
    char * s;
    Expr e;

    /* !!! check args against formals */

    while (!ATisEmpty(args)) {
        ATerm tup = ATgetFirst(args);
        if (!ATmatch(tup, "(<str>, <term>)", &s, &e))
            throw badTerm("expected an argument tuple", tup);

        body = substExpr(s, e, body);

        args = ATgetNext(args);
    }
    
    return body;
}


static PathSet nixExprRootsCached(EvalState & state, const Path & nePath)
{
    PkgPaths::iterator i = state.pkgPaths.find(nePath);
    if (i != state.pkgPaths.end())
        return i->second;
    else {
        PathSet paths = nixExprRoots(nePath);
        state.pkgPaths[nePath] = paths;
        return paths;
    }
}


static Hash hashPackage(EvalState & state, NixExpr ne)
{
    if (ne.type == NixExpr::neDerivation) {
	PathSet inputs2;
        for (PathSet::iterator i = ne.derivation.inputs.begin();
             i != ne.derivation.inputs.end(); i++)
        {
            PkgHashes::iterator j = state.pkgHashes.find(*i);
            if (j == state.pkgHashes.end())
                throw Error(format("don't know expression `%1%'") % (string) *i);
            inputs2.insert(j->second);
        }
	ne.derivation.inputs = inputs2;
    }
    return hashTerm(unparseNixExpr(ne));
}


static string processBinding(EvalState & state, Expr e, NixExpr & ne)
{
    char * s1;

    if (ATmatch(e, "NixExpr(<str>)", &s1)) {
        Path nePath(s1);
        PathSet paths = nixExprRootsCached(state, nePath);
        if (paths.size() != 1) abort();
        Path path = *(paths.begin());
        ne.derivation.inputs.insert(nePath);
        return path;
    }
    
    if (ATmatch(e, "<str>", &s1))
        return s1;

    if (ATmatch(e, "True")) return "1";
    
    if (ATmatch(e, "False")) return "";

    ATermList l;
    if (ATmatch(e, "[<list>]", &l)) {
	string s;
	bool first = true;
        while (!ATisEmpty(l)) {
	    if (!first) s = s + " "; else first = false;
	    s += processBinding(state, evalExpr(state, ATgetFirst(l)), ne);
            l = ATgetNext(l);
        }
	return s;
    }
    
    throw badTerm("invalid package binding", e);
}


static Expr evalExpr2(EvalState & state, Expr e)
{
    char * s1;
    Expr e1, e2, e3, e4;
    ATermList bnds;

    /* Normal forms. */
    if (ATmatch(e, "<str>", &s1) ||
        ATmatch(e, "[<list>]", &e1) ||
        ATmatch(e, "True") ||
        ATmatch(e, "False") ||
        ATmatch(e, "Function([<list>], <term>)", &e1, &e2) ||
        ATmatch(e, "NixExpr(<str>)", &s1))
        return e;

    try {
        Hash pkgHash = hashPackage(state, parseNixExpr(e));
        Path pkgPath = writeTerm(e, "");
        state.pkgHashes[pkgPath] = pkgHash;
        return ATmake("NixExpr(<str>)", pkgPath.c_str());
    } catch (...) { /* !!! catch parse errors only */
    }

    /* Application. */
    if (ATmatch(e, "Call(<term>, [<list>])", &e1, &e2) ||
        ATmatch(e, "App(<term>, [<list>])", &e1, &e2)) {
        e1 = evalExpr(state, e1);
        if (!ATmatch(e1, "Function([<list>], <term>)", &e3, &e4))
            throw badTerm("expecting a function", e1);
        return evalExpr(state,
            substExprMany((ATermList) e3, (ATermList) e2, e4));
    }

    /* Conditional. */
    if (ATmatch(e, "If(<term>, <term>, <term>)", &e1, &e2, &e3)) {
        e1 = evalExpr(state, e1);
        Expr x;
        if (ATmatch(e1, "True")) x = e2;
        else if (ATmatch(e1, "False")) x = e3;
        else throw badTerm("expecting a boolean", e1);
        return evalExpr(state, x);
    }

    /* Ad-hoc function for string matching. */
    if (ATmatch(e, "HasSubstr(<term>, <term>)", &e1, &e2)) {
        e1 = evalExpr(state, e1);
        e2 = evalExpr(state, e2);
        
        char * s1, * s2;
        if (!ATmatch(e1, "<str>", &s1))
            throw badTerm("expecting a string", e1);
        if (!ATmatch(e2, "<str>", &s2))
            throw badTerm("expecting a string", e2);
        
        return
            string(s1).find(string(s2)) != string::npos ?
            ATmake("True") : ATmake("False");
    }

    /* Platform constant. */
    if (ATmatch(e, "Platform")) {
        return ATmake("<str>", thisSystem.c_str());
    }

    /* Fix inclusion. */
    if (ATmatch(e, "IncludeFix(<str>)", &s1)) {
        Path fileName(s1);
        return evalFile(state, s1);
    }

    /* Relative files. */
    if (ATmatch(e, "Relative(<str>)", &s1)) {
        Path srcPath = searchPath(state.searchDirs, s1);
        Path dstPath = addToStore(srcPath);

        ClosureElem elem;
        NixExpr ne;
        ne.type = NixExpr::neClosure;
        ne.closure.roots.insert(dstPath);
        ne.closure.elems[dstPath] = elem;

        Hash pkgHash = hashPackage(state, ne);
        Path pkgPath = writeTerm(unparseNixExpr(ne), "");
        state.pkgHashes[pkgPath] = pkgHash;

        msg(lvlChatty, format("copied `%1%' -> closure `%2%'")
            % srcPath % pkgPath);

        return ATmake("NixExpr(<str>)", pkgPath.c_str());
    }

    /* Packages are transformed into Nix derivation expressions. */
    if (ATmatch(e, "Package([<list>])", &bnds)) {

        /* Evaluate the bindings and put them in a map. */
        map<string, ATerm> bndMap;
        bndMap["platform"] = ATmake("<str>", thisSystem.c_str());
        while (!ATisEmpty(bnds)) {
            ATerm bnd = ATgetFirst(bnds);
            if (!ATmatch(bnd, "(<str>, <term>)", &s1, &e1))
                throw badTerm("binding expected", bnd);
            bndMap[s1] = evalExpr(state, e1);
            bnds = ATgetNext(bnds);
        }

        /* Gather information for building the derivation
           expression. */
        NixExpr ne;
        ne.type = NixExpr::neDerivation;
        ne.derivation.platform = thisSystem;
        string name;
        Path outPath;
        Hash outHash;
        bool outHashGiven = false;
        bnds = ATempty;

        for (map<string, ATerm>::iterator it = bndMap.begin();
             it != bndMap.end(); it++)
        {
            string key = it->first;
            ATerm value = it->second;

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

            else {
                string s = processBinding(state, value, ne);
                ne.derivation.env[key] = s;

                if (key == "build") ne.derivation.builder = s;
                if (key == "name") name = s;
                if (key == "outPath") outPath = s;
                if (key == "id") { 
                    outHash = parseHash(s);
                    outHashGiven = true;
                }
            }

            bnds = ATinsert(bnds, 
                ATmake("(<str>, <term>)", key.c_str(), value));
        }

        if (ne.derivation.builder == "")
            throw badTerm("no builder specified", e);
        
        if (name == "")
            throw badTerm("no package name specified", e);
        
        /* Determine the output path. */
        if (!outHashGiven) outHash = hashPackage(state, ne);
        if (outPath == "")
            /* Hash the Nix expression with no outputs to produce a
               unique but deterministic path name for this package. */
            outPath = 
                canonPath(nixStore + "/" + ((string) outHash).c_str() + "-" + name);
        ne.derivation.env["out"] = outPath;
        ne.derivation.outputs.insert(outPath);

        /* Write the resulting term into the Nix store directory. */
        Hash pkgHash = outHashGiven
            ? hashString((string) outHash + outPath)
            : hashPackage(state, ne);
        Path pkgPath = writeTerm(unparseNixExpr(ne), "-d-" + name);
        state.pkgHashes[pkgPath] = pkgHash;

        msg(lvlChatty, format("instantiated `%1%' -> `%2%'")
            % name % pkgPath);

        return ATmake("NixExpr(<str>)", pkgPath.c_str());
    }

    /* BaseName primitive function. */
    if (ATmatch(e, "BaseName(<term>)", &e1)) {
        e1 = evalExpr(state, e1);
        if (!ATmatch(e1, "<str>", &s1)) 
            throw badTerm("string expected", e1);
        return ATmake("<str>", baseNameOf(s1).c_str());
    }

    /* Barf. */
    throw badTerm("invalid expression", e);
}


static Expr evalExpr(EvalState & state, Expr e)
{
    Nest nest(lvlVomit, format("evaluating expression: %1%") % printTerm(e));

    /* Consult the memo table to quickly get the normal form of
       previously evaluated expressions. */
    NormalForms::iterator i = state.normalForms.find(e);
    if (i != state.normalForms.end()) {
        if (i->second == state.blackHole)
            throw badTerm("infinite recursion", e);
        return i->second;
    }

    /* Otherwise, evaluate and memoize. */
    state.normalForms[e] = state.blackHole;
    Expr nf = evalExpr2(state, e);
    state.normalForms[e] = nf;
    return nf;
}


static Expr evalFile(EvalState & state, const Path & relPath)
{
    Path path = searchPath(state.searchDirs, relPath);
    Nest nest(lvlTalkative, format("evaluating file `%1%'") % path);
    Expr e = parseExprFromFile(path);
    return evalExpr(state, e);
}


static Expr evalStdin(EvalState & state)
{
    Nest nest(lvlTalkative, format("evaluating standard input"));
    Expr e = ATreadFromFile(stdin);
    if (!e) 
        throw Error(format("unable to read a term from stdin"));
    return evalExpr(state, e);
}


static void printNixExpr(EvalState & state, Expr e)
{
    ATermList es;
    char * s;
    if (ATmatch(e, "NixExpr(<str>)", &s)) {
        cout << format("%1%\n") % s;
    } 
    else if (ATmatch(e, "[<list>]", &es)) {
        while (!ATisEmpty(es)) {
            printNixExpr(state, evalExpr(state, ATgetFirst(es)));
            es = ATgetNext(es);
        }
    }
    else throw badTerm("top level does not evaluate to a (list of) Nix expression(s)", e);
}


void run(Strings args)
{
    EvalState state;
    Strings files;
    bool readStdin = false;

    state.searchDirs.push_back(".");
    state.searchDirs.push_back(nixDataDir + "/fix");
    
    for (Strings::iterator it = args.begin();
         it != args.end(); )
    {
        string arg = *it++;

        if (arg == "--includedir" || arg == "-I") {
            if (it == args.end())
                throw UsageError(format("argument required in `%1%'") % arg);
            state.searchDirs.push_back(*it++);
        }
        else if (arg == "--verbose" || arg == "-v")
            verbosity = (Verbosity) ((int) verbosity + 1);
        else if (arg == "-")
            readStdin = true;
        else if (arg[0] == '-')
            throw UsageError(format("unknown flag `%1%`") % arg);
        else
            files.push_back(arg);
    }

    openDB();

    if (readStdin) {
        Expr e = evalStdin(state);
        printNixExpr(state, e);
    }

    for (Strings::iterator it = files.begin();
         it != files.end(); it++)
    {
        Expr e = evalFile(state, *it);
        printNixExpr(state, e);
    }
}


string programId = "fix";
