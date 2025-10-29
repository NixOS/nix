#pragma once
///@file

#include <functional>
#include <map>
#include <string>

namespace nix {

typedef std::function<void(int, char **)> MainFunction;

struct RegisterLegacyCommand
{
    typedef std::map<std::string, MainFunction> Commands;

    static Commands & commands()
    {
        static Commands commands;
        return commands;
    }

    RegisterLegacyCommand(const std::string & name, MainFunction fun)
    {
        commands()[name] = fun;
    }
};

} // namespace nix
