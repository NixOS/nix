#pragma once

#include <exception>

namespace nix {

/**
 * Exit the program with a given exit code.
 */
class Exit : public std::exception
{
public:
    int status;
    Exit() : status(0) { }
    explicit Exit(int status) : status(status) { }
    virtual ~Exit();
};

}
