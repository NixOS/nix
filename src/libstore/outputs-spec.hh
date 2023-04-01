#pragma once

#include <cassert>
#include <optional>
#include <set>
#include <variant>

#include "json-impls.hh"

namespace nix {

/**
 * A non-empty set of outputs, specified by name
 */
struct OutputNames : std::set<std::string> {
    using std::set<std::string>::set;

    /* These need to be "inherited manually" */

    OutputNames(const std::set<std::string> & s)
        : std::set<std::string>(s)
    { assert(!empty()); }

    /**
     * Needs to be "inherited manually"
     */
    OutputNames(std::set<std::string> && s)
        : std::set<std::string>(s)
    { assert(!empty()); }

    /* This set should always be non-empty, so we delete this
       constructor in order make creating empty ones by mistake harder.
       */
    OutputNames() = delete;
};

/**
 * The set of all outputs, without needing to name them explicitly
 */
struct AllOutputs : std::monostate { };

typedef std::variant<AllOutputs, OutputNames> _OutputsSpecRaw;

struct OutputsSpec : _OutputsSpecRaw {
    using Raw = _OutputsSpecRaw;
    using Raw::Raw;

    /**
     * Force choosing a variant
     */
    OutputsSpec() = delete;

    using Names = OutputNames;
    using All = AllOutputs;

    inline const Raw & raw() const {
        return static_cast<const Raw &>(*this);
    }

    inline Raw & raw() {
        return static_cast<Raw &>(*this);
    }

    bool contains(const std::string & output) const;

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

struct DefaultOutputs : std::monostate { };

typedef std::variant<DefaultOutputs, OutputsSpec> _ExtendedOutputsSpecRaw;

struct ExtendedOutputsSpec : _ExtendedOutputsSpecRaw {
    using Raw = _ExtendedOutputsSpecRaw;
    using Raw::Raw;

    using Default = DefaultOutputs;
    using Explicit = OutputsSpec;

    inline const Raw & raw() const {
        return static_cast<const Raw &>(*this);
    }

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
