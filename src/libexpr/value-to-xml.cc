#include "value-to-xml.hh"
#include "xml-writer.hh"
#include "eval-inline.hh"
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

    for (auto & i : attrs)
        names.insert(i.name);

    for (auto & i : names) {
        Attr & a(*attrs.find(state.symbols.create(i)));

        XMLAttrs xmlAttrs;
        xmlAttrs["name"] = i;
        if (location && a.pos != &noPos) posToXML(xmlAttrs, *a.pos);

        XMLOpenElement _(doc, "attr", xmlAttrs);
        printValueAsXML(state, strict, location,
            *a.value, doc, context, drvsSeen);
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
            copyContext(v, context);
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
                    if (strict) state.forceValue(*a->value);
                    if (a->value->type == tString)
                        xmlAttrs["drvPath"] = drvPath = a->value->string.s;
                }

                a = v.attrs->find(state.sOutPath);
                if (a != v.attrs->end()) {
                    if (strict) state.forceValue(*a->value);
                    if (a->value->type == tString)
                        xmlAttrs["outPath"] = a->value->string.s;
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

        case tList1: case tList2: case tListN: {
            XMLOpenElement _(doc, "list");
            for (unsigned int n = 0; n < v.listSize(); ++n)
                printValueAsXML(state, strict, location, *v.listElems()[n], doc, context, drvsSeen);
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
                for (auto & i : v.lambda.fun->formals->formals)
                    doc.writeEmptyElement("attr", singletonAttrs("name", i.name));
            } else
                doc.writeEmptyElement("varpat", singletonAttrs("name", v.lambda.fun->arg));

            break;
        }

        case tExternal:
            v.external->printValueAsXML(state, strict, location, doc, context, drvsSeen);
            break;

        case tFloat:
            doc.writeEmptyElement("float", singletonAttrs("value", (format("%1%") % v.fpoint).str()));
            break;

        default:
            doc.writeEmptyElement("unevaluated");
    }
}


void ExternalValueBase::printValueAsXML(EvalState & state, bool strict,
    bool location, XMLWriter & doc, PathSet & context, PathSet & drvsSeen) const
{
    doc.writeEmptyElement("unevaluated");
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
