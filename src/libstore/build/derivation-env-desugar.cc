#include "nix/store/build/derivation-env-desugar.hh"
#include "nix/store/store-api.hh"
#include "nix/store/derivations.hh"
#include "nix/store/derivation/elaborate.hh"

namespace nix {

std::string & DesugaredEnv::atFileEnvPair(std::string_view name, std::string fileName)
{
    auto & ret = extraFiles[fileName];
    variables.insert_or_assign(
        std::string{name},
        EnvEntry{
            .prependBuildDirectory = true,
            .value = std::move(fileName),
        });
    return ret;
}

DesugaredEnv DesugaredEnv::create(Store & store, const BasicDerivation & drv, const StorePathSet & inputPaths)
{
    DesugaredEnv res;

    if (drv.structuredAttrs) {
        auto json = drv.structuredAttrs->prepareStructuredAttrs(store, drv, inputPaths, drv.outputNames());
        res.atFileEnvPair("NIX_ATTRS_SH_FILE", ".attrs.sh") = StructuredAttrs::writeShell(json);
        res.atFileEnvPair("NIX_ATTRS_JSON_FILE", ".attrs.json") = static_cast<nlohmann::json>(std::move(json)).dump();
    } else {
        /* In non-structured mode, set all bindings either directory in the
           environment or via a file, as specified by
           `passAsFile`. */
        for (auto & [envName, envVar] : drv.env) {
            if (!envVar.passAsFile) {
                res.variables.insert_or_assign(
                    envName,
                    EnvEntry{
                        .value = envVar.value,
                    });
            } else {
                res.atFileEnvPair(
                    envName + "Path",
                    ".attr-" + hashString(HashAlgorithm::SHA256, envName).to_string(HashFormat::Nix32, false)) =
                    envVar.value;
            }
        }

        /* Handle exportReferencesGraph(), if set. */
        for (auto & [fileName, storePaths] : drv.options.exportReferencesGraph) {
            /* Write closure info to <fileName>. */
            res.extraFiles.insert_or_assign(
                fileName, store.makeValidityRegistration(store.exportReferences(storePaths, inputPaths), false, false));
        }
    }

    return res;
}

} // namespace nix
