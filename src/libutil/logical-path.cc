#include "nix/util/logical-path.hh"

#include <algorithm>
#include <cstring>

#include "nix/util/file-path-impl.hh"
#include "nix/util/strings-inline.hh"
#include "nix/util/url.hh"
#include "nix/util/util.hh"

namespace nix {

void BadLogicalPath::anchor() {}

namespace {

using Trait = WindowsPathTrait<char>;

char toUpperAscii(char c)
{
    return (c >= 'a' && c <= 'z') ? char(c - 'a' + 'A') : c;
}

std::string toLowerAscii(std::string_view s)
{
    std::string out{s};
    for (auto & c : out)
        if (c >= 'A' && c <= 'Z')
            c = char(c - 'A' + 'a');
    return out;
}

bool isDriveSpec(std::string_view s)
{
    return s.size() == 2 && s[1] == ':' && Trait::isDriveLetter(s[0]);
}

/** Split on either separator, dropping empties. */
std::vector<std::string> splitOnSeps(std::string_view s)
{
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && Trait::isPathSep(s[i]))
            ++i;
        if (i >= s.size())
            break;
        size_t end = Trait::findPathSep(s, i);
        if (end == std::string_view::npos)
            end = s.size();
        out.emplace_back(s.substr(i, end - i));
        i = end;
    }
    return out;
}

/** Resolve `.` and `..` over already-split components. */
std::vector<std::string> resolveDotDot(const std::vector<std::string> & in)
{
    std::vector<std::string> out;
    for (auto & c : in) {
        if (c == ".")
            continue;
        if (c == "..") {
            /* At the root, `..` is the root -- the same choice
               `canonPathInner` makes, and the same one POSIX makes for
               `/..`. */
            if (!out.empty())
                out.pop_back();
            continue;
        }
        out.push_back(c);
    }
    return out;
}

/**
 * Interpret the leading `rootNameLen` bytes of a native path.
 *
 * The extended-length prefixes are normalised away: `\\?\C:` names the
 * same volume as `C:`, and `\\?\UNC\h\s` the same share as `\\h\s`. What
 * they additionally mean -- that Win32 must not normalise the path -- is
 * not representable here and is discussed in the design document.
 */
LogicalPath::Root parseNativeRoot(std::string_view root)
{
    /* `X:` */
    if (isDriveSpec(root))
        return LogicalPath::Drive{toUpperAscii(root[0])};

    /* Everything else starts with two separators. */
    if (!(root.size() >= 3 && Trait::isPathSep(root[0]) && Trait::isPathSep(root[1])))
        throw BadLogicalPath("unrecognised path root '%s'", root);

    bool extended = (root[2] == '?' || root[2] == '.') && root.size() >= 4 && Trait::isPathSep(root[3]);

    if (extended) {
        auto after = root.substr(4);

        /* `\\?\X:` */
        if (isDriveSpec(after))
            return LogicalPath::Drive{toUpperAscii(after[0])};

        /* `\\?\UNC\host\share`, the literal being case-insensitive. */
        if (after.size() >= 4 && toUpperAscii(after[0]) == 'U' && toUpperAscii(after[1]) == 'N'
            && toUpperAscii(after[2]) == 'C' && Trait::isPathSep(after[3])) {
            auto parts = splitOnSeps(after.substr(4));
            return LogicalPath::Unc{
                .host = parts.empty() ? "" : toLowerAscii(parts[0]),
                .share = parts.size() > 1 ? parts[1] : "",
            };
        }

        /* A device name, which `rootNameLen` ran to the next separator. */
        return LogicalPath::Device{std::string{after}};
    }

    /* `\\host\share` */
    auto parts = splitOnSeps(root.substr(2));
    return LogicalPath::Unc{
        .host = parts.empty() ? "" : toLowerAscii(parts[0]),
        .share = parts.size() > 1 ? parts[1] : "",
    };
}

/** Rank the root alternatives so ordering is stable across volumes. */
int rootRank(const LogicalPath::Root & r)
{
    return int(r.index());
}

} // namespace

LogicalPath::LogicalPath(Root root, std::vector<std::string> components)
    : root_(std::move(root))
    , components_(std::move(components))
{
    for (auto & c : components_) {
        if (c.empty())
            throw BadLogicalPath("path component must not be empty");
        if (c == "." || c == "..")
            throw BadLogicalPath("path component '%s' is not allowed", c);
        if (c.find('/') != std::string::npos)
            throw BadLogicalPath("path component '%s' must not contain '/'", c);
        if (std::memchr(c.data(), '\0', c.size()))
            throw BadLogicalPath("path component must not contain a NUL byte");
    }
    /* Also fold here, not only in `parseNativeRoot`: this constructor is
       public, so a caller can hand us `Drive{'c'}` or a mixed-case host
       directly and must get the same canonical form. */
    /* Also fold here, not only in `parseNativeRoot`: this constructor is
       public, so a caller can hand us `Drive{'c'}` or a mixed-case host
       directly and must get the same canonical form. */
    if (auto * d = std::get_if<Drive>(&root_))
        d->letter = toUpperAscii(d->letter);
    if (auto * u = std::get_if<Unc>(&root_))
        u->host = toLowerAscii(u->host);
}

