#pragma once

#include "util.hh"
#include "path.hh"

#include <optional>
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
    std::set<std::string> outputs;

    std::string to_string(const Store & store) const;
    static DerivedPathBuilt parse(const Store & store, std::string_view);
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

    std::string to_string(const Store & store) const;
    static DerivedPath parse(const Store & store, std::string_view);
};

/**
 * A built derived path with hints in the form of optional concrete output paths.
 *
 * See 'DerivedPathWithHints' for more an explanation.
 */
struct DerivedPathWithHintsBuilt {
    StorePath drvPath;
    std::map<std::string, std::optional<StorePath>> outputs;

    nlohmann::json toJSON(ref<Store> store) const;
    static DerivedPathWithHintsBuilt parse(const Store & store, std::string_view);
};

using _DerivedPathWithHintsRaw = std::variant<
    DerivedPath::Opaque,
    DerivedPathWithHintsBuilt
>;

/**
 * A derived path with hints in the form of optional concrete output paths in the built case.
 *
 * This type is currently just used by the CLI. The paths are filled in
 * during evaluation for derivations that know what paths they will
 * produce in advanced, i.e. input-addressed or fixed-output content
 * addressed derivations.
 *
 * That isn't very good, because it puts floating content-addressed
 * derivations "at a disadvantage". It would be better to never rely on
 * the output path of unbuilt derivations, and exclusively use the
 * realizations types to work with built derivations' concrete output
 * paths.
 */
// FIXME Stop using and delete this, or if that is not possible move out of libstore to libcmd.
struct DerivedPathWithHints : _DerivedPathWithHintsRaw {
    using Raw = _DerivedPathWithHintsRaw;
    using Raw::Raw;

    using Opaque = DerivedPathOpaque;
    using Built = DerivedPathWithHintsBuilt;

    inline const Raw & raw() const {
        return static_cast<const Raw &>(*this);
    }

};

typedef std::vector<DerivedPathWithHints> DerivedPathsWithHints;

nlohmann::json derivedPathsWithHintsToJSON(const DerivedPathsWithHints & buildables, ref<Store> store);

}
