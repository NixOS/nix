#include <map>
#include <iostream>

#include "globals.hh"
#include "build.hh"
#include "gc.hh"
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


static Path gcRoot;
static int rootNr = 0;


/* Print out the paths of the resulting derivation(s).  If the user
   specified the `--add-root' flag, we register the derivation as a
   garbage collection root and print out the path of the GC root
   symlink instead. */
static void printDrvPaths(EvalState & state, Expr e)
{
    ATermList es;

    /* !!! duplication w.r.t. parseDerivations in nix-env */

    if (matchAttrs(e, es)) {
        Expr a = queryAttr(e, "type");
        if (a && evalString(state, a) == "derivation") {
            a = queryAttr(e, "drvPath");
            if (a) {
                Path drvPath = evalPath(state, a);
                if (gcRoot == "")
                    printGCWarning();
                else
                    drvPath = addPermRoot(drvPath,
                        makeRootName(gcRoot, rootNr));
                cout << format("%1%\n") % drvPath;
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

    for (Strings::iterator i = args.begin();
         i != args.end(); )
    {
        string arg = *i++;

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
        else if (arg == "--add-root") {
            if (i == args.end())
                throw UsageError("`--add-root requires an argument");
            gcRoot = *i++;
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

    for (Strings::iterator i = files.begin();
         i != files.end(); i++)
    {
        Expr e = evalFile(state, absPath(*i));
        /* !!! parseOnly ignored */
        printResult(state, e, evalOnly);
    }

    printEvalStats(state);
}


string programId = "nix-instantiate";
