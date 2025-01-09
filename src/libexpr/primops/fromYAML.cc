#ifdef HAVE_RYML

#  include "primops.hh"
#  include "eval-inline.hh"

#  include <ryml.hpp>
#  include <c4/format.hpp>
#  include <c4/std/string.hpp>
#  include <boost/lexical_cast.hpp>

namespace {

using namespace nix;

/**
 * Equality check of a compile time C-string *lhs* and another string *rhs*.
 * Only call this function, if both strings have the same length.
 */
template<size_t N>
inline bool isEqualSameLengthStr(const char * rhs, const char lhs[N + 1])
{
    bool result = true;
    for (size_t i = 0; i < N; i++) {
        result &= rhs[i] == lhs[i];
    }
    return result;
}

inline bool isNull(ryml::csubstr val)
{
    size_t len = val.size();
    return len == 0 || (len == 1 && val[0] == '~')
           || (len == 4 && (val[0] == 'n' || val[0] == 'N')
               && (isEqualSameLengthStr<3>(&val[1], "ull") || isEqualSameLengthStr<4>(&val[0], "NULL")));
}

bool isInt_1_2(ryml::csubstr val)
{
    bool result = val.is_integer();
    // ryml::from_chars accepts signed binary, octal and hexadecimal integers
    // YAML 1.2 defines unsigned octal and hexadecimal integers (lower-case identifiers)
    if (result && val.size() >= 3
        && ((val.begins_with_any("+-") && val.sub(2, 1).begins_with_any("xXoObB"))
            || val.sub(1, 1).begins_with_any("XObB"))) {
        result = false;
    }
    return result;
}

std::optional<bool> parseBool_1_2(ryml::csubstr val)
{
    std::optional<bool> _bool;
    size_t len = val.size();
    if (len == 4 && (val[0] == 't' || val[0] == 'T')) {
        if (isEqualSameLengthStr<3>(&val[1], "rue") || isEqualSameLengthStr<4>(&val[0], "TRUE")) {
            _bool = true;
        }
    } else if (len == 5 && (val[0] == 'f' || val[0] == 'F')) {
        if (isEqualSameLengthStr<4>(&val[1], "alse") || isEqualSameLengthStr<5>(&val[0], "FALSE")) {
            _bool = false;
        }
    }
    return _bool;
}

std::optional<bool> parseBool_1_1(ryml::csubstr val)
{
    std::optional<bool> _bool;
    switch (val.size()) {
    case 1:
        if (val[0] == 'n' || val[0] == 'N') {
            _bool = false;
        } else if (val[0] == 'y' || val[0] == 'Y') {
            _bool = true;
        }
        break;
    case 2:
        // "no" or "on"
        if (isEqualSameLengthStr<2>(&val[0], "no") || (val[0] == 'N' && (val[1] == 'o' || val[1] == 'O'))) {
            _bool = false;
        } else if (isEqualSameLengthStr<2>(&val[0], "on") || (val[0] == 'O' && (val[1] == 'n' || val[1] == 'N'))) {
            _bool = true;
        }
        break;
    case 3:
        // "off" or "yes"
        if (isEqualSameLengthStr<3>(&val[0], "off")
            || (val[0] == 'O' && (isEqualSameLengthStr<2>(&val[1], "ff") || isEqualSameLengthStr<2>(&val[1], "FF")))) {
            _bool = false;
        } else if (
            isEqualSameLengthStr<3>(&val[0], "yes")
            || (val[0] == 'Y' && (isEqualSameLengthStr<2>(&val[1], "es") || isEqualSameLengthStr<2>(&val[1], "ES")))) {
            _bool = true;
        }
        break;
    case 4:
    case 5:
        _bool = parseBool_1_2(val);
        break;
    default:
        break;
    }
    return _bool;
}

struct FromYAMLContext
{
    struct ParserOptions
    {
        bool useBoolYAML1_1 = false;

        ParserOptions(FromYAMLContext &, const Bindings *);
    };

    EvalState & state;
    const PosIdx pos;
    const std::string_view yaml;
    const ParserOptions options;

    FromYAMLContext(EvalState &, PosIdx, std::string_view, const Bindings *);

    inline std::optional<bool> parseBool(ryml::csubstr val) const
    {
        std::optional<bool> result;
        if (options.useBoolYAML1_1) {
            result = parseBool_1_1(val);
        } else {
            result = parseBool_1_2(val);
        }
        return result;
    }

    template<typename... Args>
    void throwError [[noreturn]] (const char * c_fs, const Args &... args) const
    {
        std::string fs = "while parsing the YAML string ''%1%'':\n\n";
        fs += c_fs;
        throw EvalError(state, ErrorInfo{.msg = fmt(fs, yaml, args...), .pos = state.positions[pos]});
    }

