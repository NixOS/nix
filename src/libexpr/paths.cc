#include "nix/store/store-api.hh"
#include "nix/expr/eval.hh"
#include "nix/util/mounted-source-accessor.hh"
#include "nix/fetchers/fetch-to-store.hh"

namespace nix {

SourcePath EvalState::rootPath(CanonPath path)
{
    return {rootFS, std::move(path)};
}

SourcePath EvalState::rootPath(std::string_view path)
{
    /* FIXME: Move this out of EvalState, since it's using native
       std::filesystem::path and current working directory. */
    return {rootFS, CanonPath(absPath(path).string())};
}

SourcePath EvalState::storePath(const StorePath & path)
{
    return {rootFS, CanonPath{store->printStorePath(path)}};
}

void EvalState::ensureLazyPathCopied(const StorePath & path)
{
    if (settings.readOnlyMode)
        return;

    auto mount = storeFS->getMount(CanonPath(store->printStorePath(path)));
    if (!mount)
        return;

    /* TODO: We could memoise this in-memory if necessary. */
    auto storePath = fetchToStore(
        fetchSettings,
        *store,
        SourcePath{ref(mount)},
        /* Force a copy. mountInput does a dryRun to just calculate the storePath and narHash. */
        FetchMode::Copy,
        path.name());

    assert(storePath.name() == path.name());
}

void EvalState::ensureLazyPathsCopied(const NixStringContext & context)
{
    for (const auto & c : context)
        if (auto * o = std::get_if<NixStringContextElem::Opaque>(&c.raw))
            /* TODO: This could be done in parallel. */
            ensureLazyPathCopied(o->path);
}

StorePath
EvalState::mountInput(fetchers::Input & input, const fetchers::Input & originalInput, ref<SourceAccessor> accessor)
{
    /* To mount the input, dryRun is sufficient. We still compute the narHash (to check for mismatches) and the store
       path to figure out where to mount it. TODO: This could be relaxed in the future by making outPath and narHash
       lazier. Good code that doesn't do `toString ./.` or otherwise inspects the outPath string and only uses it for
       doing relative imports does not even require computing the store path. That is a big invasive change though and
       would require having a special "LazyStorePathString" thunk. narHash also doesn't need to be computed eagerly in
       case it's not actually specified (like during local development with a dirty tree) - in that case narHash could
       also become a lazy app/thunk that shares the state with the storePath delayed computation. */
    auto [storePath, narHash] = fetchToStore2(fetchSettings, *store, accessor, FetchMode::DryRun, input.getName());

    allowPath(storePath); // FIXME: should just whitelist the entire virtual store

    storeFS->mount(CanonPath(store->printStorePath(storePath)), accessor);

    input.attrs.insert_or_assign("narHash", narHash.to_string(HashFormat::SRI, true));

    if (originalInput.getNarHash() && narHash != *originalInput.getNarHash())
        throw Error(
            (unsigned int) 102,
            "NAR hash mismatch in input '%s', expected '%s' but got '%s'",
            originalInput.to_string(),
            narHash.to_string(HashFormat::SRI, true),
            originalInput.getNarHash()->to_string(HashFormat::SRI, true));

    return storePath;
}

} // namespace nix
