#include "primops.hh"
#include "filtering-input-accessor.hh"

namespace nix {

struct FilterPathInputAccessor : CachingFilteringInputAccessor
{
    EvalState & state;
    PosIdx pos;
    Value * filterFun;

    FilterPathInputAccessor(EvalState & state, PosIdx pos, const SourcePath & src, Value * filterFun)
        : CachingFilteringInputAccessor(src)
        , state(state)
        , pos(pos)
        , filterFun(filterFun)
    {
    }

    bool isAllowedUncached(const CanonPath & path) override
    {
        if (!path.isRoot() && !isAllowed(*path.parent())) return false;
        // Note that unlike 'builtins.{path,filterSource}', we don't
        // pass the prefix to the filter function.
        return state.callPathFilter(filterFun, {next, prefix + path}, path.abs(), pos);
    }

};

static void prim_filterPath(EvalState & state, PosIdx pos, Value * * args, Value & v)
{
    std::optional<SourcePath> path;
    Value * filterFun = nullptr;
    NixStringContext context;

    state.forceAttrs(*args[0], pos,
        "while evaluating the first argument to 'builtins.filterPath'");

    for (auto & attr : *args[0]->attrs) {
        auto n = state.symbols[attr.name];
        if (n == "path")
            path.emplace(state.coerceToPath(attr.pos, *attr.value, context,
                    "while evaluating the 'path' attribute passed to 'builtins.filterPath'"));
        else if (n == "filter") {
            state.forceValue(*attr.value, pos);
            filterFun = attr.value;
        }
        else
            state.debugThrowLastTrace(EvalError({
                .msg = hintfmt("unsupported argument '%1%' to 'filterPath'", state.symbols[attr.name]),
                .errPos = state.positions[attr.pos]
            }));
    }

    if (!path)
        state.debugThrowLastTrace(EvalError({
            .msg = hintfmt("'path' required"),
            .errPos = state.positions[pos]
        }));

    if (!filterFun)
        state.debugThrowLastTrace(EvalError({
            .msg = hintfmt("'filter' required"),
            .errPos = state.positions[pos]
        }));

    if (!context.empty())
        state.debugThrowLastTrace(EvalError({
            .msg = hintfmt("'path' argument to 'filterPath' cannot have a context"),
            .errPos = state.positions[pos]
        }));

    auto accessor = make_ref<FilterPathInputAccessor>(state, pos, *path, filterFun);

    state.registerAccessor(accessor);

    v.mkPath(accessor->root());
}

static RegisterPrimOp primop_filterPath({
    .name = "__filterPath",
    .args = {"args"},
    .doc = R"(
      This function lets you filter out files from a path. It takes a
      path and a predicate function, and returns a new path from which
      every file has been removed for which the predicate function
      returns `false`.

      For example, the following filters out all regular files in
      `./doc` that don't end with the extension `.md`:

      ```nix
      builtins.filterPath {
        path = ./doc;
        filter =
          path: type:
          (type != "regular" || hasSuffix ".md" path);
      }
      ```

      The filter function is called for all files in `path`. It takes
      two arguments. The first is a string that represents the path of
      the file to be filtered, relative to `path` (i.e. it does *not*
      contain `./doc` in the example above). The second is the file
      type, which can be one of `regular`, `directory` or `symlink`.

      Note that unlike `builtins.filterSource` and `builtins.path`,
      this function does not copy the result to the Nix store. Rather,
      the result is a virtual path that lazily applies the filter
      predicate. The result will only be copied to the Nix store if
      needed (e.g. if used in a derivation attribute like `src =
      builtins.filterPath { ... }`).
    )",
    .fun = prim_filterPath,
    .experimentalFeature = Xp::Flakes,
});

}
