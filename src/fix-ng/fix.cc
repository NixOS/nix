#include <map>
#include <iostream>

#include "globals.hh"
#include "normalise.hh"
#include "shared.hh"
#include "expr.hh"
#include "eval.hh"


#if 0
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
#endif


#if 0
static Expr evalExpr2(EvalState & state, Expr e)
{
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

    /* BaseName primitive function. */
    if (ATmatch(e, "BaseName(<term>)", &e1)) {
        e1 = evalExpr(state, e1);
        if (!ATmatch(e1, "<str>", &s1)) 
            throw badTerm("string expected", e1);
        return ATmake("<str>", baseNameOf(s1).c_str());
    }

}
#endif


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

    if (ATmatch(e, "Attrs([<list>])", &es)) {
        Expr a = queryAttr(e, "type");
        if (a && evalString(state, a) == "derivation") {
            a = queryAttr(e, "drvPath");
            if (a) {
                cout << format("%1%\n") % evalPath(state, a);
                return;
            }
        }
    }

    if (ATmatch(e, "[<list>]", &es)) {
        while (!ATisEmpty(es)) {
            printNixExpr(state, evalExpr(state, ATgetFirst(es)));
            es = ATgetNext(es);
        }
        return;
    }

    throw badTerm("top level does not evaluate to one or more Nix expressions", e);
}


void run(Strings args)
{
    EvalState state;
    Strings files;
    bool readStdin = false;

#if 0
    state.searchDirs.push_back(".");
    state.searchDirs.push_back(nixDataDir + "/fix");
#endif
    
    for (Strings::iterator it = args.begin();
         it != args.end(); )
    {
        string arg = *it++;

#if 0
        if (arg == "--includedir" || arg == "-I") {
            if (it == args.end())
                throw UsageError(format("argument required in `%1%'") % arg);
            state.searchDirs.push_back(*it++);
        }
        else
#endif
        if (arg == "--verbose" || arg == "-v")
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
        Expr e = evalFile(state, absPath(*it));
        printNixExpr(state, e);
    }

    printEvalStats(state);
}


string programId = "fix";
