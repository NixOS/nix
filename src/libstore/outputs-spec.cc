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
        return OutputsSpec::Names { tokenizeString<StringSet>(match[2].str(), ",") };

    assert(false);
}


OutputsSpec OutputsSpec::parse(std::string_view s)
{
    std::optional spec = parseOpt(s);
    if (!spec)
        throw Error("Invalid outputs specifier: '%s'", s);
    return *spec;
}


std::optional<std::pair<std::string_view, ExtendedOutputsSpec>> ExtendedOutputsSpec::parseOpt(std::string_view s)
{
    auto found = s.rfind('^');

    if (found == std::string::npos)
        return std::pair { s, ExtendedOutputsSpec::Default {} };

    auto specOpt = OutputsSpec::parseOpt(s.substr(found + 1));
    if (!specOpt)
        return std::nullopt;
    return std::pair { s.substr(0, found), ExtendedOutputsSpec::Explicit { *std::move(specOpt) } };
}


std::pair<std::string_view, ExtendedOutputsSpec> ExtendedOutputsSpec::parse(std::string_view s)
{
    std::optional spec = parseOpt(s);
    if (!spec)
        throw Error("Invalid extended outputs specifier: '%s'", s);
    return *spec;
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

}

namespace nlohmann {

using namespace nix;

OutputsSpec adl_serializer<OutputsSpec>::from_json(const json & json) {
    auto names = json.get<StringSet>();
    if (names == StringSet({"*"}))
        return OutputsSpec::All {};
    else
        return OutputsSpec::Names { std::move(names) };
}

void adl_serializer<OutputsSpec>::to_json(json & json, OutputsSpec t) {
    std::visit(overloaded {
        [&](const OutputsSpec::All &) {
            json = std::vector<std::string>({"*"});
        },
        [&](const OutputsSpec::Names & names) {
            json = names;
        },
    }, t);
}


ExtendedOutputsSpec adl_serializer<ExtendedOutputsSpec>::from_json(const json & json) {
    if (json.is_null())
        return ExtendedOutputsSpec::Default {};
    else {
        return ExtendedOutputsSpec::Explicit { json.get<OutputsSpec>() };
    }
}

void adl_serializer<ExtendedOutputsSpec>::to_json(json & json, ExtendedOutputsSpec t) {
    std::visit(overloaded {
        [&](const ExtendedOutputsSpec::Default &) {
            json = nullptr;
        },
        [&](const ExtendedOutputsSpec::Explicit & e) {
            adl_serializer<OutputsSpec>::to_json(json, e);
        },
    }, t);
}

}
