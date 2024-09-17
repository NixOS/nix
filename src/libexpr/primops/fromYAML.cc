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

    bool fail = false;
    if (t.is_map()) {
        auto attrs = context.state.buildBindings(t.num_children());

        for (ryml::ConstNodeRef child : t.children()) {
            if (child.has_key_tag()) {
                auto tag = ryml::to_tag(child.key_tag());
                if (tag != ryml::TAG_NONE && tag != ryml::TAG_STR) {
                    auto msg = ryml::formatrs<std::string>(
                        "Error: Nix supports string keys only, but the key '{}' has the tag '{}'", child.key(), child.key_tag());
                    s_error(msg.data(), msg.size(), {}, &context);
                }
            } else if (child.key_is_null()) {
                std::stringstream ss;
                ss << t;
                auto msg = ryml::formatrs<std::string>(
                    "Error: Nix supports string keys only, but the map '{}' contains a null-key", ss.str());
                s_error(msg.data(), msg.size(), {}, &context);
            }
            std::string_view key(child.key().begin(), child.key().size());
            visitYAMLNode(context, attrs.alloc(key), child);
        }

        v.mkAttrs(attrs);
    } else if (t.is_seq()) {
        context.state.mkList(v, t.num_children());

        size_t i = 0;
        for (ryml::ConstNodeRef child : t.children()) {
            visitYAMLNode(context, *(v.listElems()[i++] = context.state.allocValue()), child);
        }
    } else if (t.has_val()) {
        bool _bool;
        NixFloat _float;
        NixInt _int;
        auto val = t.val();
        auto valTag = ryml::TAG_NONE;
        bool isQuoted = t.is_val_quoted();
        bool isNull = (!isQuoted && val.empty()) || t.val_is_null();
        if (t.has_val_tag()) {
            auto tag = t.val_tag();
            valTag = tag == "!" && !isNull ? ryml::TAG_STR : ryml::to_tag(tag);
        }

        auto scalarTypeCheck = [=] (ryml::YamlTag_e tag) {
            return valTag == ryml::TAG_NONE ? !isQuoted : valTag == tag;
        };

        // Caution: ryml is able to convert integers into booleans and ryml::from_chars might ignore trailing chars
        if ((isNull && valTag != ryml::TAG_STR) || (valTag == ryml::TAG_NULL && (val == "null" || val == "~"))) {
            v.mkNull();
        } else if (scalarTypeCheck(ryml::TAG_INT) && val.is_integer() && ryml::from_chars(val, &_int)) {
            v.mkInt(_int);
        } else if (scalarTypeCheck(ryml::TAG_BOOL) && ryml::from_chars(val, &_bool)) {
            v.mkBool(_bool);
        } else if (scalarTypeCheck(ryml::TAG_FLOAT) && val.is_number() && ryml::from_chars(val, &_float)) {
            v.mkFloat(_float);
        } else if (valTag == ryml::TAG_NONE || valTag == ryml::TAG_STR) {
            std::string_view value(val.begin(), val.size());
            v.mkString(value);
        } else {
            fail = true;
        }
    } else {
        fail = true;
    }
    if (fail) {
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

      Maps are converted to attribute sets, but only strings are supported as keys.

      Scalars are converted to the type specified by their optional value tag. Parsing fails if a conversion is not possible.
      Not all YAML types are supported by Nix, e.g. Nix has no binary and timestamp data types, so that parsing of YAML with any of these types fails.
      Custom tags are ignored and a stream with multiple documents is mapped to a list except when the stream contains a single document.
    )",
    .fun = [] (EvalState & state, const PosIdx pos, Value * * args, Value & val) {
        auto yaml = state.forceStringNoCtx(*args[0], pos, "while evaluating the argument passed to builtins.fromYAML");

        NixContext context{
            .state = state,
            .pos = pos,
            .yaml = yaml
        };
        ryml::EventHandlerTree evth;
        ryml::Callbacks callbacks(&context, nullptr, nullptr, s_error);
        ryml::set_callbacks(callbacks);
        ryml::Parser parser(&evth);
        ryml::Tree tree = ryml::parse_in_arena(&parser, ryml::csubstr(yaml.begin(), yaml.size()));
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
    },
    .experimentalFeature = Xp::FromYaml
});

}

#endif
