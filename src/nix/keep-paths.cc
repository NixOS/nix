#include "command.hh"
#include "util.hh"
#include "store-api.hh"

using namespace nix;

struct CmdKeepPaths : StoreCommand
{
    std::string name() override
    {
        return "keep-paths";
    }

    std::string description() override
    {
        return "keeps store paths alive as long as nix keep-paths is alive";
    }

    void printHelp(const string & programName, std::ostream & out) override
    {
        StoreCommand::printHelp(programName, out);

        out << "\n";
        out << "Reads in newline-separated store-paths over stdin.\n";
        out << "A newline is printed after the path is registered.\n";
    }

    void run(ref<Store> store) override
    {
        try {
            while (true) {
                const auto s = readLine(0);
                if (s.empty())
                    continue;
                if (!store->isStorePath(s)) {
                    throw Error("%s is not a store path", s);
                }

                store->addTempRoot(s);
                writeLine(1, "");
            }
        } catch (EndOfFile & e) {
            return;
        }
    }
};

static RegisterCommand r1(make_ref<CmdKeepPaths>());
