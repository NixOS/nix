#include "json-to-value.hh"

#include <variant>
#include <nlohmann/json.hpp>
#include <nlohmann/detail/exceptions.hpp>

using json = nlohmann::json;

namespace nix {

// for more information, refer to
// https://github.com/nlohmann/json/blob/master/include/nlohmann/detail/input/json_sax.hpp
class JSONSax : nlohmann::json_sax<json> {
    class JSONState {
    protected:
        JSONState* parent;
        Value * v;
    public:
        virtual JSONState* resolve(EvalState &)
        {
            throw std::logic_error("tried to close toplevel json parser state");
        };
        explicit JSONState(JSONState* p) : parent(p), v(nullptr) {};
        explicit JSONState(Value* v) : v(v) {};
        JSONState(JSONState& p) = delete;
        Value& value(EvalState & state)
        {
            if (v == nullptr)
                v = state.allocValue();
            return *v;
        };
        virtual ~JSONState() {};
        virtual void add() {};
    };

    class JSONObjectState : public JSONState {
        using JSONState::JSONState;
        ValueMap attrs = ValueMap();
        virtual JSONState* resolve(EvalState & state) override
        {
            Value& v = parent->value(state);
            state.mkAttrs(v, attrs.size());
            for (auto & i : attrs)
                v.attrs->push_back(Attr(i.first, i.second));
            return parent;
        }
        virtual void add() override { v = nullptr; };
    public:
        void key(string_t& name, EvalState & state)
        {
            attrs[state.symbols.create(name)] = &value(state);
        }
    };

    class JSONListState : public JSONState {
        ValueVector values = ValueVector();
        virtual JSONState* resolve(EvalState & state) override
        {
            Value& v = parent->value(state);
            state.mkList(v, values.size());
            for (size_t n = 0; n < values.size(); ++n) {
                v.listElems()[n] = values[n];
            }
            return parent;
        }
        virtual void add() override {
            values.push_back(v);
            v = nullptr;
        };
    public:
        JSONListState(JSONState* p, std::size_t reserve) : JSONState(p)
        {
            values.reserve(reserve);
        }
    };

    EvalState & state;
    JSONState* rs;

    template<typename T, typename... Args> inline bool handle_value(T f, Args... args)
    {
        f(rs->value(state), args...);
        rs->add();
        return true;
    }

public:
    JSONSax(EvalState & state, Value & v) : state(state), rs(new JSONState(&v)) {};
    ~JSONSax() { delete rs; };

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

    bool number_float(number_float_t val, const string_t& s)
    {
        return handle_value(mkFloat, val);
    }

    bool string(string_t& val)
    {
        return handle_value<void(Value&, const char*)>(mkString, val.c_str());
    }

    bool start_object(std::size_t len)
    {
        JSONState* old = rs;
        rs = new JSONObjectState(old);
        return true;
    }

    bool key(string_t& name)
    {
        dynamic_cast<JSONObjectState*>(rs)->key(name, state);
        return true;
    }

    bool end_object() {
        JSONState* old = rs;
        rs = old->resolve(state);
        delete old;
        rs->add();
        return true;
    }

    bool end_array() {
        return end_object();
    }

    bool start_array(size_t len) {
        JSONState* old = rs;
        rs = new JSONListState(old, len != std::numeric_limits<size_t>::max() ? len : 128);
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
