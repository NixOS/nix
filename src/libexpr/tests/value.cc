#include <rapidcheck.h>

#include "tests/value.hh"
#include "value.hh"

namespace rc {
using namespace nix;

Gen<Value> genTOMLSerializableNixValue(EvalState & state) {
    auto bareKeys = gen::container<std::string>(gen::elementOf(std::string("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-")));

    return gen::oneOf(
        // nInt
        gen::apply(
            [&](NixInt val) {
                Value * v = state.allocValue();
                v->mkInt(val);
                return *v;
            },
            gen::arbitrary<NixInt>()
        ),
        // nFloat
        gen::apply(
            [&](NixFloat val) {
                Value * v = state.allocValue();
                v->mkFloat(val);
                return *v;
            },
            gen::arbitrary<NixFloat>()
        ),
        // nBool
        gen::apply(
            [&](bool val) {
                Value * v = state.allocValue();
                v->mkBool(val);
                return *v;
            },
            gen::arbitrary<bool>()
        ),
        // nString
        gen::apply(
            [&](std::string val) {
                Value * v = state.allocValue();
                v->mkString(val);
                return *v;
            },
            gen::string<std::string>()
        ),
        // nAttrs
        gen::exec(
            [&]() {
                Value * v = state.allocValue();
                int size = *gen::inRange(0, 100);
                BindingsBuilder builder = state.buildBindings(size);

                for (auto i = 0; i < size; i++) {
                    std::string key = *bareKeys;
                    Value * val = state.allocValue();
                    *val = *genTOMLSerializableNixValue(state);
                    // can't figure out why this segfaults
                    builder.insert(state.symbols.create(key), val);
                }

                v->mkAttrs(builder);

                return *v;
            }
        )
        // nList
        // gen::exec(
        //     [&]() {
        //         Value v;
        //         int size = *gen::inRange(0, 100);
        //         state.mkList(v, list.size());
        //
        //         auto elems = v.listElems();
        //
        //         for (auto i = 0; i < v.listSize(); i++) {
        //             *elems[i] = *genTOMLSerializableNixValue(state);
        //         }

        //         return v;
        //     },
        // )
    );
}

} // namespace rc
