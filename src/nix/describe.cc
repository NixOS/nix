#include "command.hh"
#include "eval-cache.hh"
#include "shared.hh"
#include "markdown.hh"

#include <regex>

using namespace nix;

struct CmdDescribe : InstallableCommand
{
    std::optional<std::string> filter;

    CmdDescribe()
    {
        addFlag({
            .longName = "filter",
            .description = "Only show options that match this regular expression.",
            .labels = {"regex"},
            .handler = {&filter},
        });
    }

    std::string description() override
    {
        return "show information about a configurable derivation";
    }

    Category category() override { return catSecondary; }

    // FIXME: add help

    void run(ref<Store> store) override
    {
        std::optional<std::regex> filterRegex;
        if (filter)
            filterRegex = std::regex(*filter, std::regex::extended | std::regex::icase);

        auto state = getEvalState();

        auto cursor = installable->getCursor(*state);

        auto type = cursor->getAttr(state->sType)->getString();

        if (type != "configurable")
            throw Error("'%s' is not a configurable derivation", installable);

        std::ostringstream str;

        std::function<void(const ref<eval_cache::AttrCursor> &, std::string_view)> recurse;
        recurse = [&](const ref<eval_cache::AttrCursor> & cursor, std::string_view attrPath)
        {
            auto type = cursor->getAttr(state->sType)->getString();

            if (type == "optionSet") {
                for (auto & attr : cursor->getAttrs()) {
                    if (attr == state->sType) continue;
                    recurse(cursor->getAttr(attr),
                        attrPath.empty()
                        ? state->symbols[attr]
                        : std::string(attrPath) + "." + state->symbols[attr]);
                }
            }

            else if (type == "option") {
                if (filterRegex && !std::regex_search(std::string(attrPath), *filterRegex)) return;
                auto typeId = trim(stripIndentation(cursor->getAttr("typeId")->getString()));
                str << fmt("* `%s` (*%s*)\n", attrPath, typeId);
                str << "\n";
                auto description = trim(stripIndentation(cursor->getAttr(state->sDescription)->getString()));
                str << indent("  ", "  ", description) << "\n\n";
            }

            else
                throw Error("unexpected type '%s'", type);
        };

        recurse(cursor->getAttr("options"), "");

        RunPager pager;
        std::cout << renderMarkdownToTerminal(str.str()) << "\n";
    }
};

static auto rCmdDescribe = registerCommand<CmdDescribe>("describe");
