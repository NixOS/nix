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

typedef std::variant<DefaultOutputs, AllOutputs, OutputNames> _OutputsSpecRaw;

struct OutputsSpec : _OutputsSpecRaw {
    using Raw = _OutputsSpecRaw;
    using Raw::Raw;

    using Names = OutputNames;
    using All = AllOutputs;
    using Default = DefaultOutputs;

    inline const Raw & raw() const {
        return static_cast<const Raw &>(*this);
    }

    /* Parse a string of the form 'prefix^output1,...outputN' or
       'prefix^*', returning the prefix and the outputs spec. */
    static std::pair<std::string, OutputsSpec> parse(std::string s);

    std::string to_string() const;
};

void to_json(nlohmann::json &, const OutputsSpec &);
void from_json(const nlohmann::json &, OutputsSpec &);

}
