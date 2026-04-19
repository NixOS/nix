#include "nix/cmd/develop.hh"
#include "nix/store/derivations.hh"
#include "nix/store/outputs-query.hh"
#include "nix/store/store-api.hh"

namespace nix {

const static std::string getEnvSh =
#include "get-env.sh.gen.hh"
    ;

StorePath getDerivationEnvironment(ref<Store> store, ref<Store> evalStore, const StorePath & drvPath)
{
    auto drv = evalStore->derivationFromPath(drvPath);

    auto builder = baseNameOf(drv.builder);
    if (builder != "bash")
        throw Error("'nix develop' only works on derivations that use 'bash' as their builder");

    auto getEnvShPath = ({
        StringSource source{getEnvSh};
        evalStore->addToStoreFromDump(
            source,
            "get-env.sh",
            FileSerialisationMethod::Flat,
            ContentAddressMethod::Raw::Text,
            HashAlgorithm::SHA256,
            {});
    });

    drv.args = {store->printStorePath(getEnvShPath)};

    /* Remove derivation checks. */
    if (drv.structuredAttrs) {
        drv.structuredAttrs->structuredAttrs.erase("outputChecks");
    } else {
        drv.env.erase("allowedReferences");
        drv.env.erase("allowedRequisites");
        drv.env.erase("disallowedReferences");
        drv.env.erase("disallowedRequisites");
    }

    drv.env.erase("name");

    /* Rehash and write the derivation. FIXME: would be nice to use
       'buildDerivation', but that's privileged. */
    drv.name += "-env";
    drv.env.emplace("name", drv.name);
    drv.inputSrcs.insert(std::move(getEnvShPath));
    for (auto & [outputName, output] : drv.outputs) {
        std::visit(
            overloaded{
                [&](const DerivationOutput::InputAddressed &) {
                    output = DerivationOutput::Deferred{};
                    drv.env[outputName] = "";
                },
                [&](const DerivationOutput::CAFixed &) {
                    output = DerivationOutput::Deferred{};
                    drv.env[outputName] = "";
                },
                [&](const auto &) {
                    // Do nothing for other types (CAFloating, Deferred, Impure)
                },
            },
            output.raw);
    }
    drv.fillInOutputPaths(*evalStore);

    auto shellDrvPath = evalStore->writeDerivation(drv);

    /* Build the derivation. */
    store->buildPaths(
        {DerivedPath::Built{
            .drvPath = makeConstantStorePathRef(shellDrvPath),
            .outputs = OutputsSpec::All{},
        }},
        bmNormal,
        evalStore);

    // `get-env.sh` will write its JSON output to an arbitrary output
    // path, so return the first non-empty output path.
    for (auto & [_0, optPath] : deepQueryPartialDerivationOutputMap(*evalStore, shellDrvPath)) {
        assert(optPath);
        auto accessor = evalStore->requireStoreObjectAccessor(*optPath);
        if (auto st = accessor->maybeLstat(CanonPath::root); st && st->fileSize.value_or(0))
            return *optPath;
    }

    throw Error("get-env.sh failed to produce an environment");
}

} // namespace nix
