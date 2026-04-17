#pragma once
///@file

#include "nix/store/path.hh"
#include "nix/store/content-address.hh"
#include "nix/store/derivations.hh"

#include "nix/util/json-impls.hh"

#include <nlohmann/json_fwd.hpp>

#include <string>
#include <variant>

namespace nix {

/**
 * Messages for the Derivation Builder Varlink protocol.
 *
 * This protocol is defined in:
 * doc/manual/source/protocols/derivation-builder/derivation-builder.varlink
 */
namespace derivation_builder_varlink {

/**
 * A Varlink protocol request message.
 *
 * Uses the same pattern as DerivationOutput with a Raw variant type.
 */
struct Request
{
    /**
     * Request to add a file to the store with content addressing.
     *
     * The actual file data is sent out-of-band via a file descriptor
     * passed using SCM_RIGHTS.
     */
    struct AddToStore
    {
        std::string name;
        ContentAddressMethod method;

        bool operator==(const AddToStore &) const = default;
    };

    /**
     * Request to add a derivation to the store.
     */
    struct AddDerivation
    {
        Derivation derivation;

        bool operator==(const AddDerivation &) const = default;
    };

    /**
     * Request to register a build output.
     *
     * This signals that a particular output has been completed and
     * associates it with a store path.
     */
    struct SubmitOutput
    {
        std::string name;
        StorePath path;

        bool operator==(const SubmitOutput &) const = default;
    };

    typedef std::variant<AddToStore, AddDerivation, SubmitOutput> Raw;

    Raw raw;

    bool operator==(const Request &) const = default;

    MAKE_WRAPPER_CONSTRUCTOR(Request);

    /**
     * Force choosing a variant
     */
    Request() = delete;
};

/**
 * A Varlink protocol response message.
 */
struct Response
{
    /**
     * Response from AddToStore containing the resulting store path.
     */
    struct AddToStore
    {
        StorePath path;

        bool operator==(const AddToStore &) const = default;
    };

    /**
     * Response from AddDerivation containing the derivation's store path.
     */
    struct AddDerivation
    {
        StorePath path;

        bool operator==(const AddDerivation &) const = default;
    };

    /**
     * Response from SubmitOutput (currently empty).
     */
    struct SubmitOutput
    {
        bool operator==(const SubmitOutput &) const = default;
    };

    typedef std::variant<AddToStore, AddDerivation, SubmitOutput> Raw;

    Raw raw;

    bool operator==(const Response &) const = default;

    MAKE_WRAPPER_CONSTRUCTOR(Response);

    /**
     * Force choosing a variant
     */
    Response() = delete;
};

} // namespace derivation_builder_varlink

/**
 * Process Varlink protocol messages for the derivation builder interface.
 */
void processVarlinkConnection(Store & store, FdSource & from, FdSink & to);

} // namespace nix

JSON_IMPL(nix::derivation_builder_varlink::Request::AddToStore)
JSON_IMPL(nix::derivation_builder_varlink::Request::AddDerivation)
JSON_IMPL(nix::derivation_builder_varlink::Request::SubmitOutput)
JSON_IMPL(nix::derivation_builder_varlink::Response::AddToStore)
JSON_IMPL(nix::derivation_builder_varlink::Response::AddDerivation)
JSON_IMPL(nix::derivation_builder_varlink::Response::SubmitOutput)
JSON_IMPL(nix::derivation_builder_varlink::Request)
JSON_IMPL(nix::derivation_builder_varlink::Response)
