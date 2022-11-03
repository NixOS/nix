#include "value-to-json.hh"
#include "json.hh"
#include "eval-inline.hh"
#include "util.hh"

#include <cstdlib>
#include <iomanip>


namespace nix {

void printValueAsJSON(EvalState & state, bool strict,
    Value & v, const PosIdx pos, JSONPlaceholder & out, PathSet & context, bool copyToStore)
{
    checkInterrupt();

    if (strict) state.forceValue(v, pos);

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
            if (copyToStore)
                out.write(state.copyPathToStore(context, v.path));
            else
                out.write(v.path);
            break;

        case nNull:
            out.write(nullptr);
            break;

        case nAttrs: {
            auto maybeString = state.tryAttrsToString(pos, v, context, false, false);
            if (maybeString) {
                out.write(*maybeString);
                break;
            }
            auto i = v.attrs->find(state.sOutPath);
            if (i == v.attrs->end()) {
                auto obj(out.object());
                StringSet names;
                for (auto & j : *v.attrs)
                    names.emplace(state.symbols[j.name]);
                for (auto & j : names) {
                    Attr & a(*v.attrs->find(state.symbols.create(j)));
                    auto placeholder(obj.placeholder(j));
                    printValueAsJSON(state, strict, *a.value, a.pos, placeholder, context, copyToStore);
                }
            } else
                printValueAsJSON(state, strict, *i->value, i->pos, out, context, copyToStore);
            break;
        }

        case nList: {
            auto list(out.list());
            for (auto elem : v.listItems()) {
                auto placeholder(list.placeholder());
                printValueAsJSON(state, strict, *elem, pos, placeholder, context, copyToStore);
            }
            break;
        }

        case nExternal:
            v.external->printValueAsJSON(state, strict, out, context, copyToStore);
            break;

        case nFloat:
            out.write(v.fpoint);
            break;

        case nThunk:
        case nFunction:
            auto e = TypeError({
                .msg = hintfmt("cannot convert %1% to JSON", showType(v)),
                .errPos = state.positions[v.determinePos(pos)]
            });
            e.addTrace(state.positions[pos], hintfmt("message for the trace"));
            state.debugThrowLastTrace(e);
            throw e;
    }
}

void printValueAsJSON(EvalState & state, bool strict,
    Value & v, const PosIdx pos, std::ostream & str, PathSet & context, bool copyToStore)
{
    JSONPlaceholder out(str);
    printValueAsJSON(state, strict, v, pos, out, context, copyToStore);
}

void ExternalValueBase::printValueAsJSON(EvalState & state, bool strict,
    JSONPlaceholder & out, PathSet & context, bool copyToStore) const
{
    state.debugThrowLastTrace(TypeError("cannot convert %1% to JSON", showType()));
}


}
