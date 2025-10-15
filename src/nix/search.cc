#include "nix/cmd/command-installable-value.hh"
#include "nix/store/globals.hh"
#include "nix/expr/eval.hh"
#include "nix/expr/eval-inline.hh"
#include "nix/expr/eval-settings.hh"
#include "nix/store/names.hh"
#include "nix/expr/get-drvs.hh"
#include "nix/main/common-args.hh"
#include "nix/main/shared.hh"
#include "nix/expr/eval-cache.hh"
#include "nix/expr/attr-path.hh"
#include "nix/util/hilite.hh"
#include "nix/util/strings-inline.hh"

#include <regex>
#include <fstream>
#include <nlohmann/json.hpp>

#include "nix/util/strings.hh"

using namespace nix;
using json = nlohmann::json;

std::string wrap(std::string prefix, std::string s)
{
    return concatStrings(prefix, s, ANSI_NORMAL);
}

struct CmdSearch : InstallableValueCommand, MixJSON
{
    std::vector<std::string> res;
    std::vector<std::string> excludeRes;

    CmdSearch()
    {
        expectArgs("regex", &res);
        addFlag(
            Flag{
                .longName = "exclude",
                .shortName = 'e',
                .description = "Hide packages whose attribute path, name or description contain *regex*.",
                .labels = {"regex"},
                .handler = {[this](std::string s) { excludeRes.push_back(s); }},
            });
    }

    std::string description() override
    {
        return "search for packages";
    }

    std::string doc() override
    {
        return
#include "search.md"
            ;
    }

    Strings getDefaultFlakeAttrPaths() override
    {
        return {"packages." + settings.thisSystem.get(), "legacyPackages." + settings.thisSystem.get()};
    }

    void run(ref<Store> store, ref<InstallableValue> installable) override
    {
        settings.readOnlyMode = true;
        evalSettings.enableImportFromDerivation.setDefault(false);

        // Recommend "^" here instead of ".*" due to differences in resulting highlighting
        if (res.empty())
            throw UsageError(
                "Must provide at least one regex! To match all packages, use '%s'.", "nix search <installable> ^");

        std::vector<std::regex> regexes;
        std::vector<std::regex> excludeRegexes;
        regexes.reserve(res.size());
        excludeRegexes.reserve(excludeRes.size());

        for (auto & re : res)
            regexes.push_back(std::regex(re, std::regex::extended | std::regex::icase));

        for (auto & re : excludeRes)
            excludeRegexes.emplace_back(re, std::regex::extended | std::regex::icase);

        auto state = getEvalState();

        std::optional<nlohmann::json> jsonOut;
        if (json)
            jsonOut = json::object();

        uint64_t results = 0;

        std::function<void(eval_cache::AttrCursor & cursor, const std::vector<Symbol> & attrPath, bool initialRecurse)>
            visit;

        visit = [&](eval_cache::AttrCursor & cursor, const std::vector<Symbol> & attrPath, bool initialRecurse) {
            auto attrPathS = state->symbols.resolve(attrPath);

            Activity act(*logger, lvlInfo, actUnknown, fmt("evaluating '%s'", concatStringsSep(".", attrPathS)));
            try {
                auto recurse = [&]() {
                    for (const auto & attr : cursor.getAttrs()) {
                        auto cursor2 = cursor.getAttr(state->symbols[attr]);
                        auto attrPath2(attrPath);
                        attrPath2.push_back(attr);
                        visit(*cursor2, attrPath2, false);
                    }
                };

                if (cursor.isDerivation()) {
                    DrvName name(cursor.getAttr(state->s.name)->getString());

                    auto aMeta = cursor.maybeGetAttr(state->s.meta);
                    auto aDescription = aMeta ? aMeta->maybeGetAttr(state->s.description) : nullptr;
                    auto description = aDescription ? aDescription->getString() : "";
                    std::replace(description.begin(), description.end(), '\n', ' ');
                    auto attrPath2 = concatStringsSep(".", attrPathS);

                    std::vector<std::smatch> attrPathMatches;
                    std::vector<std::smatch> descriptionMatches;
                    std::vector<std::smatch> nameMatches;
                    bool found = false;

                    for (auto & regex : excludeRegexes) {
                        if (std::regex_search(attrPath2, regex) || std::regex_search(name.name, regex)
                            || std::regex_search(description, regex))
                            return;
                    }

                    for (auto & regex : regexes) {
                        found = false;
                        auto addAll = [&found](std::sregex_iterator it, std::vector<std::smatch> & vec) {
                            const auto end = std::sregex_iterator();
                            while (it != end) {
                                vec.push_back(*it++);
                                found = true;
                            }
                        };

                        addAll(std::sregex_iterator(attrPath2.begin(), attrPath2.end(), regex), attrPathMatches);
                        addAll(std::sregex_iterator(name.name.begin(), name.name.end(), regex), nameMatches);
                        addAll(std::sregex_iterator(description.begin(), description.end(), regex), descriptionMatches);

                        if (!found)
                            break;
                    }

                    if (found) {
                        results++;
                        if (json) {
                            (*jsonOut)[attrPath2] = {
                                {"pname", name.name},
                                {"version", name.version},
                                {"description", description},
                            };
                        } else {
                            if (results > 1)
                                logger->cout("");
                            logger->cout(
                                "* %s%s",
                                wrap("\e[0;1m", hiliteMatches(attrPath2, attrPathMatches, ANSI_GREEN, "\e[0;1m")),
                                optionalBracket(" (", name.version, ")"));
                            if (description != "")
                                logger->cout(
                                    "  %s", hiliteMatches(description, descriptionMatches, ANSI_GREEN, ANSI_NORMAL));
                        }
                    }
                }

                else if (
                    attrPath.size() == 0 || (attrPathS[0] == "legacyPackages" && attrPath.size() <= 2)
                    || (attrPathS[0] == "packages" && attrPath.size() <= 2))
                    recurse();

                else if (initialRecurse)
                    recurse();

                else if (attrPathS[0] == "legacyPackages" && attrPath.size() > 2) {
                    auto attr = cursor.maybeGetAttr(state->s.recurseForDerivations);
                    if (attr && attr->getBool())
                        recurse();
                }

            } catch (EvalError & e) {
                if (!(attrPath.size() > 0 && attrPathS[0] == "legacyPackages"))
                    throw;
            }
        };

        for (auto & cursor : installable->getCursors(*state))
            visit(*cursor, cursor->getAttrPath(), true);

        if (json)
            printJSON(*jsonOut);

        if (!json && !results)
            throw Error("no results for the given search term(s)!");
    }
};

static auto rCmdSearch = registerCommand<CmdSearch>("search");
