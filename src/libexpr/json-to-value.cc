#include "json-to-value.hh"

#ifdef HAVE_LIBSIMDJSON

#if HAVE_BOEHMGC
#define GC_NEW_ABORTS_ON_OOM 1
#include <gc_cpp.h>
#endif

#include <simdjson.h>

using namespace simdjson;

namespace nix {

/* dom::document with gc finalizer calling dom::document::~document() */
struct DocumentRef
#if HAVE_BOEHMGC
    : gc_cleanup
#endif
{
    dom::document doc;
};
DocumentRef& allocDocument() {
    return *(new DocumentRef());
}

static void parse_json(EvalState &state, const dom::element &elem, Value &v, const DocumentRef& doc);

/* Expression representing a partially evaluated json value */
struct ExprJSON : Expr
{
    /* dom::document uses std::unique_ptr internally, so GC can't trace through elem.
       Keep a reference to doc here. */
    const DocumentRef& doc;
    const dom::element elem;
    ExprJSON(const DocumentRef& doc, const dom::element elem) : doc(doc), elem(elem) { };
    virtual void eval(EvalState & state, Env & e, Value & v) override {
        parse_json(state, elem, v, doc);
    }
};

static void parse_json(EvalState &state, const dom::element &elem, Value &v, const DocumentRef& doc) {
    // TODO (C++20): using enum dom::element_type;
    switch (elem.type()) {
    case dom::element_type::OBJECT: {
        dom::object obj = elem.get_object().value_unsafe();
        size_t len = obj.size();
        if (simdjson_unlikely(len == 0xFFFFFF)) {
            // TODO (C++20): len = std::distance(obj.begin(), obj.end());
            len = 0;
            for (auto _x : obj) {
                (void)_x;
                len++;
            }
        }
        auto builder = state.buildBindings(len);
        for (dom::key_value_pair field : obj) {
            /* JSON strings can contain null bytes.
               Putting them in nix symbols can have unforeseen consequences. */
            if (simdjson_unlikely(field.key.find((char)0) != std::string::npos)) {
                field.key = field.key.substr(0, field.key.find((char)0));
            }
            Value &v2 = builder.alloc(field.key);
            if (field.value.is_array() || field.value.is_object()) {
                v2.mkThunk(nullptr, new
#if HAVE_BOEHMGC
                (GC)
#endif
                    ExprJSON(doc, field.value));
            } else {
                parse_json(state, field.value, v2, doc);
            }
        }
        // TODO: handle duplicate values
        v.mkAttrs(builder);
    }; break;
    case dom::element_type::ARRAY: {
        dom::array arr = elem.get_array().value_unsafe();
        size_t len = arr.size();
        if (simdjson_unlikely(len == 0xFFFFFF)) {
            // TODO (C++20): len = std::distance(arr.begin(), arr.end());
            len = 0;
            for (auto _x : arr) {
                (void)_x;
                len++;
            }
        }
        state.mkList(v, len);
        size_t i = 0;
        for (dom::element x : arr) {
            Value &v2 = *state.allocValue();
            v.listElems()[i++] = &v2;
            parse_json(state, x, v2, doc);
        }
    }; break;
    case dom::element_type::STRING: {
        v.mkString(elem.get_string().value_unsafe());
    }; break;
    case dom::element_type::BOOL:
        v.mkBool(elem.get_bool().value_unsafe());
        break;
    case dom::element_type::NULL_VALUE:
        v.mkNull();
        break;
    case dom::element_type::UINT64:
    case dom::element_type::INT64:
        v.mkInt(elem.get_int64().value_unsafe());
        break;
    case dom::element_type::DOUBLE:
        v.mkFloat(elem.get_double().value_unsafe());
        break;
    default:
        assert(false);
    }
}


void parseJSON(EvalState & state, const std::string_view & s_, Value & v)
{
    static thread_local dom::parser parser;

    try {
        /* simdjson needs 64 readable bytes after the string.
           Since there's no way to guarantee this, copy the string to a temporary buffer
           This doesn't seem to be slower than calling GC_REALLOC on the string. */
        padded_string json_padded = padded_string(s_);
        auto& doc = allocDocument();
        auto doc_root = parser.parse_into_document(doc.doc, json_padded);
        parse_json(state, doc_root.value(), v, doc);
    } catch(simdjson_error &e) {
        throw JSONParseError("Error parsing JSON: %1%", e.what());
    }
}

}

#else
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
