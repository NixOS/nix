#pragma once
/**
 * @file
 * Abstract Environment interface for evaluation I/O operations.
 */

#include <optional>
#include <string>
#include "nix/util/ref.hh"

namespace nix {

struct SourceAccessor;
class Store;
struct EvalSettings;

/**
 * Environment interface for evaluation I/O operations.
 *
 * This interface abstracts external state access needed during evaluation.
 */
class Environment
{
public:
    virtual ~Environment() = default;

    /**
     * Get the root filesystem accessor.
     * Delegates filesystem operations (readFile, pathExists, readDirectory, etc.)
     * to the SourceAccessor interface.
     * @return Reference to the root source accessor
     */
    virtual ref<SourceAccessor> fsRoot() = 0;

    /**
     * Get environment variable value.
     * @param name Variable name
     * @return Optional value (nullopt if not set)
     */
    virtual std::optional<std::string> getEnv(const std::string & name) = 0;
};

/**
 * Create a system environment based on evaluation settings and store.
 * @param settings Evaluation settings
 * @param store Reference to the Nix store
 * @param buildStore Optional separate store for building (defaults to store)
 * @return A new Environment instance
 */
ref<Environment>
makeSystemEnvironment(const EvalSettings & settings, ref<Store> store, std::shared_ptr<Store> buildStore = nullptr);

} // namespace nix
