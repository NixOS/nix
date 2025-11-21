#pragma once
///@file

#include "nix/util/ref.hh"
#include "nix/store/store-reference.hh"

namespace nix {

class Settings;
class Store;

struct Machine;

typedef std::vector<Machine> Machines;

struct Machine
{

    const StoreReference storeUri;
    const StringSet systemTypes;
    const std::string sshKey;
    const unsigned int maxJobs;
    const float speedFactor;
    const StringSet supportedFeatures;
    const StringSet mandatoryFeatures;
    const std::string sshPublicHostKey;
    bool enabled = true;

    /**
     * @return Whether `system` is either `"builtin"` or in
     * `systemTypes`.
     */
    bool systemSupported(const std::string & system) const;

    /**
     * @return Whether `features` is a subset of the union of `supportedFeatures` and
     * `mandatoryFeatures`.
     */
    bool allSupported(const StringSet & features) const;

    /**
     * @return Whether `mandatoryFeatures` is a subset of `features`.
     */
    bool mandatoryMet(const StringSet & features) const;

    Machine(
        const std::string & storeUri,
        decltype(systemTypes) systemTypes,
        decltype(sshKey) sshKey,
        decltype(maxJobs) maxJobs,
        decltype(speedFactor) speedFactor,
        decltype(supportedFeatures) supportedFeatures,
        decltype(mandatoryFeatures) mandatoryFeatures,
        decltype(sshPublicHostKey) sshPublicHostKey);

    /**
     * Elaborate `storeUri` into a complete store reference,
     * incorporating information from the other fields of the `Machine`
     * as applicable.
     */
    StoreReference completeStoreReference() const;

    /**
     * Open a `Store` for this machine.
     *
     * Just a simple function composition:
     * ```c++
     * nix::openStore(completeStoreReference())
     * ```
     */
    ref<Store> openStore(Settings & settings) const;

    /**
     * Parse a machine configuration.
     *
     * Every machine is specified on its own line, and lines beginning
     * with `@` are interpreted as paths to other configuration files in
     * the same format.
     */
    static Machines parseConfig(const StringSet & defaultSystems, const std::string & config);
};

/**
 * Parse machines from the global config
 *
 * @todo Remove, globals are bad.
 */
Machines getMachines(const Settings & settings);

} // namespace nix
