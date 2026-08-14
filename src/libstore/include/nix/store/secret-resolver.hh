#pragma once
///@file

#include "nix/util/types.hh"
#include "nix/util/ref.hh"

#include <chrono>
#include <compare>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <variant>

namespace nix {

enum class SecretRepresentation {
    Inline,
    MaterialisedFile,
};

struct SecretPurpose
{
    std::string consumer;
    std::string operation;
    std::optional<std::string> host;
    std::optional<std::string> path;

    auto operator<=>(const SecretPurpose &) const = default;
};

struct SecretRequest
{
    std::string name;
    SecretRepresentation representation;
    SecretPurpose purpose;

    auto operator<=>(const SecretRequest &) const = default;
};

struct InlineSecret
{
    std::string value;
};

/**
 * A materialised secret file and the lease that keeps it alive.
 *
 * Implementations release the broker-side lease and remove any associated
 * materialisation from their destructor. Consumers must retain this object for
 * as long as they use `path()`; a bare path must never outlive the object.
 */
class SecretFile
{
private:
    /* VTable anchor to avoid weak linkage of the vtable - it breaks
       dynamic_cast across shared libraries on Darwin. */
    virtual void anchor();

public:
    virtual ~SecretFile() = default;

    virtual const std::filesystem::path & path() const noexcept = 0;
};

struct ResolvedSecret
{
    std::variant<InlineSecret, ref<SecretFile>> value;
    std::optional<std::chrono::system_clock::time_point> expiresAt;
};

/**
 * Resolve named secrets for one explicitly owned operation context.
 *
 * Implementations may keep instance-local transport or provider state, but
 * callers must not rely on a process-global resolver or cache.
 *
 * One resolver serves a whole operation, and an operation is not one thread:
 * a single store copy has as many transfers in flight as it has connections,
 * each resolving on its own thread. Implementations must therefore make
 * `resolve` safe to call concurrently on the same instance.
 */
class SecretResolver
{
private:
    /* VTable anchor to avoid weak linkage of the vtable - it breaks
       dynamic_cast across shared libraries on Darwin. */
    virtual void anchor();

public:
    virtual ~SecretResolver() = default;

    /**
     * Resolve `request`, or return `std::nullopt` when this resolver knows
     * of no secret under that name.
     *
     * Absence is an ordinary answer rather than a failure. Some consumers
     * (netrc lookups, say) are expected to carry on without the secret, so
     * they must be able to tell "nothing is provisioned" apart from "the
     * broker could not be reached". Implementations throw only for the latter.
     */
    virtual std::optional<ResolvedSecret> resolve(const SecretRequest & request) = 0;
};

/**
 * The secret-resolving authority handed to one operation, or none.
 *
 * File transfers, builds, and store opens differ in how they use this
 * authority, not in the authority itself. A single context avoids repacking
 * structurally identical resolver holders at every layer boundary.
 */
struct SecretContext
{
    std::shared_ptr<SecretResolver> secretResolver;
};

} // namespace nix
