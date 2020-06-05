#pragma once

#include "types.hh"

namespace nix {

struct Machine {

    const string storeUri;
    const std::vector<string> systemTypes;
    const string sshKey;
    const unsigned int maxJobs;
    const unsigned int speedFactor;
    const std::set<string> supportedFeatures;
    const std::set<string> mandatoryFeatures;
    const std::string sshPublicHostKey;
    bool enabled = true;

    bool allSupported(const std::set<string> & features) const;

    bool mandatoryMet(const std::set<string> & features) const;

    Machine(decltype(storeUri) storeUri,
        decltype(systemTypes) systemTypes,
        decltype(sshKey) sshKey,
        decltype(maxJobs) maxJobs,
        decltype(speedFactor) speedFactor,
        decltype(supportedFeatures) supportedFeatures,
        decltype(mandatoryFeatures) mandatoryFeatures,
        decltype(sshPublicHostKey) sshPublicHostKey);
};

typedef std::vector<Machine> Machines;

void parseMachines(const std::string & s, Machines & machines);

Machines getMachines();

}
