#include "value-to-xml.hh"
#include "xml-writer.hh"
#include "eval-inline.hh"
#include "util.hh"

#include <cstdlib>


namespace nix {


static void escapeJSON(std::ostream & str, const string & s)
{
    str << "\"";
    foreach (string::const_iterator, i, s)
        if (*i == '\"' || *i == '\\') str << "\\" << *i;
        else if (*i == '\n') str << "\\n";
        else if (*i == '\r') str << "\\r";
        else if (*i == '\t') str << "\\t";
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
                str << "{";
                StringSet names;
                foreach (Bindings::iterator, i, *v.attrs)
                    names.insert(i->name);
                bool first = true;
                foreach (StringSet::iterator, i, names) {
                    if (!first) str << ","; else first = false;
                    Attr & a(*v.attrs->find(state.symbols.create(*i)));
                    escapeJSON(str, *i);
                    str << ":";
                    printValueAsJSON(state, strict, *a.value, str, context);
                }
                str << "}";
            } else
                printValueAsJSON(state, strict, *i->value, str, context);
            break;
        }

        case tList: {
            str << "[";
            bool first = true;
            for (unsigned int n = 0; n < v.list.length; ++n) {
                if (!first) str << ","; else first = false;
                printValueAsJSON(state, strict, *v.list.elems[n], str, context);
            }
            str << "]";
            break;
        }

        default:
            throw TypeError(format("cannot convert %1% to JSON") % showType(v));
    }
}


}
