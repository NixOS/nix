#pragma once
/**
 * @file
 *
 * Proof of concept for a logical path holder that can represent a Windows
 * root. Not wired into anything; see `doc/uri-logical-paths.md`.
 *
 * `CanonPath` holds one flat `std::string` and has no notion of a root
 * name, so every path it holds is anchored to a single virtual `/`. That
 * is deliberate -- `canon-path.cc` canonicalises through
 * `canonPathInner<UnixPathTrait>`, whose `rootNameLen` returns 0 -- but it
 * means a drive-relative or UNC path cannot be held without first being
 * flattened into something else.
 *
 * `LogicalPath` separates the two things `CanonPath` conflates: what the
 * path is anchored to, and the components below it. The external form is
 * a `file` URI per RFC 8089, which is the only standard that describes
 * how a Windows root maps onto a hierarchical name.
 */

#include <compare>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "nix/util/error.hh"

namespace nix {

MakeError(BadLogicalPath, Error);

/**
 * A path anchored to an explicit root, with its components held
 * separately rather than joined.
 *
 * Invariants, all established by the constructors and preserved by every
 * mutator:
 *
 * - No component is empty, `.`, or `..`.
 * - No component contains a NUL byte or a `/`.
 * - Components are WTF-8, so a name that is not well-formed UTF-16
 *   survives (see `wtf8.hh`).
 * - A `Drive` letter is upper case, and a `Unc` host is lower case, so
 *   that `operator==` implements the case-insensitivity those two
 *   comparisons actually have.
 */
class LogicalPath
{
public:

    /**
     * The POSIX root, `/`. Also the root of Nix's own virtual file
     * systems, which is what `CanonPath` can already represent.
     */
    struct Posix
    {
        std::strong_ordering operator<=>(const Posix &) const = default;
        bool operator==(const Posix &) const = default;
    };

    /** A traditional DOS drive, `C:`. Held upper case. */
    struct Drive
    {
        char letter;

        std::strong_ordering operator<=>(const Drive &) const = default;
        bool operator==(const Drive &) const = default;
    };

    /**
     * A UNC share, `\\host\share`.
     *
     * `host` is held lower case because RFC 3986 section 3.2.2 makes the
     * authority case-insensitive. `share` is not folded, because it is a
     * path segment and RFC 3986 leaves the path case-sensitive -- even
     * though Windows itself compares it case-insensitively.
     */
    struct Unc
    {
        std::string host;
        std::string share;

        std::strong_ordering operator<=>(const Unc &) const = default;
        bool operator==(const Unc &) const = default;
    };

    /**
     * A Win32 device name, `\\.\PhysicalDrive0` or
     * `\\?\Volume{...}`.
     *
     * RFC 8089 states it "does not define a mechanism" for these, so this
     * root has no conformant `file` URI and renders under a Nix-specific
     * scheme instead. Note that the *extended-length* prefixes `\\?\C:`
     * and `\\?\UNC\host\share` are not this -- they name the same volumes
     * as `C:` and `\\host\share` and are normalised to `Drive` and `Unc`.
     */
    struct Device
    {
        std::string name;

        std::strong_ordering operator<=>(const Device &) const = default;
        bool operator==(const Device &) const = default;
    };

    using Root = std::variant<Posix, Drive, Unc, Device>;

    /** The scheme used for `Device`, which RFC 8089 cannot express. */
    static constexpr std::string_view deviceScheme = "nix-win32-device";

    LogicalPath() = default;

    /**
     * @throws BadLogicalPath if any component violates an invariant.
     */
    LogicalPath(Root root, std::vector<std::string> components);

    /**
     * Parse a native path, resolving `.` and `..` and recognising every
     * root form `WindowsPathTrait::rootNameLen` does.
     *
     * A path with no root name is taken as `Posix`, which is what a
     * relative path and a POSIX absolute path both become. This is a
     * proof of concept, so it does not distinguish drive-relative
     * (`C:foo`, relative to that drive's working directory) from
     * drive-absolute (`C:\foo`); see the design document.
     */
    static LogicalPath parseNative(std::string_view path);

    /** Render the native form, using `\` for the Windows roots. */
    std::string toNative() const;

    /**
     * Render as a URI. `Posix`, `Drive` and `Unc` produce a `file` URI
     * per RFC 8089; `Device` produces `deviceScheme`.
     *
     * Each component is percent-encoded from its WTF-8 bytes, so `%`,
     * `#`, `?`, space and a lone surrogate all survive the round trip.
     */
    std::string toUri() const;

    /**
     * @throws BadLogicalPath on a scheme that is not `file` or
     * `deviceScheme`, or on an authority a `file` URI may not carry.
     */
    static LogicalPath parseUri(std::string_view uri);

    const Root & root() const
    {
        return root_;
    }

    const std::vector<std::string> & components() const
    {
        return components_;
    }

    bool operator==(const LogicalPath & other) const = default;

    /**
     * Orders a directory immediately before its children, matching
     * `CanonPath::operator<=>`. The root sorts before the components so
     * that paths on different volumes never interleave.
     */
    std::strong_ordering operator<=>(const LogicalPath & other) const;

private:

    Root root_;
    std::vector<std::string> components_;
};

} // namespace nix
