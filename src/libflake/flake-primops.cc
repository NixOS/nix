#include <stdint.h>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "nix/flake/flake-primops.hh"
#include "nix/expr/eval.hh"
#include "nix/flake/flake.hh"
#include "nix/flake/flakeref.hh"
#include "nix/flake/settings.hh"
#include "nix/expr/attr-set.hh"
#include "nix/expr/eval-error.hh"
#include "nix/expr/eval-inline.hh"
#include "nix/expr/eval-settings.hh"
#include "nix/expr/symbol-table.hh"
#include "nix/expr/value.hh"
#include "nix/fetchers/attrs.hh"
#include "nix/fetchers/fetchers.hh"
#include "nix/util/configuration.hh"
#include "nix/util/error.hh"
#include "nix/util/experimental-features.hh"
#include "nix/util/pos-idx.hh"
#include "nix/util/pos-table.hh"
#include "nix/util/source-path.hh"
#include "nix/util/types.hh"
#include "nix/util/util.hh"

namespace nix::flake::primops {

PrimOp getFlake(const Settings & settings)
{
    auto prim_getFlake = [&settings](EvalState & state, const PosIdx pos, Value ** args, Value & v) {
        std::string flakeRefS(
            state.forceStringNoCtx(*args[0], pos, "while evaluating the argument passed to builtins.getFlake"));
        auto flakeRef = nix::parseFlakeRef(state.fetchSettings, flakeRefS, {}, true);
        if (state.settings.pureEval && !flakeRef.input.isLocked())
            throw Error(
                "cannot call 'getFlake' on unlocked flake reference '%s', at %s (use --impure to override)",
                flakeRefS,
                state.positions[pos]);

        callFlake(
            state,
            lockFlake(
                settings,
                state,
                flakeRef,
                LockFlags{
                    .updateLockFile = false,
                    .writeLockFile = false,
                    .useRegistries = !state.settings.pureEval && settings.useRegistries,
                    .allowUnlocked = !state.settings.pureEval,
                }),
            v);
    };

    return PrimOp{
        .name = "__getFlake",
        .args = {"args"},
        .doc = R"(
          Fetch a flake from a flake reference, and return its output attributes and some metadata. For example:

          ```nix
          (builtins.getFlake "nix/55bc52401966fbffa525c574c14f67b00bc4fb3a").packages.x86_64-linux.nix
          ```

          Unless impure evaluation is allowed (`--impure`), the flake reference
          must be "locked", e.g. contain a Git revision or content hash. An
          example of an unlocked usage is:

          ```nix
          (builtins.getFlake "github:edolstra/dwarffs").rev
          ```
        )",
        .fun = prim_getFlake,
        .experimentalFeature = Xp::Flakes,
    };
}

static void prim_parseFlakeRef(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    std::string flakeRefS(
        state.forceStringNoCtx(*args[0], pos, "while evaluating the argument passed to builtins.parseFlakeRef"));
    auto attrs = nix::parseFlakeRef(state.fetchSettings, flakeRefS, {}, true).toAttrs();
    auto binds = state.buildBindings(attrs.size());
    for (const auto & [key, value] : attrs) {
        auto s = state.symbols.create(key);
        auto & vv = binds.alloc(s);
        std::visit(
            overloaded{
                [&vv](const std::string & value) { vv.mkString(value); },
                [&vv](const uint64_t & value) { vv.mkInt(value); },
                [&vv](const Explicit<bool> & value) { vv.mkBool(value.t); }},
            value);
    }
    v.mkAttrs(binds);
}

nix::PrimOp parseFlakeRef({
    .name = "__parseFlakeRef",
    .args = {"flake-ref"},
    .doc = R"(
      Parse a flake reference, and return its exploded form.

      For example:

      ```nix
      builtins.parseFlakeRef "github:NixOS/nixpkgs/23.05?dir=lib"
      ```

      evaluates to:

      ```nix
      { dir = "lib"; owner = "NixOS"; ref = "23.05"; repo = "nixpkgs"; type = "github"; }
      ```
    )",
    .fun = prim_parseFlakeRef,
    .experimentalFeature = Xp::Flakes,
});

static void prim_flakeRefToString(EvalState & state, const PosIdx pos, Value ** args, Value & v)
{
    state.forceAttrs(*args[0], noPos, "while evaluating the argument passed to builtins.flakeRefToString");
    fetchers::Attrs attrs;
    for (const auto & attr : *args[0]->attrs()) {
        auto t = attr.value->type();
        if (t == nInt) {
            auto intValue = attr.value->integer().value;

            if (intValue < 0) {
                state
                    .error<EvalError>(
                        "negative value given for flake ref attr %1%: %2%", state.symbols[attr.name], intValue)
                    .atPos(pos)
                    .debugThrow();
            }

            attrs.emplace(state.symbols[attr.name], uint64_t(intValue));
        } else if (t == nBool) {
            attrs.emplace(state.symbols[attr.name], Explicit<bool>{attr.value->boolean()});
        } else if (t == nString) {
            attrs.emplace(state.symbols[attr.name], std::string(attr.value->string_view()));
        } else {
            state
                .error<EvalError>(
                    "flake reference attribute sets may only contain integers, Booleans, "
                    "and strings, but attribute '%s' is %s",
                    state.symbols[attr.name],
                    showType(*attr.value))
                .debugThrow();
        }
    }
    auto flakeRef = FlakeRef::fromAttrs(state.fetchSettings, attrs);
    v.mkString(flakeRef.to_string());
}

nix::PrimOp flakeRefToString({
    .name = "__flakeRefToString",
    .args = {"attrs"},
    .doc = R"(
      Convert a flake reference from attribute set format to URL format.

      For example:

      ```nix
      builtins.flakeRefToString {
        dir = "lib"; owner = "NixOS"; ref = "23.05"; repo = "nixpkgs"; type = "github";
      }
      ```

      evaluates to

      ```nix
      "github:NixOS/nixpkgs/23.05?dir=lib"
      ```
    )",
    .fun = prim_flakeRefToString,
    .experimentalFeature = Xp::Flakes,
});

} // namespace nix::flake::primops
