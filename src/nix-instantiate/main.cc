#include <map>
#include <iostream>

#include "globals.hh"
#include "normalise.hh"
#include "shared.hh"
#include "eval.hh"
#include "parser.hh"
#include "nixexpr-ast.hh"
#include "help.txt.hh"


void printHelp()
{
    cout << string((char *) helpText, sizeof helpText);
}


static Expr evalStdin(EvalState & state, bool parseOnly)
{
    startNest(nest, lvlTalkative, format("evaluating standard input"));
    string s, s2;
    while (getline(cin, s2)) s += s2 + "\n";
    Expr e = parseExprFromString(state, s, absPath("."));
    return parseOnly ? e : evalExpr(state, e);
}


static void printDrvPaths(EvalState & state, Expr e)
{
    ATermList es;

    /* !!! duplication w.r.t. parseDerivations in nix-env */

    if (matchAttrs(e, es)) {
        Expr a = queryAttr(e, "type");
        if (a && evalString(state, a) == "derivation") {
            a = queryAttr(e, "drvPath");
            if (a) {
                cout << format("%1%\n") % evalPath(state, a);
                return;
            }
            throw Error("bad derivation");
        } else {
            ATermMap drvMap;
            queryAllAttrs(e, drvMap);
            for (ATermIterator i(drvMap.keys()); i; ++i)
                printDrvPaths(state, evalExpr(state, drvMap.get(*i)));
            return;
        }
    }

    if (matchList(e, es)) {
        for (ATermIterator i(es); i; ++i)
            printDrvPaths(state, evalExpr(state, *i));
        return;
    }

    throw Error("expression does not evaluate to one or more derivations");
}


static void printResult(EvalState & state, Expr e, bool evalOnly)
{
    if (evalOnly)
        cout << format("%1%\n") % e;
    else
        printDrvPaths(state, e);
}


void run(Strings args)
{
    EvalState state;
    Strings files;
    bool readStdin = false;
    bool evalOnly = false;
    bool parseOnly = false;

    for (Strings::iterator it = args.begin();
         it != args.end(); )
    {
        string arg = *it++;

        if (arg == "-")
            readStdin = true;
        else if (arg == "--eval-only") {
            readOnlyMode = true;
            evalOnly = true;
        }
        else if (arg == "--parse-only") {
            readOnlyMode = true;
            parseOnly = evalOnly = true;
        }
        else if (arg[0] == '-')
            throw UsageError(format("unknown flag `%1%`") % arg);
        else
            files.push_back(arg);
    }

    openDB();

    if (readStdin) {
        Expr e = evalStdin(state, parseOnly);
        printResult(state, e, evalOnly);
    }

    for (Strings::iterator it = files.begin();
         it != files.end(); it++)
    {
        Expr e = evalFile(state, absPath(*it));
        /* !!! parseOnly ignored */
        printResult(state, e, evalOnly);
    }

    printEvalStats(state);
}


string programId = "nix-instantiate";
