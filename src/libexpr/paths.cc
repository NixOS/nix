#include "nix/store/store-api.hh"
#include "nix/expr/eval.hh"
#include "nix/expr/environment/system.hh"
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
    // FIXME: do not use systemEnvironment
    return {rootFS, CanonPath{systemEnvironment->store->printStorePath(path)}};
}

StorePath
EvalState::mountInput(fetchers::Input & input, const fetchers::Input & originalInput, ref<SourceAccessor> accessor)
{
    // FIXME: do not use systemEnvironment
    auto storePath = fetchToStore(fetchSettings, *systemEnvironment->store, accessor, FetchMode::Copy, input.getName());

    allowPath(storePath); // FIXME: should just whitelist the entire virtual store

    systemEnvironment->storeFS->mount(CanonPath(systemEnvironment->store->printStorePath(storePath)), accessor);

    auto narHash = systemEnvironment->store->queryPathInfo(storePath)->narHash;
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
