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

std::string EvalState::computeBaseName(const SourcePath & path, PosIdx pos)
{
    if (path.accessor == rootFS) {
        if (auto storePath = store->maybeParseStorePath(path.path.abs())) {
            warn(
                "Copying '%s' to the store again\n"
                "You can make Nix evaluate faster and copy fewer files by replacing `./.` with the `self` flake input, "
                "or `builtins.path { path = ./.; name = \"source\"; }`\n\n"
                "Location: %s\n",
                path,
                positions[pos]);
            return std::string(fetchToStore(*store, path, FetchMode::DryRun, storePath->name()).to_string());
        }
    }
    return std::string(path.baseName());
}

StorePath EvalState::mountInput(
    fetchers::Input & input, const fetchers::Input & originalInput, ref<SourceAccessor> accessor, bool requireLockable)
{
    auto storePath = settings.lazyTrees ? StorePath::random(input.getName())
                                        : fetchToStore(*store, accessor, FetchMode::Copy, input.getName());

    allowPath(storePath); // FIXME: should just whitelist the entire virtual store

    std::optional<Hash> _narHash;

    auto getNarHash = [&]() {
        if (!_narHash) {
            if (store->isValidPath(storePath))
                _narHash = store->queryPathInfo(storePath)->narHash;
            else
                // FIXME: use fetchToStore to make it cache this
                _narHash = accessor->hashPath(CanonPath::root);
        }
        return _narHash;
    };

    storeFS->mount(CanonPath(store->printStorePath(storePath)), accessor);

    if (requireLockable && (!settings.lazyTrees || !settings.lazyLocks || !input.isLocked()) && !input.getNarHash())
        input.attrs.insert_or_assign("narHash", getNarHash()->to_string(HashFormat::SRI, true));

    if (originalInput.getNarHash() && *getNarHash() != *originalInput.getNarHash())
        throw Error(
            (unsigned int) 102,
            "NAR hash mismatch in input '%s', expected '%s' but got '%s'",
            originalInput.to_string(),
            getNarHash()->to_string(HashFormat::SRI, true),
            originalInput.getNarHash()->to_string(HashFormat::SRI, true));

    return storePath;
}

}
