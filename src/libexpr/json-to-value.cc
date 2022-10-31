#include "json-to-value.hh"

#include <variant>

#ifdef HAVE_LIBSIMDJSON

#include <iterator>
#include <simdjson.h>

using namespace simdjson;

namespace nix {

static void parse_json(EvalState &state, const dom::element &doc, Value &v) {
  switch (doc.type()) {
  case dom::element_type::OBJECT: {
    dom::object obj = doc.get_object().value_unsafe();
    size_t len = obj.size();
    if (simdjson_unlikely(len == 0xFFFFFF)) {
        len = 0;
        for (auto _x : obj) len++;
    }
    BindingsBuilder builder(state, state.allocBindings(len));
    for (dom::key_value_pair field : obj) {
      /* JSON strings can contain null bytes.
         Putting them in nix symbols can have unforeseen consequences. */
      if (simdjson_unlikely(field.key.find((char)0) != std::string::npos)) {
        field.key = field.key.substr(0, field.key.find((char)0));
      }
      Value &v2 = builder.alloc(field.key);
      parse_json(state, field.value, v2);
    }
    // TODO: handle duplicate values
    v.mkAttrs(builder);
  }; break;
  case dom::element_type::ARRAY: {
    dom::array arr = doc.get_array().value_unsafe();
    size_t len = arr.size();
    if (simdjson_unlikely(len == 0xFFFFFF)) {
        len = 0;
        for (auto _x : arr) len++;
    }
    state.mkList(v, len);
    size_t i = 0;
    for (dom::element x : arr) {
      Value *v2 = state.allocValue();
      v.listElems()[i++] = v2;
      parse_json(state, x, *v2);
    }
  }; break;
  case dom::element_type::STRING: {
    /* Can't use mkString(std::string_view) since it calls strlen,
       but our strings aren't zero-terminated so this is really slow */
    // todo: handle null byte
    std::string_view str = doc.get_string().value_unsafe();
    char *buf = (char *)GC_MALLOC_ATOMIC(str.length() + 1);
    str.copy(buf, -1);
    buf[str.length()] = 0;
    v.mkString(buf);
  }; break;
  case dom::element_type::BOOL:
    v.mkBool(doc.get_bool().value_unsafe());
    break;
  case dom::element_type::NULL_VALUE:
    v.mkNull();
    break;
  case dom::element_type::UINT64: case dom::element_type::INT64:
    v.mkInt(doc.get_int64().value_unsafe());
    break;
  case dom::element_type::DOUBLE:
    v.mkFloat(doc.get_double().value_unsafe());
    break;
  default:
    assert(false);
  }
}

void parseJSON(EvalState & state, const std::string_view & s_, Value & v)
{
  try {
      //using namespace std::chrono;
    //high_resolution_clock::time_point t1 = high_resolution_clock::now();

    static thread_local dom::parser parser;

    /* simdjson needs 64 readable bytes after the string.
       Since there's no way to guarantee this, copy the string to a temporary buffer
       This doesn't seem to be slower than calling GC_REALLOC on the string. */
    padded_string json_padded = padded_string(s_);
    auto doc = parser.parse(json_padded);
    parse_json(state, doc.value(), v);
    //high_resolution_clock::time_point t2 = high_resolution_clock::now();
    //duration<double> time_span = duration_cast<duration<double>>(t2 - t1);

    //printMsg(lvlTalkative, format("json parse: %1%s") % time_span.count());
  } catch(simdjson_error &e) {
      throw JSONParseError(e.what());
  }
}

}

#else
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
            auto attrs2 = state.buildBindings(attrs.size());
            for (auto & i : attrs)
                attrs2.insert(i.first, i.second);
            parent->value(state).mkAttrs(attrs2.alreadySorted());
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

public:
    JSONSax(EvalState & state, Value & v) : state(state), rs(new JSONState(&v)) {};

    bool null()
    {
        rs->value(state).mkNull();
        rs->add();
        return true;
    }

    bool boolean(bool val)
    {
        rs->value(state).mkBool(val);
        rs->add();
        return true;
    }

    bool number_integer(number_integer_t val)
    {
        rs->value(state).mkInt(val);
        rs->add();
        return true;
    }

    bool number_unsigned(number_unsigned_t val)
    {
        rs->value(state).mkInt(val);
        rs->add();
        return true;
    }

    bool number_float(number_float_t val, const string_t & s)
    {
        rs->value(state).mkFloat(val);
        rs->add();
        return true;
    }

    bool string(string_t & val)
    {
        rs->value(state).mkString(val);
        rs->add();
        return true;
    }

#if NLOHMANN_JSON_VERSION_MAJOR >= 3 && NLOHMANN_JSON_VERSION_MINOR >= 8
    bool binary(binary_t&)
    {
        // This function ought to be unreachable
        assert(false);
        return true;
    }
#endif

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

void parseJSON(EvalState & state, const std::string_view & s_, Value & v)
{
    JSONSax parser(state, v);
    bool res = json::sax_parse(s_, &parser);
    if (!res)
        throw JSONParseError("Invalid JSON Value");
}

}
#endif /* ndef HAVE_LIB_SIMDJSON */
