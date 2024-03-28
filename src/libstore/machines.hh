#pragma once
///@file

#include "build-capability.hh"
#include "types.hh"

namespace nix {

class Store;

struct SchedulableCapability;

struct Machine {

    const std::string storeUri;

    const std::string sshKey;
    const std::string sshPublicHostKey;

    /**
     * NOTE: The set of capabilities is currently restricted by the constructor
     *       and the machines format.
     */
    std::vector<SchedulableCapability> capabilities;

    /** Index on `capabilities`. Pointers are references into `capabilities`. */
    std::map<std::string, std::vector<SchedulableCapability *>> capabilitiesBySystem;

    bool enabled = true;

    /**
     * @return Whether this host can build the `schedulable`.
     */
    bool canBuild(const Schedulable & schedulable) const;

    /**
     * @deprecated Use `canBuild` instead. This method is not accurate.
     *
     * @return Whether `system` is either `"builtin"` or in
     * `systemTypes`.
     */
    bool systemSupported(const std::string & system) const;

    /**
     * @deprecated Use `canBuild` instead. This method is not accurate.
     *
     * @return Whether `features` is a subset of the union of `supportedFeatures` and
     * `mandatoryFeatures`
     */
    bool allSupported(const std::set<std::string> & features) const;

    /**
     * @deprecated Use `canBuild` instead. This method is not accurate.
     * @return @Whether `mandatoryFeatures` is a subset of `features`
     */
    bool mandatoryMet(const std::set<std::string> & features) const;

    Machine(decltype(storeUri) storeUri,
        std::set<std::string> systemTypes,
        decltype(sshKey) sshKey,
        unsigned int maxJobs,
        float speedFactor,
        std::set<std::string> supportedFeatures,
        std::set<std::string> mandatoryFeatures,
        decltype(sshPublicHostKey) sshPublicHostKey);

    ref<Store> openStore() const;
};

typedef std::vector<Machine> Machines;

void parseMachines(const std::string & s, Machines & machines);

Machines getMachines();

}
