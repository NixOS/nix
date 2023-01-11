#include "util.hh"
#include "outputs-spec.hh"
#include "nlohmann/json.hpp"

#include <regex>

namespace nix {

bool OutputsSpec::contains(const std::string & outputName) const
{
    return std::visit(overloaded {
        [&](const OutputsSpec::All &) {
            return true;
        },
        [&](const OutputsSpec::Names & outputNames) {
            return outputNames.count(outputName) > 0;
        },
    }, raw());
}


std::optional<OutputsSpec> OutputsSpec::parseOpt(std::string_view s)
{
    static std::regex regex(R"((\*)|([a-z]+(,[a-z]+)*))");

    std::smatch match;
    std::string s2 { s }; // until some improves std::regex
    if (!std::regex_match(s2, match, regex))
        return std::nullopt;

    if (match[1].matched)
        return { OutputsSpec::All {} };

    if (match[2].matched)
        return { tokenizeString<OutputsSpec::Names>(match[2].str(), ",") };

    assert(false);
}


OutputsSpec OutputsSpec::parse(std::string_view s)
{
    std::optional spec = OutputsSpec::parseOpt(s);
    if (!spec)
        throw Error("Invalid outputs specifier: '%s'", s);
    return *spec;
}


std::pair<std::string_view, ExtendedOutputsSpec> ExtendedOutputsSpec::parse(std::string_view s)
{
    auto found = s.rfind('^');

    if (found == std::string::npos)
        return { s, ExtendedOutputsSpec::Default {} };

    auto spec = OutputsSpec::parse(s.substr(found + 1));
    return { s.substr(0, found), ExtendedOutputsSpec::Explicit { spec } };
}


std::string OutputsSpec::to_string() const
{
    return std::visit(overloaded {
        [&](const OutputsSpec::All &) -> std::string {
            return "*";
        },
        [&](const OutputsSpec::Names & outputNames) -> std::string {
            return concatStringsSep(",", outputNames);
        },
    }, raw());
}


std::string ExtendedOutputsSpec::to_string() const
{
    return std::visit(overloaded {
        [&](const ExtendedOutputsSpec::Default &) -> std::string {
            return "";
        },
        [&](const ExtendedOutputsSpec::Explicit & outputSpec) -> std::string {
            return "^" + outputSpec.to_string();
        },
    }, raw());
}


bool OutputsSpec::merge(const OutputsSpec & that)
{
    return std::visit(overloaded {
        [&](OutputsSpec::All &) {
            /* If we already refer to all outputs, there is nothing to do. */
            return false;
        },
        [&](OutputsSpec::Names & theseNames) {
            return std::visit(overloaded {
                [&](const OutputsSpec::All &) {
                    *this = OutputsSpec::All {};
                    return true;
                },
                [&](const OutputsSpec::Names & thoseNames) {
                    bool ret = false;
                    for (auto & i : thoseNames)
                        if (theseNames.insert(i).second)
                            ret = true;
                    return ret;
                },
            }, that.raw());
        },
    }, raw());
}


void to_json(nlohmann::json & json, const OutputsSpec & outputsSpec)
{
    std::visit(overloaded {
        [&](const OutputsSpec::All &) {
            json = std::vector<std::string>({"*"});
        },
        [&](const OutputsSpec::Names & names) {
            json = names;
        },
    }, outputsSpec);
}


void to_json(nlohmann::json & json, const ExtendedOutputsSpec & extendedOutputsSpec)
{
    std::visit(overloaded {
        [&](const ExtendedOutputsSpec::Default &) {
            json = nullptr;
        },
        [&](const ExtendedOutputsSpec::Explicit & e) {
            to_json(json, e);
        },
    }, extendedOutputsSpec);
}


void from_json(const nlohmann::json & json, OutputsSpec & outputsSpec)
{
    auto names = json.get<OutputNames>();
    if (names == OutputNames({"*"}))
        outputsSpec = OutputsSpec::All {};
    else
        outputsSpec = names;
}


void from_json(const nlohmann::json & json, ExtendedOutputsSpec & extendedOutputsSpec)
{
    if (json.is_null())
        extendedOutputsSpec = ExtendedOutputsSpec::Default {};
    else {
        extendedOutputsSpec = ExtendedOutputsSpec::Explicit { json.get<OutputsSpec>() };
    }
}

}
