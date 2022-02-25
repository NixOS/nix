#include "primops.hh"
#include "eval-inline.hh"

#include "../../toml11/toml.hpp"

namespace nix {

static void prim_fromTOML(EvalState & state, const Pos & pos, Value * * args, Value & val)
{
    auto toml = state.forceStringNoCtx(*args[0], pos);

    std::istringstream tomlStream(std::string{toml});

    std::function<void(Value &, toml::value)> visit;

    visit = [&](Value & v, toml::value t) {

        switch(t.type())
        {
            case toml::value_t::table:
                {
                    auto table = toml::get<toml::table>(t);

                    size_t size = 0;
                    for (auto & i : table) { (void) i; size++; }

                    auto attrs = state.buildBindings(size);

                    for(auto & elem : table)
                        visit(attrs.alloc(elem.first), elem.second);

                    v.mkAttrs(attrs);
                }
                break;;
            case toml::value_t::array:
                {
                    auto array = toml::get<std::vector<toml::value>>(t);

                    size_t size = array.size();
                    state.mkList(v, size);
                    for (size_t i = 0; i < size; ++i)
                        visit(*(v.listElems()[i] = state.allocValue()), array[i]);
                }
                break;;
            case toml::value_t::boolean:
                v.mkBool(toml::get<bool>(t));
                break;;
            case toml::value_t::integer:
                v.mkInt(toml::get<int64_t>(t));
                break;;
            case toml::value_t::floating:
                v.mkFloat(toml::get<NixFloat>(t));
                break;;
            case toml::value_t::string:
                v.mkString(toml::get<std::string>(t));
                break;;
            case toml::value_t::local_datetime:
            case toml::value_t::offset_datetime:
            case toml::value_t::local_date:
            case toml::value_t::local_time:
                // We fail since Nix doesn't have date and time types
                throw std::runtime_error("Dates and times are not supported");
                break;;
            case toml::value_t::empty:
                v.mkNull();
                break;;

        }
    };

    try {
        visit(val, toml::parse(tomlStream, "fromTOML" /* the "filename" */));
    } catch (std::exception & e) { // TODO: toml::syntax_error
        throw EvalError({
            .msg = hintfmt("while parsing a TOML string: %s", e.what()),
            .errPos = pos
        });
    }
}

static RegisterPrimOp primop_fromTOML("fromTOML", 1, prim_fromTOML);

}
