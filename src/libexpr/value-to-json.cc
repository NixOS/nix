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

    switch (v.type()) {

        case nInt:
            out.write(v.integer);
            break;

        case nBool:
            out.write(v.boolean);
            break;

        case nString:
            copyContext(v, context);
            out.write(v.string.s);
            break;

        case nPath:
            out.write(state.copyPathToStore(context, v.path));
            break;

        case nNull:
            out.write(nullptr);
            break;

        case nAttrs: {
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

        case nList: {
            auto list(out.list());
            for (unsigned int n = 0; n < v.listSize(); ++n) {
                auto placeholder(list.placeholder());
                printValueAsJSON(state, strict, *v.listElems()[n], placeholder, context);
            }
            break;
        }

        case nExternal:
            v.external->printValueAsJSON(state, strict, out, context);
            break;

        case nFloat:
            out.write(v.fpoint);
            break;

        case nThunk:
            throw TypeError("cannot convert %1% to JSON", showType(v));

        case nFunction:
            throw TypeError("cannot convert %1% to JSON", showType(v));
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
    throw TypeError("cannot convert %1% to JSON", showType());
}


}
