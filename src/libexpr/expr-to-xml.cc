#include "expr-to-xml.hh"
#include "xml-writer.hh"
#include "nixexpr-ast.hh"
#include "aterm.hh"
#include "util.hh"

#include <cstdlib>


namespace nix {

    
static XMLAttrs singletonAttrs(const string & name, const string & value)
{
    XMLAttrs attrs;
    attrs[name] = value;
    return attrs;
}


/* set<Expr> is safe because all the expressions are also reachable
   from the stack, therefore can't be garbage-collected. */
typedef set<Expr> ExprSet;


static void printTermAsXML(Expr e, XMLWriter & doc, PathSet & context,
    ExprSet & drvsSeen);


static void showAttrs(const ATermMap & attrs, XMLWriter & doc,
    PathSet & context, ExprSet & drvsSeen)
{
    StringSet names;
    for (ATermMap::const_iterator i = attrs.begin(); i != attrs.end(); ++i)
        names.insert(aterm2String(i->key));
    for (StringSet::iterator i = names.begin(); i != names.end(); ++i) {
        XMLOpenElement _(doc, "attr", singletonAttrs("name", *i));
        printTermAsXML(attrs.get(toATerm(*i)), doc, context, drvsSeen);
    }
}


static void printTermAsXML(Expr e, XMLWriter & doc, PathSet & context,
    ExprSet & drvsSeen)
{
    XMLAttrs attrs;
    string s;
    ATerm s2;
    int i;
    ATermList as, es, formals;
    ATerm body, pos;

    checkInterrupt();

    if (matchStr(e, s, context)) /* !!! show the context? */
        doc.writeEmptyElement("string", singletonAttrs("value", s));

    else if (matchPath(e, s2))
        doc.writeEmptyElement("path", singletonAttrs("value", aterm2String(s2)));

    else if (matchNull(e))
        doc.writeEmptyElement("null");

    else if (matchInt(e, i))
        doc.writeEmptyElement("int", singletonAttrs("value", (format("%1%") % i).str()));

    else if (e == eTrue)
        doc.writeEmptyElement("bool", singletonAttrs("value", "true"));

    else if (e == eFalse)
        doc.writeEmptyElement("bool", singletonAttrs("value", "false"));

    else if (matchAttrs(e, as)) {
        ATermMap attrs;
        queryAllAttrs(e, attrs);

        Expr a = attrs.get(toATerm("type"));
        if (a && matchStr(a, s, context) && s == "derivation") {

            XMLAttrs xmlAttrs;
            Path outPath, drvPath;
            
            a = attrs.get(toATerm("drvPath"));
            if (matchStr(a, drvPath, context))
                xmlAttrs["drvPath"] = drvPath;
        
            a = attrs.get(toATerm("outPath"));
            if (matchStr(a, outPath, context))
                xmlAttrs["outPath"] = outPath;
        
            XMLOpenElement _(doc, "derivation", xmlAttrs);

            if (drvsSeen.find(e) == drvsSeen.end()) {
                drvsSeen.insert(e);
                showAttrs(attrs, doc, context, drvsSeen);
            } else
                doc.writeEmptyElement("repeated");
        }

        else {
            XMLOpenElement _(doc, "attrs");
            showAttrs(attrs, doc, context, drvsSeen);
        }
    }

    else if (matchList(e, es)) {
        XMLOpenElement _(doc, "list");
        for (ATermIterator i(es); i; ++i)
            printTermAsXML(*i, doc, context, drvsSeen);
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
                    printTermAsXML(*j, doc, context, drvsSeen);
                }
            }
        }
    }

    else
        doc.writeEmptyElement("unevaluated");
}


void printTermAsXML(Expr e, std::ostream & out, PathSet & context)
{
    XMLWriter doc(true, out);
    XMLOpenElement root(doc, "expr");
    ExprSet drvsSeen;    
    printTermAsXML(e, doc, context, drvsSeen);
}

 
}
