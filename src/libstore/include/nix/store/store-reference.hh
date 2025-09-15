#pragma once
///@file

#include <variant>

#include "nix/util/types.hh"

namespace nix {

/**
 * A parsed Store URI (URI is a slight misnomer...), parsed but not yet
 * resolved to a specific instance and query params validated.
 *
 * Supported values are:
 *
 * - `local`: The Nix store in /nix/store and database in
 *   /nix/var/nix/db, accessed directly.
 *
 * - `daemon`: The Nix store accessed via a Unix domain socket
 *   connection to nix-daemon.
 *
 * - `unix://<path>`: The Nix store accessed via a Unix domain socket
 *   connection to nix-daemon, with the socket located at `<path>`.
 *
 * - `auto` or ``: Equivalent to `local` or `daemon` depending on
 *   whether the user has write access to the local Nix
 *   store/database.
 *
 * - `file://<path>`: A binary cache stored in `<path>`.
 *
 * - `https://<path>`: A binary cache accessed via HTTP.
 *
 * - `s3://<path>`: A writable binary cache stored on Amazon's Simple
 *   Storage Service.
 *
 * - `ssh://[user@]<host>`: A remote Nix store accessed by running
 *   `nix-store --serve` via SSH.
 *
 * You can pass parameters to the store type by appending
 * `?key=value&key=value&...` to the URI.
 */
struct StoreReference
{
    using Params = StringMap;

    /**
     * Special store reference `""` or `"auto"`
     */
    struct Auto
    {
        inline bool operator==(const Auto & rhs) const = default;
        inline auto operator<=>(const Auto & rhs) const = default;
    };

    /**
     * General case, a regular `scheme://authority` URL.
     */
    struct Specified
    {
        std::string scheme;
        std::string authority = "";

        bool operator==(const Specified & rhs) const = default;
        auto operator<=>(const Specified & rhs) const = default;
    };

    /**
     * Special case for `daemon` to avoid normalization.
     */
    struct Daemon : Specified
    {
        Daemon()
            : Specified({.scheme = "unix"})
        {
        }
    };

    /**
     * Special case for `local` to avoid normalization.
     */
    struct Local : Specified
    {
        Local()
            : Specified({.scheme = "local"})
        {
        }
    };

    typedef std::variant<Auto, Specified, Daemon, Local> Variant;

    Variant variant;

    Params params;

    bool operator==(const StoreReference & rhs) const = default;

    /**
     * Render the whole store reference as a URI, optionally including parameters.
     */
    std::string render(bool withParams = true) const;

    std::string to_string() const
    {
        return render();
    }

    /**
     * Parse a URI into a store reference.
     */
    static StoreReference parse(const std::string & uri, const Params & extraParams = Params{});
};

static inline std::ostream & operator<<(std::ostream & os, const StoreReference & ref)
{
    return os << ref.render();
}

/**
 * Split URI into protocol+hierarchy part and its parameter set.
 */
std::pair<std::string, StoreReference::Params> splitUriAndParams(const std::string & uri);

} // namespace nix