    void visitYAMLNode(Value & v, ryml::ConstNodeRef t, bool isTopNode = false);

    NixInt::Inner parseInt(ryml::csubstr val);

    std::optional<NixFloat> parseFloat(std::optional<bool> isInt, ryml::csubstr val);
};

FromYAMLContext::FromYAMLContext(EvalState & state, PosIdx pos, std::string_view yaml, const Bindings * options)
    : state(state)
    , pos(pos)
    , yaml(yaml)
    , options(*this, options)
{
}

FromYAMLContext::ParserOptions::ParserOptions(FromYAMLContext & context, const Bindings * options)
{
    auto symbol = context.state.symbols.create("useBoolYAML1_1");
    const Attr * useBoolYAML1_1 = options->get(symbol);
    if (useBoolYAML1_1) {
        this->useBoolYAML1_1 =
            context.state.forceBool(*useBoolYAML1_1->value, {}, "while evaluating the attribute \"useBoolYAML1_1\"");
    }
}

void s_error [[noreturn]] (const char * msg, size_t len, ryml::Location, void * fromYAMLContext)
{
    auto context = static_cast<const FromYAMLContext *>(fromYAMLContext);
    if (context) {
        context->throwError("%2%", std::string_view(msg, len));
    } else {
        throw Error({.msg = fmt("failed assertion in rapidyaml library:\n\n%1%", std::string_view(msg, len))});
    }
}

/**
 * Tries to parse a string into an integer according to the YAML 1.2 core schema, wrapping boost::try_lexical_convert
 * The caller has to ensure that `val` represents an integer!
 */
NixInt::Inner FromYAMLContext::parseInt(ryml::csubstr val)
{
    size_t len = val.size();
    NixInt::Inner _int = 0;
    static_assert(sizeof(NixInt::Inner) == sizeof(int64_t));
    if (len > 2 && val[1] == 'x') {
        size_t i = 2;
        for (; i < len && val[i] == '0'; i++)
            ;
        size_t maxChars = i + 64 / 4;
        if (len > maxChars || (len == maxChars && val[i] >= '8')) {
            throwError("cannot convert '%2%' to an integer because it would overflow", val);
        }
        for (char decoded; i < len; i++) {
            if (val[i] <= '9') {
                decoded = val[i] - '0';
            } else if (val[i] <= 'F') {
                decoded = val[i] - ('A' - 10);
            } else {
                decoded = val[i] - ('a' - 10);
            }
            _int = (_int << 4) | decoded;
        }
    } else if (len > 2 && val[1] == 'o') {
        size_t i = 2;
        for (; i < len && val[i] == '0'; i++)
            ;
        size_t maxChars = i + 64 / 3; // MSB of Nix integer is the sign bit
        if (len > maxChars) {
            throwError("cannot convert '%2%' to an integer because it would overflow", val);
        }
        for (; i < len; i++) {
            _int = (_int << 3) | (val[i] - '0');
        }
    } else if (!boost::conversion::detail::try_lexical_convert(val.data(), val.size(), _int)) {
        auto reason = val[0] == '-' ? "underflow" : "overflow";
        throwError("cannot convert '%2%' to an integer because it would %3%", val, reason);
    }
    return _int;
}

/**
 * Tries to parse a string into a floating point number according to the YAML 1.2 core schema, wrapping ryml::from_chars
 */
std::optional<NixFloat> FromYAMLContext::parseFloat(std::optional<bool> isInt, ryml::csubstr val)
{
    std::optional<NixFloat> maybe_float;
    size_t len = val.size();
    if (isInt.value_or(false)) {
        try {
            NixInt::Inner _int = parseInt(val);
            if (_int != 0 || val[0] != '-') {
                maybe_float.emplace(_int);
            } else {
                maybe_float = -0.0;
            }
            return maybe_float;
        } catch (...) {
            if (len > 2 && (val[1] == 'x' || val[1] == 'o')) {
                throw;
            }
            // continue with parsing decimal integer as float
        }
    }
    // first character has to match [0-9+-.]
    if (len >= 1 && val[0] >= '+' && val[0] <= '9' && val[0] != ',' && val[0] != '/') {
        size_t skip = val[0] == '+' || val[0] == '-';
        if ((len == skip + 4) && val[skip + 0] == '.') {
            auto sub = &val[skip + 1];
            if (skip == 0
                && (isEqualSameLengthStr<3>(sub, "nan")
                    || (sub[0] == 'N' && (sub[1] == 'a' || sub[1] == 'A') && sub[2] == 'N'))) {
                maybe_float = std::numeric_limits<NixFloat>::quiet_NaN();
            } else if (
                ((sub[0] == 'i' || sub[0] == 'I') && isEqualSameLengthStr<2>(sub + 1, "nf"))
                || isEqualSameLengthStr<3>(sub, "INF")) {
                NixFloat inf = std::numeric_limits<NixFloat>::infinity();
                maybe_float = val[0] == '-' ? -inf : inf;
            }
        }
        auto sub = &val[0] + 1;
        if (len == skip + 3 && (isEqualSameLengthStr<3>(sub, "nan") || isEqualSameLengthStr<3>(sub, "inf"))) {
            // ryml::from_chars converts "nan" and "inf"
        } else if (
            !maybe_float && ((!isInt && val.is_number()) || (isInt && val.is_real()))
            && val.sub(1, std::min(size_t(2), len - 1)).first_of("xXoObB") == ryml::npos) {
            // isInt => !*isInt because of ((isInt && *isInt) == false)
            NixFloat _float;
            if (!ryml::from_chars(val.sub(val[0] == '+'), &_float)) {
                throwError("cannot convert '%2%' to a floating point number", val);
            }
            constexpr NixFloat fmin = std::numeric_limits<NixFloat>::min();
            // denormals aren't round trip safe
            if ((_float > 0. && _float < fmin) || (_float < 0. && _float > -fmin)) {
                throwError("cannot convert '%2%' to a floating point number because it is denormal", val);
            }
            maybe_float = _float;
        }
    }
    return maybe_float;
}

/**
 * Parse YAML according to the YAML 1.2 core schema by default
 * The behaviour can be modified by the FromYAMLOptions object in FromYAMLContext
 */
void FromYAMLContext::visitYAMLNode(Value & v, ryml::ConstNodeRef t, bool isTopNode)
{
    ryml::csubstr valTagStr;
    auto valTag = ryml::TAG_NONE;
    bool valTagCustom = t.has_val_tag();
    bool valTagNonSpecificStr = false;
    if (valTagCustom) {
        valTagStr = t.val_tag();
        if (!(valTagNonSpecificStr = valTagStr == "!")) {
            valTag = ryml::to_tag(valTagStr);
            valTagCustom = valTag == ryml::TAG_NONE;
            if (valTagCustom) {
                auto fs = "Error: Nix has no support for the unknown tag ''%2%'' in node ''%3%''";
                throwError(fs, valTagStr, t);
            }
        }
    }
    if (t.is_map()) {
        if (valTag != ryml::TAG_NONE && valTag != ryml::TAG_MAP) {
            auto fs = "Error: Nix parsed ''%2%'' as map and only supported is the tag ''!!map'', but ''%3%'' was used";
            throwError(fs, t, valTagStr);
        }
        auto attrs = state.buildBindings(t.num_children());

        for (ryml::ConstNodeRef child : t.children()) {
            auto key = child.key();
            if (child.has_key_tag()) {
                auto tag = ryml::to_tag(child.key_tag());
                if (tag != ryml::TAG_NONE && tag != ryml::TAG_STR) {
                    auto fs = "Error: Nix supports string keys only, but the key ''%2%'' has the tag ''%3%''";
                    throwError(fs, child.key(), child.key_tag());
                }
            } else if (child.is_key_plain() && isNull(key)) {
                auto fs = "Error: Nix supports string keys only, but the map ''%2%'' contains a null-key";
                throwError(fs, t);
            }
            visitYAMLNode(attrs.alloc({key.begin(), key.size()}), child);
        }

        v.mkAttrs(attrs);
        Symbol key;
        // enforce uniqueness of keys
        for (const auto & attr : *attrs.alreadySorted()) {
            if (key == attr.name) {
                auto fs = "Error: Non-unique key %2% after deserializing the map ''%3%''";
                throwError(fs, state.symbols[key], t);
            }
            key = attr.name;
        }
    } else if (t.is_seq()) {
        if (valTag != ryml::TAG_NONE && valTag != ryml::TAG_SEQ) {
            auto fs =
                "Error: Nix parsed ''%2%'' as sequence and only supported is the tag ''!!seq'', but ''%3%'' was used";
            throwError(fs, t, valTagStr);
        }
        ListBuilder list(state, t.num_children());

        bool isStream = t.is_stream();
        size_t i = 0;
        for (ryml::ConstNodeRef child : t.children()) {
            // a stream of documents is handled as sequence, too
            visitYAMLNode(*(list[i++] = state.allocValue()), child, isTopNode && isStream);
        }
        v.mkList(list);
    } else if (t.has_val()) {
        auto val = t.val();
        bool isPlain = t.is_val_plain();
        bool isEmpty = isPlain && val.empty();
        if (isTopNode && isEmpty) {
            throwError("Error: Empty document (plain empty scalars outside of collection)%2%", "");
        }
        if (valTagNonSpecificStr) {
            valTag = ryml::TAG_STR;
        }

        auto scalarTypeCheck = [=](ryml::YamlTag_e tag) { return valTag == ryml::TAG_NONE ? isPlain : valTag == tag; };

        // Caution: ryml::from_chars converts integers into booleans and also it might ignore trailing chars.
        // Furthermore it doesn't accept a leading '+' character in integers
        std::optional<bool> isInt;
        std::optional<bool> _bool;
        std::optional<NixFloat> _float;
        if (scalarTypeCheck(ryml::TAG_NULL) && isNull(val)) {
            v.mkNull();
        } else if (scalarTypeCheck(ryml::TAG_BOOL) && (_bool = parseBool(val))) {
            v.mkBool(*_bool);
        } else if (scalarTypeCheck(ryml::TAG_INT) && *(isInt = isInt_1_2(val))) {
            v.mkInt(parseInt(val));
        } else if (
            ((valTag == ryml::TAG_FLOAT && (isInt = isInt_1_2(val))) || (valTag == ryml::TAG_NONE && isPlain))
            && (_float = parseFloat(isInt, val))) {
            // if the value is tagged with !!float, then isInt_1_2 evaluation is enforced because the int regex is not a
            // subset of the float regex...
            v.mkFloat(*_float);
        } else if ((valTag == ryml::TAG_NONE && !valTagCustom) || valTag == ryml::TAG_STR) {
            std::string_view value(val.begin(), val.size());
            v.mkString(value);
        } else {
            throwError("Error: Value ''%2%'' with tag ''%3%'' is invalid", val, valTagStr);
        }
    } else {
        auto val = t.has_val() ? t.val() : "";
        auto fs = "BUG: Encountered unreachable code while parsing ''%2%'' with tag ''%3%''";
        throwError(fs, val, valTagStr);
    }
}

} /* namespace */

