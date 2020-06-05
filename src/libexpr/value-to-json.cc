#include "value-to-json.hh"
#include "json.hh"
#include "eval-inline.hh"
#include "util.hh"

#include <cstdlib>
#include <iomanip>


namespace nix {

void printValueAsJSON(EvalState & state, bool strict,
    Value & v, JSONPlaceholder & out, PathSet & context)
{
    checkInterrupt();

    if (strict) state.forceValue(v);

    switch (v.type) {

        case tInt:
            out.write(v.integer);
            break;

        case tBool:
            out.write(v.boolean);
            break;

        case tString:
            copyContext(v, context);
            out.write(v.string.s);
            break;

        case tPath:
            out.write(state.copyPathToStore(context, v.path));
            break;

        case tNull:
            out.write(nullptr);
            break;

        case tAttrs: {
            auto maybeString = state.tryAttrsToString(noPos, v, context, false, false);
            if (maybeString) {
                out.write(*maybeString);
                break;
            }
            auto i = v.attrs->find(state.sOutPath);
            if (i == v.attrs->end()) {
                auto obj(out.object());
                StringSet names;
                for (auto & j : *v.attrs)
                    names.insert(j.name);
                for (auto & j : names) {
                    Attr & a(*v.attrs->find(state.symbols.create(j)));
                    auto placeholder(obj.placeholder(j));
                    printValueAsJSON(state, strict, *a.value, placeholder, context);
                }
            } else
                printValueAsJSON(state, strict, *i->value, out, context);
            break;
        }

        case tList1: case tList2: case tListN: {
            auto list(out.list());
            for (unsigned int n = 0; n < v.listSize(); ++n) {
                auto placeholder(list.placeholder());
                printValueAsJSON(state, strict, *v.listElems()[n], placeholder, context);
            }
            break;
        }

        case tExternal:
            v.external->printValueAsJSON(state, strict, out, context);
            break;

        case tFloat:
            out.write(v.fpoint);
            break;

        default:
            throw TypeError(format("cannot convert %1% to JSON") % showType(v));
    }
}

void printValueAsJSON(EvalState & state, bool strict,
    Value & v, std::ostream & str, PathSet & context)
{
    JSONPlaceholder out(str);
    printValueAsJSON(state, strict, v, out, context);
}

void ExternalValueBase::printValueAsJSON(EvalState & state, bool strict,
    JSONPlaceholder & out, PathSet & context) const
{
    throw TypeError(format("cannot convert %1% to JSON") % showType());
}


}
