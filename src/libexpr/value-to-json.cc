#include "value-to-json.hh"
#include "eval-inline.hh"
#include "util.hh"

#include <cstdlib>
#include <iomanip>


namespace nix {


void escapeJSON(std::ostream & str, const string & s)
{
    str << "\"";
    for (auto & i : s)
        if (i == '\"' || i == '\\') str << "\\" << i;
        else if (i == '\n') str << "\\n";
        else if (i == '\r') str << "\\r";
        else if (i == '\t') str << "\\t";
        else if (i >= 0 && i < 32)
            str << "\\u" << std::setfill('0') << std::setw(4) << std::hex << (uint16_t) i << std::dec;
        else str << i;
    str << "\"";
}


void printValueAsJSON(EvalState & state, bool strict,
    Value & v, std::ostream & str, PathSet & context)
{
    checkInterrupt();

    if (strict) state.forceValue(v);

    switch (v.type()) {

        case Value::tInt:
            str << v.asInt();
            break;

        case Value::tBool:
            str << (v.asBool() ? "true" : "false");
            break;

        case Value::tString:
            copyContext(v, context);
            escapeJSON(str, v.asString());
            break;

        case Value::tPath:
            escapeJSON(str, state.copyPathToStore(context, v.asPath()));
            break;

        case Value::tNull:
            str << "null";
            break;

        case Value::tAttrs: {
            Bindings::iterator i = v.asAttrs()->find(state.sOutPath);
            if (i == v.asAttrs()->end()) {
                JSONObject json(str);
                StringSet names;
                for (auto & j : *v.asAttrs())
                    names.insert(j.name);
                for (auto & j : names) {
                    Attr & a(*v.asAttrs()->find(state.symbols.create(j)));
                    json.attr(j);
                    printValueAsJSON(state, strict, *a.value, str, context);
                }
            } else
                printValueAsJSON(state, strict, *i->value, str, context);
            break;
        }

        case Value::tList0:
        case Value::tList1:
        case Value::tList2:
        case Value::tListN: {
            JSONList json(str);
            Value::asList list(v);
            for (unsigned int n = 0; n < list.length(); ++n) {
                json.elem();
                printValueAsJSON(state, strict, *list[n], str, context);
            }
            break;
        }

        case Value::tExternal:
            v.asExternal()->printValueAsJSON(state, strict, str, context);
            break;

        default:
            throw TypeError(format("cannot convert %1% to JSON") % showType(v));
    }
}


void ExternalValueBase::printValueAsJSON(EvalState & state, bool strict,
      std::ostream & str, PathSet & context) const
{
    throw TypeError(format("cannot convert %1% to JSON") % showType());
}


}
