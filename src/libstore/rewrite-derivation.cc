#include "rewrite-derivation.hh"
#include "parsed-derivations.hh"
#include "util.hh"

namespace nix {

void recomputeOutputs(Store & store, Derivation & drv) {
    for (auto & i : drv.outputs) {
        debug("Rewriting env var %s", i.first);
        auto outputEnvVar = drv.env.find(i.first);
        if (outputEnvVar != drv.env.end()) {
            outputEnvVar->second = "";
            debug("Rewrote env var %s", outputEnvVar->first);
        }
        i.second = DerivationOutput{ StorePath::dummy, "", "" };
    }

    /* Use the masked derivation expression to compute the output path. */
    Hash h = hashDerivationModulo(store, drv, true);

    for (auto & i : drv.outputs) {
        // XXX: There's certainly a better and less error-prone way
        // of getting the name than to look it up in the drv environment
        string name = ParsedDerivation(StorePath::dummy, drv).getStringAttr("name").value_or("");
        StorePath outPath = store.makeOutputPath(i.first, h, name);
        auto outputEnvVar = drv.env.find(i.first);
        if (outputEnvVar != drv.env.end())
            outputEnvVar->second = store.printStorePath(outPath);
        debug(format("Rewrote output %1% to %2%")
            % store.printStorePath(drv.outputs.at(i.first).path)
            % store.printStorePath(outPath));
        i.second = DerivationOutput { outPath, "", "" };
    }
}

void rewriteDerivation(Store & store, Derivation & drv, const StringMap & rewrites) {

    debug("Rewriting the derivation");

    for (auto &rewrite: rewrites) {
        debug("rewriting %s as %s", rewrite.first, rewrite.second);
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

    if (!drv.isFixedOutput()) {
        recomputeOutputs(store, drv);
    }
}

}
