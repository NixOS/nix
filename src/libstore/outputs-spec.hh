#pragma once

#include <optional>
#include <set>
#include <variant>

#include "json-impls.hh"

namespace nix {

struct OutputNames : std::set<std::string> {
    using std::set<std::string>::set;

    // These need to be "inherited manually"
    OutputNames(const std::set<std::string> & s)
        : std::set<std::string>(s)
    { }
    OutputNames(std::set<std::string> && s)
        : std::set<std::string>(s)
    { }

    /* This set should always be non-empty, so we delete this
       constructor in order make creating empty ones by mistake harder.
       */
    OutputNames() = delete;
};

struct AllOutputs {
    bool operator < (const AllOutputs & _) const { return false; }
};

typedef std::variant<AllOutputs, OutputNames> _OutputsSpecRaw;

struct OutputsSpec : _OutputsSpecRaw {
    using Raw = _OutputsSpecRaw;
    using Raw::Raw;

    /* Force choosing a variant */
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

    /* Modify the receiver outputs spec so it is the union of it's old value
       and the argument. Return whether the output spec needed to be modified
       --- if it didn't it was already "large enough". */
    bool merge(const OutputsSpec & outputs);

    /* Parse a string of the form 'output1,...outputN' or
       '*', returning the outputs spec. */
    static OutputsSpec parse(std::string_view s);
    static std::optional<OutputsSpec> parseOpt(std::string_view s);

    std::string to_string() const;
};

struct DefaultOutputs {
    bool operator < (const DefaultOutputs & _) const { return false; }
};

typedef std::variant<DefaultOutputs, OutputsSpec> _ExtendedOutputsSpecRaw;

struct ExtendedOutputsSpec : _ExtendedOutputsSpecRaw {
    using Raw = _ExtendedOutputsSpecRaw;
    using Raw::Raw;

    using Default = DefaultOutputs;
    using Explicit = OutputsSpec;

    inline const Raw & raw() const {
        return static_cast<const Raw &>(*this);
    }

    /* Parse a string of the form 'prefix^output1,...outputN' or
       'prefix^*', returning the prefix and the extended outputs spec. */
    static std::pair<std::string_view, ExtendedOutputsSpec> parse(std::string_view s);

    std::string to_string() const;
};

}

JSON_IMPL(OutputsSpec)
JSON_IMPL(ExtendedOutputsSpec)
