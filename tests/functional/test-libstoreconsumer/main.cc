#include "nix/store/globals.hh"
#include "nix/store/store-open.hh"
#include "nix/store/build.hh"
#include "nix/store/build-result.hh"
#include "nix/store/derived-path.hh"
#include "nix/store/outputs-spec.hh"
#include "nix/store/store-api.hh"
#include <iostream>
#include <exception>
#include <string>
#include <vector>

int main(int argc, char ** argv)
{
    using namespace nix;

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

        const auto results = store->getBuilder()->buildPathsWithResults(paths, bmNormal);

        for (const auto & result : results) {
            if (auto * successP = result.tryGetSuccess()) {
                for (const auto & [outputName, realisation] : successP->builtOutputs) {
                    std::cout << store->printStorePath(realisation.outPath) << "\n";
                }
            }
        }

        return 0;

    } catch (const std::exception & e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
