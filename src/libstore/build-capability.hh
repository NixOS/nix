#pragma once
///@file

#include <optional>
#include <set>
#include <string>

namespace nix {

class Schedulable {
public:
    virtual std::string_view getSystem() const = 0;
    virtual const std::set<std::string> & getRequiredFeatures() const = 0;
    virtual bool getPreferLocalBuild() const = 0;
};

/**
 * Parameters that determine which derivations can be built.
 *
 * *Where* it can be built is determined by context.
 */
struct BuildCapability {
    /**
     * For a derivation to be buildable by this capability, `system` must match the derivation `system` by case sensitive string equality.
     *
     * In a given context, multiple `system`s may be supported. This is represented by having multiple `BuildCapability`s.
     */
    std::string system;

    /**
     * For a derivation to be buildable by this capability, `supportedFeatures` must be a superset of the derivation's `requiredFeatures`, or be equal.
     */
    std::set<std::string> supportedFeatures;

    /**
     * For a derivation to be buildable by this capability, `mandatoryFeatures` must be a subset of the derivation's `requiredFeatures`, or be equal.
     */
    std::set<std::string> mandatoryFeatures;

    bool canBuild(const Schedulable & schedulable) const;
};

/**
 * Extends `BuildCapability` to include scheduling information.
 */
struct SchedulableCapability {
    /**
     * Which derivations can be built.
     */
    BuildCapability capability;

    /**
     * An upper bound on the number of derivations that can be built at once.
     *
     * If `std::nullopt`, the concurrency is unlimited, or controlled by the remote side.
     */
    std::optional<int> maxJobs;

    /**
     * Whether the capability is local to the current machine.
     *
     * This may include VMs that are running on the same machine.
     * It is the user's responsibility to configure their VMs so that there is no unnecessary copying between VMs.
     *
     * This parameter interacts with the `preferLocalBuild` derivation attribute for builds to indicate that the overhead of copying can be expected to be larger than the actual build.
     */
    bool isLocal;

    /**
     * A proportional measure of build performance, typically configured by the user.
     * Is divided by load to find the best candidate for a build.
     *
     * Must be positive.
     */
    float speedFactor;
};

} // namespace nix
