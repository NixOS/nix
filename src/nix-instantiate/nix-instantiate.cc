#include "globals.hh"
#include "shared.hh"
#include "eval.hh"
#include "eval-inline.hh"
#include "get-drvs.hh"
#include "attr-path.hh"
#include "value-to-xml.hh"
#include "util.hh"
#include "store-api.hh"
#include "common-opts.hh"
#include "misc.hh"

#include <map>
#include <iostream>


using namespace nix;


void printHelp()
{
    showManPage("nix-instantiate");
}


static Expr * parseStdin(EvalState & state)
{
    startNest(nest, lvlTalkative, format("parsing standard input"));
    return state.parseExprFromString(drainFD(0), absPath("."));
}


static Path gcRoot;
static int rootNr = 0;
static bool indirectRoot = false;


void processExpr(EvalState & state, const Strings & attrPaths,
    bool parseOnly, bool strict, Bindings & autoArgs,
    bool evalOnly, bool xmlOutput, bool location, Expr * e)
{
    if (parseOnly) {
        std::cout << format("%1%\n") % *e;
        return;
    }

    Value vRoot;
    state.eval(e, vRoot);

    foreach (Strings::const_iterator, i, attrPaths) {
        Value & v(*findAlongAttrPath(state, *i, autoArgs, vRoot));
        state.forceValue(v);

        PathSet context;
        if (evalOnly)
            if (xmlOutput)
                printValueAsXML(state, strict, location, v, std::cout, context);
            else {
                if (strict) state.strictForceValue(v);
                std::cout << v << std::endl;
            }
        else {
            DrvInfos drvs;
            getDerivations(state, v, "", autoArgs, drvs, false);
            foreach (DrvInfos::iterator, i, drvs) {
                Path drvPath = i->queryDrvPath();

                /* What output do we want? */
                string outputName = i->queryOutputName();
                if (outputName == "")
                    throw Error(format("derivation `%1%' lacks an `outputName' attribute ") % drvPath);

                if (gcRoot == "")
                    printGCWarning();
                else {
                    Path rootName = gcRoot;
                    if (++rootNr > 1) rootName += "-" + int2String(rootNr);
                    drvPath = addPermRoot(*store, drvPath, rootName, indirectRoot);
                }
                std::cout << format("%1%%2%\n") % drvPath % (outputName != "out" ? "!" + outputName : "");
            }
        }
    }
}


void run(Strings args)
{
    EvalState state;
    Strings files;
    bool readStdin = false;
    bool fromArgs = false;
    bool findFile = false;
    bool evalOnly = false;
    bool parseOnly = false;
    bool xmlOutput = false;
    bool xmlOutputSourceLocation = true;
    bool strict = false;
    Strings attrPaths;
    Bindings autoArgs;
    bool wantsReadWrite = false;

    for (Strings::iterator i = args.begin(); i != args.end(); ) {
        string arg = *i++;

        if (arg == "-")
            readStdin = true;
        else if (arg == "--expr" || arg == "-E")
            fromArgs = true;
        else if (arg == "--eval" || arg == "--eval-only")
            evalOnly = true;
        else if (arg == "--read-write-mode")
            wantsReadWrite = true;
        else if (arg == "--parse" || arg == "--parse-only")
            parseOnly = evalOnly = true;
        else if (arg == "--find-file")
            findFile = true;
        else if (arg == "--attr" || arg == "-A") {
            if (i == args.end())
                throw UsageError("`--attr' requires an argument");
            attrPaths.push_back(*i++);
        }
        else if (parseOptionArg(arg, i, args.end(), state, autoArgs))
            ;
        else if (parseSearchPathArg(arg, i, args.end(), state))
            ;
        else if (arg == "--add-root") {
            if (i == args.end())
                throw UsageError("`--add-root' requires an argument");
            gcRoot = absPath(*i++);
        }
        else if (arg == "--indirect")
            indirectRoot = true;
        else if (arg == "--xml")
            xmlOutput = true;
        else if (arg == "--no-location")
            xmlOutputSourceLocation = false;
        else if (arg == "--strict")
            strict = true;
        else if (arg == "--repair")
            state.repair = true;
        else if (arg[0] == '-')
            throw UsageError(format("unknown flag `%1%'") % arg);
        else
            files.push_back(arg);
    }

    if (evalOnly && !wantsReadWrite)
        settings.readOnlyMode = true;

    if (attrPaths.empty()) attrPaths.push_back("");

    if (findFile) {
        foreach (Strings::iterator, i, files) {
            Path p = state.findFile(*i);
            if (p == "") throw Error(format("unable to find `%1%'") % *i);
            std::cout << p << std::endl;
        }
        return;
    }

    store = openStore();

    if (readStdin) {
        Expr * e = parseStdin(state);
        processExpr(state, attrPaths, parseOnly, strict, autoArgs,
            evalOnly, xmlOutput, xmlOutputSourceLocation, e);
    } else if (files.empty() && !fromArgs)
        files.push_back("./default.nix");

    foreach (Strings::iterator, i, files) {
        Expr * e = fromArgs
            ? state.parseExprFromString(*i, absPath("."))
            : state.parseExprFromFile(resolveExprPath(lookupFileArg(state, *i)));
        processExpr(state, attrPaths, parseOnly, strict, autoArgs,
            evalOnly, xmlOutput, xmlOutputSourceLocation, e);
    }

    state.printStats();
}


string programId = "nix-instantiate";
