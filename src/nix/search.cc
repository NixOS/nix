#include "command.hh"
#include "globals.hh"
#include "eval.hh"
#include "eval-inline.hh"
#include "names.hh"
#include "get-drvs.hh"
#include "common-args.hh"
#include "json.hh"
#include "json-to-value.hh"

#include <regex>
#include <fstream>

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

    bool writeCache = true;
    bool useCache = true;

    CmdSearch()
    {
        expectArg("regex", &re, true);

        mkFlag()
            .longName("update-cache")
            .shortName('u')
            .description("update the package search cache")
            .handler([&]() { writeCache = true; useCache = false; });

        mkFlag()
            .longName("no-cache")
            .description("do not use or update the package search cache")
            .handler([&]() { writeCache = false; useCache = false; });
    }

    std::string name() override
    {
        return "search";
    }

    std::string description() override
    {
        return "query available packages";
    }

    Examples examples() override
    {
        return {
            Example{
                "To show all available packages:",
                "nix search"
            },
            Example{
                "To show any packages containing 'blender' in its name or description:",
                "nix search blender"
            },
            Example{
                "To search for Firefox and Chromium:",
                "nix search 'firefox|chromium'"
            },
        };
    }

    void run(ref<Store> store) override
    {
        settings.readOnlyMode = true;

        // Empty search string should match all packages
        // Use "^" here instead of ".*" due to differences in resulting highlighting
        // (see #1893 -- libc++ claims empty search string is not in POSIX grammar)
        if (re.empty()) re = "^";

        std::regex regex(re, std::regex::extended | std::regex::icase);

        auto state = getEvalState();

        bool first = true;

        auto jsonOut = json ? std::make_unique<JSONObject>(std::cout) : nullptr;

        auto sToplevel = state->symbols.create("_toplevel");
        auto sRecurse = state->symbols.create("recurseForDerivations");

        bool fromCache = false;

        std::function<void(Value *, std::string, bool, JSONObject *)> doExpr;

        doExpr = [&](Value * v, std::string attrPath, bool toplevel, JSONObject * cache) {
            debug("at attribute '%s'", attrPath);

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

                    if (cache) {
                        cache->attr("type", "derivation");
                        cache->attr("name", drv.queryName());
                        cache->attr("system", drv.querySystem());
                        if (description != "") {
                            auto meta(cache->object("meta"));
                            meta.attr("description", description);
                        }
                    }
                }

                else if (v->type == tAttrs) {

                    if (!toplevel) {
                        auto attrs = v->attrs;
                        Bindings::iterator j = attrs->find(sRecurse);
                        if (j == attrs->end() || !state->forceBool(*j->value, *j->pos)) {
                            debug("skip attribute '%s'", attrPath);
                            return;
                        }
                    }

                    bool toplevel2 = false;
                    if (!fromCache) {
                        Bindings::iterator j = v->attrs->find(sToplevel);
                        toplevel2 = j != v->attrs->end() && state->forceBool(*j->value, *j->pos);
                    }

                    for (auto & i : *v->attrs) {
                        auto cache2 =
                            cache ? std::make_unique<JSONObject>(cache->object(i.name)) : nullptr;
                        doExpr(i.value,
                            attrPath == "" ? (std::string) i.name : attrPath + "." + (std::string) i.name,
                            toplevel2 || fromCache, cache2 ? cache2.get() : nullptr);
                    }
                }

            } catch (AssertionError & e) {
            } catch (Error & e) {
                if (!toplevel) {
                    e.addPrefix(fmt("While evaluating the attribute '%s':\n", attrPath));
                    throw;
                }
            }
        };

        Path jsonCacheFileName = getCacheDir() + "/nix/package-search.json";

        if (useCache && pathExists(jsonCacheFileName)) {

            warn("using cached results; pass '-u' to update the cache");

            Value vRoot;
            parseJSON(*state, readFile(jsonCacheFileName), vRoot);

            fromCache = true;

            doExpr(&vRoot, "", true, nullptr);
        }

        else {
            createDirs(dirOf(jsonCacheFileName));

            Path tmpFile = fmt("%s.tmp.%d", jsonCacheFileName, getpid());

            std::ofstream jsonCacheFile;

            try {
                // iostream considered harmful
                jsonCacheFile.exceptions(std::ofstream::failbit);
                jsonCacheFile.open(tmpFile);

                auto cache = writeCache ? std::make_unique<JSONObject>(jsonCacheFile, false) : nullptr;

                doExpr(getSourceExpr(*state), "", true, cache.get());

            } catch (std::exception &) {
                /* Fun fact: catching std::ios::failure does not work
                   due to C++11 ABI shenanigans.
                   https://gcc.gnu.org/bugzilla/show_bug.cgi?id=66145 */
                if (!jsonCacheFile)
                    throw Error("error writing to %s", tmpFile);
            }

            if (writeCache && rename(tmpFile.c_str(), jsonCacheFileName.c_str()) == -1)
                throw SysError("cannot rename '%s' to '%s'", tmpFile, jsonCacheFileName);
        }
    }
};

static RegisterCommand r1(make_ref<CmdSearch>());
