#include "nix/store/globals.hh"
#include "nix/store/store-open.hh"
#include "nix/store/build-result.hh"
#include <iostream>

using namespace nix;

extern "C" [[gnu::retain]] const char * __asan_default_options()
{
    // We leak a bunch of memory knowingly on purpose. It's not worthwhile to
    // diagnose that memory being leaked for now.
    return "abort_on_error=1:print_summary=1:detect_leaks=0";
}

int main(int argc, char ** argv)
{
    try {
        if (argc != 2) {
            std::cerr << "Usage: " << argv[0] << " store/path/to/something.drv\n";
            return 1;
        }

        std::string drvPath = argv[1];

        initLibStore();

        auto store = nix::openStore();

        // build the derivation

        std::vector<DerivedPath> paths{DerivedPath::Built{
            .drvPath = makeConstantStorePathRef(store->parseStorePath(drvPath)), .outputs = OutputsSpec::Names{"out"}}};

        const auto results = store->buildPathsWithResults(paths, bmNormal, store);

        for (const auto & result : results) {
            for (const auto & [outputName, realisation] : result.builtOutputs) {
                std::cout << store->printStorePath(realisation.outPath) << "\n";
            }
        }

        return 0;

    } catch (const std::exception & e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
