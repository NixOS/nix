#pragma once

#include <variant>

#include "util.hh"

#include "nlohmann/json_fwd.hpp"

namespace nix {

typedef std::set<std::string> OutputNames;

struct AllOutputs {
    bool operator < (const AllOutputs & _) const { return false; }
};

struct DefaultOutputs {
    bool operator < (const DefaultOutputs & _) const { return false; }
};

typedef std::variant<DefaultOutputs, AllOutputs, OutputNames> OutputsSpec;

/* Parse a string of the form 'prefix^output1,...outputN' or
   'prefix^*', returning the prefix and the outputs spec. */
std::pair<std::string, OutputsSpec> parseOutputsSpec(const std::string & s);

std::string printOutputsSpec(const OutputsSpec & outputsSpec);

void to_json(nlohmann::json &, const OutputsSpec &);
void from_json(const nlohmann::json &, OutputsSpec &);

}
