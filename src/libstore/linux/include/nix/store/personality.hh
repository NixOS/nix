#pragma once
///@file

#include <string_view>

namespace nix::linux {

struct PersonalityArgs
{
    /**
     * Nix system-double. Used for settings PER_LINUX32 for 32 bit applications
     * on 64 bit kernels. See personality(2).
     */
    std::string_view system;
    bool disableASLR;
};

void setPersonality(PersonalityArgs args);

} // namespace nix::linux
