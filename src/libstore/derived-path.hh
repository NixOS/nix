#pragma once
///@file

#include "util.hh"
#include "path.hh"
#include "realisation.hh"
#include "outputs-spec.hh"
#include "comparator.hh"

#include <variant>

#include <nlohmann/json_fwd.hpp>

namespace nix {

class Store;

/**
 * An opaque derived path.
 *
 * Opaque derived paths are just store paths, and fully evaluated. They
 * cannot be simplified further. Since they are opaque, they cannot be
 * built, but they can fetched.
 */
struct DerivedPathOpaque {
    StorePath path;

    nlohmann::json toJSON(ref<Store> store) const;
    std::string to_string(const Store & store) const;
    static DerivedPathOpaque parse(const Store & store, std::string_view);

    GENERATE_CMP(DerivedPathOpaque, me->path);
};

/**
 * A derived path that is built from a derivation
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
struct DerivedPathBuilt {
    StorePath drvPath;
    OutputsSpec outputs;

    /**
     * Uses `^` as the separator
     */
    std::string to_string(const Store & store) const;
    /**
     * Uses `!` as the separator
     */
    std::string to_string_legacy(const Store & store) const;
    /**
     * The caller splits on the separator, so it works for both variants.
     */
    static DerivedPathBuilt parse(const Store & store, std::string_view drvPath, std::string_view outputs);
    nlohmann::json toJSON(ref<Store> store) const;

    GENERATE_CMP(DerivedPathBuilt, me->drvPath, me->outputs);
};

using _DerivedPathRaw = std::variant<
    DerivedPathOpaque,
    DerivedPathBuilt
>;

/**
 * A "derived path" is a very simple sort of expression that evaluates
 * to (concrete) store path. It is either:
 *
 * - opaque, in which case it is just a concrete store path with
 *   possibly no known derivation
 *
 * - built, in which case it is a pair of a derivation path and an
 *   output name.
 */
struct DerivedPath : _DerivedPathRaw {
    using Raw = _DerivedPathRaw;
    using Raw::Raw;

    using Opaque = DerivedPathOpaque;
    using Built = DerivedPathBuilt;

    inline const Raw & raw() const {
        return static_cast<const Raw &>(*this);
    }

    /**
     * Uses `^` as the separator
     */
    std::string to_string(const Store & store) const;
    /**
     * Uses `!` as the separator
     */
    std::string to_string_legacy(const Store & store) const;
    /**
     * Uses `^` as the separator
     */
    static DerivedPath parse(const Store & store, std::string_view);
    /**
     * Uses `!` as the separator
     */
    static DerivedPath parseLegacy(const Store & store, std::string_view);
};

typedef std::vector<DerivedPath> DerivedPaths;

}
