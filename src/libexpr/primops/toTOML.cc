#include "primops.hh"
#include "value-to-toml.hh"

namespace nix {

/* Convert the argument (an attribute set) to a TOML string.
   Not all Nix values can be sensibly or completely represented
   (e.g., functions). */
static void prim_toTOML(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    state.forceAttrs(*args[0], pos, "while evaluating the argument passed to builtins.toTOML");

    std::ostringstream out;
    NixStringContext context;
    printValueAsTOML(state, true, *args[0], pos, out, context);
    v.mkString(out.str(), context);
}

static RegisterPrimOp primop_toTOML({
    .name = "__toTOML",
    .args = {"e"},
    .doc = R"(
      Return a string containing a TOML representation of the attribute set *e*.
      Strings, integers, floats, booleans, and lists are mapped to their
      TOML equivalents. Null values are not supported in TOML and can not be
      converted. Attribute sets (except derivations) are represented
      as tables. Derivations are translated to a TOML string containing the
      derivation’s output path. Paths are copied to the store and represented
      as a TOML string of the resulting store path.

      This function is only avaliable if the experimental feature `to-toml` is
      enabled.
    )",
    .fun = prim_toTOML,
    .experimentalFeature = Xp::ToTOML,
});

}
