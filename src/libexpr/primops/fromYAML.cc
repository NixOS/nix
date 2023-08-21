#ifdef HAVE_RYML

#include "primops.hh"
#include "eval-inline.hh"

#include <ryml.hpp>
#include <c4/format.hpp>
#include <c4/std/string.hpp>


namespace nix {

struct NixContext {
    EvalState & state;
    const PosIdx pos;
    std::string_view yaml;
};

static void s_error [[ noreturn ]] (const char* msg, size_t len, ryml::Location, void *nixContext)
{
    auto context = static_cast<const NixContext *>(nixContext);
    if (nixContext) {
        throw EvalError({
            .msg = hintfmt("while parsing the YAML string '%1%':\n\n%2%",
                context->yaml, std::string_view(msg, len)),
            .errPos = context->state.positions[context->pos]
        });
    } else {
        throw EvalError({
            .msg = hintfmt("failed assertion in rapidyaml library:\n\n%1%",
                std::string_view(msg, len))
        });
    }
}

static void visitYAMLNode(NixContext & context, Value & v, ryml::ConstNodeRef t) {

    auto valTypeCheck = [=] (ryml::YamlTag_e tag, bool defaultVal = true) {
        auto valTag = ryml::TAG_NONE;
        if (t.has_val_tag()) {
            auto tag = t.val_tag();
            valTag = tag == "!" ? ryml::TAG_STR : ryml::to_tag(tag);
        }
        return valTag == ryml::TAG_NONE ? defaultVal : valTag == tag;
    };

    v.mkBlackhole();
    if (t.has_key_tag()) {
        auto tag = ryml::to_tag(t.key_tag());
        if (tag != ryml::TAG_NONE && tag != ryml::TAG_STR) {
            auto msg = ryml::formatrs<std::string>(
                "Error: Nix supports string keys only, but the key '{}' has the tag '{}'", t.key(), t.key_tag());
            s_error(msg.data(), msg.size(), {}, &context);
        }
    }
    if (t.is_map()) {
        if (t.num_children() == 1) { // special case for YAML string ":"
            auto child = t.child(0);
            if (child.key().empty() && !child.is_key_quoted() && child.has_val() && child.val().empty() && !child.is_val_quoted()) {
                v.mkNull();
            }
        }
        if (v.type() != nNull) {
            auto attrs = context.state.buildBindings(t.num_children());

            for (ryml::ConstNodeRef child : t.children()) {
                std::string_view key(child.key().begin(), child.key().size());
                visitYAMLNode(context, attrs.alloc(key), child);
            }

            v.mkAttrs(attrs);
        }
    } else if (t.is_seq()) {
        context.state.mkList(v, t.num_children());

        size_t i = 0;
        for (ryml::ConstNodeRef child : t.children())
            visitYAMLNode(context, *(v.listElems()[i++] = context.state.allocValue()), child);
    } else if (valTypeCheck(ryml::TAG_NULL) && t.val_is_null()) {
        v.mkNull();
    } else if (t.has_val()) {
        bool _bool;
        NixFloat _float;
        NixInt _int;
        bool isQuoted = t.is_val_quoted();
        auto val = t.val();
        // Caution: ryml is able to convert integers into booleans and ryml::from_chars might ignore trailing chars
        if (t.has_val_tag() && t.val_tag() == "!" && val.empty() && !isQuoted) { // special case for YAML string "!"
            v.mkNull();
        } else if (valTypeCheck(ryml::TAG_INT, !isQuoted) && val.is_integer() && ryml::from_chars(val, &_int)) {
            v.mkInt(_int);
        } else if (valTypeCheck(ryml::TAG_BOOL, !isQuoted) && ryml::from_chars(val, &_bool)) {
            v.mkBool(_bool);
        } else if (valTypeCheck(ryml::TAG_FLOAT, !isQuoted) && val.is_number() && ryml::from_chars(val, &_float)) {
            v.mkFloat(_float);
        }
        if (valTypeCheck(ryml::TAG_STR) && v.type() == nThunk) {
            std::string_view value(val.begin(), val.size());
            v.mkString(value);
        }
    }
    if (v.type() == nThunk) {
        auto val = t.has_val() ? t.val() : "";
        auto tag = t.has_val_tag() ? t.val_tag() : "";
        auto msg = ryml::formatrs<std::string>(
            "Error: The YAML value '{}' with '{}' tag cannot be represented as Nix data type", val, tag);
        s_error(msg.data(), msg.size(), {}, &context);
    }
}

static RegisterPrimOp primop_fromYAML({
    .name = "__fromYAML",
    .args = {"e"},
    .doc = R"(
      Convert a YAML 1.2 string to a Nix value, if a conversion is possible. For example,

      ```nix
      builtins.fromYAML ''{x: [1, 2, 3], y: !!str null, z: null}''
      ```

      returns the value `{ x = [ 1 2 3 ]; y = "null"; z = null; }`.

      Maps are converted to attribute sets, but attribute sets require String keys, so that no other key data types are supported.

      Scalars are converted to the type specified by their optional value tag and parsing fails, if a conversion is not possible.
      Not all YAML types are supported by Nix, e.g. Nix has no binary and timestamp data types, so that parsing of YAML with any of these types fails.
      Custom tags are ignored and a stream with multiple documents is mapped to a list except when the stream contains a single document.
    )",
    .experimentalFeature = Xp::FromYaml,
    .fun = [] (EvalState & state, const PosIdx pos, Value * * args, Value & val) {
        auto yaml = state.forceStringNoCtx(*args[0], pos, "while evaluating the argument passed to builtins.fromYAML");

        NixContext context{
            .state = state,
            .pos = pos,
            .yaml = yaml
        };
        ryml::set_callbacks({nullptr, nullptr, nullptr, s_error}); // failed assertion should throw an exception
        ryml::Parser parser{{&context, nullptr, nullptr, s_error}};
        auto tree = parser.parse_in_arena({}, ryml::csubstr(yaml.begin(), yaml.size()));
        tree.resolve(); // resolve references
        tree.resolve_tags();

        auto root = tree.crootref();
        if (!root.has_val() && !root.is_map() && !root.is_seq()) {
            std::string msg = "YAML string has no content";
            s_error(msg.data(), msg.size(), {}, &context);
        }
        if (root.is_stream() && root.num_children() == 1 && root.child(0).is_doc())
            root = root.child(0);
        visitYAMLNode(context, val, root);
    }
});

}

#endif
