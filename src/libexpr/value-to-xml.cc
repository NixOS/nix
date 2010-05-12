#include "value-to-xml.hh"
#include "xml-writer.hh"
#include "util.hh"

#include <cstdlib>


namespace nix {

    
static XMLAttrs singletonAttrs(const string & name, const string & value)
{
    XMLAttrs attrs;
    attrs[name] = value;
    return attrs;
}


static void printValueAsXML(EvalState & state, bool strict, bool location,
    Value & v, XMLWriter & doc, PathSet & context, PathSet & drvsSeen);


static void posToXML(XMLAttrs & xmlAttrs, const Pos & pos)
{
    xmlAttrs["path"] = pos.file;
    xmlAttrs["line"] = (format("%1%") % pos.line).str();
    xmlAttrs["column"] = (format("%1%") % pos.column).str();
}


static void showAttrs(EvalState & state, bool strict, bool location,
    Bindings & attrs, XMLWriter & doc, PathSet & context, PathSet & drvsSeen)
{
    StringSet names;
    
    foreach (Bindings::iterator, i, attrs)
        names.insert(i->first);
    
    foreach (StringSet::iterator, i, names) {
        Attr & a(attrs[state.symbols.create(*i)]);
        
        XMLAttrs xmlAttrs;
        xmlAttrs["name"] = *i;
        if (location && a.pos != &noPos) posToXML(xmlAttrs, *a.pos);
        
        XMLOpenElement _(doc, "attr", xmlAttrs);
        printValueAsXML(state, strict, location,
            a.value, doc, context, drvsSeen);
    }
}


static void printValueAsXML(EvalState & state, bool strict, bool location,
    Value & v, XMLWriter & doc, PathSet & context, PathSet & drvsSeen)
{
    checkInterrupt();

    if (strict) state.forceValue(v);
        
    switch (v.type) {

        case tInt:
            doc.writeEmptyElement("int", singletonAttrs("value", (format("%1%") % v.integer).str()));
            break;

        case tBool:
            doc.writeEmptyElement("bool", singletonAttrs("value", v.boolean ? "true" : "false"));
            break;

        case tString:
            /* !!! show the context? */
            doc.writeEmptyElement("string", singletonAttrs("value", v.string.s));
            break;

        case tPath:
            doc.writeEmptyElement("path", singletonAttrs("value", v.path));
            break;

        case tNull:
            doc.writeEmptyElement("null");
            break;

        case tAttrs:
            if (state.isDerivation(v)) {
                XMLAttrs xmlAttrs;
            
                Bindings::iterator a = v.attrs->find(state.symbols.create("derivation"));

                Path drvPath;
                a = v.attrs->find(state.sDrvPath);
                if (a != v.attrs->end()) {
                    if (strict) state.forceValue(a->second.value);
                    if (a->second.value.type == tString)
                        xmlAttrs["drvPath"] = drvPath = a->second.value.string.s;
                }
        
                a = v.attrs->find(state.sOutPath);
                if (a != v.attrs->end()) {
                    if (strict) state.forceValue(a->second.value);
                    if (a->second.value.type == tString)
                        xmlAttrs["outPath"] = a->second.value.string.s;
                }

                XMLOpenElement _(doc, "derivation", xmlAttrs);

                if (drvPath != "" && drvsSeen.find(drvPath) == drvsSeen.end()) {
                    drvsSeen.insert(drvPath);
                    showAttrs(state, strict, location, *v.attrs, doc, context, drvsSeen);
                } else
                    doc.writeEmptyElement("repeated");
            }

            else {
                XMLOpenElement _(doc, "attrs");
                showAttrs(state, strict, location, *v.attrs, doc, context, drvsSeen);
            }
            
            break;

        case tList: {
            XMLOpenElement _(doc, "list");
            for (unsigned int n = 0; n < v.list.length; ++n)
                printValueAsXML(state, strict, location, *v.list.elems[n], doc, context, drvsSeen);
            break;
        }

        case tLambda: {
            XMLAttrs xmlAttrs;
            if (location) posToXML(xmlAttrs, v.lambda.fun->pos);
            XMLOpenElement _(doc, "function", xmlAttrs);
            
            if (v.lambda.fun->matchAttrs) {
                XMLAttrs attrs;
                if (!v.lambda.fun->arg.empty()) attrs["name"] = v.lambda.fun->arg;
                if (v.lambda.fun->formals->ellipsis) attrs["ellipsis"] = "1";
                XMLOpenElement _(doc, "attrspat", attrs);
                foreach (Formals::Formals_::iterator, i, v.lambda.fun->formals->formals)
                    doc.writeEmptyElement("attr", singletonAttrs("name", i->name));
            } else
                doc.writeEmptyElement("varpat", singletonAttrs("name", v.lambda.fun->arg));
            
            break;
        }

        default:
            doc.writeEmptyElement("unevaluated");
    }
}


void printValueAsXML(EvalState & state, bool strict, bool location,
    Value & v, std::ostream & out, PathSet & context)
{
    XMLWriter doc(true, out);
    XMLOpenElement root(doc, "expr");
    PathSet drvsSeen;    
    printValueAsXML(state, strict, location, v, doc, context, drvsSeen);
}

 
}
