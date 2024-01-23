#pragma once
///@file

#include <variant>

#include "types.hh"

namespace nix {

/**
 * A parsed Store URI (URI is a slight misnomer...), parsed but not yet
 * resolved to a specific instance and query parms validated.
 *
 * Supported values are:
 *
 * - ‘local’: The Nix store in /nix/store and database in
 *   /nix/var/nix/db, accessed directly.
 *
 * - ‘daemon’: The Nix store accessed via a Unix domain socket
 *   connection to nix-daemon.
 *
 * - ‘unix://<path>’: The Nix store accessed via a Unix domain socket
 *   connection to nix-daemon, with the socket located at <path>.
 *
 * - ‘auto’ or ‘’: Equivalent to ‘local’ or ‘daemon’ depending on
 *   whether the user has write access to the local Nix
 *   store/database.
 *
 * - ‘file://<path>’: A binary cache stored in <path>.
 *
 * - ‘https://<path>’: A binary cache accessed via HTTP.
 *
 * - ‘s3://<path>’: A writable binary cache stored on Amazon's Simple
 *   Storage Service.
 *
 * - ‘ssh://[user@]<host>’: A remote Nix store accessed by running
 *   ‘nix-store --serve’ via SSH.
 *
 * You can pass parameters to the store type by appending
 * ‘?key=value&key=value&...’ to the URI.
 */
struct StoreURI {
    using Params = std::map<std::string, std::string>;

    /**
     * Special keyword `` or `auto`
     */
    struct Auto {
        inline auto operator <=> (const Auto & rhs) const = default;
    };

    /**
     * Special keyword `local`
     */
    struct Local {
        inline auto operator <=> (const Local & rhs) const = default;
    };

    /**
     * Special keyword `daemon`
     */
    struct Daemon {
        inline auto operator <=> (const Daemon & rhs) const = default;
    };

    /**
     * General case, a regular `scheme://authority` URL.
     */
    struct Generic {
        std::string scheme;
        std::string authority;

        auto operator <=> (const Generic & rhs) const = default;
    };

    typedef std::variant<
        Auto,
        Local,
        Daemon,
        Generic
    > Variant;

    Variant variant;

    Params params;

    auto operator <=> (const StoreURI & rhs) const = default;

    static StoreURI parse(const std::string & uri, const Params & extraParams = Params {});
};

/**
 * Split URI into protocol+hierarchy part and its parameter set.
 */
std::pair<std::string, StoreURI::Params> splitUriAndParams(const std::string & uri);

}
