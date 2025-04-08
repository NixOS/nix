#include "nix/store/store-api.hh"
#include "nix/expr/eval.hh"
#include "nix/util/mounted-source-accessor.hh"
#include "nix/fetchers/fetch-to-store.hh"

namespace nix {

SourcePath EvalState::rootPath(CanonPath path)
{
    return {rootFS, std::move(path)};
}

SourcePath EvalState::rootPath(PathView path)
{
    return {rootFS, CanonPath(absPath(path))};
}

SourcePath EvalState::storePath(const StorePath & path)
{
    return {rootFS, CanonPath{store->printStorePath(path)}};
}

StorePath EvalState::devirtualize(const StorePath & path, StringMap * rewrites)
{
    if (auto mount = storeFS->getMount(CanonPath(store->printStorePath(path)))) {
        auto storePath = fetchToStore(
            *store, SourcePath{ref(mount)}, settings.readOnlyMode ? FetchMode::DryRun : FetchMode::Copy, path.name());
        assert(storePath.name() == path.name());
        if (rewrites)
            rewrites->emplace(path.hashPart(), storePath.hashPart());
        return storePath;
    } else
        return path;
}

SingleDerivedPath EvalState::devirtualize(const SingleDerivedPath & path, StringMap * rewrites)
{
    if (auto o = std::get_if<SingleDerivedPath::Opaque>(&path.raw()))
        return SingleDerivedPath::Opaque{devirtualize(o->path, rewrites)};
    else
        return path;
}

std::string EvalState::devirtualize(std::string_view s, const NixStringContext & context)
{
    StringMap rewrites;

    for (auto & c : context)
        if (auto o = std::get_if<NixStringContextElem::Opaque>(&c.raw))
            devirtualize(o->path, &rewrites);

    return rewriteStrings(std::string(s), rewrites);
}

}
