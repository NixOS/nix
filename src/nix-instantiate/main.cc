#include <map>
#include <iostream>

#include "globals.hh"
#include "normalise.hh"
#include "shared.hh"
#include "eval.hh"
#include "parser.hh"


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


static Expr evalStdin(EvalState & state)
{
    startNest(nest, lvlTalkative, format("evaluating standard input"));
    string s, s2;
    while (getline(cin, s2)) s += s2 + "\n";
    Expr e = parseExprFromString(s, absPath("."));
    return evalExpr(state, e);
}


static void printNixExpr(EvalState & state, Expr e)
{
    ATMatcher m;
    ATermList es;

    if (atMatch(m, e) >> "Attrs" >> es) {
        Expr a = queryAttr(e, "type");
        if (a && evalString(state, a) == "derivation") {
            a = queryAttr(e, "drvPath");
            if (a) {
                cout << format("%1%\n") % evalPath(state, a);
                return;
            }
        }
    }

    if (atMatch(m, e) >> "List" >> es) {
        for (ATermIterator i(es); i; ++i)
            printNixExpr(state, evalExpr(state, *i));
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
    state.searchDirs.push_back(nixDataDir + "/nix");
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


string programId = "nix-instantiate";
