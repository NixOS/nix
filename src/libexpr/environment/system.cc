#include "nix/expr/environment/system.hh"
#include "nix/expr/eval-settings.hh"
#include "nix/store/store-api.hh"
#include "nix/util/environment-variables.hh"
#include "nix/util/mounted-source-accessor.hh"
#include "nix/util/source-accessor.hh"
#include "nix/fetchers/filtering-source-accessor.hh"
#include <boost/unordered/unordered_flat_set.hpp>

namespace nix {

SystemEnvironment::SystemEnvironment(const EvalSettings & settings, ref<Store> store, std::shared_ptr<Store> buildStore)
    : settings(settings)
    , store(store)
    , buildStore(buildStore ? buildStore : store)
    , storeFS(makeMountedSourceAccessor({
          {CanonPath::root, makeEmptySourceAccessor()},
          /* In the pure eval case, we can simply require
             valid paths. However, in the *impure* eval
             case this gets in the way of the union
             mechanism, because an invalid access in the
             upper layer will *not* be caught by the union
             source accessor, but instead abort the entire
             lookup.

             This happens when the store dir in the
             ambient file system has a path (e.g. because
             another Nix store there), but the relocated
             store does not.

             TODO make the various source accessors doing
             access control all throw the same type of
             exception, and make union source accessor
             catch it, so we don't need to do this hack.
           */
          {CanonPath(store->storeDir), store->getFSAccessor(settings.pureEval)},
      }))
    , rootFSAccessor([&] {
        /* In pure eval mode, we provide a filesystem that only
           contains the Nix store.

           Otherwise, use a union accessor to make the augmented store
           available at its logical location while still having the
           underlying directory available. This is necessary for
           instance if we're evaluating a file from the physical
           /nix/store while using a chroot store, and also for lazy
           mounted fetchTree. */
        auto accessor = settings.pureEval ? storeFS.cast<SourceAccessor>()
                                          : makeUnionSourceAccessor({getFSSourceAccessor(), storeFS});

        /* Apply access control if needed. */
        if (settings.restrictEval || settings.pureEval)
            accessor = AllowListSourceAccessor::create(
                accessor, {}, {}, [&settings](const CanonPath & path) -> RestrictedPathError {
                    auto modeInformation = settings.pureEval ? "in pure evaluation mode (use '--impure' to override)"
                                                             : "in restricted mode";
                    throw RestrictedPathError("access to absolute path '%1%' is forbidden %2%", path, modeInformation);
                });

        return accessor;
    }())
{
}

ref<SourceAccessor> SystemEnvironment::fsRoot()
{
    return rootFSAccessor;
}

std::optional<std::string> SystemEnvironment::getEnv(const std::string & name)
{
    if (settings.restrictEval || settings.pureEval)
        return std::nullopt;
    return nix::getEnv(name);
}

} // namespace nix
