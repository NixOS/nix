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
#include "attr-path.hh"
#include "help.txt.hh"


void printHelp()
{
    cout << string((char *) helpText, sizeof helpText);
}


static Expr parseStdin(EvalState & state)
{
    startNest(nest, lvlTalkative, format("parsing standard input"));
    string s, s2;
    while (getline(cin, s2)) s += s2 + "\n";
    return parseExprFromString(state, s, absPath("."));
}


static Path gcRoot;
static int rootNr = 0;
static bool indirectRoot = false;


static void printResult(EvalState & state, Expr e,
    bool evalOnly, bool printArgs)
{
    if (evalOnly)
        cout << format("%1%\n") % e;
    
    else if (printArgs) {
        ATermList formals;
        ATerm body, pos;
        if (matchFunction(e, formals, body, pos)) {
            for (ATermIterator i(formals); i; ++i) {
                Expr name; ATerm d1, d2;
                if (!matchFormal(*i, name, d1, d2)) abort();
                cout << format("%1%\n") % aterm2String(name);
            }
        } else
            printMsg(lvlError, "warning: expression does not evaluate to a function");
    }
    
    else {
        DrvInfos drvs;
        getDerivations(state, e, "", drvs);
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
    bool printArgs = false;
    string attrPath;

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
        else if (arg == "--print-args") {
            readOnlyMode = true;
            printArgs = true;
        }
        else if (arg == "--add-root") {
            if (i == args.end())
                throw UsageError("`--add-root requires an argument");
            gcRoot = absPath(*i++);
        }
        else if (arg == "--attr" || arg == "-A") {
            if (i == args.end())
                throw UsageError("`--attr requires an argument");
            attrPath = *i++;
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
        Expr e = findAlongAttrPath(state, attrPath, parseStdin(state));
        if (!parseOnly) e = evalExpr(state, e);
        printResult(state, e, evalOnly, printArgs);
    }

    for (Strings::iterator i = files.begin();
         i != files.end(); i++)
    {
        Path path = absPath(*i);
        Expr e = findAlongAttrPath(state, attrPath,
            parseExprFromFile(state, path));
        if (!parseOnly) e = evalExpr(state, e);
        printResult(state, e, evalOnly, printArgs);
    }

    printEvalStats(state);
}


string programId = "nix-instantiate";
