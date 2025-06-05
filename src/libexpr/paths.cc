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
            fetchSettings,
            *store,
            SourcePath{ref(mount)},
            settings.readOnlyMode ? FetchMode::DryRun : FetchMode::Copy,
            path.name());
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

std::string EvalState::computeBaseName(const SourcePath & path)
{
    if (path.accessor == rootFS) {
        if (auto storePath = store->maybeParseStorePath(path.path.abs())) {
            warn(
                "Performing inefficient double copy of path '%s' to the store. "
                "This can typically be avoided by rewriting an attribute like `src = ./.` "
                "to `src = builtins.path { path = ./.; name = \"source\"; }`.",
                path);
            return std::string(
                fetchToStore(fetchSettings, *store, path, FetchMode::DryRun, storePath->name()).to_string());
        }
    }
    return std::string(path.baseName());
}

StorePath EvalState::mountInput(
    fetchers::Input & input, const fetchers::Input & originalInput, ref<SourceAccessor> accessor, bool requireLockable)
{
    auto storePath = settings.lazyTrees
                         ? StorePath::random(input.getName())
                         : fetchToStore(fetchSettings, *store, accessor, FetchMode::Copy, input.getName());

    allowPath(storePath); // FIXME: should just whitelist the entire virtual store

    storeFS->mount(CanonPath(store->printStorePath(storePath)), accessor);

    if (requireLockable && (!settings.lazyTrees || !input.isLocked()) && !input.getNarHash()) {
        auto narHash = accessor->hashPath(CanonPath::root);
        input.attrs.insert_or_assign("narHash", narHash.to_string(HashFormat::SRI, true));
    }

    // FIXME: what to do with the NAR hash in lazy mode?
    if (!settings.lazyTrees && originalInput.getNarHash()) {
        auto expected = originalInput.computeStorePath(*store);
        if (storePath != expected)
            throw Error(
                (unsigned int) 102,
                "NAR hash mismatch in input '%s', expected '%s' but got '%s'",
                originalInput.to_string(),
                store->printStorePath(storePath),
                store->printStorePath(expected));
    }

    return storePath;
}

}
