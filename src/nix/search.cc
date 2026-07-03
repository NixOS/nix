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
#include "nix/expr/parallel-eval.hh"

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

    StringSet getRoles() override
    {
        return {"nix-search"};
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

        std::optional<Sync<nlohmann::json>> jsonOut;
        if (json)
            jsonOut.emplace(json::object());

        std::atomic<uint64_t> results = 0;
        std::atomic<uint64_t> total = 0;

        FutureVector futures(*state->executor);

        std::function<void(eval_cache::AttrCursor & cursor, const AttrPath & attrPath, bool initialRecurse)> visit;

        visit = [&](eval_cache::AttrCursor & cursor, const AttrPath & attrPath, bool initialRecurse) {
            auto attrPathS = state->symbols.resolve({attrPath});
            auto attrPathStr = attrPath.to_string(*state);

            /*
            Activity act(*logger, lvlInfo, actUnknown, fmt("evaluating '%s'", attrPathStr));
            */
            try {
                auto recurse = [&]() {
                    Executor::WorkItems work;
                    for (const auto & attr : cursor.getAttrs()) {
                        auto cursor2 = cursor.getAttr(state->symbols[attr]);
                        auto attrPath2(attrPath);
                        attrPath2.push_back(attr);
                        state->addWork(
                            work,
                            std::string_view(state->symbols[attr]).find("Packages") != std::string_view::npos ? 0 : 2,
                            [cursor2, attrPath2, visit]() { visit(*cursor2, attrPath2, false); });
                        /* Spawn incrementally rather than after the whole
                           enumeration, so that idle worker threads can start
                           on the first attributes while we're still
                           enumerating the rest. */
                        if (work.size() >= 256)
                            futures.spawn(std::exchange(work, {}));
                    }
                    futures.spawn(std::move(work));
                };

                if (cursor.isDerivation()) {
                    total++;

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
                            (*jsonOut->lock())[attrPathStr] = {
                                {"pname", name.name},
                                {"version", name.version},
                                {"description", description},
                            };
                        } else {
                            auto out =
                                fmt("%s* %s%s",
                                    results > 1 ? "\n" : "",
                                    wrap("\e[0;1m", hiliteMatches(attrPathStr, attrPathMatches, ANSI_GREEN, "\e[0;1m")),
                                    optionalBracket(" (", name.version, ")"));
                            if (description != "")
                                out += fmt(
                                    "\n  %s", hiliteMatches(description, descriptionMatches, ANSI_GREEN, ANSI_NORMAL));
                            logger->cout(out);
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

        Executor::WorkItems work;
        for (auto & cursor : installable->getCursors(*state, false))
            state->addWork(work, 1, [cursor, visit]() { visit(*cursor, cursor->getAttrPath(), true); });

        futures.spawn(std::move(work));
        futures.finishAll();

        if (json)
            printJSON(*(jsonOut->lock()));

        if (!json && !results)
            throw Error("no results for the given search term(s)!");

        notice("Found %d matching packages out of %d.", results, total);
    }
};

static auto rCmdSearch = registerCommand<CmdSearch>("search");
