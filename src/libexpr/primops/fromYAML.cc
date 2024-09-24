#ifdef HAVE_RYML

#  include "primops.hh"
#  include "eval-inline.hh"

#  include <ryml.hpp>
#  include <c4/format.hpp>
#  include <c4/std/string.hpp>

namespace {

using namespace nix;

struct FromYAMLOptions
{
    bool useBoolYAML1_1 = false;

    FromYAMLOptions(EvalState & state, Value options)
    {
        state.forceAttrs(options, PosIdx{}, "");
        auto symbol = state.symbols.create("useBoolYAML1_1");
        const Attr * useBoolYAML1_1 = options.attrs()->get(symbol);
        if (useBoolYAML1_1) {
            this->useBoolYAML1_1 = state.forceBool(*useBoolYAML1_1->value, {}, "");
        }
    }
};

struct NixContext
{
    EvalState & state;
    const PosIdx pos;
    std::string_view yaml;
    const FromYAMLOptions options;
};

template<typename... Args>
void throwError [[noreturn]] (const NixContext & context, const char * c_fs, const Args &... args)
{
    std::string fs = "while parsing the YAML string ''%1%'':\n\n";
    fs += c_fs;
    throw EvalError(
        context.state, ErrorInfo{.msg = fmt(fs, context.yaml, args...), .pos = context.state.positions[context.pos]});
}

void s_error [[noreturn]] (const char * msg, size_t len, ryml::Location, void * nixContext)
{
    auto context = static_cast<const NixContext *>(nixContext);
    if (context) {
        throwError(*context, "%2%", std::string_view(msg, len));
    } else {
        throw Error({.msg = fmt("failed assertion in rapidyaml library:\n\n%1%", std::string_view(msg, len))});
    }
}

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

/**
 * Tries to parse a string into a floating point number according to the YAML 1.2 core schema, wrapping ryml::from_chars
 */
std::optional<NixFloat> parseFloat(std::optional<bool> isInt, ryml::csubstr val)
{
    std::optional<NixFloat> maybe_float;
    NixFloat _float;
    size_t len = val.size();
    // first character has to match [0-9+-.]
    if (isInt.value_or(false)) {
        NixInt::Inner _int;
        if (len > 0 && ryml::from_chars(val.sub(val[0] == '+'), &_int)) {
            maybe_float.emplace(_int);
        }
    } else if (len >= 1 && val[0] >= '+' && val[0] <= '9' && val[0] != ',' && val[0] != '/') {
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
            && val.sub(1, std::min(size_t(2), len - 1)).first_of("xXoObB") == ryml::npos
            && ryml::from_chars(val.sub(val[0] == '+'), &_float)) {
            // isInt => !*isInt because of (isInt && *isInt) == false)
            maybe_float = _float;
        }
    }
    return maybe_float;
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

inline std::optional<bool> parseBool(const FromYAMLOptions & options, ryml::csubstr val)
{
    std::optional<bool> result;
    if (options.useBoolYAML1_1) {
        result = parseBool_1_1(val);
    } else {
        result = parseBool_1_2(val);
    }
    return result;
}

/**
 * Parse YAML according to the YAML 1.2 core schema by default
 * The behaviour can be modified by the FromYAMLOptions object in NixContext
 */
void visitYAMLNode(const NixContext & context, Value & v, ryml::ConstNodeRef t, bool isTopNode = false)
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
                throwError(context, fs, valTagStr, t);
            }
        }
    }
    if (t.is_map()) {
        if (valTag != ryml::TAG_NONE && valTag != ryml::TAG_MAP) {
            auto fs = "Error: Nix parsed ''%2%'' as map and only supported is the tag ''!!map'', but ''%3%'' was used";
            throwError(context, fs, t, valTagStr);
        }
        auto attrs = context.state.buildBindings(t.num_children());

        for (ryml::ConstNodeRef child : t.children()) {
            if (child.has_key_tag()) {
                auto tag = ryml::to_tag(child.key_tag());
                if (tag != ryml::TAG_NONE && tag != ryml::TAG_STR) {
                    auto fs = "Error: Nix supports string keys only, but the key ''%2%'' has the tag ''%3%''";
                    throwError(context, fs, child.key(), child.key_tag());
                }
            } else if (child.key_is_null()) {
                auto fs = "Error: Nix supports string keys only, but the map ''%2%'' contains a null-key";
                throwError(context, fs, t);
            } else if (
                child.is_key_folded() && child.is_key_plain() && (!child.has_key_tag() || child.key_tag() != "?")) {
                auto fs = "Error: Map with plain multiline implicit key ''%2%''";
                throwError(context, fs, child.key());
            }
            std::string_view key(child.key().begin(), child.key().size());
            visitYAMLNode(context, attrs.alloc(key), child);
        }

        v.mkAttrs(attrs);
        Symbol key;
        // enforce uniqueness of keys
        for (const auto & attr : *attrs.alreadySorted()) {
            if (key == attr.name) {
                auto fs = "Error: Non-unique key %2% after deserializing the map ''%3%''";
                throwError(context, fs, context.state.symbols[key], t);
            }
            key = attr.name;
        }
    } else if (t.is_seq()) {
        if (valTag != ryml::TAG_NONE && valTag != ryml::TAG_SEQ) {
            auto fs =
                "Error: Nix parsed ''%2%'' as sequence and only supported is the tag ''!!seq'', but ''%3%'' was used";
            throwError(context, fs, t, valTagStr);
        }
        ListBuilder list(context.state, t.num_children());

        bool isStream = t.is_stream();
        size_t i = 0;
        for (ryml::ConstNodeRef child : t.children()) {
            // a stream of documents is handled as sequence, too
            visitYAMLNode(context, *(list[i++] = context.state.allocValue()), child, isTopNode && isStream);
        }
        v.mkList(list);
    } else if (t.has_val()) {
        auto val = t.val();
        bool isPlain = t.is_val_plain();
        bool isEmpty = isPlain && val.empty();
        if (isTopNode && isEmpty) {
            throwError(context, "Error: Empty document (plain empty scalars outside of collection)%2%", "");
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
        NixInt::Inner _int;
        bool trim = valTag == ryml::TAG_NULL || valTag == ryml::TAG_BOOL || valTag == ryml::TAG_INT
                    || valTag == ryml::TAG_FLOAT;
        auto vs = trim ? val.trim("\n\t ") : val;
        bool nullTagged = (valTag == ryml::TAG_NULL) && !isPlain
                          && (vs.empty() || vs == "~"
                              || (vs.size() == 4
                                  && (((vs[0] == 'n' || vs[0] == 'N') && isEqualSameLengthStr<3>(&vs[1], "ull"))
                                      || isEqualSameLengthStr<4>(&vs[0], "NULL"))));
        if (((t.val_is_null() || isEmpty) && valTag != ryml::TAG_STR) || nullTagged) {
            v.mkNull();
        } else if (scalarTypeCheck(ryml::TAG_BOOL) && (_bool = parseBool(context.options, vs))) {
            v.mkBool(*_bool);
        } else if (
            scalarTypeCheck(ryml::TAG_INT) && *(isInt = isInt_1_2(vs))
            && ryml::from_chars(vs.sub(vs[0] == '+'), &_int)) {
            v.mkInt(_int);
        } else if (
            ((valTag == ryml::TAG_FLOAT && (isInt || (isInt = isInt_1_2(vs)))) || (valTag == ryml::TAG_NONE && isPlain))
            && (_float = parseFloat(isInt, vs))) {
            // if the value is tagged with !!float, then isInt_1_2 evaluation is enforced because the int regex is not a
            // subset of the float regex...
            v.mkFloat(*_float);
        } else if ((valTag == ryml::TAG_NONE && !valTagCustom) || valTag == ryml::TAG_STR) {
            std::string_view value(val.begin(), val.size());
            v.mkString(value);
        } else {
            throwError(context, "Error: Value ''%2%'' with tag ''%3%'' is invalid", val, valTagStr);
        }
    } else if (isTopNode) {
        throwError(context, "Error: Empty document (plain empty scalars outside of collection)", "");
    } else {
        auto val = t.has_val() ? t.val() : "";
        auto fs = "BUG: Encountered unreachable code while parsing ''%2%'' with tag ''%3%''";
        throwError(context, fs, val, valTagStr);
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
             auto yaml =
                 state.forceStringNoCtx(*args[0], pos, "while evaluating the argument passed to builtins.fromYAML");

             NixContext context{.state = state, .pos = pos, .yaml = yaml, .options = {state, *args[1]}};
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
             visitYAMLNode(context, val, root, true);
         },
     .experimentalFeature = Xp::FromYaml});

} /* namespace nix */

#endif
