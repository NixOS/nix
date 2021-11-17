#pragma once

#include "types.hh"

namespace nix {

class Store;

struct Machine {

    const std::string storeUri;
    const std::vector<std::string> systemTypes;
    const std::string sshKey;
    const unsigned int maxJobs;
    const unsigned int speedFactor;
    const std::set<std::string> supportedFeatures;
    const std::set<std::string> mandatoryFeatures;
    const std::string sshPublicHostKey;
    bool enabled = true;

    bool allSupported(const std::set<std::string> & features) const;

    bool mandatoryMet(const std::set<std::string> & features) const;

    Machine(decltype(storeUri) storeUri,
        decltype(systemTypes) systemTypes,
        decltype(sshKey) sshKey,
        decltype(maxJobs) maxJobs,
        decltype(speedFactor) speedFactor,
        decltype(supportedFeatures) supportedFeatures,
        decltype(mandatoryFeatures) mandatoryFeatures,
        decltype(sshPublicHostKey) sshPublicHostKey);

    ref<Store> openStore() const;
};

typedef std::vector<Machine> Machines;

void parseMachines(const std::string & s, Machines & machines);

Machines getMachines();

}
