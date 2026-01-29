#pragma once
///@file

#include <string>

namespace nix::linux {

struct PersonalityArgs
{
    std::string_view system;
    bool impersonateLinux26;
};

void setPersonality(PersonalityArgs args);

} // namespace nix::linux
