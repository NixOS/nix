#pragma once
///@file

#include <string>

namespace nix {

class Settings;

}

namespace nix::linux {

void setPersonality(const nix::Settings & settings, std::string_view system);

}
