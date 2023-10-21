#include <regex>
#include <nlohmann/json.hpp>

#include "util.hh"
#include "regex-combinators.hh"
#include "outputs-spec.hh"
#include "path-regex.hh"

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
    }, raw);
}

static std::string outputSpecRegexStr =
    regex::either(
        regex::group(R"(\*)"),
        regex::group(regex::list(nameRegexStr)));

std::optional<OutputsSpec> OutputsSpec::parseOpt(std::string_view s)
{
    static std::regex regex(std::string { outputSpecRegexStr });

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
        throw Error("invalid outputs specifier '%s'", s);
    return std::move(*spec);
}


std::optional<std::pair<std::string_view, ExtendedOutputsSpec>> ExtendedOutputsSpec::parseOpt(std::string_view s)
{
    auto found = s.rfind('^');

    if (found == std::string::npos)
        return std::pair { s, ExtendedOutputsSpec::Default {} };

    auto specOpt = OutputsSpec::parseOpt(s.substr(found + 1));
    if (!specOpt)
        return std::nullopt;
    return std::pair { s.substr(0, found), ExtendedOutputsSpec::Explicit { std::move(*specOpt) } };
}


std::pair<std::string_view, ExtendedOutputsSpec> ExtendedOutputsSpec::parse(std::string_view s)
{
    std::optional spec = parseOpt(s);
    if (!spec)
        throw Error("invalid extended outputs specifier '%s'", s);
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
    }, raw);
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
    }, raw);
}


OutputsSpec OutputsSpec::union_(const OutputsSpec & that) const
{
    return std::visit(overloaded {
        [&](const OutputsSpec::All &) -> OutputsSpec {
            return OutputsSpec::All { };
        },
        [&](const OutputsSpec::Names & theseNames) -> OutputsSpec {
            return std::visit(overloaded {
                [&](const OutputsSpec::All &) -> OutputsSpec {
                    return OutputsSpec::All {};
                },
                [&](const OutputsSpec::Names & thoseNames) -> OutputsSpec {
                    OutputsSpec::Names ret = theseNames;
                    ret.insert(thoseNames.begin(), thoseNames.end());
                    return ret;
                },
            }, that.raw);
        },
    }, raw);
}


bool OutputsSpec::isSubsetOf(const OutputsSpec & that) const
{
    return std::visit(overloaded {
        [&](const OutputsSpec::All &) {
            return true;
        },
        [&](const OutputsSpec::Names & thoseNames) {
            return std::visit(overloaded {
                [&](const OutputsSpec::All &) {
                    return false;
                },
                [&](const OutputsSpec::Names & theseNames) {
                    bool ret = true;
                    for (auto & o : theseNames)
                        if (thoseNames.count(o) == 0)
                            ret = false;
                    return ret;
                },
            }, raw);
        },
    }, that.raw);
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
    }, t.raw);
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
    }, t.raw);
}

}
