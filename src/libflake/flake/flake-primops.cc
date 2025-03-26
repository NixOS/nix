#include "flake-primops.hh"
#include "eval.hh"
#include "flake.hh"
#include "flakeref.hh"
#include "settings.hh"

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

} // namespace nix::flake::primops
