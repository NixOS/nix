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
#include "xml-writer.hh"
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


static XMLAttrs singletonAttrs(const string & name, const string & value)
{
    XMLAttrs attrs;
    attrs[name] = value;
    return attrs;
}


static void printTermAsXML(EvalState & state, Expr e, XMLWriter & doc)
{
    XMLAttrs attrs;
    ATerm s;
    int i;
    ATermList as;
    
    if (matchStr(e, s))
        doc.writeEmptyElement("string", singletonAttrs("value", aterm2String(s)));

    else if (matchPath(e, s))
        doc.writeEmptyElement("path", singletonAttrs("value", aterm2String(s)));

    else if (matchUri(e, s))
        doc.writeEmptyElement("uri", singletonAttrs("value", aterm2String(s)));

    else if (matchNull(e))
        doc.writeEmptyElement("null");

    else if (matchInt(e, i))
        doc.writeEmptyElement("int",singletonAttrs("value", (format("%1%") % i).str()));

    else if (matchAttrs(e, as)) {
        XMLOpenElement _(doc, "attrs");
        ATermMap attrs(128);
        queryAllAttrs(e, attrs);
        for (ATermMap::const_iterator i = attrs.begin(); i != attrs.end(); ++i) {
            XMLOpenElement _(doc, "attr", singletonAttrs("name", aterm2String(i->key)));
            printTermAsXML(state, i->value, doc);
        }
    }

    else
        doc.writeEmptyElement("unknown");
}


static void printResult(EvalState & state, Expr e,
    bool evalOnly, bool printArgs, bool xmlOutput,
    const ATermMap & autoArgs)
{
    if (evalOnly)
        if (xmlOutput) {
            XMLWriter doc(true, cout);
            XMLOpenElement root(doc, "expr");
            printTermAsXML(state, e, doc);
        } else
            cout << format("%1%\n") % e;
    
    else if (printArgs) {
        XMLWriter doc(true, cout);
        XMLOpenElement root(doc, "args");
            
        ATermList formals;
        ATerm body, pos;
        
        if (matchFunction(e, formals, body, pos)) {
            for (ATermIterator i(formals); i; ++i) {
                Expr name; ValidValues valids; ATerm dummy;
                if (!matchFormal(*i, name, valids, dummy)) abort();

                XMLAttrs attrs;
                attrs["name"] = aterm2String(name);
                XMLOpenElement elem(doc, "arg", attrs);

                ATermList valids2;
                if (matchValidValues(valids, valids2)) {
                    for (ATermIterator j(valids2); j; ++j) {
                        Expr e = evalExpr(state, *j);
                        XMLAttrs attrs;
                        attrs["value"] = showValue(e);
                        XMLOpenElement elem(doc, "value", attrs);
                    }
                }
            }
        } else
            printMsg(lvlError, "warning: expression does not evaluate to a function");
    }
    
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
    bool xmlOutput = false;
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
        else if (arg == "--print-args") {
            readOnlyMode = true;
            printArgs = true;
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
        else if (arg[0] == '-')
            throw UsageError(format("unknown flag `%1%'") % arg);
        else
            files.push_back(arg);
    }

    openDB();

    if (readStdin) {
        Expr e = findAlongAttrPath(state, attrPath, parseStdin(state));
        if (!parseOnly) e = evalExpr(state, e);
        printResult(state, e, evalOnly, printArgs, xmlOutput, autoArgs);
    }

    for (Strings::iterator i = files.begin();
         i != files.end(); i++)
    {
        Path path = absPath(*i);
        Expr e = findAlongAttrPath(state, attrPath,
            parseExprFromFile(state, path));
        if (!parseOnly) e = evalExpr(state, e);
        printResult(state, e, evalOnly, printArgs, xmlOutput, autoArgs);
    }

    printEvalStats(state);
}


string programId = "nix-instantiate";
