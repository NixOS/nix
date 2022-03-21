#pragma once

#include <functional>
#include <map>
#include <string>

namespace nix {

typedef std::function<void(int, char * *)> MainFunction;

struct RegisterLegacyCommand
{
    typedef std::map<std::string, MainFunction> Commands;
    static Commands * commands;

    RegisterLegacyCommand(const std::string & name, MainFunction fun)
    {
        if (!commands) commands = new Commands;
        (*commands)[name] = fun;
    }
};

}
