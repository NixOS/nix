#pragma once
///@file

#include "nix/store/path.hh"
#include "nix/store/outputs-spec.hh"
#include "nix/util/configuration.hh"
#include "nix/util/ref.hh"
#include "nix/util/json-impls.hh"

#include <variant>

#include <nlohmann/json_fwd.hpp>

namespace nix {

struct StoreDirConfig;

/**
 * An opaque derived path.
 *
 * Opaque derived paths are just store paths, and fully evaluated. They
 * cannot be simplified further. Since they are opaque, they cannot be
 * built, but they can fetched.
 */
struct DerivedPathOpaque
{
    StorePath path;

    std::string to_string(const StoreDirConfig & store) const;
    static DerivedPathOpaque parse(const StoreDirConfig & store, std::string_view);

    bool operator==(const DerivedPathOpaque &) const = default;
    auto operator<=>(const DerivedPathOpaque &) const = default;
};

struct SingleDerivedPath;

/**
 * A single derived path that is built from a derivation
 *
 * Built derived paths are pair of a derivation and an output name. They are
 * evaluated by building the derivation, and then taking the resulting output
 * path of the given output name.
 */
struct SingleDerivedPathBuilt
{
    ref<const SingleDerivedPath> drvPath;
    OutputName output;

    /**
     * Get the store path this is ultimately derived from (by realising
     * and projecting outputs).
     *
     * Note that this is *not* a property of the store object being
     * referred to, but just of this path --- how we happened to be
     * referring to that store object. In other words, this means this
     * function breaks "referential transparency". It should therefore
     * be used only with great care.
     */
    const StorePath & getBaseStorePath() const;

    /**
     * Uses `^` as the separator
     */
    std::string to_string(const StoreDirConfig & store) const;
    /**
     * Uses `!` as the separator
     */
    std::string to_string_legacy(const StoreDirConfig & store) const;
    /**
     * The caller splits on the separator, so it works for both variants.
     *
     * @param xpSettings Stop-gap to avoid globals during unit tests.
     */
    static SingleDerivedPathBuilt parse(
        const StoreDirConfig & store,
        ref<const SingleDerivedPath> drvPath,
        OutputNameView outputs,
        const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings);

    bool operator==(const SingleDerivedPathBuilt &) const noexcept;
    std::strong_ordering operator<=>(const SingleDerivedPathBuilt &) const noexcept;
};

using _SingleDerivedPathRaw = std::variant<DerivedPathOpaque, SingleDerivedPathBuilt>;

/**
 * A "derived path" is a very simple sort of expression (not a Nix
 * language expression! But an expression in a the general sense) that
 * evaluates to (concrete) store path. It is either:
 *
 * - opaque, in which case it is just a concrete store path with
 *   possibly no known derivation
 *
 * - built, in which case it is a pair of a derivation path and an
 *   output name.
 */
struct SingleDerivedPath : _SingleDerivedPathRaw
{
    using Raw = _SingleDerivedPathRaw;
    using Raw::Raw;

    using Opaque = DerivedPathOpaque;
    using Built = SingleDerivedPathBuilt;

    inline const Raw & raw() const
    {
        return static_cast<const Raw &>(*this);
    }

    bool operator==(const SingleDerivedPath &) const = default;
    auto operator<=>(const SingleDerivedPath &) const = default;

    /**
     * Get the store path this is ultimately derived from (by realising
     * and projecting outputs).
     *
     * Note that this is *not* a property of the store object being
     * referred to, but just of this path --- how we happened to be
     * referring to that store object. In other words, this means this
     * function breaks "referential transparency". It should therefore
     * be used only with great care.
     */
    const StorePath & getBaseStorePath() const;

    /**
     * Uses `^` as the separator
     */
    std::string to_string(const StoreDirConfig & store) const;
    /**
     * Uses `!` as the separator
     */
    std::string to_string_legacy(const StoreDirConfig & store) const;
    /**
     * Uses `^` as the separator
     *
     * @param xpSettings Stop-gap to avoid globals during unit tests.
     */
    static SingleDerivedPath parse(
        const StoreDirConfig & store,
        std::string_view,
        const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings);
    /**
     * Uses `!` as the separator
     *
     * @param xpSettings Stop-gap to avoid globals during unit tests.
     */
    static SingleDerivedPath parseLegacy(
        const StoreDirConfig & store,
        std::string_view,
        const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings);
};

static inline ref<SingleDerivedPath> makeConstantStorePathRef(StorePath drvPath)
{
    return make_ref<SingleDerivedPath>(SingleDerivedPath::Opaque{drvPath});
}

/**
 * A set of derived paths that are built from a derivation
 *
 * Built derived paths are pair of a derivation and some output names.
 * They are evaluated by building the derivation, and then replacing the
 * output names with the resulting outputs.
 *
 * Note that does mean a derived store paths evaluates to multiple
 * opaque paths, which is sort of icky as expressions are supposed to
 * evaluate to single values. Perhaps this should have just a single
 * output name.
 */
struct DerivedPathBuilt
{
    ref<const SingleDerivedPath> drvPath;
    OutputsSpec outputs;

