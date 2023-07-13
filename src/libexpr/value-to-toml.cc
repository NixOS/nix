#include "value-to-toml.hh"
#include "eval-inline.hh"
#include "util.hh"
#include "store-api.hh"

#include "../toml11/toml.hpp"

#include <cstdlib>
#include <iomanip>


namespace nix {
using toml_value = toml::basic_value<toml::discard_comments, std::map, std::vector>;

toml_value printValueAsTOML(EvalState & state, bool strict,
    Value & v, const PosIdx pos, NixStringContext & context, bool copyToStore)
{
    checkInterrupt();

    if (strict) state.forceValue(v, pos);

    toml_value out;

    switch (v.type()) {
        case nInt:
            out = v.integer;
            break;

        case nBool:
            out = v.boolean;
            break;

        case nString:
            copyContext(v, context);
            out = v.string.s;
            break;

        case nPath:
            if (copyToStore)
                out = state.store->printStorePath(state.copyPathToStore(context, v.path()));
            else
                out = v.path().path.abs();
            break;

        case nAttrs: {
            auto maybeString = state.tryAttrsToString(pos, v, context, false, false);
            if (maybeString) {
                out = *maybeString;
                break;
            }
            auto i = v.attrs->find(state.sOutPath);
            if (i == v.attrs->end()) {
                out = toml_value::table_type();
                StringSet names;
                for (auto & j : *v.attrs)
                    names.emplace(state.symbols[j.name]);
                for (auto & j : names) {
                    Attr & a(*v.attrs->find(state.symbols.create(j)));
                    out[j] = printValueAsTOML(state, strict, *a.value, a.pos, context, copyToStore);
                }
            } else
                return printValueAsTOML(state, strict, *i->value, i->pos, context, copyToStore);
            break;
        }

        case nList: {
            out = toml_value::array_type();
            for (auto elem : v.listItems())
                out.push_back(printValueAsTOML(state, strict, *elem, pos, context, copyToStore));
            break;
        }

        case nExternal:
            return v.external->printValueAsTOML(state, strict, context, copyToStore);
            break;

        case nFloat:
            out = v.fpoint;
            break;

        case nNull:
        case nThunk:
        case nFunction:
            auto e = TypeError({
                .msg = hintfmt("cannot convert %1% to a TOML value", showType(v)),
                .errPos = state.positions[v.determinePos(pos)]
            });
            e.addTrace(state.positions[pos], hintfmt("message for the trace"));
            state.debugThrowLastTrace(e);
            throw e;
    }
    return out;
}

void printValueAsTOML(EvalState & state, bool strict,
    Value & v, const PosIdx pos, std::ostream & str, NixStringContext & context, bool copyToStore)
{
    str << printValueAsTOML(state, strict, v, pos, context, copyToStore);
}

toml_value ExternalValueBase::printValueAsTOML(EvalState & state, bool strict,
    NixStringContext & context, bool copyToStore) const
{
    state.debugThrowLastTrace(TypeError("cannot convert %1% to a TOML value", showType()));
}


}
