#pragma once

#include "nix/cmd/command.hh"
#include "nix/cmd/installable-flake.hh"
#include "nix/flake/flake.hh"

namespace nix {

class FlakeCommand : virtual Args, public MixFlakeOptions
{
protected:
    std::string flakeUrl = ".";

public:

    FlakeCommand();

    FlakeRef getFlakeRef();

    flake::LockedFlake lockFlake();

    std::vector<FlakeRef> getFlakeRefsForCompletion() override;
};

} // namespace nix
