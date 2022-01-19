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

#include <iterator>
#include <regex>
#include <fstream>

using namespace nix;

std::string wrap(std::string prefix, std::string s)
{
    return prefix + s + ANSI_NORMAL;
}

#define HILITE_COLOR ANSI_GREEN

std::string hilite(const std::string & s, const std::smatch & m, std::string postfix)
{
    return
        m.empty()
        ? s
        : std::string(m.prefix())
          + HILITE_COLOR + std::string(m.str()) + postfix
          + std::string(m.suffix());
}

std::string hilite_all(const std::string &s, std::vector<std::smatch> matches, std::string postfix) {
    // Don't waste time on trivial highlights
    if (matches.size() == 0)
        return s;
    else if (matches.size() == 1)
        return hilite(s, matches[0], postfix);

    std::sort(matches.begin(), matches.end(), [](const auto &a, const auto &b) {
        return a.position() < b.position();
    });

    std::string out;
    ssize_t last_end = 0;
    for (size_t i = 0; i < matches.size(); i++) {
        auto m = matches[i];
        size_t start = m.position();
        out.append(s.substr(last_end, m.position() - last_end));
        // Merge continous matches
        ssize_t end = start + m.length();
        while(i + 1 < matches.size() && matches[i+1].position() <= end) {
            auto n = matches[++i];
            ssize_t nend = start + (n.position() - start + n.length());
            if(nend > end)
                end = nend;
        }
        out.append(HILITE_COLOR);
        out.append(s.substr(start, end - start));
        out.append(postfix);
        last_end = end;
    }
    out.append(s.substr(last_end));
    return out;
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
        return {
            "packages." + settings.thisSystem.get() + ".",
            "legacyPackages." + settings.thisSystem.get() + "."
        };
    }

    void run(ref<Store> store) override
    {
        settings.readOnlyMode = true;
        evalSettings.enableImportFromDerivation.setDefault(false);

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

        std::function<void(eval_cache::AttrCursor & cursor, const std::vector<Symbol> & attrPath, bool initialRecurse)> visit;

        visit = [&](eval_cache::AttrCursor & cursor, const std::vector<Symbol> & attrPath, bool initialRecurse)
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
                        visit(*cursor2, attrPath2, false);
                    }
                };

                if (cursor.isDerivation()) {
                    DrvName name(cursor.getAttr("name")->getString());

                    auto aMeta = cursor.maybeGetAttr("meta");
                    auto aDescription = aMeta ? aMeta->maybeGetAttr("description") : nullptr;
                    auto description = aDescription ? aDescription->getString() : "";
                    std::replace(description.begin(), description.end(), '\n', ' ');
                    auto attrPath2 = concatStringsSep(".", attrPath);

                    std::vector<std::smatch> attrPathMatches;
                    std::vector<std::smatch> descriptionMatches;
                    std::vector<std::smatch> nameMatches;
                    bool found = false;

                    for (auto & regex : regexes) {
                        found = false;
                        auto add_all = [&found](std::sregex_iterator it, std::vector<std::smatch>& vec){
                            const auto end = std::sregex_iterator();
                            while(it != end) {
                                vec.push_back(*it++);
                                found = true;
                            }
                        };

                        add_all(std::sregex_iterator(attrPath2.begin(), attrPath2.end(), regex), attrPathMatches);
                        add_all(std::sregex_iterator(name.name.begin(), name.name.end(), regex), nameMatches);
                        add_all(std::sregex_iterator(description.begin(), description.end(), regex), descriptionMatches);

                        if(!found)
                            break;
                    }

                    if (found)
                    {
                        results++;
                        if (json) {
                            auto jsonElem = jsonOut->object(attrPath2);
                            jsonElem.attr("pname", name.name);
                            jsonElem.attr("version", name.version);
                            jsonElem.attr("description", description);
                        } else {
                            auto name2 = hilite_all(name.name, nameMatches, "\e[0;2m");
                            if (results > 1) logger->cout("");
                            logger->cout(
                                "* %s%s",
                                wrap("\e[0;1m", hilite_all(attrPath2, attrPathMatches, "\e[0;1m")),
                                name.version != "" ? " (" + name.version + ")" : "");
                            if (description != "")
                                logger->cout(
                                    "  %s", hilite_all(description, descriptionMatches, ANSI_NORMAL));
                        }
                    }
                }

                else if (
                    attrPath.size() == 0
                    || (attrPath[0] == "legacyPackages" && attrPath.size() <= 2)
                    || (attrPath[0] == "packages" && attrPath.size() <= 2))
                    recurse();

                else if (initialRecurse)
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
            visit(*cursor, parseAttrPath(*state, prefix), true);

        if (!json && !results)
            throw Error("no results for the given search term(s)!");
    }
};

static auto rCmdSearch = registerCommand<CmdSearch>("search");
