#include "globals.hh"
#include "shared.hh"
#include "eval.hh"
#include "eval-inline.hh"
#include "get-drvs.hh"
#include "attr-path.hh"
#include "value-to-xml.hh"
#include "value-to-json.hh"
#include "util.hh"
#include "store-api.hh"
#include "common-opts.hh"
#include "misc.hh"

#include <map>
#include <iostream>


using namespace nix;


static Expr * parseStdin(EvalState & state)
{
    startNest(nest, lvlTalkative, format("parsing standard input"));
    return state.parseExprFromString(drainFD(0), absPath("."));
}


static Path gcRoot;
static int rootNr = 0;
static bool indirectRoot = false;


enum OutputKind { okPlain, okXML, okJSON };


void processExpr(EvalState & state, const Strings & attrPaths,
    bool parseOnly, bool strict, Bindings & autoArgs,
    bool evalOnly, OutputKind output, bool location, Expr * e)
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
        if (evalOnly) {
            Value vRes;
            if (autoArgs.empty())
                vRes = v;
            else
                state.autoCallFunction(autoArgs, v, vRes);
            if (output == okXML)
                printValueAsXML(state, strict, location, vRes, std::cout, context);
            else if (output == okJSON)
                printValueAsJSON(state, strict, vRes, std::cout, context);
            else {
                if (strict) state.forceValueDeep(vRes);
                std::cout << vRes << std::endl;
            }
        } else {
            DrvInfos drvs;
            getDerivations(state, v, "", autoArgs, drvs, false);
            foreach (DrvInfos::iterator, i, drvs) {
                Path drvPath = i->queryDrvPath();

                /* What output do we want? */
                string outputName = i->queryOutputName();
                if (outputName == "")
                    throw Error(format("derivation ‘%1%’ lacks an ‘outputName’ attribute ") % drvPath);

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

    state.printCanaries();
}


int main(int argc, char * * argv)
{
    return handleExceptions(argv[0], [&]() {
        initNix();

        Strings files, searchPath;
        bool readStdin = false;
        bool fromArgs = false;
        bool findFile = false;
        bool evalOnly = false;
        bool parseOnly = false;
        OutputKind outputKind = okPlain;
        bool xmlOutputSourceLocation = true;
        bool strict = false;
        Strings attrPaths;
        bool wantsReadWrite = false;
        std::map<string, string> autoArgs_;
        bool repair = false;

        parseCmdLine(argc, argv, [&](Strings::iterator & arg, const Strings::iterator & end) {
            if (*arg == "--help")
                showManPage("nix-instantiate");
            else if (*arg == "--version")
                printVersion("nix-instantiate");
            else if (*arg == "-")
                readStdin = true;
            else if (*arg == "--expr" || *arg == "-E")
                fromArgs = true;
            else if (*arg == "--eval" || *arg == "--eval-only")
                evalOnly = true;
            else if (*arg == "--read-write-mode")
                wantsReadWrite = true;
            else if (*arg == "--parse" || *arg == "--parse-only")
                parseOnly = evalOnly = true;
            else if (*arg == "--find-file")
                findFile = true;
            else if (*arg == "--attr" || *arg == "-A")
                attrPaths.push_back(getArg(*arg, arg, end));
            else if (parseAutoArgs(arg, end, autoArgs_))
                ;
            else if (parseSearchPathArg(arg, end, searchPath))
                ;
            else if (*arg == "--add-root")
                gcRoot = getArg(*arg, arg, end);
            else if (*arg == "--indirect")
                indirectRoot = true;
            else if (*arg == "--xml")
                outputKind = okXML;
            else if (*arg == "--json")
                outputKind = okJSON;
            else if (*arg == "--no-location")
                xmlOutputSourceLocation = false;
            else if (*arg == "--strict")
                strict = true;
            else if (*arg == "--repair")
                repair = true;
            else if (*arg == "--dry-run")
                settings.readOnlyMode = true;
            else if (*arg != "" && arg->at(0) == '-')
                return false;
            else
                files.push_back(*arg);
            return true;
        });

        EvalState state(searchPath);
        state.repair = repair;

        Bindings & autoArgs(*evalAutoArgs(state, autoArgs_));

        if (evalOnly && !wantsReadWrite)
            settings.readOnlyMode = true;

        if (attrPaths.empty()) attrPaths.push_back("");

        if (findFile) {
            foreach (Strings::iterator, i, files) {
                Path p = state.findFile(*i);
                if (p == "") throw Error(format("unable to find ‘%1%’") % *i);
                std::cout << p << std::endl;
            }
            return;
        }

        store = openStore();

        if (readStdin) {
            Expr * e = parseStdin(state);
            processExpr(state, attrPaths, parseOnly, strict, autoArgs,
                evalOnly, outputKind, xmlOutputSourceLocation, e);
        } else if (files.empty() && !fromArgs)
            files.push_back("./default.nix");

        foreach (Strings::iterator, i, files) {
            Expr * e = fromArgs
                ? state.parseExprFromString(*i, absPath("."))
                : state.parseExprFromFile(resolveExprPath(lookupFileArg(state, *i)));
            processExpr(state, attrPaths, parseOnly, strict, autoArgs,
                evalOnly, outputKind, xmlOutputSourceLocation, e);
        }

        state.printStats();
    });
}
