#include <cstdlib>
#include <exception>
#include <stdexcept>
#define RYML_SINGLE_HDR_DEFINE_NOW
#include "../../rapidyaml/ryml_all.hpp"

#include "primops.hh"
#include "eval-inline.hh"

namespace nix {

struct NixContext {
    EvalState & state;
    const PosIdx pos;
    std::string_view yaml;
};

static void s_error(const char* msg, size_t len, ryml::Location loc, void *nixContext)
{
    auto context = (const NixContext *) nixContext;
    throw EvalError({
        .msg = hintfmt("while parsing the YAML string '%1%':\n\n%2%",
            context->yaml, std::string_view(msg, len)),
        .errPos = context->state.positions[context->pos]
    });
}

static void visitYAMLNode(NixContext &context, Value & v, ryml::NodeRef t) {
    const bool strFallback = false;

    auto valTypeCheck = [=] (ryml::YamlTag_e tag, bool _default = true) {
        return t.has_val_tag() ? ryml::to_tag(t.val_tag()) == tag : _default;
    };

    v.mkBlackhole();
    if (!strFallback && t.has_key_tag()) {
        if (ryml::to_tag(t.key_tag()) != ryml::TAG_STR) {
            auto msg = ryml::formatrs<std::string>(
                "Error: Nix supports string keys only, but the key '{}' has the tag '{}'", t.key(), t.key_tag());
            s_error(msg.data(), msg.size(), {}, &context);
        }
    }
    if (t.is_map()) {
        auto attrs = context.state.buildBindings(t.num_children());

        for (ryml::NodeRef child : t.children()) {
            std::string_view key(child.key().begin(), child.key().size());
            visitYAMLNode(context, attrs.alloc(key), child);
        }

        v.mkAttrs(attrs);
    } else if (t.is_seq()) {
        context.state.mkList(v, t.num_children());

        size_t i = 0;
        for (ryml::NodeRef child : t.children())
            visitYAMLNode(context, *(v.listElems()[i++] = context.state.allocValue()), child);
    } else if (valTypeCheck(ryml::TAG_NULL) && t.val_is_null()) {
        v.mkNull();
    } else if (t.has_val()) {
        NixFloat _float;
        NixInt _int;
        bool _bool;
        // caution: ryml is able to convert integers into booleans
        if (valTypeCheck(ryml::TAG_INT, !t.is_quoted()) && ryml::from_chars(t.val(), &_int)) {
            v.mkInt(_int);
        } else if (valTypeCheck(ryml::TAG_BOOL, !t.is_quoted()) && ryml::from_chars(t.val(), &_bool)) {
            v.mkBool(_bool);
        } else if (valTypeCheck(ryml::TAG_FLOAT, !t.is_quoted()) && ryml::from_chars(t.val(), &_float)) {
            v.mkFloat(_float);
        }
        if ((strFallback || valTypeCheck(ryml::TAG_STR)) && v.type() == nThunk) {
            std::string_view value(t.val().begin(), t.val().size());
            v.mkString(value);
        }
    }
    if (v.type() == nThunk) {
        auto msg = ryml::formatrs<std::string>(
            "Error: The YAML value '{}' with '{}' tag cannot be represented as Nix data type", t.val(), t.val_tag());
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

      Maps are converted to attribute sets, but attribute sets require String keys, so that no other key data types are sypported.
      Scalars are converted to the type specified by their optional value tag and parsing fails, if a conversion is not possible.
      Not all YAML types are supported by Nix, e.g. Nix has no binary and timestamp data types, so that parsing of YAML with any of these types fails.
    )",
    .fun = [] (EvalState & state, const PosIdx pos, Value * * args, Value & val) {
        auto yaml = state.forceStringNoCtx(*args[0], pos);

        NixContext context{
            .state = state,
            .pos = pos,
            .yaml = yaml
        };
        ryml::Parser parser{{&context, nullptr, nullptr, s_error}};
        auto tree = parser.parse_in_arena({}, ryml::csubstr(yaml.begin(), yaml.size()));
        tree.resolve(); // resolve references

        auto root = tree.rootref();
        if (root.is_stream() && root.num_children() == 1)
            root = root.child(0);
        visitYAMLNode(context, val, root);
    }
});

}
