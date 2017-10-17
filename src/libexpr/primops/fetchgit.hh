#pragma once

#include <string>

namespace nix {

class EvalState;

Path exportGit(EvalState & state, const std::string & uri,
    const std::string & ref, const std::string & rev = "");

}
