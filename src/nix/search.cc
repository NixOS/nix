#include "command.hh"
#include "globals.hh"
#include "eval.hh"
#include "eval-inline.hh"
#include "names.hh"
#include "get-drvs.hh"
#include "common-args.hh"
#include "json.hh"
#include "shared.hh"
#include "eval-cache.hh"
#include "attr-path.hh"

#include <regex>
#include <fstream>

using namespace nix;

std::string wrap(std::string prefix, std::string s)
{
    return prefix + s + ANSI_NORMAL;
}

std::string hilite(const std::string & s, const std::smatch & m, std::string postfix)
{
    return
        m.empty()
        ? s
        : std::string(m.prefix())
          + ANSI_GREEN + std::string(m.str()) + postfix
          + std::string(m.suffix());
}

struct CmdSearch : InstallableCommand, MixJSON
{
    std::vector<std::string> res;

    CmdSearch()
    {
        expectArgs("regex", &res);
    }

    std::string description() override
    {
        return "query available packages";
    }

    Examples examples() override
    {
        return {
            Example{
                "To show all packages in the flake in the current directory:",
                "nix search"
            },
            Example{
                "To show packages in the 'nixpkgs' flake containing 'blender' in its name or description:",
                "nix search nixpkgs blender"
            },
            Example{
                "To search for Firefox or Chromium:",
                "nix search nixpkgs 'firefox|chromium'"
            },
            Example{
                "To search for packages containing 'git' and either 'frontend' or 'gui':",
                "nix search nixpkgs git 'frontend|gui'"
            }
        };
    }

    Strings getDefaultFlakeAttrPaths() override
    {
        return {
            "packages." + settings.thisSystem.get() + ".",
            "legacyPackages." + settings.thisSystem.get() + "."
        };
    }

    void run(ref<Store> store) override
    {
        settings.readOnlyMode = true;

        // Empty search string should match all packages
        // Use "^" here instead of ".*" due to differences in resulting highlighting
        // (see #1893 -- libc++ claims empty search string is not in POSIX grammar)
        if (res.empty())
            res.push_back("^");

        std::vector<std::regex> regexes;
        regexes.reserve(res.size());

        for (auto & re : res)
            regexes.push_back(std::regex(re, std::regex::extended | std::regex::icase));

        auto state = getEvalState();

        auto jsonOut = json ? std::make_unique<JSONObject>(std::cout) : nullptr;

        uint64_t results = 0;

        std::function<void(eval_cache::AttrCursor & cursor, const std::vector<Symbol> & attrPath)> visit;

        visit = [&](eval_cache::AttrCursor & cursor, const std::vector<Symbol> & attrPath)
        {
            Activity act(*logger, lvlInfo, actUnknown,
                fmt("evaluating '%s'", concatStringsSep(".", attrPath)));
            try {
                auto recurse = [&]()
                {
                    for (const auto & attr : cursor.getAttrs()) {
                        auto cursor2 = cursor.getAttr(attr);
                        auto attrPath2(attrPath);
                        attrPath2.push_back(attr);
                        visit(*cursor2, attrPath2);
                    }
                };

                if (cursor.isDerivation()) {
                    size_t found = 0;

                    DrvName name(cursor.getAttr("name")->getString());

                    auto aMeta = cursor.maybeGetAttr("meta");
                    auto aDescription = aMeta ? aMeta->maybeGetAttr("description") : nullptr;
                    auto description = aDescription ? aDescription->getString() : "";
                    std::replace(description.begin(), description.end(), '\n', ' ');
                    auto attrPath2 = concatStringsSep(".", attrPath);

                    std::smatch attrPathMatch;
                    std::smatch descriptionMatch;
                    std::smatch nameMatch;

                    for (auto & regex : regexes) {
                        std::regex_search(attrPath2, attrPathMatch, regex);
                        std::regex_search(name.name, nameMatch, regex);
                        std::regex_search(description, descriptionMatch, regex);
                        if (!attrPathMatch.empty()
                            || !nameMatch.empty()
                            || !descriptionMatch.empty())
                            found++;
                    }

                    if (found == res.size()) {
                        results++;
                        if (json) {
                            auto jsonElem = jsonOut->object(attrPath2);
                            jsonElem.attr("pname", name.name);
                            jsonElem.attr("version", name.version);
                            jsonElem.attr("description", description);
                        } else {
                            auto name2 = hilite(name.name, nameMatch, "\e[0;2m");
                            if (results > 1) logger->stdout("");
                            logger->stdout(
                                "* %s%s",
                                wrap("\e[0;1m", hilite(attrPath2, attrPathMatch, "\e[0;1m")),
                                name.version != "" ? " (" + name.version + ")" : "");
                            if (description != "")
                                logger->stdout(
                                    "  %s", hilite(description, descriptionMatch, ANSI_NORMAL));
                        }
                    }
                }

                else if (
                    attrPath.size() == 0
                    || (attrPath[0] == "legacyPackages" && attrPath.size() <= 2)
                    || (attrPath[0] == "packages" && attrPath.size() <= 2))
                    recurse();

                else if (attrPath[0] == "legacyPackages" && attrPath.size() > 2) {
                    auto attr = cursor.maybeGetAttr(state->sRecurseForDerivations);
                    if (attr && attr->getBool())
                        recurse();
                }

            } catch (EvalError & e) {
                if (!(attrPath.size() > 0 && attrPath[0] == "legacyPackages"))
                    throw;
            }
        };

        for (auto & [cursor, prefix] : installable->getCursors(*state))
            visit(*cursor, parseAttrPath(*state, prefix));

        if (!json && !results)
            throw Error("no results for the given search term(s)!");
    }
};

static auto r1 = registerCommand<CmdSearch>("search");
