#pragma once
///@file

#include <algorithm>
#include <compare>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>

namespace nix {

class FilenameView;

/**
 * A newtype around `std::string` that ensures at construction time that
 * it is a single, portable file name: the name of an entry in a
 * directory, in Nix's model of file system objects (e.g. in a NAR).
 *
 * A valid `Filename`:
 * - Is not empty
 * - Is not `.` or `..`
 * - Contains no `/`
 * - Contains no NUL bytes
 *
 * Unlike `OsFilename`, this says nothing about whether the name is valid
 * on the host OS. For example, `\` is allowed here, but is a separator
 * on Windows.
 *
 * See also `FilenameView`, which is to this what `std::string_view` is
 * to `std::string`.
 */
class Filename
{
    std::string name;

    /**
     * Does not validate; only for callers that already know the
     * invariants hold.
     */
    explicit Filename(std::string s)
        : name(std::move(s))
    {
    }

public:
    /**
     * Why @p s is not a valid `Filename`, or `std::nullopt` if it is.
     *
     * In the header and `constexpr` so that `FilenameLiteral` can check
     * literals at compile time.
     */
    static constexpr std::optional<std::string_view> whyInvalid(std::string_view s) noexcept
    {
        if (s.empty())
            return "cannot be empty";
        if (s == ".")
            return "cannot be '.'";
        if (s == "..")
            return "cannot be '..'";
        if (s.find('/') != s.npos)
            return "cannot contain '/'";
        if (s.find('\0') != s.npos)
            return "cannot contain a NUL byte";
        return std::nullopt;
    }

    /**
     * Construct a `Filename`, asserting that it is valid.
     *
     * Use this when we are witnessing an internal invariant / we as the
     * programmer know the validation should pass. Fails with an
     * assertion otherwise.
     */
    static Filename fromStringAsserting(std::string s);

    /**
     * Construct a `Filename`, throwing an `Error` if it is not valid.
     *
     * Use this when the input comes from users / the external world, and we are
     * not sure whether the invariant will pass.
     */
    static Filename fromStringThrowing(std::string s);

    /**
     * Copy a `FilenameView`, which needs no checks.
     */
    Filename(FilenameView name);

    const std::string & str() const noexcept
    {
        return name;
    }

    auto operator<=>(const Filename &) const = default;

    /* Needed explicitly, since declaring the `FilenameView` overload
       below stops this from being implicitly declared. */
    bool operator==(const Filename &) const = default;

    /**
     * Comparison with `FilenameView`, so that maps keyed by `Filename`
     * can be looked up by one (e.g. a `CanonPath` component), with
     * `std::less<>`, without copying it.
     */
    bool operator==(FilenameView other) const noexcept;
    std::strong_ordering operator<=>(FilenameView other) const noexcept;
};

/**
 * Like `Filename`, but not owning the name it refers to, the way
 * `std::string_view` is to `std::string`.
 *
 * This is what to take for a name that is only borrowed, e.g. by
 * `FileSystemObjectSink::OnDirectory::createChild`, and what a
 * `CanonPath` yields for its components.
 */
class FilenameView
{
    std::string_view name;

    /**
     * Does not validate; only for callers that already know the
     * invariants hold.
     */
    explicit constexpr FilenameView(std::string_view s) noexcept
        : name(s)
    {
    }

    [[noreturn]] static void invalidAsserting(std::string_view s, std::string_view why);

    /* `CanonPath` components are always valid file names. */
    friend class CanonPath;

public:
    FilenameView(const Filename & name) noexcept
        : name(name.str())
    {
    }

    /**
     * Like `Filename::fromStringAsserting`.
     *
     * `constexpr` so that `operator""_filename` can use it.
     */
    static constexpr FilenameView fromStringAsserting(std::string_view s)
    {
        if (auto why = Filename::whyInvalid(s))
            invalidAsserting(s, *why);
        return FilenameView{s};
    }

    /**
     * Like `Filename::fromStringThrowing`.
     */
    static FilenameView fromStringThrowing(std::string_view s);

    constexpr std::string_view str() const noexcept
    {
        return name;
    }

    constexpr operator std::string_view() const noexcept
    {
        return name;
    }

    constexpr bool operator==(FilenameView other) const noexcept
    {
        return name == other.name;
    }

    constexpr std::strong_ordering operator<=>(FilenameView other) const noexcept
    {
        return name <=> other.name;
    }

    /**
     * Comparison with plain strings, e.g. literals.
     */
    constexpr bool operator==(std::string_view other) const noexcept
    {
        return name == other;
    }

    constexpr std::strong_ordering operator<=>(std::string_view other) const noexcept
    {
        return name <=> other;
    }
};

inline Filename::Filename(FilenameView name)
    : name(name.str())
{
}

inline bool Filename::operator==(FilenameView other) const noexcept
{
    return std::string_view{name} == other.str();
}

inline std::strong_ordering Filename::operator<=>(FilenameView other) const noexcept
{
    return std::string_view{name} <=> other.str();
}

/**
 * Print the name as is, e.g. for formatting it in messages.
 */
std::ostream & operator<<(std::ostream & out, const Filename & name);

/**
 * Print the name as is, e.g. for formatting it in messages.
 */
std::ostream & operator<<(std::ostream & out, FilenameView name);

/**
 * A string literal for `operator""_filename`.
 */
template<size_t N>
struct FilenameLiteral
{
    char data[N];

    consteval FilenameLiteral(const char (&s)[N])
    {
        static_assert(N > 0);
        std::copy_n(s, N, data);
    }

    constexpr std::string_view view() const
    {
        return {data, N - 1};
    }
};

/**
 * A `FilenameView` of a string literal, checked at compile time. For
 * example, `"foo"_filename`. It refers to the literal itself, so needs
 * no copying or allocation; it converts to a `Filename` where one is
 * needed.
 */
template<FilenameLiteral L>
consteval FilenameView operator""_filename()
{
    /* Checked here rather than in `FilenameLiteral`'s constructor, since
       a failure there would just make this operator not match, without
       saying why. */
#if __cpp_static_assert >= 202306L
/* Clang offers user-generated messages before C++26, but warns. */
#  ifdef __clang__
#    pragma clang diagnostic push
#    pragma clang diagnostic ignored "-Wc++26-extensions"
#  endif
    static_assert(!Filename::whyInvalid(L.view()), Filename::whyInvalid(L.view()).value_or(""));
#  ifdef __clang__
#    pragma clang diagnostic pop
#  endif
#else
    static_assert(
        !Filename::whyInvalid(L.view()),
        "invalid file name literal: must not be empty, `.` or `..`, nor contain `/` or NUL bytes");
#endif
    return FilenameView::fromStringAsserting(L.view());
}

} // namespace nix
