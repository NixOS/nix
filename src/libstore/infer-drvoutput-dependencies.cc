#include "drv-output-info.hh"
#include "derivations.hh"
#include "store-api.hh"

namespace nix {
std::set<DrvInput> computeDrvInputs(Store& store, Derivation& drv) {
     std::set<DrvInput> res;
     for (auto & [depDrvPath, wantedDepOutputs] : drv.inputDrvs) {
        for (auto wantedOutput: wantedDepOutputs) {
            auto inputClosure = store.drvInputClosure(DrvOutputId{depDrvPath, wantedOutput});
            res.insert(inputClosure.begin(), inputClosure.end());
        }
    }
    for (auto & inputPath : drv.inputSrcs) {
        auto inputClosure = store.drvInputClosure(inputPath);
        res.insert(inputClosure.begin(), inputClosure.end());
    }
    return res;
}

std::set<DrvInput> shrinkDrvInputs(Store& store, std::set<DrvInput> allDrvInputs, StorePathSet& references) {
    std::set<DrvInput> res;
    for (auto potentialInput : allDrvInputs) {
        auto shouldBeKept = std::visit(overloaded {
            [&](StorePath opaque) -> bool { return references.count(opaque); },
            [&](DrvOutputId id) -> bool {
                try {
                    return references.count(*store.queryOutputPathOf(id.drvPath, id.outputName));
                } catch (Error&) { return false; }
            },
        }, static_cast<RawDrvInput>(potentialInput)
        );
        if (shouldBeKept)
            res.insert(potentialInput);
    }
    return res;
}

void registerOutputs(Store& store,
                     StorePath& drvPath,
                     Derivation& deriver,
                     std::map<std::string, StorePath> outputMappings) {
    std::set<DrvInput> buildTimeInputs = computeDrvInputs(store, deriver);

    for (auto& [outputName, outputPath] : outputMappings) {
        registerOneOutput(store, DrvOutputId{drvPath, outputName}, drvPath,
                          buildTimeInputs, outputPath);
    }
}

void registerOneOutput(Store& store,
                       DrvOutputId id,
                       StorePath& resolvedDrvPath,
                       std::set<DrvInput> buildTimeInputs,
                       StorePath& outputPath) {
    StorePathSet outputPathDeps = store.queryPathInfo(outputPath)->references;
    auto dependencies = shrinkDrvInputs(store, buildTimeInputs, outputPathDeps);
    for (auto& dep : dependencies) {
        auto rawDep = dep.variant();
        if (auto depId = std::get_if<DrvOutputId>(&rawDep)) {
            auto depInfo = store.queryDrvOutputInfo(*depId);
            auto depDrv = store.readDerivation(depId->drvPath);
            registerOutputs(store, depId->drvPath, depDrv, {{depId->outputName, depInfo->outPath}});
        }
    }
    store.registerDrvOutput(id, DrvOutputInfo{
                                    .outPath = outputPath,
                                    .dependencies = dependencies,
                                });
}
}
