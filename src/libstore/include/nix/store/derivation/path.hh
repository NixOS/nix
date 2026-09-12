#pragma once
///@file

#include "nix/store/path.hh"

#include <optional>

namespace nix {

/**
 * A \ref StorePath "store path" that is known to be a derivation ---
 * `isDerivation` holds --- because that is checked once, when one is
 * made, rather than wherever one is used.
 *
 * Where a derivation is what the format or the interface means, this
 * says so in the type. It converts to `StorePath` freely, since it is
 * one; going the other way is the checked step.
 */
struct DerivationPath
{
    StorePath path;

private:
    struct Unchecked
    {};

    DerivationPath(StorePath path, Unchecked) noexcept
        : path{std::move(path)}
    {
    }

public:

    /**
     * @throws FormatError if `path` is not a derivation.
     */
    static DerivationPath require(StorePath path)
    {
        path.requireDerivation();
        return DerivationPath{std::move(path), Unchecked{}};
    }

    /**
     * `std::nullopt` rather than an exception, for a lookup: a path
     * that is not a derivation simply cannot be a key.
     */
    static std::optional<DerivationPath> tryFrom(StorePath path) noexcept
    {
        if (!path.isDerivation())
            return std::nullopt;
        return DerivationPath{std::move(path), Unchecked{}};
    }

    operator const StorePath &() const noexcept
    {
        return path;
    }

    const StorePath * operator->() const noexcept
    {
        return &path;
    }

    bool operator==(const DerivationPath & other) const noexcept = default;
    auto operator<=>(const DerivationPath & other) const noexcept = default;
};

} // namespace nix