namespace nix {

static RegisterPrimOp primop_fromYAML(
    {.name = "__fromYAML",
     .args = {"e", "attrset"},
     .doc = R"(
       Convert a YAML 1.2 string *e* to a Nix value, if a conversion is possible.
       The second argument is an attribute set with optional parameters for the parser.
       For example,

       ```nix
       builtins.fromYAML ''{x: [1, 2, 3], y: !!str null, z: null}'' {}
       ```

       returns the value `{ x = [ 1 2 3 ]; y = "null"; z = null; }`.

       Maps are converted to attribute sets, but only strings are supported as keys.

       Scalars are converted to the type specified by their optional value tag. Parsing fails if a conversion is not possible.
       Nix does not support all data types defined by the different YAML specs, e.g. Nix has no binary and timestamp data types.
       Thus the types and tags defined by the YAML 1.2 core schema are used exclusively, i.e. untagged timestamps are parsed as strings.
       Using any other tag fails.
       A stream with multiple documents is mapped to a list except when the stream contains a single document.

       Supported optional parameters in *attrset*:
         - useBoolYAML1_1 :: bool ? false: When enabled booleans are parsed according to the YAML 1.1 spec, which matches more values than YAML 1.2.
                                           This option improves compatibility because many applications and configs are still using YAML 1.1 features.
     )",
     .fun =
         [](EvalState & state, const PosIdx pos, Value ** args, Value & val) {
             auto yaml = state.forceStringNoCtx(
                 *args[0], pos, "while evaluating the first argument passed to builtins.fromYAML");
             state.forceAttrs(*args[1], pos, "while evaluating the second argument passed to builtins.fromYAML");
             auto options = args[1]->attrs();

             FromYAMLContext context(state, pos, yaml, options);
             ryml::Callbacks callbacks;
             callbacks.m_error = s_error;
             ryml::set_callbacks(callbacks);
             callbacks.m_user_data = &context;
             ryml::EventHandlerTree evth(callbacks);
             ryml::Parser parser(&evth);
             ryml::Tree tree = ryml::parse_in_arena(&parser, ryml::csubstr(yaml.begin(), yaml.size()));
             tree.resolve(); // resolve references
             tree.resolve_tags();

             auto root = tree.crootref();
             if (root.is_stream() && root.num_children() == 1 && root.child(0).is_doc()) {
                 root = root.child(0);
             }
             context.visitYAMLNode(val, root, true);
         },
     .experimentalFeature = Xp::FromYaml});

} /* namespace nix */

#endif
