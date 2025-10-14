#pragma once
/**
 * @file
 * System Environment implementation for evaluation I/O operations.
 */

#include "nix/expr/environment.hh"
#include "nix/util/mounted-source-accessor.hh"
#include "nix/util/ref.hh"

namespace nix {

struct SourceAccessor;
struct EvalSettings;
class Store;

/**
 * System environment implementation.
 *
 * Provides filesystem access and environment variables based on
 * evaluation settings and store configuration.
 */
class SystemEnvironment : public Environment
{
private:
    /**
     * Evaluation settings, for the purpose of querying those settings that
     * affect the interpretation of the ambient environment.
     */
    const EvalSettings & settings;

    // FIXME: Ideally the following fields wouldn't be exposed, but this class
    // currently doubles as a context container in many places.
public:

    /**
     * Store used to materialise .drv files.
     */
    ref<Store> store;

    /**
     * Store used to build stuff.
     */
    ref<Store> buildStore;

    /**
     * The accessor corresponding to `store`.
     */
    ref<MountedSourceAccessor> storeFS;

    /**
     * The accessor for the root filesystem.
     */
    ref<SourceAccessor> rootFSAccessor;

    SystemEnvironment(const EvalSettings & settings, ref<Store> store, std::shared_ptr<Store> buildStore = nullptr);

    ref<SourceAccessor> fsRoot() override;
    std::optional<std::string> getEnv(const std::string & name) override;
};

} // namespace nix
