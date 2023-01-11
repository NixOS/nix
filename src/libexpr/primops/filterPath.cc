#include "primops.hh"

namespace nix {

struct FilteringInputAccessor : InputAccessor
{
    EvalState & state;
    PosIdx pos;
    ref<InputAccessor> next;
    CanonPath prefix;
    Value * filterFun;

    std::map<CanonPath, bool> cache;

    FilteringInputAccessor(EvalState & state, PosIdx pos, const SourcePath & src, Value * filterFun)
        : state(state)
        , pos(pos)
        , next(src.accessor)
        , prefix(src.path)
        , filterFun(filterFun)
    {
    }

    std::string readFile(const CanonPath & path) override
    {
        checkAccess(path);
        return next->readFile(prefix + path);
    }

    bool pathExists(const CanonPath & path) override
    {
        return isAllowed(path) && next->pathExists(prefix + path);
    }

    Stat lstat(const CanonPath & path) override
    {
        checkAccess(path);
        return next->lstat(prefix + path);
    }

    DirEntries readDirectory(const CanonPath & path) override
    {
        checkAccess(path);
        DirEntries entries;
        for (auto & entry : next->readDirectory(prefix + path)) {
            if (isAllowed(path + entry.first))
                entries.insert(std::move(entry));
        }
        return entries;
    }

    std::string readLink(const CanonPath & path) override
    {
        checkAccess(path);
        return next->readLink(prefix + path);
    }

    void checkAccess(const CanonPath & path)
    {
        if (!isAllowed(path))
            throw Error("access to path '%s' has been filtered out", showPath(path));
    }

    bool isAllowed(const CanonPath & path)
    {
        auto i = cache.find(path);
        if (i != cache.end()) return i->second;
        auto res = isAllowedUncached(path);
        cache.emplace(path, res);
        return res;
    }

    bool isAllowedUncached(const CanonPath & path)
    {
        if (!path.isRoot() && !isAllowed(*path.parent())) return false;
        // Note that unlike 'builtins.{path,filterSource}', we don't
        // pass the prefix to the filter function.
        return state.callPathFilter(filterFun, {next, prefix + path}, path.abs(), pos);
    }

    std::string showPath(const CanonPath & path) override
    {
        return next->showPath(prefix + path);
    }
};

static void prim_filterPath(EvalState & state, PosIdx pos, Value * * args, Value & v)
{
    std::optional<SourcePath> path;
    Value * filterFun = nullptr;
    PathSet context;

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

    auto accessor = make_ref<FilteringInputAccessor>(state, pos, *path, filterFun);

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
