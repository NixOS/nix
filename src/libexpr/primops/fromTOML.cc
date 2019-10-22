#include "primops.hh"
#include "eval-inline.hh"

#include "cpptoml/cpptoml.h"

namespace nix {

static void prim_fromTOML(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    using namespace cpptoml;

    auto toml = state.forceStringNoCtx(*args[0], pos);

    std::istringstream tomlStream(toml);

    std::function<void(Value &, std::shared_ptr<base>)> visit;

    visit = [&](Value & v, std::shared_ptr<base> t) {

        if (auto t2 = t->as_table()) {

            size_t size = 0;
            for (auto & i : *t2) { (void) i; size++; }

            state.mkAttrs(v, size);

            for (auto & i : *t2) {
                auto & v2 = *state.allocAttr(v, state.symbols.create(i.first));

                if (auto i2 = i.second->as_table_array()) {
                    size_t size2 = i2->get().size();
                    state.mkList(v2, size2);
                    for (size_t j = 0; j < size2; ++j)
                        visit(*(v2.listElems()[j] = state.allocValue()), i2->get()[j]);
                }
                else
                    visit(v2, i.second);
            }

            v.attrs->sort();
        }

        else if (auto t2 = t->as_array()) {
            size_t size = t2->get().size();

            state.mkList(v, size);

            for (size_t i = 0; i < size; ++i)
                visit(*(v.listElems()[i] = state.allocValue()), t2->get()[i]);
        }

        // Handle cases like 'a = [[{ a = true }]]', which IMHO should be
        // parsed as a array containing an array containing a table,
        // but instead are parsed as an array containing a table array
        // containing a table.
        else if (auto t2 = t->as_table_array()) {
            size_t size = t2->get().size();

            state.mkList(v, size);

            for (size_t j = 0; j < size; ++j)
                visit(*(v.listElems()[j] = state.allocValue()), t2->get()[j]);
        }

        else if (t->is_value()) {
            if (auto val = t->as<int64_t>())
                mkInt(v, val->get());
            else if (auto val = t->as<NixFloat>())
                mkFloat(v, val->get());
            else if (auto val = t->as<bool>())
                mkBool(v, val->get());
            else if (auto val = t->as<std::string>())
                mkString(v, val->get());
            else
                throw EvalError("unsupported value type in TOML");
        }

        else abort();
    };

    try {
        visit(v, parser(tomlStream).parse());
    } catch (std::runtime_error & e) {
        throw EvalError("while parsing a TOML string at %s: %s", pos, e.what());
    }
}

static RegisterPrimOp r("fromTOML", 1, prim_fromTOML);

}
