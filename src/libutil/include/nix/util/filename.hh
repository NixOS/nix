#pragma once
///@file

#include "nix/util/error.hh"
#include "nix/util/fmt.hh"

#include <algorithm>
#include <compare>
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>

namespace nix {

/**
 * An invalid `Filename` or `OsFilename`, from outside.
 */
class BadFilename final : public CloneableError<BadFilename, Error>
{
    void anchor() override;

public:
    /**
     * Why a name is not a valid `Filename` or `OsFilename`.
     */
    enum class Problem {
        Empty,
        Dot,
        DotDot,
        /**
         * Contains `/`. Only for `Filename`.
         */
        Slash,
        /**
         * Has a root or parent path. Only for `OsFilename`, where this
         * also covers e.g. `\` or a drive on Windows.
         */
        ParentPath,
        Nul,
    };

private:
    /* They check names with `whyInvalid`. */
    friend class Filename;
    friend class FilenameView;
    template<size_t N>
    friend struct FilenameLiteral;

    /**
     * Why @p s is not a valid `Filename`, or `std::nullopt` if it is.
     *
     * In the header and `constexpr` so that `FilenameLiteral` can check
     * literals at compile time.
     */
    static constexpr std::optional<Problem> whyInvalid(std::string_view s) noexcept
    {
        if (s.empty())
            return Problem::Empty;
        if (s == ".")
            return Problem::Dot;
        if (s == "..")
            return Problem::DotDot;
        if (s.contains('/'))
            return Problem::Slash;
        if (s.contains('\0'))
            return Problem::Nul;
        return std::nullopt;
    }

public:

    /**
     * The problem as an explanation, e.g. "cannot be empty".
     */
    static const char * describe(Problem problem);

    /**
     * Why the name is not valid, as `whyInvalid` says.
     */
    Problem problem;

    /**
     * For a `Filename`.
     */
    BadFilename(std::string_view name, Problem problem)
        : CloneableError("invalid file name '%s': %s", name, describe(problem))
        , problem(problem)
    {
    }

    /**
     * For an `OsFilename`. The reason comes first, here and in the
     * message, since a NUL in @p name would cut the message short.
     */
    BadFilename(Problem problem, const std::filesystem::path & name)
        : CloneableError("invalid filename: %1%: %2%", describe(problem), PathFmt(name))
        , problem(problem)
    {
    }
};

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
 * Nor is it necessarily Unicode: it is arbitrary bytes, subject only to
 * the rules above. Converting to and from `OsFilename` on Windows treats
 * it as UTF-8, so there a name that is not valid UTF-8 (or, someday,
 * WTF-8) fails to convert. That does not restrict `Filename` to UTF-8,
 * however. Even on Windows, such a name is still considered valid in
 * general, and will work wherever the OS is not involved, e.g. in a
 * `MemorySourceAccessor`.
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
     * Construct a `Filename`, asserting that it is valid.
     *
     * Use this when we are witnessing an internal invariant / we as the
     * programmer know the validation should pass. Fails with an
     * assertion otherwise.
     */
    static Filename fromStringAsserting(std::string s);

    /**
     * Construct a `Filename`, throwing a `BadFilename` if it is not
     * valid.
     *
     * Use this when the input comes from users / the external world, and we are
     * not sure whether the invariant will pass.
     */
    static Filename fromStringThrowing(std::string s);

    /**
     * Construct a `Filename` without checking it at all.
     *
     * Use this when it is valid by construction, e.g. it was read from a
     * directory, where checking it would be pointless overhead.
     */
    static Filename fromStringUnchecked(std::string s) noexcept
    {
        return Filename{std::move(s)};
    }

    /**
     * Copy a `FilenameView`, which needs no checks.
     */
    Filename(FilenameView name);

    const std::string & str() const & noexcept
    {
        return name;
    }

    /**
     * Unlike the other `str`, moves the name out, so that nothing can
     * refer into a temporary.
     */
    std::string str() && noexcept
    {
        return std::move(name);
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
public:
    /**
     * The owning counterpart of this view.
     */
    using Owned = Filename;

private:
    std::string_view name;

    /**
     * Does not validate; only for callers that already know the
     * invariants hold.
     */
    explicit constexpr FilenameView(std::string_view s) noexcept
        : name(s)
    {
    }

    /**
     * The branch of `fromStringAsserting` which doesn't need to go in the
     * header.
     */
    [[noreturn]] static void dieInvalid(std::string_view s, BadFilename::Problem why);

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
        if (auto why = BadFilename::whyInvalid(s))
            dieInvalid(s, *why);
        return FilenameView{s};
    }

    /**
     * Like `Filename::fromStringThrowing`.
     */
    static FilenameView fromStringThrowing(std::string_view s);

    /**
     * Like `Filename::fromStringUnchecked`.
     */
    static constexpr FilenameView fromStringUnchecked(std::string_view s) noexcept
    {
        return FilenameView{s};
    }

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

    /**
     * Whether this is a valid `Filename`.
     */
    constexpr bool valid() const
    {
        return !BadFilename::whyInvalid(view());
    }
};

/**
 * A `FilenameView` of a string literal, checked at compile time. For
 * example, `"foo"_filename`. It refers to the literal itself, so needs
 * no copying or allocation; it converts to a `Filename` where one is
 * needed.
 */
template<FilenameLiteral L>
    requires(L.valid())
consteval FilenameView operator""_filename()
{
    /* Checked by a constraint rather than in `FilenameLiteral`'s
       constructor, since a failure there would just make this operator
       not match, without saying why, whereas the compiler reports the
       unsatisfied `L.valid()`. Unlike a `static_assert`, a constraint can
       also be tested. */
    /* TODO: Once we can rely on C++26 user-generated `static_assert`
       messages, give `whyInvalid`'s reason too. */
    return FilenameView::fromStringAsserting(L.view());
}

} // namespace nix
