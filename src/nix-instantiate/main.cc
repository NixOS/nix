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


static void printTermAsXML(Expr e, XMLWriter & doc)
{
    XMLAttrs attrs;
    ATerm s;
    int i;
    ATermList as, formals;
    ATerm body, pos;

    if (matchStr(e, s))
        doc.writeEmptyElement("string", singletonAttrs("value", aterm2String(s)));

    else if (matchPath(e, s))
        doc.writeEmptyElement("path", singletonAttrs("value", aterm2String(s)));

    else if (matchUri(e, s))
        doc.writeEmptyElement("uri", singletonAttrs("value", aterm2String(s)));

    else if (matchNull(e))
        doc.writeEmptyElement("null");

    else if (matchInt(e, i))
        doc.writeEmptyElement("int", singletonAttrs("value", (format("%1%") % i).str()));

    else if (e == eTrue)
        doc.writeEmptyElement("bool", singletonAttrs("value", "true"));

    else if (e == eFalse)
        doc.writeEmptyElement("bool", singletonAttrs("value", "false"));

    else if (matchAttrs(e, as)) {
        XMLOpenElement _(doc, "attrs");
        ATermMap attrs(128);
        queryAllAttrs(e, attrs);
        StringSet names;
        for (ATermMap::const_iterator i = attrs.begin(); i != attrs.end(); ++i)
            names.insert(aterm2String(i->key));
        for (StringSet::iterator i = names.begin(); i != names.end(); ++i) {
            XMLOpenElement _(doc, "attr", singletonAttrs("name", *i));
            printTermAsXML(attrs.get(toATerm(*i)), doc);
        }
    }

    else if (matchFunction(e, formals, body, pos)) {
        XMLOpenElement _(doc, "function");
        
        for (ATermIterator i(formals); i; ++i) {
            Expr name; ValidValues valids; ATerm dummy;
            if (!matchFormal(*i, name, valids, dummy)) abort();
            XMLOpenElement _(doc, "arg", singletonAttrs("name", aterm2String(name)));

            ATermList valids2;
            if (matchValidValues(valids, valids2)) {
                for (ATermIterator j(valids2); j; ++j) {
                    XMLOpenElement _(doc, "value");
                    printTermAsXML(*j, doc);
                }
            }
        }
    }

    else
        doc.writeEmptyElement("unevaluated");
}


static void printResult(EvalState & state, Expr e,
    bool evalOnly, bool xmlOutput, const ATermMap & autoArgs)
{
    if (evalOnly)
        if (xmlOutput) {
            XMLWriter doc(true, cout);
            XMLOpenElement root(doc, "expr");
            printTermAsXML(e, doc);
        } else
            cout << format("%1%\n") % e;
    
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


Expr strictEval(EvalState & state, Expr e)
{
    e = evalExpr(state, e);

    ATermList as;

    if (matchAttrs(e, as)) {
        ATermList as2 = ATempty;
        for (ATermIterator i(as); i; ++i) {
            ATerm name; Expr e; ATerm pos;
            if (!matchBind(*i, name, e, pos)) abort(); /* can't happen */
            as2 = ATinsert(as2, makeBind(name, strictEval(state, e), pos));
        }
        return makeAttrs(ATreverse(as2));
    }

    ATermList formals;
    ATerm body, pos;

    if (matchFunction(e, formals, body, pos)) {
        ATermList formals2 = ATempty;
        
        for (ATermIterator i(formals); i; ++i) {
            Expr name; ValidValues valids; ATerm dummy;
            if (!matchFormal(*i, name, valids, dummy)) abort();

            ATermList valids2;
            if (matchValidValues(valids, valids2)) {
                ATermList valids3 = ATempty;
                for (ATermIterator j(valids2); j; ++j)
                    valids3 = ATinsert(valids3, strictEval(state, *j));
                valids = makeValidValues(ATreverse(valids3));
            }

            formals2 = ATinsert(formals2, makeFormal(name, valids, dummy));
        }
        return makeFunction(ATreverse(formals2), body, pos);
    }
    
    return e;
}


Expr doEval(EvalState & state, string attrPath, bool parseOnly, bool strict,
    const ATermMap & autoArgs, Expr e)
{
    e = findAlongAttrPath(state, attrPath, autoArgs, e);
    if (!parseOnly)
        if (strict)
            e = strictEval(state, e);
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
            printMsg(lvlError, format("X %1% Y %2%") % name % value);
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
