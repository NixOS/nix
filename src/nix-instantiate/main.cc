#include <map>
#include <iostream>

#include "globals.hh"
#include "build.hh"
#include "gc.hh"
#include "shared.hh"
#include "eval.hh"
#include "parser.hh"
#include "nixexpr-ast.hh"
#include "get-drvs.hh"
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
static bool indirectRoot = false;


static void printResult(EvalState & state, Expr e, bool evalOnly)
{
    if (evalOnly)
        cout << format("%1%\n") % e;
    else {
        DrvInfos drvs;
        getDerivations(state, e, drvs);
        for (DrvInfos::iterator i = drvs.begin(); i != drvs.end(); ++i) {
            Path drvPath = i->queryDrvPath(state);
            if (gcRoot == "")
                printGCWarning();
            else
                drvPath = addPermRoot(drvPath,
                    makeRootName(gcRoot, rootNr),
                    indirectRoot);
            cout << format("%1%\n") % drvPath;
        }
    }
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
            gcRoot = absPath(*i++);
        }
        else if (arg == "--indirect")
            indirectRoot = true;
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