LogicalPath LogicalPath::parseNative(std::string_view path)
{
    Root root = Posix{};
    std::string_view rest = path;

    if (auto rootLen = Trait::rootNameLen(path)) {
        root = parseNativeRoot(path.substr(0, rootLen));
        rest = path.substr(rootLen);
    }

    return LogicalPath{std::move(root), resolveDotDot(splitOnSeps(rest))};
}

std::string LogicalPath::toNative() const
{
    std::string res = std::visit(
        overloaded{
            [](const Posix &) -> std::string { return "/"; },
            [](const Drive & d) -> std::string { return std::string{d.letter} + ":\\"; },
            [](const Unc & u) -> std::string { return "\\\\" + u.host + "\\" + u.share + "\\"; },
            [](const Device & d) -> std::string { return "\\\\?\\" + d.name + "\\"; },
        },
        root_);

    for (size_t i = 0; i < components_.size(); ++i) {
        if (i)
            res += std::holds_alternative<Posix>(root_) ? '/' : '\\';
        res += components_[i];
    }

    /* A root with no components keeps its trailing separator; one with
       components must not have gained a duplicate. */
    if (!components_.empty() && res.size() > 1) {
        /* nothing to trim: the loop only inserts between components */
    }
    return res;
}

std::string LogicalPath::toUri() const
{
    /* `:` is legal unencoded in a path segment (RFC 3986 section 3.3),
       which is what lets the drive render as `C:` rather than `C%3A`.
       Everything else outside the unreserved set is encoded, including
       `/`, `%`, `#`, `?` and space. */
    static const std::string keepInSegment = ":@";

    std::string res;
    std::vector<std::string> segments;

    std::visit(
        overloaded{
            [&](const Posix &) { res = "file://"; },
            [&](const Drive & d) {
                res = "file://";
                segments.push_back(std::string{d.letter} + ":");
            },
            [&](const Unc & u) {
                res = "file://" + percentEncode(u.host);
                segments.push_back(u.share);
            },
            [&](const Device & d) {
                res = std::string{deviceScheme} + "://";
                segments.push_back(d.name);
            },
        },
        root_);

    for (auto & c : components_)
        segments.push_back(c);

    for (auto & s : segments) {
        res += '/';
        res += percentEncode(s, keepInSegment);
    }

    /* A rootless-looking `file://` with no segments is `file:///`. */
    if (segments.empty())
        res += '/';

    return res;
}

LogicalPath LogicalPath::parseUri(std::string_view uri)
{
    /* This deliberately does not call `parseURL`.
       `url.cc:184` rejects any `file` URL carrying a non-empty host, so
       the RFC 8089 appendix E.3.1 form `file://host/share/...` -- the
       only standard mapping for a UNC path -- cannot be parsed by it.
       Two tests assert that rejection (`libutil-tests/url.cc:390` and
       `:676`), so relaxing it is a decision for the Nix maintainers
       rather than something this proof of concept should assume. See the
       design document.

       The percent-encoding primitives from `url.hh` are still used; only
       the scheme/authority split is done here. */
    auto sep = uri.find("://");
    if (sep == std::string_view::npos)
        throw BadLogicalPath("URI '%s' has no '://'", uri);

    auto scheme = uri.substr(0, sep);
    auto rest = uri.substr(sep + 3);

    bool isFile = scheme == "file";
    bool isDevice = scheme == deviceScheme;
    if (!isFile && !isDevice)
        throw BadLogicalPath("URI '%s' has scheme '%s', expected 'file' or '%s'", uri, scheme, deviceScheme);

    auto slash = rest.find('/');
    auto encodedAuthority = rest.substr(0, slash);
    auto encodedPath = slash == std::string_view::npos ? std::string_view{} : rest.substr(slash);

    std::vector<std::string> segments;
    for (auto & s : splitString<std::vector<std::string>>(std::string{encodedPath}, "/"))
        if (!s.empty())
            segments.push_back(percentDecode(s));

    auto shift = [&]() -> std::string {
        if (segments.empty())
            throw BadLogicalPath("URI '%s' is missing a path segment its root requires", uri);
        auto s = std::move(segments.front());
        segments.erase(segments.begin());
        return s;
    };

    Root root = Posix{};

    if (isDevice) {
        root = Device{shift()};
    } else if (!encodedAuthority.empty()) {
        root = Unc{
            .host = toLowerAscii(percentDecode(encodedAuthority)),
            .share = shift(),
        };
    } else if (!segments.empty() && isDriveSpec(segments.front())) {
        root = Drive{toUpperAscii(shift()[0])};
    }

    return LogicalPath{std::move(root), resolveDotDot(segments)};
}

std::strong_ordering LogicalPath::operator<=>(const LogicalPath & other) const
{
    if (auto cmp = rootRank(root_) <=> rootRank(other.root_); cmp != 0)
        return cmp;
    if (auto cmp = root_ <=> other.root_; cmp != 0)
        return cmp;

    /* Component-wise, so that a directory precedes its children and two
       volumes never interleave. Comparing the joined strings would not
       give this: `foo` < `foo!` < `foo/bar` byte-wise, but `foo/bar` must
       sort directly after `foo`. */
    auto n = std::min(components_.size(), other.components_.size());
    for (size_t i = 0; i < n; ++i)
        if (auto cmp = components_[i] <=> other.components_[i]; cmp != 0)
            return cmp;
    return components_.size() <=> other.components_.size();
}

} // namespace nix
