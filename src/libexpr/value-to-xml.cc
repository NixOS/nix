#include "nix/expr/value-to-xml.hh"
#include "nix/util/xml-writer.hh"
#include "nix/expr/eval-inline.hh"
#include "nix/util/finally.hh"
#include "nix/util/signals.hh"
#include "value-path.hh"

#include <cstdlib>
#include <sstream>
#include <boost/unordered/unordered_flat_map.hpp>

namespace nix {

MakeError(XMLCycleError, InfiniteRecursionError);

using SeenValuePaths = boost::unordered_flat_map<const void *, ValuePath>;

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
    StringSet & drvsSeen,
    SeenValuePaths & seen,
    ValuePath * currentPath,
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
    StringSet & drvsSeen,
    SeenValuePaths & seen,
    ValuePath * currentPath)
{
    StringSet names;

    for (auto & a : attrs.lexicographicOrder(state.symbols)) {
        XMLAttrs xmlAttrs;
        xmlAttrs["name"] = state.symbols[a->name];
        if (location && a->pos)
            posToXML(state, xmlAttrs, state.positions[a->pos]);

        XMLOpenElement _(doc, "attr", xmlAttrs);
        if (currentPath)
            currentPath->emplace_back(a->name);
        Finally popSegment([&] {
            if (currentPath)
                currentPath->pop_back();
        });
        printValueAsXML(state, strict, location, *a->value, doc, context, drvsSeen, seen, currentPath, a->pos);
    }
}

static void printValueAsXML(
    EvalState & state,
    bool strict,
    bool location,
    Value & v,
    XMLWriter & doc,
    NixStringContext & context,
    StringSet & drvsSeen,
    SeenValuePaths & seen,
    ValuePath * currentPath,
    const PosIdx pos)
{
    checkInterrupt();

    auto _level = state.addCallDepth(pos);

    if (strict)
        state.forceValue(v, pos);

    auto cycleError = [&](const ValuePath & firstSeenAt) {
        if (currentPath)
            state
                .error<InfiniteRecursionError>(
                    "infinite recursion encountered while converting Nix value to XML: %s is the same as %s (only cycle-free Nix values can be converted to XML)",
                    showValuePath(state.symbols, *currentPath),
                    showValuePath(state.symbols, firstSeenAt))
                .atPos(v.determinePos(pos))
                .debugThrow();
        else
            // Internal signal caught by the public wrapper, which then re-runs
            // with path tracking against a discard XMLWriter. Bypasses
            // EvalErrorBuilder so we don't need an explicit template
            // instantiation in eval-error.cc.
            throw XMLCycleError(
                state,
                "infinite recursion encountered while converting Nix value to XML (only cycle-free Nix values can be converted to XML)");
    };

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

            std::string drvPath;
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
                showAttrs(state, strict, location, *v.attrs(), doc, context, drvsSeen, seen, currentPath);
            else
                doc.writeEmptyElement("repeated");
        }

        else {
            const void * key = v.attrs();
            auto [it, fresh] = seen.try_emplace(key, currentPath ? *currentPath : ValuePath{});
            if (!fresh)
                cycleError(it->second);
            Finally cleanup([&] { seen.erase(key); });
            XMLOpenElement _(doc, "attrs");
            showAttrs(state, strict, location, *v.attrs(), doc, context, drvsSeen, seen, currentPath);
        }

        break;

    case nList: {
        const void * key = &v;
        auto [it, fresh] = seen.try_emplace(key, currentPath ? *currentPath : ValuePath{});
        if (!fresh)
            cycleError(it->second);
        Finally cleanup([&] { seen.erase(key); });
        XMLOpenElement _(doc, "list");
        size_t i = 0;
        for (auto v2 : v.listView()) {
            if (currentPath)
                currentPath->emplace_back(i);
            Finally popSegment([&] {
                if (currentPath)
                    currentPath->pop_back();
            });
            printValueAsXML(state, strict, location, *v2, doc, context, drvsSeen, seen, currentPath, pos);
            i++;
        }
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
    // Historically, a tried and then ignored value (e.g. through tryEval) was
    // reverted to the original thunk.
    case nFailed:
        doc.writeEmptyElement("unevaluated");
        break;
    }
}

void ExternalValueBase::printValueAsXML(
    EvalState & state,
    bool strict,
    bool location,
    XMLWriter & doc,
    NixStringContext & context,
    StringSet & drvsSeen,
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
    StringSet drvsSeen;
    SeenValuePaths seen;
    try {
        XMLWriter doc(true, out);
        XMLOpenElement root(doc, "expr");
        printValueAsXML(state, strict, location, v, doc, context, drvsSeen, seen, nullptr, pos);
    } catch (XMLCycleError &) {
        // The fast pass detected a cycle. Re-walk against a discard target with path tracking
        // so we can produce a richer error that names both ends of the cycle.
        std::ostringstream discard;
        XMLWriter doc(true, discard);
        XMLOpenElement root(doc, "expr");
        seen.clear();
        drvsSeen.clear();
        ValuePath path;
        printValueAsXML(state, strict, location, v, doc, context, drvsSeen, seen, &path, pos);
    }
}

} // namespace nix
