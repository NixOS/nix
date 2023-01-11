#pragma once

#include <set>
#include <variant>

#include "nlohmann/json_fwd.hpp"

namespace nix {

typedef std::set<std::string> OutputNames;

struct AllOutputs {
    bool operator < (const AllOutputs & _) const { return false; }
};

struct DefaultOutputs {
    bool operator < (const DefaultOutputs & _) const { return false; }
};

typedef std::variant<DefaultOutputs, AllOutputs, OutputNames> _ExtendedOutputsSpecRaw;

struct ExtendedOutputsSpec : _ExtendedOutputsSpecRaw {
    using Raw = _ExtendedOutputsSpecRaw;
    using Raw::Raw;

    using Names = OutputNames;
    using All = AllOutputs;
    using Default = DefaultOutputs;

    inline const Raw & raw() const {
        return static_cast<const Raw &>(*this);
    }

    /* Parse a string of the form 'prefix^output1,...outputN' or
       'prefix^*', returning the prefix and the outputs spec. */
    static std::pair<std::string, ExtendedOutputsSpec> parse(std::string s);

    std::string to_string() const;
};

void to_json(nlohmann::json &, const ExtendedOutputsSpec &);
void from_json(const nlohmann::json &, ExtendedOutputsSpec &);

}
