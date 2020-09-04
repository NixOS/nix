#include "command.hh"
#include "common-args.hh"
#include "shared.hh"
#include "eval.hh"
#include "flake/flake.hh"
#include "store-api.hh"
#include "fetchers.hh"
#include "registry.hh"

using namespace nix;
using namespace nix::flake;

struct CmdRegistryList : StoreCommand
{
    std::string description() override
    {
        return "list available Nix flakes";
    }

    void run(nix::ref<nix::Store> store) override
    {
        using namespace fetchers;

        auto registries = getRegistries(store);

        for (auto & registry : registries) {
            for (auto & entry : registry->entries) {
                // FIXME: format nicely
                logger->stdout_("%s %s %s",
                    registry->type == Registry::Flag   ? "flags " :
                    registry->type == Registry::User   ? "user  " :
                    registry->type == Registry::System ? "system" :
                    "global",
                    entry.from.to_string(),
                    entry.to.to_string());
            }
        }
    }
};

struct CmdRegistryAdd : MixEvalArgs, Command
{
    std::string fromUrl, toUrl;

    std::string description() override
    {
        return "add/replace flake in user flake registry";
    }

    CmdRegistryAdd()
    {
        expectArg("from-url", &fromUrl);
        expectArg("to-url", &toUrl);
    }

    void run() override
    {
        auto fromRef = parseFlakeRef(fromUrl);
        auto toRef = parseFlakeRef(toUrl);
        fetchers::Attrs extraAttrs;
        if (toRef.subdir != "") extraAttrs["dir"] = toRef.subdir;
        auto userRegistry = fetchers::getUserRegistry();
        userRegistry->remove(fromRef.input);
        userRegistry->add(fromRef.input, toRef.input, extraAttrs);
        userRegistry->write(fetchers::getUserRegistryPath());
    }
};

struct CmdRegistryRemove : virtual Args, MixEvalArgs, Command
{
    std::string url;

    std::string description() override
    {
        return "remove flake from user flake registry";
    }

    CmdRegistryRemove()
    {
        expectArg("url", &url);
    }

    void run() override
    {
        auto userRegistry = fetchers::getUserRegistry();
        userRegistry->remove(parseFlakeRef(url).input);
        userRegistry->write(fetchers::getUserRegistryPath());
    }
};

struct CmdRegistryPin : virtual Args, EvalCommand
{
    std::string url;

    std::string description() override
    {
        return "pin a flake to its current version in user flake registry";
    }

    CmdRegistryPin()
    {
        expectArg("url", &url);
    }

    void run(nix::ref<nix::Store> store) override
    {
        auto ref = parseFlakeRef(url);
        auto userRegistry = fetchers::getUserRegistry();
        userRegistry->remove(ref.input);
        auto [tree, resolved] = ref.resolve(store).input.fetch(store);
        fetchers::Attrs extraAttrs;
        if (ref.subdir != "") extraAttrs["dir"] = ref.subdir;
        userRegistry->add(ref.input, resolved, extraAttrs);
        userRegistry->write(fetchers::getUserRegistryPath());
    }
};

struct CmdRegistry : virtual NixMultiCommand
{
    CmdRegistry()
        : MultiCommand({
                {"list", []() { return make_ref<CmdRegistryList>(); }},
                {"add", []() { return make_ref<CmdRegistryAdd>(); }},
                {"remove", []() { return make_ref<CmdRegistryRemove>(); }},
                {"pin", []() { return make_ref<CmdRegistryPin>(); }},
            })
    {
    }

    std::string description() override
    {
        return "manage the flake registry";
    }

    Category category() override { return catSecondary; }

    void run() override
    {
        if (!command)
            throw UsageError("'nix registry' requires a sub-command.");
        command->second->prepare();
        command->second->run();
    }
};

static auto r1 = registerCommand<CmdRegistry>("registry");
