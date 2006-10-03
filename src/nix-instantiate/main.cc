#include <map>
#include <iostream>

#include "globals.hh"
#include "build.hh"
#include "gc.hh"
#include "shared.hh"
#include "eval.hh"
#include "parser.hh"
#include "get-drvs.hh"
#include "attr-path.hh"
#include "expr-to-xml.hh"
#include "util.hh"
#include "store.hh"
#include "help.txt.hh"


using namespace nix;


void printHelp()
{
    std::cout << string((char *) helpText, sizeof helpText);
}


static Expr parseStdin(EvalState & state)
{
    startNest(nest, lvlTalkative, format("parsing standard input"));
    string s, s2;
    while (getline(std::cin, s2)) s += s2 + "\n";
    return parseExprFromString(state, s, absPath("."));
}


static Path gcRoot;
static int rootNr = 0;
static bool indirectRoot = false;


static void printResult(EvalState & state, Expr e,
    bool evalOnly, bool xmlOutput, const ATermMap & autoArgs)
{
    ATermList context;
    
    if (evalOnly)
        if (xmlOutput)
            printTermAsXML(e, std::cout, context);
        else
            std::cout << format("%1%\n") % e;
    
    else {
        DrvInfos drvs;
        getDerivations(state, e, "", autoArgs, drvs);
        for (DrvInfos::iterator i = drvs.begin(); i != drvs.end(); ++i) {
            Path drvPath = i->queryDrvPath(state);
            if (gcRoot == "")
                printGCWarning();
            else
                drvPath = addPermRoot(drvPath,
                    makeRootName(gcRoot, rootNr),
                    indirectRoot);
            std::cout << format("%1%\n") % drvPath;
        }
    }
}


Expr doEval(EvalState & state, string attrPath, bool parseOnly, bool strict,
    const ATermMap & autoArgs, Expr e)
{
    e = findAlongAttrPath(state, attrPath, autoArgs, e);
    if (!parseOnly)
        if (strict)
            e = strictEvalExpr(state, e);
        else
            e = evalExpr(state, e);
    return e;
}


void run(Strings args)
{
    EvalState state;
    Strings files;
    bool readStdin = false;
    bool evalOnly = false;
    bool parseOnly = false;
    bool xmlOutput = false;
    bool strict = false;
    string attrPath;
    ATermMap autoArgs(128);

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
        else if (arg == "--attr" || arg == "-A") {
            if (i == args.end())
                throw UsageError("`--attr' requires an argument");
            attrPath = *i++;
        }
        else if (arg == "--arg") {
            if (i == args.end())
                throw UsageError("`--arg' requires two arguments");
            string name = *i++;
            if (i == args.end())
                throw UsageError("`--arg' requires two arguments");
            Expr value = parseExprFromString(state, *i++, absPath("."));
            autoArgs.set(toATerm(name), value);
        }
        else if (arg == "--add-root") {
            if (i == args.end())
                throw UsageError("`--add-root' requires an argument");
            gcRoot = absPath(*i++);
        }
        else if (arg == "--indirect")
            indirectRoot = true;
        else if (arg == "--xml")
            xmlOutput = true;
        else if (arg == "--strict")
            strict = true;
        else if (arg[0] == '-')
            throw UsageError(format("unknown flag `%1%'") % arg);
        else
            files.push_back(arg);
    }

    openDB();

    if (readStdin) {
        Expr e = parseStdin(state);
        e = doEval(state, attrPath, parseOnly, strict, autoArgs, e);
        printResult(state, e, evalOnly, xmlOutput, autoArgs);
    }

    for (Strings::iterator i = files.begin();
         i != files.end(); i++)
    {
        Path path = absPath(*i);
        Expr e = parseExprFromFile(state, path);
        e = doEval(state, attrPath, parseOnly, strict, autoArgs, e);
        printResult(state, e, evalOnly, xmlOutput, autoArgs);
    }

    printEvalStats(state);
}


string programId = "nix-instantiate";
