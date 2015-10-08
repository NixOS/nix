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

    switch (v.type()) {

        case Value::tInt:
            doc.writeEmptyElement("int", singletonAttrs("value", (format("%1%") % v.asInt()).str()));
            break;

        case Value::tBool:
            doc.writeEmptyElement("bool", singletonAttrs("value", v.asBool() ? "true" : "false"));
            break;

        case Value::tString:
            /* !!! show the context? */
            copyContext(v, context);
            doc.writeEmptyElement("string", singletonAttrs("value", v.asString()));
            break;

        case Value::tPath:
            doc.writeEmptyElement("path", singletonAttrs("value", v.asPath()));
            break;

        case Value::tNull:
            doc.writeEmptyElement("null");
            break;

        case Value::tAttrs:
            if (state.isDerivation(v)) {
                XMLAttrs xmlAttrs;

                Bindings::iterator a = v.asAttrs()->find(state.symbols.create("derivation"));

                Path drvPath;
                a = v.asAttrs()->find(state.sDrvPath);
                if (a != v.asAttrs()->end()) {
                    if (strict) state.forceValue(*a->value);
                    if (a->value->type() == Value::tString)
                        xmlAttrs["drvPath"] = drvPath = a->value->asString();
                }

                a = v.asAttrs()->find(state.sOutPath);
                if (a != v.asAttrs()->end()) {
                    if (strict) state.forceValue(*a->value);
                    if (a->value->type() == Value::tString)
                        xmlAttrs["outPath"] = a->value->asString();
                }

                XMLOpenElement _(doc, "derivation", xmlAttrs);

                if (drvPath != "" && drvsSeen.find(drvPath) == drvsSeen.end()) {
                    drvsSeen.insert(drvPath);
                    showAttrs(state, strict, location, *v.asAttrs(), doc, context, drvsSeen);
                } else
                    doc.writeEmptyElement("repeated");
            }

            else {
                XMLOpenElement _(doc, "attrs");
                showAttrs(state, strict, location, *v.asAttrs(), doc, context, drvsSeen);
            }

            break;

        case Value::tList0:
        case Value::tList1:
        case Value::tList2:
        case Value::tListN: {
            XMLOpenElement _(doc, "list");
            Value::asList list(v);
            for (unsigned int n = 0; n < list.length(); ++n)
                printValueAsXML(state, strict, location, *list[n], doc, context, drvsSeen);
            break;
        }

        case Value::tLambda: {
            XMLAttrs xmlAttrs;
            if (location) posToXML(xmlAttrs, v.asLambda()->pos);
            XMLOpenElement _(doc, "function", xmlAttrs);

            if (v.asLambda()->matchAttrs) {
                XMLAttrs attrs;
                if (!v.asLambda()->arg.empty()) attrs["name"] = v.asLambda()->arg;
                if (v.asLambda()->formals->ellipsis) attrs["ellipsis"] = "1";
                XMLOpenElement _(doc, "attrspat", attrs);
                for (auto & i : v.asLambda()->formals->formals)
                    doc.writeEmptyElement("attr", singletonAttrs("name", i.name));
            } else
                doc.writeEmptyElement("varpat", singletonAttrs("name", v.asLambda()->arg));

            break;
        }

        case Value::tExternal:
            v.asExternal()->printValueAsXML(state, strict, location, doc, context, drvsSeen);
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
