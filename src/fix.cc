#include <map>
#include <iostream>

#include "globals.hh"
#include "normalise.hh"
#include "shared.hh"


typedef ATerm Expr;

typedef map<ATerm, ATerm> NormalForms;

struct EvalState 
{
    Strings searchDirs;
    NormalForms normalForms;
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

    if (ATmatch(e, "Lam(<str>, <term>)", &s, &e2))
        if (x == s)
            return e;
    /* !!! unfair substitutions */

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


static Expr evalExpr2(EvalState & state, Expr e)
{
    char * s1;
    Expr e1, e2, e3, e4;
    ATermList bnds;

    /* Normal forms. */
    if (ATmatch(e, "<str>", &s1) ||
        ATmatch(e, "Function([<list>], <term>)", &e1, &e2) ||
        ATmatch(e, "FSId(<str>)", &s1))
        return e;

    try {
        parseFState(e);
        return ATmake("FSId(<str>)", 
            ((string) writeTerm(e, "")).c_str());
    } catch (...) { /* !!! catch parse errors only */
    }

    /* Application. */
    if (ATmatch(e, "App(<term>, [<list>])", &e1, &e2)) {
        e1 = evalExpr(state, e1);
        if (!ATmatch(e1, "Function([<list>], <term>)", &e3, &e4))
            throw badTerm("expecting a function", e1);
        return evalExpr(state,
            substExprMany((ATermList) e3, (ATermList) e2, e4));
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
        elem.path = dstPath;
        elem.id = id;
        FState fs;
        fs.type = FState::fsSlice;
        fs.slice.roots.push_back(id);
        fs.slice.elems.push_back(elem);

        FSId termId = hashString("producer-" + (string) id
            + "-" + dstPath);
        writeTerm(unparseFState(fs), "", termId);
        return ATmake("FSId(<str>)", ((string) termId).c_str());
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
        bnds = ATempty;

        for (map<string, ATerm>::iterator it = bndMap.begin();
             it != bndMap.end(); it++)
        {
            string key = it->first;
            ATerm value = it->second;

            if (ATmatch(value, "FSId(<str>)", &s1)) {
                FSId id = parseHash(s1);
                Strings paths = fstatePaths(id, false);
                if (paths.size() != 1) abort();
                string path = *(paths.begin());
                fs.derive.inputs.push_back(id);
                fs.derive.env.push_back(StringPair(key, path));
                if (key == "build") fs.derive.builder = path;
            }
            else if (ATmatch(value, "<str>", &s1)) {
                if (key == "name") name = s1;
                if (key == "id") outId = parseHash(s1);
                fs.derive.env.push_back(StringPair(key, s1));
            }
            else throw badTerm("invalid package argument", value);

            bnds = ATinsert(bnds, 
                ATmake("(<str>, <term>)", key.c_str(), value));
        }

        if (fs.derive.builder == "")
            throw badTerm("no builder specified", e);
        
        if (name == "")
            throw badTerm("no package name specified", e);
        
        /* Hash the fstate-expression with no outputs to produce a
           unique but deterministic path name for this package. */
        if (outId == FSId())
            outId = hashTerm(unparseFState(fs));
        string outPath = 
            canonPath(nixStore + "/" + ((string) outId).c_str() + "-" + name);
        fs.derive.env.push_back(StringPair("out", outPath));
        fs.derive.outputs.push_back(DeriveOutput(outPath, outId));
        debug(format("%1%: %2%") % (string) outId % name);

        /* Write the resulting term into the Nix store directory. */
        FSId termId = hashString("producer-" + (string) outId
            + "-" + outPath);
        writeTerm(unparseFState(fs), "-d-" + name, termId);
        return ATmake("FSId(<str>)", ((string) termId).c_str());
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
    /* Consult the memo table to quickly get the normal form of
       previously evaluated expressions. */
    NormalForms::iterator i = state.normalForms.find(e);
    if (i != state.normalForms.end()) return i->second;

    /* Otherwise, evaluate and memoize. */
    Expr nf = evalExpr2(state, e);
    state.normalForms[e] = nf;
    return nf;
}


static Expr evalFile(EvalState & state, string relPath)
{
    string path = searchPath(state.searchDirs, relPath);
    Expr e = ATreadFromNamedFile(path.c_str());
    if (!e) 
        throw Error(format("unable to read a term from `%1%'") % path);
    return evalExpr(state, e);
}


void run(Strings args)
{
    EvalState state;
    Strings files;

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
        else if (arg[0] == '-')
            throw UsageError(format("unknown flag `%1%`") % arg);
        else
            files.push_back(arg);
    }

    if (files.empty()) throw UsageError("no files specified");

    for (Strings::iterator it = files.begin();
         it != files.end(); it++)
    {
        Expr e = evalFile(state, *it);
        char * s;
        if (ATmatch(e, "FSId(<str>)", &s)) {
            cout << format("%1%\n") % s;
        }
        else throw badTerm("top level is not a package", e);
    }
}


string programId = "fix";
