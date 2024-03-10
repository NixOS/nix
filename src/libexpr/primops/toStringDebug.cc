#include "primops.hh"
#include "print-options.hh"

namespace nix {

static void prim_toStringDebug(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    v.mkString(printValue(state, *args[0], debugPrintOptions));
}

static RegisterPrimOp primop_toStringDebug({
    .name = "toStringDebug",
    .args = {"value"},
    .doc = R"(
      Format a value as a string for debugging purposes.

      Unlike [`toString`](@docroot@/language/builtins.md#builtins-toString),
      `toStringDebug` will never error and will always produce human-readable
      output (including for values that throw errors). For this reason,
      `toStringDebug` is ideal for interpolation into messages in
      [`trace`](@docroot@/language/builtins.md#builtins-trace)
      calls and [`assert`](@docroot@/language/constructs.html#assertions)
      statements.

      Output will be pretty-printed and include ANSI escape sequences.
      If the value contains too many values (for instance, more than 32
      attributes or list items), some values will be elided.
    )",
    .fun = prim_toStringDebug,
});

}
