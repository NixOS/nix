#include "util.hh"
#include "outputs-spec.hh"
#include "nlohmann/json.hpp"

#include <regex>

namespace nix {

std::pair<std::string, ExtendedOutputsSpec> ExtendedOutputsSpec::parse(std::string s)
{
    static std::regex regex(R"((.*)\^((\*)|([a-z]+(,[a-z]+)*)))");

    std::smatch match;
    if (!std::regex_match(s, match, regex))
        return {s, DefaultOutputs()};

    if (match[3].matched)
        return {match[1], AllOutputs()};

    return {match[1], tokenizeString<OutputNames>(match[4].str(), ",")};
}

std::string ExtendedOutputsSpec::to_string() const
{
    return std::visit(overloaded {
        [&](const ExtendedOutputsSpec::Default &) -> std::string {
            return "";
        },
        [&](const ExtendedOutputsSpec::All &) -> std::string {
            return "*";
        },
        [&](const ExtendedOutputsSpec::Names & outputNames) -> std::string {
            return "^" + concatStringsSep(",", outputNames);
        },
    }, raw());
}

void to_json(nlohmann::json & json, const ExtendedOutputsSpec & extendedOutputsSpec)
{
    if (std::get_if<DefaultOutputs>(&extendedOutputsSpec))
        json = nullptr;

    else if (std::get_if<AllOutputs>(&extendedOutputsSpec))
        json = std::vector<std::string>({"*"});

    else if (auto outputNames = std::get_if<OutputNames>(&extendedOutputsSpec))
        json = *outputNames;
}

void from_json(const nlohmann::json & json, ExtendedOutputsSpec & extendedOutputsSpec)
{
    if (json.is_null())
        extendedOutputsSpec = DefaultOutputs();
    else {
        auto names = json.get<OutputNames>();
        if (names == OutputNames({"*"}))
            extendedOutputsSpec = AllOutputs();
        else
            extendedOutputsSpec = names;
    }
}

}
