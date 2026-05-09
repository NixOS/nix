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

#include <algorithm>
#include <optional>
#include <regex>
#include <string_view>
#include <nlohmann/json.hpp>

#include "nix/util/strings.hh"

using json = nlohmann::json;

namespace nix {

std::string wrap(std::string prefix, std::string s)
{
    return concatStrings(prefix, s, ANSI_NORMAL);
}

static bool hasNoRegexMetacharacters(std::string_view re)
{
    return re.find_first_of(".[]()*+?{}|\\^$") == std::string_view::npos;
}

/**
 * Locale-independent ASCII lower-casing. We deliberately avoid
 * `nix::toLower`, which routes through `std::tolower` and therefore depends
 * on the active C locale. `std::regex` with `icase` uses its own (C++)
 * locale for case-folding; for the literal pre-filter to be equivalent to
 * the regex it replaces, both must fold the same way. Folding ASCII bytes
 * directly keeps us aligned with `std::regex`'s default ("C" locale)
 * behaviour regardless of what `setlocale` may later do to the process.
 */
static std::string asciiLower(std::string_view s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s)
        out.push_back(c >= 'A' && c <= 'Z' ? char(c + ('a' - 'A')) : c);
    return out;
}

static bool containsCI(std::string_view haystack, std::string_view needleLower)
{
    auto it = std::search(haystack.begin(), haystack.end(), needleLower.begin(), needleLower.end(), [](char a, char b) {
        char la = (a >= 'A' && a <= 'Z') ? char(a + ('a' - 'A')) : a;
        return la == b;
    });
    return it != haystack.end();
}

/**
 * A user-supplied search pattern, compiled as a POSIX-extended regex with an
 * optional case-insensitive substring fast path.
 *
 * libc++'s std::regex allocates backtracking state on every call, so on the
 * `nix search` workload regex matching dominates the profile. When the pattern
 * contains no POSIX-extended regex metacharacters, a case-insensitive substring
 * search is equivalent and orders of magnitude cheaper; we use it as a
 * pre-filter before invoking std::regex.
 */
struct SearchPattern
{
    std::regex regex;
    /** Lowercased pattern text, set iff `re` had no regex metacharacters. */
    std::optional<std::string> literal;

    explicit SearchPattern(const std::string & re)
        : regex(re, std::regex::extended | std::regex::icase)
    {
        if (hasNoRegexMetacharacters(re))
            literal = asciiLower(re);
    }
};

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

        std::vector<SearchPattern> patterns, excludeSearchPatterns;
        patterns.reserve(res.size());
        excludeSearchPatterns.reserve(excludeRes.size());
        for (auto & re : res)
            patterns.emplace_back(re);
        for (auto & re : excludeRes)
            excludeSearchPatterns.emplace_back(re);

        auto state = getEvalState();

        std::optional<nlohmann::json> jsonOut;
        if (json)
            jsonOut = json::object();

        uint64_t results = 0;

        std::function<void(eval_cache::AttrCursor & cursor, const AttrPath & attrPath, bool initialRecurse)> visit;

        visit = [&](eval_cache::AttrCursor & cursor, const AttrPath & attrPath, bool initialRecurse) {
            auto attrPathS = state->symbols.resolve({attrPath});
            auto attrPathStr = attrPath.to_string(*state);

            Activity act(*logger, lvlInfo, actUnknown, fmt("evaluating '%s'", attrPathStr));
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

                    auto literalMissesAllFields = [&](const std::optional<std::string> & lit) {
                        return lit && !containsCI(attrPathStr, *lit) && !containsCI(name.name, *lit)
                               && !containsCI(description, *lit);
                    };

                    auto addAll = [&found](std::sregex_iterator it, std::vector<std::smatch> & vec) {
                        const auto end = std::sregex_iterator();
                        while (it != end) {
                            vec.push_back(*it++);
                            found = true;
                        }
                    };

                    for (auto & p : excludeSearchPatterns) {
                        if (literalMissesAllFields(p.literal))
                            continue;
                        if (std::regex_search(attrPathStr, p.regex) || std::regex_search(name.name, p.regex)
                            || std::regex_search(description, p.regex))
                            return;
                    }

                    for (auto & p : patterns) {
                        if (literalMissesAllFields(p.literal)) {
                            found = false;
                            break;
                        }
                        found = false;
                        addAll(std::sregex_iterator(attrPathStr.begin(), attrPathStr.end(), p.regex), attrPathMatches);
                        addAll(std::sregex_iterator(name.name.begin(), name.name.end(), p.regex), nameMatches);
                        addAll(
                            std::sregex_iterator(description.begin(), description.end(), p.regex), descriptionMatches);
                        if (!found)
                            break;
                    }

                    if (found) {
                        results++;
                        if (json) {
                            (*jsonOut)[attrPathStr] = {
                                {"pname", name.name},
                                {"version", name.version},
                                {"description", description},
                            };
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

        if (json)
            printJSON(*jsonOut);

        if (!json && !results)
            throw Error("no results for the given search term(s)!");
    }
};

static auto rCmdSearch = registerCommand<CmdSearch>("search");

} // namespace nix
