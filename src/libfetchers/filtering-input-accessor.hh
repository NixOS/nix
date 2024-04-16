#pragma once

#include "input-accessor.hh"
#include "source-path.hh"

namespace nix {

/**
 * A function that returns an exception of type
 * `RestrictedPathError` explaining that access to `path` is
 * forbidden.
 */
typedef std::function<RestrictedPathError(const CanonPath & path)> MakeNotAllowedError;

/**
 * An abstract wrapping `InputAccessor` that performs access
 * control. Subclasses should override `isAllowed()` to implement an
 * access control policy. The error message is customized at construction.
 */
struct FilteringInputAccessor : InputAccessor
{
    ref<InputAccessor> next;
    CanonPath prefix;
    MakeNotAllowedError makeNotAllowedError;

    FilteringInputAccessor(const SourcePath & src, MakeNotAllowedError && makeNotAllowedError)
        : next(src.accessor)
        , prefix(src.path)
        , makeNotAllowedError(std::move(makeNotAllowedError))
    {
        displayPrefix.clear();
    }

    std::string readFile(const CanonPath & path) override;

    bool pathExists(const CanonPath & path) override;

    std::optional<Stat> maybeLstat(const CanonPath & path) override;

    DirEntries readDirectory(const CanonPath & path) override;

    std::string readLink(const CanonPath & path) override;

    std::string showPath(const CanonPath & path) override;

    /**
     * Call `makeNotAllowedError` to throw a `RestrictedPathError`
     * exception if `isAllowed()` returns `false` for `path`.
     */
    void checkAccess(const CanonPath & path);

    /**
     * Return `true` iff access to path is allowed.
     */
    virtual bool isAllowed(const CanonPath & path) = 0;
};

/**
 * A wrapping `InputAccessor` that checks paths against a set of
 * allowed prefixes.
 */
struct AllowListInputAccessor : public FilteringInputAccessor
{
    /**
     * Grant access to the specified prefix.
     */
    virtual void allowPrefix(CanonPath prefix) = 0;

    static ref<AllowListInputAccessor> create(
        ref<InputAccessor> next,
        std::set<CanonPath> && allowedPrefixes,
        MakeNotAllowedError && makeNotAllowedError);

    using FilteringInputAccessor::FilteringInputAccessor;
};

/**
 * A wrapping `InputAccessor` mix-in where `isAllowed()` caches the result of virtual `isAllowedUncached()`.
 */
struct CachingFilteringInputAccessor : FilteringInputAccessor
{
    std::map<CanonPath, bool> cache;

    using FilteringInputAccessor::FilteringInputAccessor;

    bool isAllowed(const CanonPath & path) override;

    virtual bool isAllowedUncached(const CanonPath & path) = 0;
};

}
