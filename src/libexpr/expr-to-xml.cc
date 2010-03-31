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
        ATerm attrRHS = attrs.get(toATerm(*i));
	ATerm attr;
	Pos pos;
	XMLAttrs xmlAttrs;

	xmlAttrs["name"] = *i;
	if(matchAttrRHS(attrRHS, attr, pos)) {
	    ATerm path;
	    int line, column;
	    if (matchPos(pos, path, line, column)) {
		xmlAttrs["path"] = aterm2String(path);
		xmlAttrs["line"] = (format("%1%") % line).str();
		xmlAttrs["column"] = (format("%1%") % column).str();
	    }
	} else
	    abort(); // Should not happen.

        XMLOpenElement _(doc, "attr", xmlAttrs);
        printTermAsXML(attr, doc, context, drvsSeen);
    }
}


static void printPatternAsXML(Pattern pat, XMLWriter & doc)
{
    ATerm name;
    ATermList formals;
    Pattern pat1, pat2;
    ATermBool ellipsis;
    if (matchVarPat(pat, name))
        doc.writeEmptyElement("varpat", singletonAttrs("name", aterm2String(name)));
    else if (matchAttrsPat(pat, formals, ellipsis)) {
        XMLOpenElement _(doc, "attrspat");
        for (ATermIterator i(formals); i; ++i) {
            Expr name; ATerm dummy;
            if (!matchFormal(*i, name, dummy)) abort();
            doc.writeEmptyElement("attr", singletonAttrs("name", aterm2String(name)));
        }
        if (ellipsis == eTrue) doc.writeEmptyElement("ellipsis");
    }
    else if (matchAtPat(pat, pat1, pat2)) {
        XMLOpenElement _(doc, "at");
        printPatternAsXML(pat1, doc);
        printPatternAsXML(pat2, doc);
    }
}


static void printTermAsXML(Expr e, XMLWriter & doc, PathSet & context,
    ExprSet & drvsSeen)
{
    XMLAttrs attrs;
    string s;
    ATerm s2;
    int i;
    ATermList as, es;
    ATerm pat, body, pos;

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
        queryAllAttrs(e, attrs, true);

        Expr aRHS = attrs.get(toATerm("type"));
	Expr a = NULL;
	if (aRHS)
	    matchAttrRHS(aRHS, a, pos);
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

    else if (matchFunction(e, pat, body, pos)) {
        ATerm path;
	int line, column;
	XMLAttrs xmlAttrs;
	if (matchPos(pos, path, line, column)) {
	    xmlAttrs["path"] = aterm2String(path);
	    xmlAttrs["line"] = (format("%1%") % line).str();
	    xmlAttrs["column"] = (format("%1%") % column).str();
	}
	XMLOpenElement _(doc, "function", xmlAttrs);
        printPatternAsXML(pat, doc);
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
