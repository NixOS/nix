#include <map>
#include <iostream>

#include "globals.hh"
#include "normalise.hh"
#include "shared.hh"


typedef ATerm Expr;

typedef map<ATerm, ATerm> NormalForms;
typedef map<FSId, Strings> PkgPaths;
typedef map<FSId, Hash> PkgHashes;

struct EvalState 
{
    Strings searchDirs;
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


static Expr evalFile(EvalState & state, string fileName);
static Expr evalExpr(EvalState & state, Expr e);


static string searchPath(const Strings & searchDirs, string relPath)
{
    if (string(relPath, 0, 1) == "/") return relPath;

    for (Strings::const_iterator i = searchDirs.begin();
         i != searchDirs.end(); i++)
    {
        string path = *i + "/" + relPath;
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


static Strings fstatePathsCached(EvalState & state, const FSId & id)
{
    PkgPaths::iterator i = state.pkgPaths.find(id);
    if (i != state.pkgPaths.end())
        return i->second;
    else {
        Strings paths = fstatePaths(id);
        state.pkgPaths[id] = paths;
        return paths;
    }
}


static Hash hashPackage(EvalState & state, FState fs)
{
    if (fs.type == FState::fsDerive) {
	FSIdSet inputs2;
        for (FSIdSet::iterator i = fs.derive.inputs.begin();
             i != fs.derive.inputs.end(); i++)
        {
            PkgHashes::iterator j = state.pkgHashes.find(*i);
            if (j == state.pkgHashes.end())
                throw Error(format("unknown package id %1%") % (string) *i);
            inputs2.insert(j->second);
        }
	fs.derive.inputs = inputs2;
    }
    return hashTerm(unparseFState(fs));
}


static string processBinding(EvalState & state, Expr e, FState & fs)
{
    char * s1;

    if (ATmatch(e, "FSId(<str>)", &s1)) {
        FSId id = parseHash(s1);
        Strings paths = fstatePathsCached(state, id);
        if (paths.size() != 1) abort();
        string path = *(paths.begin());
        fs.derive.inputs.insert(id);
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
	    s += processBinding(state, evalExpr(state, ATgetFirst(l)), fs);
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
        ATmatch(e, "FSId(<str>)", &s1))
        return e;

    try {
        Hash pkgHash = hashPackage(state, parseFState(e));
        FSId pkgId = writeTerm(e, "");
        state.pkgHashes[pkgId] = pkgHash;
        return ATmake("FSId(<str>)", ((string) pkgId).c_str());
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
        return ATmake("<str>", SYSTEM);
    }

    /* Fix inclusion. */
    if (ATmatch(e, "IncludeFix(<str>)", &s1)) {
        string fileName(s1);
        return evalFile(state, s1);
    }

    /* Relative files. */
    if (ATmatch(e, "Relative(<str>)", &s1)) {
        string srcPath = searchPath(state.searchDirs, s1);
        string dstPath;
        FSId id;
        addToStore(srcPath, dstPath, id, true);

        SliceElem elem;
        elem.id = id;
        FState fs;
        fs.type = FState::fsSlice;
        fs.slice.roots.insert(dstPath);
        fs.slice.elems[dstPath] = elem;

        Hash pkgHash = hashPackage(state, fs);
        FSId pkgId = writeTerm(unparseFState(fs), "");
        state.pkgHashes[pkgId] = pkgHash;

        msg(lvlChatty, format("copied `%1%' -> %2%")
            % srcPath % (string) pkgId);

        return ATmake("FSId(<str>)", ((string) pkgId).c_str());
    }

    /* Packages are transformed into Derive fstate expressions. */
    if (ATmatch(e, "Package([<list>])", &bnds)) {

        /* Evaluate the bindings and put them in a map. */
        map<string, ATerm> bndMap;
        bndMap["platform"] = ATmake("<str>", SYSTEM);
        while (!ATisEmpty(bnds)) {
            ATerm bnd = ATgetFirst(bnds);
            if (!ATmatch(bnd, "(<str>, <term>)", &s1, &e1))
                throw badTerm("binding expected", bnd);
            bndMap[s1] = evalExpr(state, e1);
            bnds = ATgetNext(bnds);
        }

        /* Gather information for building the Derive expression. */
        FState fs;
        fs.type = FState::fsDerive;
        fs.derive.platform = SYSTEM;
        string name;
        FSId outId;
        bool outIdGiven = false;
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
                    fs.derive.args.push_back(processBinding(state, arg, fs));
                    args = ATgetNext(args);
                }
            } 

            else {
                string s = processBinding(state, value, fs);
                fs.derive.env[key] = s;

                if (key == "build") fs.derive.builder = s;
                if (key == "name") name = s;
                if (key == "id") { 
                    outId = parseHash(s);
                    outIdGiven = true;
                }
            }

            bnds = ATinsert(bnds, 
                ATmake("(<str>, <term>)", key.c_str(), value));
        }

        if (fs.derive.builder == "")
            throw badTerm("no builder specified", e);
        
        if (name == "")
            throw badTerm("no package name specified", e);
        
        /* Hash the fstate-expression with no outputs to produce a
           unique but deterministic path name for this package. */
        if (!outIdGiven) outId = hashPackage(state, fs);
        string outPath = 
            canonPath(nixStore + "/" + ((string) outId).c_str() + "-" + name);
        fs.derive.env["out"] = outPath;
        fs.derive.outputs[outPath] = outId;

        /* Write the resulting term into the Nix store directory. */
        Hash pkgHash = outIdGiven
            ? hashString((string) outId + outPath)
            : hashPackage(state, fs);
        FSId pkgId = writeTerm(unparseFState(fs), "-d-" + name);
        state.pkgHashes[pkgId] = pkgHash;

        msg(lvlChatty, format("instantiated `%1%' -> %2%")
            % name % (string) pkgId);

        return ATmake("FSId(<str>)", ((string) pkgId).c_str());
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


static Expr evalFile(EvalState & state, string relPath)
{
    string path = searchPath(state.searchDirs, relPath);
    Nest nest(lvlTalkative, format("evaluating file `%1%'") % path);
    Expr e = ATreadFromNamedFile(path.c_str());
    if (!e) 
        throw Error(format("unable to read a term from `%1%'") % path);
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


static void printFSId(EvalState & state, Expr e)
{
    ATermList es;
    char * s;
    if (ATmatch(e, "FSId(<str>)", &s)) {
        cout << format("%1%\n") % s;
    } 
    else if (ATmatch(e, "[<list>]", &es)) {
        while (!ATisEmpty(es)) {
            printFSId(state, evalExpr(state, ATgetFirst(es)));
            es = ATgetNext(es);
        }
    }
    else throw badTerm("top level does not evaluate to a (list of) Nix expression(s)", e);
}


void run(Strings args)
{
    openDB();

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

    if (readStdin) {
        Expr e = evalStdin(state);
        printFSId(state, e);
    }

    for (Strings::iterator it = files.begin();
         it != files.end(); it++)
    {
        Expr e = evalFile(state, *it);
        printFSId(state, e);
    }
}


string programId = "fix";