    /**
     * Get the store path this is ultimately derived from (by realising
     * and projecting outputs).
     *
     * Note that this is *not* a property of the store object being
     * referred to, but just of this path --- how we happened to be
     * referring to that store object. In other words, this means this
     * function breaks "referential transparency". It should therefore
     * be used only with great care.
     */
    const StorePath & getBaseStorePath() const;

    /**
     * Uses `^` as the separator
     */
    std::string to_string(const StoreDirConfig & store) const;
    /**
     * Uses `!` as the separator
     */
    std::string to_string_legacy(const StoreDirConfig & store) const;
    /**
     * The caller splits on the separator, so it works for both variants.
     *
     * @param xpSettings Stop-gap to avoid globals during unit tests.
     */
    static DerivedPathBuilt parse(
        const StoreDirConfig & store,
        ref<const SingleDerivedPath>,
        std::string_view,
        const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings);

    bool operator==(const DerivedPathBuilt &) const noexcept;
    // TODO libc++ 16 (used by darwin) missing `std::set::operator <=>`, can't do yet.
    bool operator<(const DerivedPathBuilt &) const noexcept;
};

using _DerivedPathRaw = std::variant<DerivedPathOpaque, DerivedPathBuilt>;

/**
 * A "derived path" is a very simple sort of expression that evaluates
 * to one or more (concrete) store paths. It is either:
 *
 * - opaque, in which case it is just a single concrete store path with
 *   possibly no known derivation
 *
 * - built, in which case it is a pair of a derivation path and some
 *   output names.
 */
struct DerivedPath : _DerivedPathRaw
{
    using Raw = _DerivedPathRaw;
    using Raw::Raw;

    using Opaque = DerivedPathOpaque;
    using Built = DerivedPathBuilt;

    inline const Raw & raw() const
    {
        return static_cast<const Raw &>(*this);
    }

    bool operator==(const DerivedPath &) const = default;
    // TODO libc++ 16 (used by darwin) missing `std::set::operator <=>`, can't do yet.
    // auto operator <=> (const DerivedPath &) const = default;

    /**
     * Get the store path this is ultimately derived from (by realising
     * and projecting outputs).
     *
     * Note that this is *not* a property of the store object being
     * referred to, but just of this path --- how we happened to be
     * referring to that store object. In other words, this means this
     * function breaks "referential transparency". It should therefore
     * be used only with great care.
     */
    const StorePath & getBaseStorePath() const;

    /**
     * Uses `^` as the separator
     */
    std::string to_string(const StoreDirConfig & store) const;
    /**
     * Uses `!` as the separator
     */
    std::string to_string_legacy(const StoreDirConfig & store) const;
    /**
     * Uses `^` as the separator
     *
     * @param xpSettings Stop-gap to avoid globals during unit tests.
     */
    static DerivedPath parse(
        const StoreDirConfig & store,
        std::string_view,
        const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings);
    /**
     * Uses `!` as the separator
     *
     * @param xpSettings Stop-gap to avoid globals during unit tests.
     */
    static DerivedPath parseLegacy(
        const StoreDirConfig & store,
        std::string_view,
        const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings);

    /**
     * Convert a `SingleDerivedPath` to a `DerivedPath`.
     */
    static DerivedPath fromSingle(const SingleDerivedPath &);
};

typedef std::vector<DerivedPath> DerivedPaths;

/**
 * Used by various parser functions to require experimental features as
 * needed.
 *
 * Somewhat unfortunate this cannot just be an implementation detail for
 * this module.
 *
 * @param xpSettings Stop-gap to avoid globals during unit tests.
 */
void drvRequireExperiment(
    const SingleDerivedPath & drv, const ExperimentalFeatureSettings & xpSettings = experimentalFeatureSettings);
} // namespace nix

JSON_IMPL(nix::SingleDerivedPath::Opaque)
JSON_IMPL_WITH_XP_FEATURES(nix::SingleDerivedPath::Built)
JSON_IMPL_WITH_XP_FEATURES(nix::SingleDerivedPath)
JSON_IMPL_WITH_XP_FEATURES(nix::DerivedPath::Built)
JSON_IMPL_WITH_XP_FEATURES(nix::DerivedPath)
