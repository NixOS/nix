#include "json-to-value.hh"

#include <variant>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace nix {

// for more information, refer to
// https://github.com/nlohmann/json/blob/master/include/nlohmann/detail/input/json_sax.hpp
class JSONSax : nlohmann::json_sax<json> {
    class JSONState {
    protected:
        std::unique_ptr<JSONState> parent;
        RootValue v;
    public:
        virtual std::unique_ptr<JSONState> resolve(EvalState &)
        {
            throw std::logic_error("tried to close toplevel json parser state");
        }
        explicit JSONState(std::unique_ptr<JSONState> && p) : parent(std::move(p)) {}
        explicit JSONState(Value * v) : v(allocRootValue(v)) {}
        JSONState(JSONState & p) = delete;
        Value & value(EvalState & state)
        {
            if (!v)
                v = allocRootValue(state.allocValue());
            return **v;
        }
        virtual ~JSONState() {}
        virtual void add() {}
    };

    class JSONObjectState : public JSONState {
        using JSONState::JSONState;
        ValueMap attrs;
        std::unique_ptr<JSONState> resolve(EvalState & state) override
        {
            Value & v = parent->value(state);
            state.mkAttrs(v, attrs.size());
            for (auto & i : attrs)
                v.attrs->push_back(Attr(i.first, i.second));
            return std::move(parent);
        }
        void add() override { v = nullptr; }
    public:
        void key(string_t & name, EvalState & state)
        {
            attrs.insert_or_assign(state.symbols.create(name), &value(state));
        }
    };

    class JSONListState : public JSONState {
        ValueVector values;
        std::unique_ptr<JSONState> resolve(EvalState & state) override
        {
            Value & v = parent->value(state);
            state.mkList(v, values.size());
            for (size_t n = 0; n < values.size(); ++n) {
                v.listElems()[n] = values[n];
            }
            return std::move(parent);
        }
        void add() override {
            values.push_back(*v);
            v = nullptr;
        }
    public:
        JSONListState(std::unique_ptr<JSONState> && p, std::size_t reserve) : JSONState(std::move(p))
        {
            values.reserve(reserve);
        }
    };

    EvalState & state;
    std::unique_ptr<JSONState> rs;

    template<typename T, typename... Args> inline bool handle_value(T f, Args... args)
    {
        f(rs->value(state), args...);
        rs->add();
        return true;
    }

public:
    JSONSax(EvalState & state, Value & v) : state(state), rs(new JSONState(&v)) {};

    bool null()
    {
        return handle_value(mkNull);
    }

    bool boolean(bool val)
    {
        return handle_value(mkBool, val);
    }

    bool number_integer(number_integer_t val)
    {
        return handle_value(mkInt, val);
    }

    bool number_unsigned(number_unsigned_t val)
    {
        return handle_value(mkInt, val);
    }

    bool number_float(number_float_t val, const string_t & s)
    {
        return handle_value(mkFloat, val);
    }

    bool string(string_t & val)
    {
        return handle_value<void(Value&, const char*)>(mkString, val.c_str());
    }

    bool start_object(std::size_t len)
    {
        rs = std::make_unique<JSONObjectState>(std::move(rs));
        return true;
    }

    bool key(string_t & name)
    {
        dynamic_cast<JSONObjectState*>(rs.get())->key(name, state);
        return true;
    }

    bool end_object() {
        rs = rs->resolve(state);
        rs->add();
        return true;
    }

    bool end_array() {
        return end_object();
    }

    bool start_array(size_t len) {
        rs = std::make_unique<JSONListState>(std::move(rs),
            len != std::numeric_limits<size_t>::max() ? len : 128);
        return true;
    }

    bool parse_error(std::size_t, const std::string&, const nlohmann::detail::exception& ex) {
        throw JSONParseError(ex.what());
    }
};

void parseJSON(EvalState & state, const string & s_, Value & v)
{
    JSONSax parser(state, v);
    bool res = json::sax_parse(s_, &parser);
    if (!res)
        throw JSONParseError("Invalid JSON Value");
}

}
