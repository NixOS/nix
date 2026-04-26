#include "nix/cmd/command-installable-value.hh"
#include "nix/store/globals.hh"
#include "nix/expr/eval.hh"
#include "nix/expr/eval-inline.hh"
#include "nix/expr/eval-settings.hh"
#include "nix/store/names.hh"
#include "nix/main/common-args.hh"
#include "nix/main/shared.hh"
#include "nix/expr/eval-cache.hh"
#include "nix/expr/attr-path.hh"
#include "nix/util/hilite.hh"
#include "nix/util/strings-inline.hh"
#include "nix/expr/value-to-json.hh"

#include <regex>
#include <nlohmann/json.hpp>

#include "nix/util/strings.hh"

using json = nlohmann::json;

namespace nix {

std::string wrap(std::string prefix, std::string s)
{
    return concatStrings(prefix, s, ANSI_NORMAL);
}

struct CmdSearch : InstallableValueCommand, MixJSON
{
    std::vector<std::string> res;
    std::vector<std::string> excludeRes;
    std::optional<std::string> apply;
    bool calcDerivation = false;
    bool checkCache = false;
    bool jsonLines = false;

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
        addFlag(
            Flag{
                .longName = "apply",
                .description = "Apply the function *expr* to each matching derivation (implies --json).",
                .labels = {"expr"},
                .handler = {&apply},
            });
        addFlag(
            Flag{
                .longName = "calc-derivation",
                .description = "Calculate output paths and derivation paths for matching derivations. This will instantiate the derivations (write .drv files to the store).",
                .handler = {&calcDerivation, true},
            });
        addFlag(
            Flag{
                .longName = "check-cache",
                .description = "Check if the output path is available in a binary cache without downloading. Implies --json.",
                .handler = {&checkCache, true},
            });
        addFlag(
            Flag{
                .longName = "json-lines",
                .description = "Output results as JSON Lines (JSONL) format, one result per line. Implies --json.",
                .handler = {&jsonLines, true},
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
        // Disable IFD by default for performance, but allow override via --option
        if (!evalSettings.enableImportFromDerivation.isOverridden())
            evalSettings.enableImportFromDerivation = false;

        // By default, run in read-only mode for performance (don't instantiate drvs).
        // But if --calc-derivation or IFD is enabled, we need to allow instantiation.
        settings.readOnlyMode = !calcDerivation && !evalSettings.enableImportFromDerivation;

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

        // --json-lines, --apply, and --check-cache imply --json
        if (jsonLines || apply || checkCache)
            json = true;

        std::optional<nlohmann::json> jsonOut;
        if (json && !jsonLines)
            jsonOut = json::object();

        // Parse apply expression once if provided
        Value * vApply = nullptr;
        if (apply) {
            vApply = state->allocValue();
            state->eval(state->parseExprFromString(*apply, state->rootPath(".")), *vApply);
        }

        uint64_t results = 0;

        std::function<void(eval_cache::AttrCursor & cursor, const AttrPath & attrPath, bool initialRecurse)> visit;

        visit = [&](eval_cache::AttrCursor & cursor, const AttrPath & attrPath, bool initialRecurse) {
            auto attrPathS = state->symbols.resolve({attrPath});
            auto attrPathStr = attrPath.to_string(*state);

            Activity act(*logger, json ? lvlDebug : lvlInfo, actUnknown, fmt("evaluating '%s'", attrPathStr));
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

                    std::vector<std::smatch> attrPathMatches;
                    std::vector<std::smatch> descriptionMatches;
                    std::vector<std::smatch> nameMatches;
                    bool found = false;

                    for (auto & regex : excludeRegexes) {
                        if (std::regex_search(attrPathStr, regex) || std::regex_search(name.name, regex)
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

                        addAll(std::sregex_iterator(attrPathStr.begin(), attrPathStr.end(), regex), attrPathMatches);
                        addAll(std::sregex_iterator(name.name.begin(), name.name.end(), regex), nameMatches);
                        addAll(std::sregex_iterator(description.begin(), description.end(), regex), descriptionMatches);

                        if (!found)
                            break;
                    }

                    if (found) {
                        results++;
                        if (json) {
                            nlohmann::json jsonEntry;

                            if (vApply) {
                                try {
                                    // Get the derivation value and apply the user's function
                                    auto & v = cursor.forceValue();
                                    auto vRes = state->allocValue();
                                    state->callFunction(*vApply, v, *vRes, noPos);

                                    // Convert result to JSON
                                    NixStringContext context;
                                    jsonEntry = printValueAsJSON(*state, true, *vRes, noPos, context, false);
                                } catch (Error & e) {
                                    // If apply fails, output error information
                                    jsonEntry = json::object({
                                        {"pname", name.name},
                                        {"version", name.version},
                                        {"description", description},
                                        {"applyError", e.msg()},
                                    });
                                }
                            } else {
                                // Default output: pname, version, description
                                jsonEntry = json::object({
                                    {"pname", name.name},
                                    {"version", name.version},
                                    {"description", description},
                                });

                                if (calcDerivation) {
                                    try {
                                        // Calculate both outPath and drvPath
                                        // This will instantiate the derivation (write .drv file)
                                        auto aOutPath = cursor.maybeGetAttr(state->s.outPath);
                                        if (aOutPath) {
                                            auto outPathStr = aOutPath->getString();
                                            jsonEntry["outPath"] = outPathStr;
                                        }

                                        auto drvPath = cursor.forceDerivation();
                                        jsonEntry["drvPath"] = state->store->printStorePath(drvPath);
                                    } catch (Error & e) {
                                        jsonEntry["derivationError"] = e.msg();
                                    }
                                }

                                if (checkCache) {
                                    try {
                                        // Get outPath without instantiating
                                        auto aOutPath = cursor.maybeGetAttr(state->s.outPath);
                                        if (aOutPath) {
                                            auto outPathStr = aOutPath->getString();
                                            auto outPath = state->store->parseStorePath(outPathStr);

                                            // Check if path is substitutable (cached)
                                            StorePathSet paths{outPath};
                                            auto substitutable = state->store->querySubstitutablePaths(paths);
                                            jsonEntry["cached"] = substitutable.count(outPath) > 0;
                                        } else {
                                            jsonEntry["cached"] = false;
                                        }
                                    } catch (Error & e) {
                                        // If we can't check, mark as unknown
                                        jsonEntry["cached"] = false;
                                    }
                                }
                            }

                            if (jsonLines) {
                                // Output as JSON Lines (one JSON object per line)
                                auto jsonLine = json::object();
                                jsonLine[attrPathStr] = jsonEntry;
                                logger->cout("%s", jsonLine.dump());
                            } else {
                                (*jsonOut)[attrPathStr] = jsonEntry;
                            }
                        } else {
                            if (results > 1)
                                logger->cout("");
                            logger->cout(
                                "* %s%s",
                                wrap("\e[0;1m", hiliteMatches(attrPathStr, attrPathMatches, ANSI_GREEN, "\e[0;1m")),
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

        if (json && !jsonLines)
            printJSON(*jsonOut);

        if (!json && !results)
            throw Error("no results for the given search term(s)!");
    }
};

static auto rCmdSearch = registerCommand<CmdSearch>("search");

} // namespace nix
