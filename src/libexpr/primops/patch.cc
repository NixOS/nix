#include "primops.hh"

namespace nix {

static void prim_patch(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    std::vector<std::string> patches;
    std::optional<SourcePath> src;

    state.forceAttrs(*args[0], pos,
        "while evaluating the first argument to 'builtins.patch'");

    for (auto & attr : *args[0]->attrs) {
        std::string_view n(state.symbols[attr.name]);

        auto check = [&]()
        {
            if (!patches.empty())
                state.debugThrowLastTrace(EvalError({
                    .msg = hintfmt("'builtins.patch' does not support both 'patches' and 'patchFiles'"),
                    .errPos = state.positions[attr.pos]
                }));
        };

        if (n == "src") {
            PathSet context;
            src.emplace(state.coerceToPath(pos, *attr.value, context,
                    "while evaluating the 'src' attribute passed to 'builtins.patch'"));
        }

        else if (n == "patchFiles") {
            check();
            state.forceList(*attr.value, attr.pos,
                "while evaluating the 'patchFiles' attribute passed to 'builtins.patch'");
            for (auto elem : attr.value->listItems()) {
                // FIXME: use realisePath
                PathSet context;
                auto patchFile = state.coerceToPath(attr.pos, *elem, context,
                    "while evaluating the 'patchFiles' attribute passed to 'builtins.patch'");
                patches.push_back(patchFile.readFile());
            }
        }

        else if (n == "patches") {
            check();
            auto err = "while evaluating the 'patches' attribute passed to 'builtins.patch'";
            state.forceList(*attr.value, attr.pos, err);
            for (auto elem : attr.value->listItems())
                patches.push_back(std::string(state.forceStringNoCtx(*elem, attr.pos, err)));
        }

        else
            throw Error({
                .msg = hintfmt("attribute '%s' isn't supported in call to 'builtins.patch'", n),
                .errPos = state.positions[pos]
            });
    }

    if (!src)
        state.debugThrowLastTrace(EvalError({
            .msg = hintfmt("attribute 'src' is missing in call to 'builtins.patch'"),
            .errPos = state.positions[pos]
        }));

    if (!src->path.isRoot())
        throw UnimplementedError("applying patches to a non-root path ('%s') is not yet supported", src->path);

    auto accessor = makePatchingInputAccessor(src->accessor, patches);

    state.registerAccessor(accessor);

    v.mkPath(SourcePath{accessor, src->path});
}

static RegisterPrimOp primop_patch({
    .name = "__patch",
    .args = {"args"},
    .doc = R"(
      Apply patches to a source tree. This function has the following required argument:

        - src\
          The input source tree.

      It also takes one of the following:

        - patchFiles\
          A list of patch files to be applied to `src`.

        - patches\
          A list of patches (i.e. strings) to be applied to `src`.

      It returns a source tree that lazily and non-destructively
      applies the specified patches to `src`.

      Example:

      ```nix
      let
        tree = builtins.patch {
          src = fetchTree {
            type = "github";
            owner = "NixOS";
            repo = "patchelf";
            rev = "be0cc30a59b2755844bcd48823f6fbc8d97b93a7";
          };
          patches = [
            ''
              diff --git a/src/patchelf.cc b/src/patchelf.cc
              index 6882b28..28f511c 100644
              --- a/src/patchelf.cc
              +++ b/src/patchelf.cc
              @@ -1844,6 +1844,8 @@ void showHelp(const std::string & progName)

               int mainWrapped(int argc, char * * argv)
               {
              +    printf("Hello!");
              +
                   if (argc <= 1) {
                       showHelp(argv[0]);
                       return 1;

            ''
          ];
        };
      in builtins.readFile (tree + "/src/patchelf.cc")
      ```
    )",
    .fun = prim_patch,
});

}
