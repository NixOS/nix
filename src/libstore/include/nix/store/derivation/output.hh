#pragma once
///@file

#include "nix/store/path.hh"
#include "nix/store/content-address.hh"
#include "nix/store/outputs-spec.hh"
#include "nix/util/json-impls.hh"
#include "nix/util/variant-wrapper.hh"

#include <variant>

namespace nix {

struct StoreDirConfig;

namespace derivation {

/**
 * A single output of a `Basic` (and `Full`) derivation.
 */
struct Output
{
    /**
     * The traditional non-fixed-output derivation type.
     */
    struct InputAddressed
    {
        StorePath path;

        bool operator==(const InputAddressed &) const = default;
        auto operator<=>(const InputAddressed &) const = default;
    };

    /**
     * Fixed-output derivations, whose output paths are content
     * addressed according to that fixed output.
     */
    struct CAFixed
    {
        /**
         * Method and hash used for expected hash computation.
         *
         * References are not allowed by fiat.
         */
        ContentAddress ca;

        /**
         * Return the \ref StorePath "store path" corresponding to this output
         *
         * @param drvName The name of the derivation this is an output of, without the `.drv`.
         * @param outputName The name of this output.
         */
        StorePath path(const StoreDirConfig & store, std::string_view drvName, OutputNameView outputName) const;

        bool operator==(const CAFixed &) const = default;
        auto operator<=>(const CAFixed &) const = default;
    };

    /**
     * Floating-output derivations, whose output paths are content
     * addressed, but not fixed, and so are dynamically calculated from
     * whatever the output ends up being.
     * */
    struct CAFloating
    {
        /**
         * How the file system objects will be serialized for hashing
         */
        ContentAddressMethod method;

        /**
         * How the serialization will be hashed
         */
        HashAlgorithm hashAlgo;

        bool operator==(const CAFloating &) const = default;
        auto operator<=>(const CAFloating &) const = default;
    };

    /**
     * Input-addressed output which depends on a (CA) derivation whose hash
     * isn't known yet.
     */
    struct Deferred
    {
        bool operator==(const Deferred &) const = default;
        auto operator<=>(const Deferred &) const = default;
    };

    /**
     * Impure output which is moved to a content-addressed location (like
     * CAFloating) but isn't registered as a realization.
     */
    struct Impure
    {
        /**
         * How the file system objects will be serialized for hashing
         */
        ContentAddressMethod method;

        /**
         * How the serialization will be hashed
         */
        HashAlgorithm hashAlgo;

        bool operator==(const Impure &) const = default;
        auto operator<=>(const Impure &) const = default;
    };

    typedef std::variant<InputAddressed, CAFixed, CAFloating, Deferred, Impure> Raw;

    Raw raw;

    bool operator==(const Output &) const = default;
    auto operator<=>(const Output &) const = default;

    MAKE_WRAPPER_CONSTRUCTOR(Output);

    /**
     * Force choosing a variant
     */
    Output() = delete;

    /**
     * \note when you use this function you should make sure that you're
     * passing the right derivation name. When in doubt, you should use
     * the safer interface provided by
     * BasicDerivation::outputsAndOptPaths
     */
    std::optional<StorePath>
    path(const StoreDirConfig & store, std::string_view drvName, OutputNameView outputName) const;
};

template<typename Out = Output>
using Outputs = std::map<std::string, Out>;

/**
 * These are analogues to the previous `Outputs` data type, but they
 * also contains, for each output, the (optional) store path in which
 * it would be written. To calculate values of these types, see the
 * corresponding functions in `Basic`.
 */
typedef std::map<std::string, std::pair<Output, std::optional<StorePath>>> OutputsAndOptPaths;

} // namespace derivation

using DerivationOutput = derivation::Output;

} // namespace nix

JSON_IMPL_WITH_XP_FEATURES(nix::DerivationOutput)
