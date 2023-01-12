#include "outputs-spec.hh"
#include "nlohmann/json.hpp"

#include <regex>

namespace nix {

std::pair<std::string, OutputsSpec> parseOutputsSpec(const std::string & s)
{
    static std::regex regex(R"((.*)\^((\*)|([a-z]+(,[a-z]+)*)))");

    std::smatch match;
    if (!std::regex_match(s, match, regex))
        return {s, DefaultOutputs()};

    if (match[3].matched)
        return {match[1], AllOutputs()};

    return {match[1], tokenizeString<OutputNames>(match[4].str(), ",")};
}

std::string printOutputsSpec(const OutputsSpec & outputsSpec)
{
    if (std::get_if<DefaultOutputs>(&outputsSpec))
        return "";

    if (std::get_if<AllOutputs>(&outputsSpec))
        return "^*";

    if (auto outputNames = std::get_if<OutputNames>(&outputsSpec))
        return "^" + concatStringsSep(",", *outputNames);

    assert(false);
}

void to_json(nlohmann::json & json, const OutputsSpec & outputsSpec)
{
    if (std::get_if<DefaultOutputs>(&outputsSpec))
        json = nullptr;

    else if (std::get_if<AllOutputs>(&outputsSpec))
        json = std::vector<std::string>({"*"});

    else if (auto outputNames = std::get_if<OutputNames>(&outputsSpec))
        json = *outputNames;
}

void from_json(const nlohmann::json & json, OutputsSpec & outputsSpec)
{
    if (json.is_null())
        outputsSpec = DefaultOutputs();
    else {
        auto names = json.get<OutputNames>();
        if (names == OutputNames({"*"}))
            outputsSpec = AllOutputs();
        else
            outputsSpec = names;
    }
}

}
