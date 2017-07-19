#include "command.hh"
#include "globals.hh"
#include "eval.hh"
#include "eval-inline.hh"
#include "names.hh"
#include "get-drvs.hh"
#include "common-args.hh"
#include "json.hh"

#include <regex>

using namespace nix;

std::string hilite(const std::string & s, const std::smatch & m)
{
    return
        m.empty()
        ? s
        : std::string(m.prefix())
          + ANSI_RED + std::string(m.str()) + ANSI_NORMAL
          + std::string(m.suffix());
}

struct CmdSearch : SourceExprCommand, MixJSON
{
    std::string re;

    CmdSearch()
    {
        expectArg("regex", &re, true);
    }

    std::string name() override
    {
        return "search";
    }

    std::string description() override
    {
        return "query available packages";
    }

    void run(ref<Store> store) override
    {
        settings.readOnlyMode = true;

        std::regex regex(re, std::regex::extended | std::regex::icase);

        auto state = getEvalState();

        std::function<void(Value *, std::string, bool)> doExpr;

        bool first = true;

        auto jsonOut = json ? std::make_unique<JSONObject>(std::cout, true) : nullptr;

        auto sToplevel = state->symbols.create("_toplevel");

        doExpr = [&](Value * v, std::string attrPath, bool toplevel) {
            debug("at attribute ‘%s’", attrPath);

            try {

                state->forceValue(*v);

                if (v->type == tLambda && toplevel) {
                    Value * v2 = state->allocValue();
                    state->autoCallFunction(*state->allocBindings(1), *v, *v2);
                    v = v2;
                    state->forceValue(*v);
                }

                if (state->isDerivation(*v)) {

                    DrvInfo drv(*state, attrPath, v->attrs);

                    DrvName parsed(drv.queryName());

                    std::smatch attrPathMatch;
                    std::regex_search(attrPath, attrPathMatch, regex);

                    auto name = parsed.name;
                    std::smatch nameMatch;
                    std::regex_search(name, nameMatch, regex);

                    std::string description = drv.queryMetaString("description");
                    std::replace(description.begin(), description.end(), '\n', ' ');
                    std::smatch descriptionMatch;
                    std::regex_search(description, descriptionMatch, regex);

                    if (!attrPathMatch.empty()
                        || !nameMatch.empty()
                        || !descriptionMatch.empty())
                    {
                        if (json) {

                            auto jsonElem = jsonOut->object(attrPath);

                            jsonElem.attr("pkgName", parsed.name);
                            jsonElem.attr("version", parsed.version);
                            jsonElem.attr("description", description);

                        } else {
                            if (!first) std::cout << "\n";
                            first = false;

                            std::cout << fmt(
                                "Attribute name: %s\n"
                                "Package name: %s\n"
                                "Version: %s\n"
                                "Description: %s\n",
                                hilite(attrPath, attrPathMatch),
                                hilite(name, nameMatch),
                                parsed.version,
                                hilite(description, descriptionMatch));
                        }
                    }
                }

                else if (v->type == tAttrs) {

                    if (!toplevel) {
                        auto attrs = v->attrs;
                        Bindings::iterator j = attrs->find(state->symbols.create("recurseForDerivations"));
                        if (j == attrs->end() || !state->forceBool(*j->value, *j->pos)) return;
                    }

                    Bindings::iterator j = v->attrs->find(sToplevel);
                    bool toplevel2 = j != v->attrs->end() && state->forceBool(*j->value, *j->pos);

                    for (auto & i : *v->attrs) {
                        doExpr(i.value,
                            attrPath == "" ? (std::string) i.name : attrPath + "." + (std::string) i.name,
                            toplevel2);
                    }
                }

            } catch (AssertionError & e) {
            } catch (Error & e) {
                if (!toplevel) {
                    e.addPrefix(fmt("While evaluating the attribute ‘%s’:\n", attrPath));
                    throw;
                }
            }
        };

        doExpr(getSourceExpr(*state), "", true);
    }
};

static RegisterCommand r1(make_ref<CmdSearch>());
