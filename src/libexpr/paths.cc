#include "nix/store/store-api.hh"
#include "nix/expr/eval.hh"
#include "nix/util/mounted-source-accessor.hh"
#include "nix/fetchers/fetch-to-store.hh"
#include "nix/fetchers/fetchers.hh"

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

StorePath
EvalState::mountInput(fetchers::Input & input, const fetchers::Input & originalInput, ref<SourceAccessor> accessor)
{
    auto [storePath, narHash] = fetchToStore2(fetchSettings, *store, accessor, FetchMode::Copy, input.getName());

    allowPath(storePath); // FIXME: should just whitelist the entire virtual store

    storeFS->mount(CanonPath(store->printStorePath(storePath)), accessor);

    input.attrs.insert_or_assign("narHash", narHash.to_string(HashFormat::SRI, true));

    // Record the mapping from this source store path to the original
    // filesystem path so that source-origins can resolve provenance.
    // Prefer the accessor's originalRootPath (set by git/path input schemes
    // to the source tree root) over input.getSourcePath() (which returns
    // the flake directory, not the git root for git-tracked flakes).
    if (accessor->originalRootPath) {
        sourceStoreToOriginalPath.try_emplace(storePath, *accessor->originalRootPath);
    } else if (auto origPath = input.getSourcePath()) {
        sourceStoreToOriginalPath.try_emplace(storePath, *origPath);
        accessor->originalRootPath = *origPath;
    }

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
