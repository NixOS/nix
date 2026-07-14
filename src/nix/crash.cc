#include "nix/cmd/command.hh"

#include <bitset>

using namespace nix;

static void doThrow()
{
    throw Error("exception in destructor");
}

struct CmdCrash : Command
{
    std::string type;

    CmdCrash()
    {
        expectArg("type", &type);
    }

    Category category() override
    {
        return catUndocumented;
    }

    std::string description() override
    {
        return "crash the program to test crash reporting";
    }

    void run() override
    {
        if (type == "segfault") {
            printError("Triggering a segfault...");
            volatile int * p = nullptr;
            *p = 123;
        }

        else if (type == "assert") {
            printError("Triggering an assertion failure...");
            assert(false && "This is an assertion failure");
        }

        else if (type == "logic-error") {
            printError("Triggering a C++ logic error...");
            std::bitset<4>{"012"};
        }

        else if (type == "panic") {
            printError("Triggering a panic...");
            panic("test panic");
        }

        else if (type == "abort") {
            printError("Triggering an abort...");
            std::abort();
        }

        else if (type == "terminate") {
            printError("Triggering an std::terminate...");

            struct Foo
            {
                ~Foo()
                {
                    doThrow();
                }
            };

            Foo foo;
        }

        else {
            throw Error("unknown crash type '%s'", type);
        }
    }
};

static auto rCrash = registerCommand<CmdCrash>("__crash");
