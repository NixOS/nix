#pragma once

#include <functional>
#include <map>
#include <string>

namespace nix {

typedef std::function<void(int, char * *)> MainFunction;

struct RegisterLegacyCommand
{
    typedef std::map<std::string, MainFunction, std::less<>> Commands;
    static Commands * commands;

    RegisterLegacyCommand(std::string_view name, MainFunction fun)
    {
        if (!commands) commands = new Commands;
        (*commands)[std::string { name }] = fun;
    }
};

}
