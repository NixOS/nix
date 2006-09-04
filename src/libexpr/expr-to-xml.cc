#include "expr-to-xml.hh"
#include "xml-writer.hh"
#include "nixexpr-ast.hh"
#include "aterm.hh"


namespace nix {

    
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
    ATermList as, es, formals;
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

    else if (matchList(e, es)) {
        XMLOpenElement _(doc, "list");
        for (ATermIterator i(es); i; ++i)
            printTermAsXML(*i, doc);
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


void printTermAsXML(Expr e, std::ostream & out)
{
    XMLWriter doc(true, out);
    XMLOpenElement root(doc, "expr");
    printTermAsXML(e, doc);
}

 
}
