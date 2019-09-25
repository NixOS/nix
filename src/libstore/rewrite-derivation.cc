#include "rewrite-derivation.hh"
#include "parsed-derivations.hh"

namespace nix {

std::string rewriteStrings(std::string s, const StringRewrites & rewrites)
{
    for (auto & i : rewrites) {
        size_t j = 0;
        while ((j = s.find(i.first, j)) != string::npos)
            s.replace(j, i.first.size(), i.second);
    }
    return s;
}

void recomputeOutputs(Store & store, Derivation & drv) {
    DerivationOutputs oldOutputs = drv.outputs;

    for (auto & i : drv.outputs) {
        auto outputEnvVar = drv.env.find(i.first);
        if (outputEnvVar != drv.env.end())
            outputEnvVar->second = "";
        drv.outputs[i.first] = DerivationOutput("", "", "");
    }

    /* Use the masked derivation expression to compute the output
        path. */
    Hash h = hashDerivationModulo(store, drv);

    for (auto & i : drv.outputs)
        if (i.second.path == "") {
            // XXX: There's certainly a better and less error-prone way
            // of getting the name than to look it up in the drv environment
            string name = ParsedDerivation("", drv).getStringAttr("name").value_or("");
            Path outPath = store.makeOutputPath(i.first, h, name);
            auto outputEnvVar = drv.env.find(i.first);
            if (outputEnvVar != drv.env.end())
                outputEnvVar->second = outPath;
            i.second.path = outPath;
            debug(format("Rewrote output %1% to %2%") % oldOutputs[i.first].path % outPath);
        }
}

void rewriteDerivation(Store & store, Derivation & drv, const PathMap & pathRewrites) {
    StringRewrites rewrites;

    for (auto rewrite: pathRewrites) {
        if (rewrite.first != rewrite.second) {
            rewrites.emplace(
                baseNameOf(rewrite.first),
                baseNameOf(rewrite.second)
                );
            debug(format("rewriting %1% as %2%") % baseNameOf(rewrite.first) % baseNameOf(rewrite.second));
        }
    }

    drv.builder = rewriteStrings(drv.builder, rewrites);
    for (auto & arg: drv.args) {
        arg = rewriteStrings(arg, rewrites);
    }

    StringPairs newEnv;
    for (auto & envVar: drv.env) {
        auto envName = rewriteStrings(envVar.first, rewrites);
        auto envValue = rewriteStrings(envVar.second, rewrites);
        newEnv.emplace(envName, envValue);
    }
    drv.env = newEnv;

    // Remove all the input derivations because we've already resolved their
    // output path and we don't want them to have an influence on the output
    // paths.
    // XXX: We only do that if we effectively rewrote some inputs, because this
    // changes the output path of the derivation and we want to maintain
    // backwards compatibility
    if (!rewrites.empty()) {
        drv.inputDrvs = {};
        drv.inputSrcs = {};
        for (auto & inputPathPair : pathRewrites) {
            drv.inputSrcs.emplace(inputPathPair.second);
        }
    }

    if (!drv.isFixedOutput()) {
        recomputeOutputs(store, drv);
    }
}

PathMap gatherInputPaths(Store & store, BasicDerivation & drv, bool isDerivation)
{
    PathMap directInputsPathMap;

    // XXX: Make this non dummy
    auto drvPath = "";

    /* First, the input derivations. */
    if (isDerivation)
        for (auto & i : dynamic_cast<Derivation*>(&drv)->inputDrvs) {
            /* Add the relevant output closures of the input derivation
               `i' as input paths.  Only add the closures of output paths
               that are specified as inputs. */
            assert(store.isValidPath(i.first));
            Derivation inDrv = store.derivationFromPath(i.first);
            for (auto & j : i.second)
                if (inDrv.outputs.find(j) != inDrv.outputs.end())
                    directInputsPathMap.emplace(
                        inDrv.outputs[j].path,
                        store.resolveAliases(inDrv.outputs[j].path)
                    );
                else
                    throw Error(
                        format("derivation '%1%' requires non-existent output '%2%' from input derivation '%3%'")
                        % drvPath % j % i.first);
        }

    /* Second, the input sources. */
    for (auto & inputSrc: drv.inputSrcs) {
        directInputsPathMap.emplace(
            inputSrc,
            store.resolveAliases(inputSrc)
        );
    }
    return directInputsPathMap;

}

}
