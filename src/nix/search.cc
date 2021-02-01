#include "command.hh"
#include "globals.hh"
#include "eval.hh"
#include "eval-inline.hh"
#include "names.hh"
#include "get-drvs.hh"
#include "common-args.hh"
#include "json.hh"
#include "shared.hh"
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

        std::function<void(Value & cursor, const Pos& pos, const std::vector<Symbol> & attrPath, bool initialRecurse)> visit;

        visit = [&](Value & cursor, const Pos& pos, const std::vector<Symbol> & attrPath, bool initialRecurse)
        {
            Activity act(*logger, lvlInfo, actUnknown,
                fmt("evaluating '%s'", concatStringsSep(".", attrPath)));
            try {
                auto recurse = [&]()
                {
                    for (const auto & attr : evalState->listAttrFields(cursor, pos)) {
                        Value* cursor2;
                        try {
                            auto cursor2Res = evalState->lazyGetOptionalAttrField(cursor, {attr}, pos);
                            if (cursor2Res.error)
                                continue; // Shouldn't happen, but who knows
                            cursor2 = cursor2Res.value;
                        } catch (EvalError&) {
                            continue;
                        }
                        auto attrPath2(attrPath);
                        attrPath2.push_back(attr);
                        visit(*cursor2, pos, attrPath2, false);
                    }
                };

                if (evalState->isDerivation(cursor)) {
                    size_t found = 0;

                    DrvName name(evalState->forceStringNoCtx(*evalState->getAttrField(cursor, {evalState->sName}, pos)));

                    auto descriptionGetRes = evalState->getOptionalAttrField(cursor, {evalState->sMeta, evalState->sDescription}, pos);
                    auto description = !descriptionGetRes.error ? state->forceStringNoCtx(*descriptionGetRes.value) : "";
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
                    auto recurseForDrvGetRes = evalState->getOptionalAttrField(cursor, {evalState->sRecurseForDerivations}, pos);
                    if (!recurseForDrvGetRes.error && evalState->forceBool(*recurseForDrvGetRes.value, pos))
                        recurse();
                }

            } catch (EvalError & e) {
                if (!(attrPath.size() > 0 && attrPath[0] == "legacyPackages"))
                    throw;
            }
        };

        for (auto & [cursor, pos, prefix] : installable->toValues(*state))
            visit(*cursor, pos, parseAttrPath(*state, prefix), true);

        if (!json && !results)
            throw Error("no results for the given search term(s)!");
    }
};

static auto rCmdSearch = registerCommand<CmdSearch>("search");
