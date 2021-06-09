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
#include "value.hh"

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
        std::function<void(Value & current, const std::vector<Symbol> & attrPath, bool initialRecurse)> visit2;

        Value * vTmp = state->allocValue();

        visit2 = [&](Value & current, const std::vector<Symbol> & attrPath, bool initialRecurse)
        {
            Activity act(*logger, lvlInfo, actUnknown,
                fmt("evaluating '%s'", concatStringsSep(".", attrPath)));
            auto recurse = [&]()
            {
                for (auto & attr : state->getFields(current, noPos)) {
                    auto attrPath2(attrPath);
                    attrPath2.push_back(attr.name);
                    visit2(*attr.value, attrPath2, false);
                }
            };

            try {
                auto maybeTypeField = state->lazyGetAttrField(current, {state->sType}, noPos, *vTmp);
                if (maybeTypeField == EvalState::LazyValueType::PlainValue
                    && vTmp->type() == nix::nString
                    && strcmp(vTmp->string.s, "derivation") == 0) {
                    size_t found = 0;

                    state->getAttrFieldThrow(current, {state->sName}, noPos, *vTmp);
                    DrvName name(state->forceString(*vTmp));

                    auto hasDescription = state->getAttrField(current, {state->sMeta, state->sDescription}, noPos, *vTmp);
                    auto description = hasDescription ? state->forceString(*vTmp) : "";
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
                            if (results > 1) logger->cout("");
                            logger->cout(
                                    "* %s%s",
                                    wrap("\e[0;1m", hilite(attrPath2, attrPathMatch, "\e[0;1m")),
                                    name.version != "" ? " (" + name.version + ")" : "");
                            if (description != "")
                                logger->cout(
                                        "  %s", hilite(description, descriptionMatch, ANSI_NORMAL));
                        }
                    }
                } else if (
                        attrPath.size() == 0
                        || (attrPath[0] == "legacyPackages" && attrPath.size() <= 2)
                        || (attrPath[0] == "packages" && attrPath.size() <= 2))
                    recurse();

                else if (initialRecurse)
                    recurse();

                else if (attrPath[0] == "legacyPackages" && attrPath.size() > 2) {
                    auto recurseFieldInfo = state->lazyGetAttrField(current, {state->sRecurseForDerivations}, noPos, *vTmp);
                    auto hasRecurse = recurseFieldInfo == EvalState::LazyValueType::PlainValue;
                    if (hasRecurse && state->forceBool(*vTmp, noPos))
                        recurse();
                }
            } catch (EvalError & e) {
                if (!(attrPath.size() > 0 && attrPath[0] == "legacyPackages"))
                    throw;
            }
        };

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
                            if (results > 1) logger->cout("");
                            logger->cout(
                                "* %s%s",
                                wrap("\e[0;1m", hilite(attrPath2, attrPathMatch, "\e[0;1m")),
                                name.version != "" ? " (" + name.version + ")" : "");
                            if (description != "")
                                logger->cout(
                                    "  %s", hilite(description, descriptionMatch, ANSI_NORMAL));
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

        for (auto & [value, pos, prefix] : installable->toValues(*state))
            visit2(*value, parseAttrPath(*state, prefix), true);

        if (!json && !results)
            throw Error("no results for the given search term(s)!");
    }
};

static auto rCmdSearch = registerCommand<CmdSearch>("search");
