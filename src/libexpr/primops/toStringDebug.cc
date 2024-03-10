#include "primops.hh"
#include "print-options.hh"
#include "value.hh"

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

      Output may change in future Nix versions. Currently, output is
      pretty-printed and include ANSI escape sequences. If the value contains
      too many values (for instance, more than 32 attributes or list items),
      some values will be elided.
    )",
    .fun = prim_toStringDebug,
});

static void prim_toStringDebugOptions(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    auto options = PrintOptions::fromValue(state, *args[0]);
    v.mkString(printValue(state, *args[1], options));
}

static RegisterPrimOp primop_toStringDebugOptions({
    .name = "toStringDebugOptions",
    .args = {"options", "value"},
    .doc = R"(
      Format a value as a string for debugging purposes.

      Like
      [`toStringDebug`](@docroot@/language/builtins.md#builtins-toStringDebug)
      but accepts an additional attribute set of arguments as its first value:

      - `ansiColors` (boolean, default `true`): Whether or not to include ANSI
        escapes for coloring in the output.
      - `force` (boolean, default `true`): Whether or not to force values while
        printing output.
      - `derivationPaths` (boolean, default `true`): If `force` is set, print
        derivations as `.drv` paths instead of as attribute sets.
      - `trackRepeated` (boolean, default `true`): Whether or not to track
        repeated values while printing output. This will help avoid excessive
        output while printing self-referential structures. The specific cycle
        detection algorithm may not detect all repeated values and may change
        between releases.
      - `maxDepth` (integer, default 15): The maximum depth to print values to.
        Depth is increased when printing nested lists and attribute sets. If
        `maxDepth` is -1, values will be printed to unlimited depth (or until
        Nix crashes).
      - `maxAttrs` (integer, default 32): The maximum number of attributes to
        print in attribute sets. Further attributes will be replaced with a
        `«234 attributes elided»` message. Note that this is the maximum number
        of attributes to print for the entire `toStringDebugOptions` call (if
        it were per-attribute set, it would be possible for
        `toStringDebugOptions` to produce essentially unbounded output). If
        `maxAttrs` is -1, all attributes will be printed.
      - `maxListItems` (integer, default 32): The maximum number of list items to
        print. Further items will be replaced with a `«234 items elided»`
        message. If `maxListItems` is -1, all items will be printed.
      - `maxStringLength` (integer, default 1024): The maximum number of bytes
        to print of strings. Further data will be replaced with a `«234 bytes
        elided»` message. If `maxStringLength` is -1, full strings will be
        printed.
      - `prettyIndent` (integer, default 2): The number of spaces of indent to
        use when pretty-printing values. If `prettyIndent` is 0, values will be
        printed on a single line.

      Missing attributes will be substituted with a default value. Default
      values may change between releases.
    )",
    .fun = prim_toStringDebugOptions,
});

}
