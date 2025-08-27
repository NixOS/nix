#pragma once
///@file

#include "nix/util/types.hh"
#include "nix/store/path.hh"

namespace nix {

class Store;
struct Derivation;
struct DerivationOptions;

/**
 * Derivations claim to "just" specify their environment variables, but
 * actually do a number of different features, such as "structured
 * attrs", "pass as file", and "export references graph", things are
 * more complicated then they appear.
 *
 * The good news is that we can simplify all that to the following view,
 * where environment variables and extra files are specified exactly,
 * with no special cases.
 *
 * Because we have `DesugaredEnv`, `DerivationBuilder` doesn't need to
 * know about any of those above features, and their special case.
 */
struct DesugaredEnv
{
    struct EnvEntry
    {
        /**
         * Whether to prepend the (inside via) path to the sandbox build
         * directory to `value`. This is useful for when the env var
         * should point to a file visible to the builder.
         */
        bool prependBuildDirectory = false;

        /**
         * String value of env var, or contents of the file.
         */
        std::string value;
    };

    /**
     * The final environment variables to set.
     */
    std::map<std::string, EnvEntry, std::less<>> variables;

    /**
     * Extra file to be placed in the build directory.
     *
     * @note `EnvEntry::prependBuildDirectory` can be used to refer to
     * those files without knowing what the build directory is.
     */
    StringMap extraFiles;

    /**
     * A common case is to define an environment variable that points to
     * a file, which contains some contents.
     *
     * In base:
     * ```
     * export VAR=FILE_NAME
     * echo CONTENTS >FILE_NAME
     * ```
     *
     * This function assists in doing both parts, so the file name is
     * kept in sync.
     */
    std::string & atFileEnvPair(std::string_view name, std::string fileName);

    /**
     * Given a (resolved) derivation, its options, and the closure of
     * its inputs (which we can get since the derivation is resolved),
     * desugar the environment to create a `DesguaredEnv`.
     *
     * @todo drvOptions will go away as a separate argument when it is
     * just part of `Derivation`.
     */
    static DesugaredEnv create(
        Store & store, const Derivation & drv, const DerivationOptions & drvOptions, const StorePathSet & inputPaths);
};

} // namespace nix
