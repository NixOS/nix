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

    Setting<bool> useRegistries{
        this, true, "use-registries", "Whether to use flake registries to resolve flake references.", {}, true};

    Setting<bool> acceptFlakeConfig{
        this,
        false,
        "accept-flake-config",
        "Whether to accept nix configuration from a flake without prompting.",
        {},
        true};

    Setting<std::string> commitLockFileSummary{
        this,
        "",
        "commit-lock-file-summary",
        R"(
          The commit summary to use when committing changed flake lock files. If
          empty, the summary is generated based on the action performed.
        )",
        {"commit-lockfile-summary"},
        true};
};

// TODO: don't use a global variable.
extern FlakeSettings flakeSettings;

}
