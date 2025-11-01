#include "nix/expr/value-to-xml.hh"
#include "nix/util/xml-writer.hh"
#include "nix/expr/eval-inline.hh"
#include "nix/util/signals.hh"

#include <cstdlib>

namespace nix {

static XMLAttrs singletonAttrs(const std::string & name, std::string_view value)
{
    XMLAttrs attrs;
    attrs[name] = value;
    return attrs;
}

static void printValueAsXML(
    EvalState & state,
    bool strict,
    bool location,
    Value & v,
    XMLWriter & doc,
    NixStringContext & context,
    PathSet & drvsSeen,
    const PosIdx pos);

static void posToXML(EvalState & state, XMLAttrs & xmlAttrs, const Pos & pos)
{
    if (auto path = std::get_if<SourcePath>(&pos.origin))
        xmlAttrs["path"] = path->path.abs();
    xmlAttrs["line"] = fmt("%1%", pos.line);
    xmlAttrs["column"] = fmt("%1%", pos.column);
}

static void showAttrs(
    EvalState & state,
    bool strict,
    bool location,
    const Bindings & attrs,
    XMLWriter & doc,
    NixStringContext & context,
    PathSet & drvsSeen)
{
    StringSet names;

    for (auto & a : attrs.lexicographicOrder(state.symbols)) {
        XMLAttrs xmlAttrs;
        xmlAttrs["name"] = state.symbols[a->name];
        if (location && a->pos)
            posToXML(state, xmlAttrs, state.positions[a->pos]);

        XMLOpenElement _(doc, "attr", xmlAttrs);
        printValueAsXML(state, strict, location, *a->value, doc, context, drvsSeen, a->pos);
    }
}

static void printValueAsXML(
    EvalState & state,
    bool strict,
    bool location,
    Value & v,
    XMLWriter & doc,
    NixStringContext & context,
    PathSet & drvsSeen,
    const PosIdx pos)
{
    checkInterrupt();

    if (strict)
        state.forceValue(v, pos);

    switch (v.type()) {

    case nInt:
        doc.writeEmptyElement("int", singletonAttrs("value", fmt("%1%", v.integer())));
        break;

    case nBool:
        doc.writeEmptyElement("bool", singletonAttrs("value", v.boolean() ? "true" : "false"));
        break;

    case nString:
        /* !!! show the context? */
        copyContext(v, context);
        doc.writeEmptyElement("string", singletonAttrs("value", v.string_view()));
        break;

    case nPath:
        doc.writeEmptyElement("path", singletonAttrs("value", v.path().to_string()));
        break;

    case nNull:
        doc.writeEmptyElement("null");
        break;

    case nAttrs:
        if (state.isDerivation(v)) {
            XMLAttrs xmlAttrs;

            Path drvPath;
            if (auto a = v.attrs()->get(state.s.drvPath)) {
                if (strict)
                    state.forceValue(*a->value, a->pos);
                if (a->value->type() == nString)
                    xmlAttrs["drvPath"] = drvPath = a->value->string_view();
            }

            if (auto a = v.attrs()->get(state.s.outPath)) {
                if (strict)
                    state.forceValue(*a->value, a->pos);
                if (a->value->type() == nString)
                    xmlAttrs["outPath"] = a->value->string_view();
            }

            XMLOpenElement _(doc, "derivation", xmlAttrs);

            if (drvPath != "" && drvsSeen.insert(drvPath).second)
                showAttrs(state, strict, location, *v.attrs(), doc, context, drvsSeen);
            else
                doc.writeEmptyElement("repeated");
        }

        else {
            XMLOpenElement _(doc, "attrs");
            showAttrs(state, strict, location, *v.attrs(), doc, context, drvsSeen);
        }

        break;

    case nList: {
        XMLOpenElement _(doc, "list");
        for (auto v2 : v.listView())
            printValueAsXML(state, strict, location, *v2, doc, context, drvsSeen, pos);
        break;
    }

    case nFunction: {
        if (!v.isLambda()) {
            // FIXME: Serialize primops and primopapps
            doc.writeEmptyElement("unevaluated");
            break;
        }
        XMLAttrs xmlAttrs;
        if (location)
            posToXML(state, xmlAttrs, state.positions[v.lambda().fun->pos]);
        XMLOpenElement _(doc, "function", xmlAttrs);

        if (auto formals = v.lambda().fun->getFormals()) {
            XMLAttrs attrs;
            if (v.lambda().fun->arg)
                attrs["name"] = state.symbols[v.lambda().fun->arg];
            if (formals->ellipsis)
                attrs["ellipsis"] = "1";
            XMLOpenElement _(doc, "attrspat", attrs);
            for (auto & i : formals->lexicographicOrder(state.symbols))
                doc.writeEmptyElement("attr", singletonAttrs("name", state.symbols[i.name]));
        } else
            doc.writeEmptyElement("varpat", singletonAttrs("name", state.symbols[v.lambda().fun->arg]));

        break;
    }

    case nExternal:
        v.external()->printValueAsXML(state, strict, location, doc, context, drvsSeen, pos);
        break;

    case nFloat:
        doc.writeEmptyElement("float", singletonAttrs("value", fmt("%1%", v.fpoint())));
        break;

    case nThunk:
        doc.writeEmptyElement("unevaluated");
    }
}

void ExternalValueBase::printValueAsXML(
    EvalState & state,
    bool strict,
    bool location,
    XMLWriter & doc,
    NixStringContext & context,
    PathSet & drvsSeen,
    const PosIdx pos) const
{
    doc.writeEmptyElement("unevaluated");
}

void printValueAsXML(
    EvalState & state,
    bool strict,
    bool location,
    Value & v,
    std::ostream & out,
    NixStringContext & context,
    const PosIdx pos)
{
    XMLWriter doc(true, out);
    XMLOpenElement root(doc, "expr");
    PathSet drvsSeen;
    printValueAsXML(state, strict, location, v, doc, context, drvsSeen, pos);
}

} // namespace nix
