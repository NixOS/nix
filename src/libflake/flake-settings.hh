#pragma once
///@file

#include "types.hh"
#include "config.hh"
#include "util.hh"

#include <map>
#include <limits>

#include <sys/types.h>

namespace nix {

struct FlakeSettings : public Config
{
    FlakeSettings();

    Setting<std::string> flakeRegistry{this, "https://channels.nixos.org/flake-registry.json", "flake-registry",
        R"(
          Path or URI of the global flake registry.

          When empty, disables the global flake registry.
        )",
        {}, true, Xp::Flakes};


    Setting<bool> useRegistries{this, true, "use-registries",
        "Whether to use flake registries to resolve flake references.",
        {}, true, Xp::Flakes};

    Setting<bool> acceptFlakeConfig{this, false, "accept-flake-config",
        "Whether to accept nix configuration from a flake without prompting.",
        {}, true, Xp::Flakes};

    Setting<std::string> commitLockFileSummary{
        this, "", "commit-lockfile-summary",
        R"(
          The commit summary to use when committing changed flake lock files. If
          empty, the summary is generated based on the action performed.
        )",
        {}, true, Xp::Flakes};
};

// FIXME: don't use a global variable.
extern FlakeSettings flakeSettings;

}
