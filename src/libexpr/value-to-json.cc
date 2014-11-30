#include "value-to-json.hh"
#include "eval-inline.hh"
#include "util.hh"

#include <cstdlib>
#include <iomanip>


namespace nix {


void escapeJSON(std::ostream & str, const string & s)
{
    str << "\"";
    foreach (string::const_iterator, i, s)
        if (*i == '\"' || *i == '\\') str << "\\" << *i;
        else if (*i == '\n') str << "\\n";
        else if (*i == '\r') str << "\\r";
        else if (*i == '\t') str << "\\t";
        else if (*i >= 0 && *i < 32)
            str << "\\u" << std::setfill('0') << std::setw(4) << std::hex << (uint16_t) *i << std::dec;
        else str << *i;
    str << "\"";
}


void printValueAsJSON(EvalState & state, bool strict,
    Value & v, std::ostream & str, PathSet & context)
{
    checkInterrupt();

    if (strict) state.forceValue(v);

    switch (v.type) {

        case tInt:
            str << v.integer;
            break;

        case tBool:
            str << (v.boolean ? "true" : "false");
            break;

        case tString:
            copyContext(v, context);
            escapeJSON(str, v.string.s);
            break;

        case tPath:
            escapeJSON(str, state.copyPathToStore(context, v.path));
            break;

        case tNull:
            str << "null";
            break;

        case tAttrs: {
            Bindings::iterator i = v.attrs->find(state.sOutPath);
            if (i == v.attrs->end()) {
                JSONObject json(str);
                StringSet names;
                foreach (Bindings::iterator, i, *v.attrs)
                    names.insert(i->name);
                foreach (StringSet::iterator, i, names) {
                    Attr & a(*v.attrs->find(state.symbols.create(*i)));
                    json.attr(*i);
                    printValueAsJSON(state, strict, *a.value, str, context);
                }
            } else
                printValueAsJSON(state, strict, *i->value, str, context);
            break;
        }

        case tList: {
            JSONList json(str);
            for (unsigned int n = 0; n < v.list.length; ++n) {
                json.elem();
                printValueAsJSON(state, strict, *v.list.elems[n], str, context);
            }
            break;
        }

	case tExternal:
            v.external->printValueAsJSON(state, strict, str, context);
            break;

        default:
            throw TypeError(format("cannot convert %1% to JSON") % showType(v));
    }
}


void ExternalValueBase::printValueAsJSON(EvalState & state, bool strict,
      std::ostream & str, PathSet & context)
{
    throw TypeError(format("cannot convert %1% to JSON") % showType());
}


}
