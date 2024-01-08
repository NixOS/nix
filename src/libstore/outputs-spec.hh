#pragma once
///@file

#include <cassert>
#include <optional>
#include <set>
#include <variant>

#include "comparator.hh"
#include "json-impls.hh"
#include "comparator.hh"
#include "variant-wrapper.hh"

namespace nix {

/**
 * An (owned) output name. Just a type alias used to make code more
 * readible.
 */
typedef std::string OutputName;

/**
 * A borrowed output name. Just a type alias used to make code more
 * readible.
 */
typedef std::string_view OutputNameView;

struct OutputsSpec {
    /**
     * A non-empty set of outputs, specified by name
     */
    struct Names : std::set<OutputName> {
        using std::set<OutputName>::set;

        /* These need to be "inherited manually" */

        Names(const std::set<OutputName> & s)
            : std::set<OutputName>(s)
        { assert(!empty()); }

        /**
         * Needs to be "inherited manually"
         */
        Names(std::set<OutputName> && s)
            : std::set<OutputName>(s)
        { assert(!empty()); }

        /* This set should always be non-empty, so we delete this
           constructor in order make creating empty ones by mistake harder.
           */
        Names() = delete;
    };

    /**
     * The set of all outputs, without needing to name them explicitly
     */
    struct All : std::monostate { };

    typedef std::variant<All, Names> Raw;

    Raw raw;

    GENERATE_CMP(OutputsSpec, me->raw);

    MAKE_WRAPPER_CONSTRUCTOR(OutputsSpec);

    /**
     * Force choosing a variant
     */
    OutputsSpec() = delete;

    bool contains(const OutputName & output) const;

    /**
     * Create a new OutputsSpec which is the union of this and that.
     */
    OutputsSpec union_(const OutputsSpec & that) const;

    /**
     * Whether this OutputsSpec is a subset of that.
     */
    bool isSubsetOf(const OutputsSpec & outputs) const;

    /**
     * Parse a string of the form 'output1,...outputN' or '*', returning
     * the outputs spec.
     */
    static OutputsSpec parse(std::string_view s);
    static std::optional<OutputsSpec> parseOpt(std::string_view s);

    std::string to_string() const;
};

struct ExtendedOutputsSpec {
    struct Default : std::monostate { };
    using Explicit = OutputsSpec;

    typedef std::variant<Default, Explicit> Raw;

    Raw raw;

    GENERATE_CMP(ExtendedOutputsSpec, me->raw);

    MAKE_WRAPPER_CONSTRUCTOR(ExtendedOutputsSpec);

    /**
     * Force choosing a variant
     */
    ExtendedOutputsSpec() = delete;

    /**
     * Parse a string of the form 'prefix^output1,...outputN' or
     * 'prefix^*', returning the prefix and the extended outputs spec.
     */
    static std::pair<std::string_view, ExtendedOutputsSpec> parse(std::string_view s);
    static std::optional<std::pair<std::string_view, ExtendedOutputsSpec>> parseOpt(std::string_view s);

    std::string to_string() const;
};

}

JSON_IMPL(OutputsSpec)
JSON_IMPL(ExtendedOutputsSpec)
